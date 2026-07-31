/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "util/test/suite.h"

#include "../libretro/replicated-pair-spike.h"

#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/gba/core.h>
#include <mgba/internal/gba/gba.h>
#include <mgba-util/vfs.h>

#include <fcntl.h>

#ifndef GBA_LINK_CONTINUOUS_ROM_PATH
#error "GBA_LINK_CONTINUOUS_ROM_PATH must name the continuous link-test ROM"
#endif

#define LINK_RESULT_ADDRESS 0x02000000
#define LINK_RESULT_MAGIC 0x31544B4C
#define LINK_RESULT_STATUS_OFFSET 8
#define LINK_RESULT_RUNNING 3
#define LINK_RESULT_TRANSFERS_OFFSET 24
#define MIN_EXPECTED_TRANSFERS 280
#define TEST_VIDEO_WIDTH 256
#define TEST_VIDEO_HEIGHT 224

struct PairSpikeFixture {
	struct mCore* core;
	struct VFile* save;
	mColor* video;
};

static struct mLogger _silentLogger;

static void _discardLog(
		struct mLogger* logger, int category,
		enum mLogLevel level, const char* format, va_list args) {
	UNUSED(logger);
	UNUSED(category);
	UNUSED(level);
	UNUSED(format);
	UNUSED(args);
}

static int _setup(void** state) {
	struct PairSpikeFixture* fixture =
	    calloc(1, sizeof(*fixture));
	assert_non_null(fixture);
	fixture->core = GBACoreCreate();
	assert_non_null(fixture->core);
	assert_true(fixture->core->init(fixture->core));
	mCoreInitConfig(fixture->core, NULL);
	assert_true(fixture->core->loadROM(
	    fixture->core,
	    VFileOpen(GBA_LINK_CONTINUOUS_ROM_PATH, O_RDONLY)));
	fixture->save = VFileMemChunk(NULL, 0);
	assert_non_null(fixture->save);
	assert_true(fixture->core->loadTemporarySave(
	    fixture->core, fixture->save));
	fixture->video = calloc(
	    TEST_VIDEO_WIDTH * TEST_VIDEO_HEIGHT,
	    sizeof(*fixture->video));
	assert_non_null(fixture->video);
	fixture->core->setVideoBuffer(
	    fixture->core, fixture->video, TEST_VIDEO_WIDTH);
	fixture->core->reset(fixture->core);
	*state = fixture;
	return 0;
}

static int _teardown(void** state) {
	struct PairSpikeFixture* fixture = *state;
	mLibretroReplicatedPairSpikeStop();
	mCoreConfigDeinit(&fixture->core->config);
	fixture->core->deinit(fixture->core);
	assert_true(fixture->save->close(fixture->save));
	free(fixture->video);
	free(fixture);
	return 0;
}

M_TEST_DEFINE(runsOneFreshPairedFramePerCall) {
	struct PairSpikeFixture* fixture = *state;
	assert_true(mLibretroReplicatedPairSpikeStart(fixture->core));
	assert_true(mLibretroReplicatedPairSpikeIsActive());
	for (uint32_t frame = 1; frame <= 600; ++frame) {
		assert_true(mLibretroReplicatedPairSpikeRunFrame(0));
		assert_int_equal(
		    fixture->core->frameCounter(fixture->core), frame);
	}
	assert_int_equal(
	    fixture->core->rawRead32(
	        fixture->core, LINK_RESULT_ADDRESS, -1),
	    LINK_RESULT_MAGIC);
	assert_int_equal(
	    fixture->core->rawRead32(
	        fixture->core,
	        LINK_RESULT_ADDRESS + LINK_RESULT_STATUS_OFFSET, -1),
	    LINK_RESULT_RUNNING);
	uint32_t transfers = fixture->core->rawRead32(
	    fixture->core,
	    LINK_RESULT_ADDRESS + LINK_RESULT_TRANSFERS_OFFSET, -1);
	/*
	 * The late-attachment-safe fixture deliberately starts at most one
	 * transaction per video boundary. Startup rendezvous consumes a few of
	 * these 600 frames, so require sustained traffic without freezing the
	 * assertion to the former unpaced fixture's 619-transfer count.
	 */
	assert_in_range(transfers, MIN_EXPECTED_TRANSFERS, UINT32_MAX);
	mLibretroReplicatedPairSpikeStop();
	assert_false(mLibretroReplicatedPairSpikeIsActive());
	assert_null(((struct GBA*) fixture->core->board)->sio.driver);
}

M_TEST_DEFINE(rejectsUnsupportedCoreAndIsIdempotent) {
	struct PairSpikeFixture* fixture = *state;
	assert_false(mLibretroReplicatedPairSpikeStart(NULL));
	assert_true(mLibretroReplicatedPairSpikeStart(fixture->core));
	assert_true(mLibretroReplicatedPairSpikeStart(fixture->core));
	mLibretroReplicatedPairSpikeStop();
	mLibretroReplicatedPairSpikeStop();
}

int main(void) {
	_silentLogger.log = _discardLog;
	mLogSetDefaultLogger(&_silentLogger);
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup_teardown(
		    runsOneFreshPairedFramePerCall, _setup, _teardown),
		cmocka_unit_test_setup_teardown(
		    rejectsUnsupportedCoreAndIsIdempotent,
		    _setup, _teardown),
	};
	int result = cmocka_run_group_tests(tests, NULL, NULL);
	mLogSetDefaultLogger(NULL);
	return result;
}
