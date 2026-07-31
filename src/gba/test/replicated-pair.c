/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "util/test/suite.h"

#include <fcntl.h>

#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/gba/core.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/replicated-pair.h>
#include <mgba/internal/gba/replicated-runtime.h>
#include <mgba-util/vfs.h>

#ifndef GBA_LINK_CONTINUOUS_ROM_PATH
#error "GBA_LINK_CONTINUOUS_ROM_PATH must name the continuous link-test ROM"
#endif

#define LINK_RESULT_ADDRESS 0x02000000
#define LINK_RESULT_RUNNING 3
#define LINK_RESULT_FAIL 0x80000000U

struct LinkTestResult {
	uint32_t magic;
	uint32_t version;
	uint32_t status;
	uint32_t playerId;
	uint32_t observableAttachments;
	uint32_t effectiveParticipants;
	uint32_t transfers;
	uint32_t baudMask;
	uint32_t dataErrors;
	uint32_t missedIrqs;
	uint32_t duplicateIrqs;
	uint32_t busyObservations;
	uint32_t timeouts;
	uint16_t lastSIOCNT;
	uint16_t lastRCNT;
	uint16_t expected[4];
	uint16_t received[4];
	uint32_t completionSpins[4];
};

struct PairFixture {
	struct mCore* sources[2];
	struct VFile* saves[2];
	uint8_t* roms[2];
	struct GBAReplicaBundle bundles[2];
	struct GBAReplicaPayload payloads[2];
	struct GBAReplicaManifest manifests[2];
};

static struct mLogger _silentLogger;

static void _discardLog(
	struct mLogger* logger, int category, enum mLogLevel level,
	const char* format, va_list args) {
	UNUSED(logger);
	UNUSED(category);
	UNUSED(level);
	UNUSED(format);
	UNUSED(args);
}

M_TEST_SUITE_SETUP(GBAReplicatedPair) {
	_silentLogger.log = _discardLog;
	mLogSetDefaultLogger(&_silentLogger);
	return 0;
}

M_TEST_SUITE_TEARDOWN(GBAReplicatedPair) {
	mLogSetDefaultLogger(NULL);
	return 0;
}

static enum GBAReplicaResult _assemble(
	const struct GBAReplicaBundle* bundle,
	struct GBAReplicaPayload* payload) {
	struct GBAReplicaAssembler assembler;
	enum GBAReplicaResult result = GBAReplicaAssemblerInit(
	    &assembler, &bundle->manifest, bundle->manifest.player,
	    bundle->manifest.generation, NULL);
	if (result != GBA_REPLICA_OK) {
		return result;
	}
	for (size_t offset = 0; offset < bundle->encodedSize;
	     offset += bundle->manifest.chunkSize) {
		size_t size = bundle->encodedSize - offset;
		if (size > bundle->manifest.chunkSize) {
			size = bundle->manifest.chunkSize;
		}
		result = GBAReplicaAssemblerAdd(
		    &assembler, bundle->manifest.player,
		    bundle->manifest.generation, offset,
		    &bundle->encodedData[offset], size);
		if (result != GBA_REPLICA_OK) {
			break;
		}
	}
	if (result == GBA_REPLICA_OK) {
		result = GBAReplicaAssemblerFinalize(&assembler, payload);
	}
	GBAReplicaAssemblerDeinit(&assembler);
	return result;
}

static struct mCore* _createSource(
	struct VFile** save, uint8_t** ownedRom) {
	struct mCore* core = GBACoreCreate();
	assert_non_null(core);
	assert_true(core->init(core));
	mCoreInitConfig(core, NULL);
	struct VFile* source = VFileOpen(
	    GBA_LINK_CONTINUOUS_ROM_PATH, O_RDONLY);
	assert_non_null(source);
	size_t sourceSize = source->size(source);
	size_t romSize = 0x40001;
	*ownedRom = calloc(1, romSize);
	assert_non_null(*ownedRom);
	assert_int_equal(source->read(source, *ownedRom, sourceSize), sourceSize);
	assert_true(source->close(source));
	assert_true(core->loadROM(
	    core, VFileFromMemory(*ownedRom, romSize)));
	*save = VFileMemChunk(NULL, 0);
	assert_non_null(*save);
	assert_true(core->loadTemporarySave(core, *save));
	core->reset(core);
	return core;
}

static void _initFixture(struct PairFixture* fixture) {
	memset(fixture, 0, sizeof(*fixture));
	for (unsigned i = 0; i < 2; ++i) {
		fixture->sources[i] = _createSource(
		    &fixture->saves[i], &fixture->roms[i]);
		struct GBA* sourceGBA = fixture->sources[i]->board;
		assert_non_null(sourceGBA->memory.rom);
		assert_null(sourceGBA->sio.driver);
		assert_false(GBASIOMultiplayerIsBusy(sourceGBA->sio.siocnt));
		assert_false(mTimingIsScheduled(
		    &sourceGBA->timing, &sourceGBA->sio.completeEvent));
		assert_int_equal(
		    GBAReplicaCapture(
		        fixture->sources[i], i, 77,
		        GBA_REPLICA_ENCODING_NONE,
		        GBA_REPLICA_DEFAULT_CHUNK_SIZE,
		        &fixture->bundles[i]),
		    GBA_REPLICA_OK);
		assert_int_equal(
		    _assemble(&fixture->bundles[i], &fixture->payloads[i]),
		    GBA_REPLICA_OK);
		fixture->manifests[i] = fixture->bundles[i].manifest;
	}
}

static void _deinitFixture(struct PairFixture* fixture) {
	for (unsigned i = 0; i < 2; ++i) {
		GBAReplicaPayloadDeinit(&fixture->payloads[i]);
		GBAReplicaBundleDeinit(&fixture->bundles[i]);
		mCoreConfigDeinit(&fixture->sources[i]->config);
		fixture->sources[i]->deinit(fixture->sources[i]);
		fixture->saves[i]->close(fixture->saves[i]);
		free(fixture->roms[i]);
	}
}

static void _install(
	struct GBAReplicatedPair* pair, struct PairFixture* fixture) {
	GBAReplicatedPairInit(pair);
	assert_int_equal(
	    GBAReplicatedPairInstall(
	        pair, fixture->sources[0], fixture->manifests,
	        fixture->payloads, 77),
	    GBA_REPLICATED_PAIR_OK);
}

static void _readResult(struct mCore* core, struct LinkTestResult* result) {
	memset(result, 0, sizeof(*result));
	for (size_t offset = 0; offset < sizeof(*result);
	     offset += sizeof(uint32_t)) {
		uint32_t value = core->rawRead32(
		    core, LINK_RESULT_ADDRESS + offset, -1);
		size_t size = sizeof(*result) - offset;
		if (size > sizeof(value)) {
			size = sizeof(value);
		}
		memcpy((uint8_t*) result + offset, &value, size);
	}
}

M_TEST_DEFINE(installsCanonicalP0P1LocalLockstepPair) {
	struct PairFixture fixture;
	_initFixture(&fixture);
	struct GBAReplicatedPair pair;
	_install(&pair, &fixture);
	assert_non_null(GBAReplicatedPairCore(&pair, 0));
	assert_non_null(GBAReplicatedPairCore(&pair, 1));
	assert_null(GBAReplicatedPairCore(&pair, 2));
	assert_int_equal(
	    GBASIOLockstepCoordinatorAttached(&pair.coordinator), 2);
	assert_int_equal(
	    pair.players[0].driver.d.deviceId(&pair.players[0].driver.d), 0);
	assert_int_equal(
	    pair.players[1].driver.d.deviceId(&pair.players[1].driver.d), 1);
	assert_ptr_equal(
	    ((struct GBA*) pair.players[0].core->board)->sio.driver,
	    &pair.players[0].driver.d);
	assert_ptr_equal(
	    ((struct GBA*) pair.players[1].core->board)->sio.driver,
	    &pair.players[1].driver.d);
	GBAReplicatedPairStop(&pair);
	GBAReplicatedPairStop(&pair);
	_deinitFixture(&fixture);
}

M_TEST_DEFINE(frameInputsAdvanceExactlyOnceAndRejectConflicts) {
	struct PairFixture fixture;
	_initFixture(&fixture);
	struct GBAReplicatedPair pair;
	_install(&pair, &fixture);
	assert_int_equal(
	    GBAReplicatedPairRunFrame(&pair),
	    GBA_REPLICATED_PAIR_INVALID_STATE);
	assert_int_equal(
	    GBAReplicatedPairSetInputs(&pair, 0, 1, 2),
	    GBA_REPLICATED_PAIR_OK);
	assert_int_equal(
	    GBAReplicatedPairSetInputs(&pair, 0, 1, 2),
	    GBA_REPLICATED_PAIR_OK);
	assert_int_equal(
	    GBAReplicatedPairSetInputs(&pair, 0, 2, 2),
	    GBA_REPLICATED_PAIR_INPUT_CONFLICT);
	uint32_t before[2] = {
		pair.players[0].core->frameCounter(pair.players[0].core),
		pair.players[1].core->frameCounter(pair.players[1].core),
	};
	assert_int_equal(
	    GBAReplicatedPairRunFrame(&pair), GBA_REPLICATED_PAIR_OK);
	for (unsigned i = 0; i < 2; ++i) {
		assert_int_equal(
		    pair.players[i].core->frameCounter(pair.players[i].core),
		    before[i] + 1);
	}
	assert_int_equal(pair.frameNumber, 1);
	GBAReplicatedPairStop(&pair);
	_deinitFixture(&fixture);
}

M_TEST_DEFINE(continuousMultiTransfersPreserveHardwareSemantics) {
	struct PairFixture fixture;
	_initFixture(&fixture);
	struct GBAReplicatedPair pair;
	_install(&pair, &fixture);
	for (uint64_t frame = 0; frame < 120; ++frame) {
		assert_int_equal(
		    GBAReplicatedPairSetInputs(&pair, frame, 0, 0),
		    GBA_REPLICATED_PAIR_OK);
		assert_int_equal(
		    GBAReplicatedPairRunFrame(&pair), GBA_REPLICATED_PAIR_OK);
	}
	struct LinkTestResult results[2];
	for (unsigned i = 0; i < 2; ++i) {
		_readResult(pair.players[i].core, &results[i]);
		assert_false(results[i].status & LINK_RESULT_FAIL);
		assert_int_equal(results[i].status, LINK_RESULT_RUNNING);
		assert_int_equal(results[i].playerId, i);
		assert_int_equal(results[i].observableAttachments, 1);
		assert_int_equal(results[i].baudMask, 0xF);
		assert_true(results[i].transfers > 16);
		assert_int_equal(results[i].dataErrors, 0);
		assert_int_equal(results[i].missedIrqs, 0);
		assert_int_equal(results[i].duplicateIrqs, 0);
		assert_int_equal(results[i].timeouts, 0);
		struct GBA* gba = pair.players[i].core->board;
		assert_false(GBASIOMultiplayerIsBusy(gba->sio.siocnt));
	}
	struct GBAReplicatedPairMetrics metrics;
	assert_true(GBAReplicatedPairGetMetrics(&pair, &metrics));
	assert_int_equal(metrics.frameNumber, 120);
	assert_true(metrics.transferStarts > 16);
	assert_int_equal(metrics.transferStarts, metrics.transferCompletions);
	assert_int_equal(metrics.transferredWords,
	    metrics.transferCompletions * 2);
	assert_true(metrics.waitEvents >= metrics.transferStarts);
	assert_true(metrics.sleeps[0]);
	assert_true(metrics.sleeps[1]);
	GBAReplicatedPairStop(&pair);
	_deinitFixture(&fixture);
}

M_TEST_DEFINE(perFrameStateTraceIsRepeatable) {
	struct PairFixture fixture;
	_initFixture(&fixture);
	struct GBAReplicatedPair first;
	struct GBAReplicatedPair second;
	_install(&first, &fixture);
	_install(&second, &fixture);
	for (uint64_t frame = 0; frame < 60; ++frame) {
		uint16_t p0 = frame & 1 ? 1 : 0;
		uint16_t p1 = frame & 2 ? 2 : 0;
		assert_int_equal(
		    GBAReplicatedPairSetInputs(&first, frame, p0, p1),
		    GBA_REPLICATED_PAIR_OK);
		assert_int_equal(
		    GBAReplicatedPairSetInputs(&second, frame, p0, p1),
		    GBA_REPLICATED_PAIR_OK);
		assert_int_equal(
		    GBAReplicatedPairRunFrame(&first), GBA_REPLICATED_PAIR_OK);
		assert_int_equal(
		    GBAReplicatedPairRunFrame(&second), GBA_REPLICATED_PAIR_OK);
		assert_memory_equal(first.stateTrace, second.stateTrace,
		    sizeof(first.stateTrace));
	}
	struct GBAReplicatedPairMetrics firstMetrics;
	struct GBAReplicatedPairMetrics secondMetrics;
	assert_true(GBAReplicatedPairGetMetrics(&first, &firstMetrics));
	assert_true(GBAReplicatedPairGetMetrics(&second, &secondMetrics));
	assert_memory_equal(&firstMetrics, &secondMetrics,
	    sizeof(firstMetrics));
	GBAReplicatedPairStop(&second);
	GBAReplicatedPairStop(&first);
	_deinitFixture(&fixture);
}

static void _exchangeAuthored(
	struct GBAReplicatedRuntime* sender,
	struct GBAReplicatedRuntime* receiver, uint16_t keys,
	bool reverse, bool duplicate) {
	struct GBALinkV2Packet packets[
	    GBA_REPLICATED_RUNTIME_MAX_AUTHOR_PACKETS];
	uint8_t count = 0;
	assert_int_equal(
	    GBAReplicatedRuntimeAuthorInput(
	        sender, keys, packets, &count),
	    GBA_REPLICATED_RUNTIME_OK);
	assert_true(count >= 1);
	assert_true(count <= GBA_REPLICATED_RUNTIME_MAX_AUTHOR_PACKETS);
	enum GBALinkRole senderRole = sender->localRole;
	for (unsigned i = 0; i < count; ++i) {
		unsigned index = reverse ? count - i - 1 : i;
		assert_int_equal(
		    GBAReplicatedRuntimeHandleInput(
		        receiver, senderRole,
		        &packets[index].payload.inputBatch),
		    GBA_REPLICATED_RUNTIME_OK);
	}
	if (duplicate) {
		assert_int_equal(
		    GBAReplicatedRuntimeHandleInput(
		        receiver, senderRole,
		        &packets[count - 1].payload.inputBatch),
		    GBA_REPLICATED_RUNTIME_OK);
	}
}

M_TEST_DEFINE(frameRuntimeReleasesOnceAndPacketsScaleOnlyWithFrames) {
	struct PairFixture fixture;
	_initFixture(&fixture);
	struct GBAReplicatedPair hostPair;
	struct GBAReplicatedPair clientPair;
	_install(&hostPair, &fixture);
	_install(&clientPair, &fixture);
	struct GBAReplicatedRuntime host;
	struct GBAReplicatedRuntime client;
	assert_true(GBAReplicatedRuntimeInit(
	    &host, &hostPair, 77, GBA_LINK_ROLE_HOST, 3, 0));
	assert_true(GBAReplicatedRuntimeInit(
	    &client, &clientPair, 77, GBA_LINK_ROLE_CLIENT, 3, 0));

	uint64_t packetsAt60 = 0;
	uint64_t wordsAt60 = 0;
	for (uint64_t frame = 0; frame < 120; ++frame) {
		assert_false(GBAReplicatedRuntimeFrameReady(&host));
		assert_false(GBAReplicatedRuntimeFrameReady(&client));
		_exchangeAuthored(
		    &host, &client, frame & 1, frame == 0, frame == 7);
		_exchangeAuthored(
		    &client, &host, frame & 2, false, frame == 7);
		assert_true(GBAReplicatedRuntimeFrameReady(&host));
		assert_true(GBAReplicatedRuntimeFrameReady(&client));
		assert_int_equal(
		    GBAReplicatedRuntimeRunFrame(&host),
		    GBA_REPLICATED_RUNTIME_OK);
		assert_int_equal(
		    GBAReplicatedRuntimeRunFrame(&client),
		    GBA_REPLICATED_RUNTIME_OK);
		assert_int_equal(
		    GBAReplicatedRuntimeRunFrame(&host),
		    GBA_REPLICATED_RUNTIME_INVALID_STATE);
		assert_memory_equal(hostPair.stateTrace, clientPair.stateTrace,
		    sizeof(hostPair.stateTrace));
		if (frame == 59) {
			struct GBAReplicatedRuntimeMetrics runtimeMetrics;
			struct GBAReplicatedPairMetrics pairMetrics;
			assert_true(GBAReplicatedRuntimeGetMetrics(
			    &host, &runtimeMetrics));
			assert_true(GBAReplicatedPairGetMetrics(
			    &hostPair, &pairMetrics));
			packetsAt60 = runtimeMetrics.authoredPackets;
			wordsAt60 = pairMetrics.transferredWords;
		}
	}
	struct GBAReplicatedRuntimeMetrics hostMetrics;
	struct GBAReplicatedRuntimeMetrics clientMetrics;
	struct GBAReplicatedPairMetrics pairMetrics;
	assert_true(GBAReplicatedRuntimeGetMetrics(&host, &hostMetrics));
	assert_true(GBAReplicatedRuntimeGetMetrics(&client, &clientMetrics));
	assert_true(GBAReplicatedPairGetMetrics(&hostPair, &pairMetrics));
	assert_int_equal(hostMetrics.framesReleased, 120);
	assert_int_equal(clientMetrics.framesReleased, 120);
	assert_int_equal(hostMetrics.authoredPackets, 121);
	assert_int_equal(clientMetrics.authoredPackets, 121);
	assert_int_equal(hostMetrics.authoredPackets - packetsAt60, 60);
	assert_true(pairMetrics.transferredWords > wordsAt60);
	assert_true(wordsAt60);
	assert_int_equal(hostMetrics.exactDuplicates, 1);
	/* Reordered initial seed plus the injected runtime duplicate. */
	assert_int_equal(clientMetrics.exactDuplicates, 2);

	GBAReplicatedRuntimeDeinit(&client);
	GBAReplicatedRuntimeDeinit(&host);
	GBAReplicatedPairStop(&clientPair);
	GBAReplicatedPairStop(&hostPair);
	_deinitFixture(&fixture);
}

M_TEST_DEFINE(invalidBundleOrderFailsWithoutPartialLifetime) {
	struct PairFixture fixture;
	_initFixture(&fixture);
	struct GBAReplicatedPair pair;
	GBAReplicatedPairInit(&pair);
	struct GBAReplicaManifest wrong[2] = {
		fixture.manifests[0], fixture.manifests[1],
	};
	wrong[0].player = 1;
	assert_int_equal(
	    GBAReplicatedPairInstall(
	        &pair, fixture.sources[0], wrong,
	        fixture.payloads, 77),
	    GBA_REPLICATED_PAIR_INVALID_ARGUMENT);
	assert_int_equal(
	    GBASIOLockstepCoordinatorAttached(&pair.coordinator), 0);
	GBAReplicatedPairStop(&pair);
	_deinitFixture(&fixture);
}

M_TEST_SUITE_DEFINE_SETUP_TEARDOWN(GBAReplicatedPair,
	cmocka_unit_test(installsCanonicalP0P1LocalLockstepPair),
	cmocka_unit_test(frameInputsAdvanceExactlyOnceAndRejectConflicts),
	cmocka_unit_test(continuousMultiTransfersPreserveHardwareSemantics),
	cmocka_unit_test(perFrameStateTraceIsRepeatable),
	cmocka_unit_test(frameRuntimeReleasesOnceAndPacketsScaleOnlyWithFrames),
	cmocka_unit_test(invalidBundleOrderFailsWithoutPartialLifetime))
