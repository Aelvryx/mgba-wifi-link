/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "replicated-pair-spike.h"

#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/gba/core.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/sio/lockstep.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/vfs.h>

#include <time.h>

#define PAIR_SPIKE_MAX_RUN_LOOPS 2000000
#define PAIR_SPIKE_VIDEO_WIDTH 256
#define PAIR_SPIKE_VIDEO_HEIGHT 224
#define LINK_RESULT_ADDRESS 0x02000000
#define LINK_RESULT_MAGIC 0x31544B4C
#define LINK_RESULT_TRANSFERS_OFFSET 24

struct PairSpikeUser {
	struct mLockstepUser d;
	int requestedId;
	bool asleep;
	uint64_t sleeps;
	uint64_t wakes;
};

struct PairSpikeState {
	bool active;
	bool coordinatorInitialized;
	bool shadowInitialized;
	bool shadowConfigInitialized;
	struct mCore* primary;
	struct mCore* shadow;
	void* shadowRomData;
	struct VFile* shadowSave;
	mColor* shadowVideo;
	struct GBASIOLockstepCoordinator coordinator;
	struct GBASIOLockstepDriver drivers[2];
	struct PairSpikeUser users[2];
	uint64_t runLoops;
	uint64_t presentedFrames;
	uint64_t startedAtNanoseconds;
	uint64_t runNanoseconds;
	uint64_t maximumFrameNanoseconds;
};

static struct PairSpikeState _pair;

static int _requestedId(struct mLockstepUser* user) {
	return ((struct PairSpikeUser*) user)->requestedId;
}

static void _sleep(struct mLockstepUser* user) {
	struct PairSpikeUser* pairUser =
	    (struct PairSpikeUser*) user;
	pairUser->asleep = true;
	++pairUser->sleeps;
}

static void _wake(struct mLockstepUser* user) {
	struct PairSpikeUser* pairUser =
	    (struct PairSpikeUser*) user;
	pairUser->asleep = false;
	++pairUser->wakes;
}

static uint64_t _monotonicNanoseconds(void) {
	struct timespec now;
	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
		return 0;
	}
	return (uint64_t) now.tv_sec * UINT64_C(1000000000) +
	       (uint64_t) now.tv_nsec;
}

static char* _duplicateOptionString(const char* source) {
	if (!source) {
		return NULL;
	}
	size_t length = strlen(source) + 1;
	char* copy = malloc(length);
	if (copy) {
		memcpy(copy, source, length);
	}
	return copy;
}

static bool _copyOptions(
	struct mCoreOptions* destination,
	const struct mCoreOptions* source) {
	*destination = *source;
	destination->bios = NULL;
	destination->shader = NULL;
	destination->savegamePath = NULL;
	destination->savestatePath = NULL;
	destination->screenshotPath = NULL;
	destination->patchPath = NULL;
	destination->cheatsPath = NULL;

#define COPY_OPTION_STRING(NAME)                                      \
	do {                                                           \
		destination->NAME = _duplicateOptionString(source->NAME); \
		if (source->NAME && !destination->NAME) {                 \
			return false;                                        \
		}                                                          \
	} while (0)
	COPY_OPTION_STRING(bios);
	COPY_OPTION_STRING(shader);
	COPY_OPTION_STRING(savegamePath);
	COPY_OPTION_STRING(savestatePath);
	COPY_OPTION_STRING(screenshotPath);
	COPY_OPTION_STRING(patchPath);
	COPY_OPTION_STRING(cheatsPath);
#undef COPY_OPTION_STRING
	return true;
}

static bool _cloneEffectiveRom(
	struct GBA* primaryGba, size_t romSize) {
	if (!romSize) {
		return false;
	}
	_pair.shadowRomData = malloc(romSize);
	if (!_pair.shadowRomData) {
		return false;
	}
	if (primaryGba->memory.rom) {
		memcpy(
		    _pair.shadowRomData, primaryGba->memory.rom,
		    romSize);
		return true;
	}
	struct VFile* sourceRom = primaryGba->romVf
	                              ? primaryGba->romVf
	                              : primaryGba->mbVf;
	if (!sourceRom) {
		return false;
	}
	off_t originalPosition = sourceRom->seek(
	    sourceRom, 0, SEEK_CUR);
	if (sourceRom->seek(sourceRom, 0, SEEK_SET) < 0) {
		return false;
	}
	size_t total = 0;
	while (total < romSize) {
		ssize_t read = sourceRom->read(
		    sourceRom,
		    (uint8_t*) _pair.shadowRomData + total,
		    romSize - total);
		if (read <= 0) {
			break;
		}
		total += (size_t) read;
	}
	if (originalPosition >= 0) {
		sourceRom->seek(sourceRom, originalPosition, SEEK_SET);
	}
	return total == romSize;
}

static void _createDriver(unsigned index, struct mCore* core) {
	struct PairSpikeUser* user = &_pair.users[index];
	user->requestedId = (int) index;
	user->d.requestedId = _requestedId;
	user->d.sleep = _sleep;
	user->d.wake = _wake;
	GBASIOLockstepDriverCreate(&_pair.drivers[index], &user->d);
	GBASIOLockstepCoordinatorAttach(
	    &_pair.coordinator, &_pair.drivers[index]);
	core->setPeripheral(
	    core, mPERIPH_GBA_LINK_PORT,
	    &_pair.drivers[index].d);
}

static void _logSummary(void) {
	uint64_t wallNanoseconds = _pair.startedAtNanoseconds
	                               ? _monotonicNanoseconds() -
	                                     _pair.startedAtNanoseconds
	                               : 0;
	uint64_t averageUs = _pair.presentedFrames
	                         ? _pair.runNanoseconds /
	                               _pair.presentedFrames / 1000
	                         : 0;
	uint32_t transfers = 0;
	if (_pair.primary->rawRead32(
	        _pair.primary, LINK_RESULT_ADDRESS, -1) ==
	    LINK_RESULT_MAGIC) {
		transfers = _pair.primary->rawRead32(
		    _pair.primary,
		    LINK_RESULT_ADDRESS +
		        LINK_RESULT_TRANSFERS_OFFSET,
		    -1);
	}
	uint64_t wallFpsMilli = wallNanoseconds
	                            ? _pair.presentedFrames *
	                                  UINT64_C(1000000000000) /
	                                  wallNanoseconds
	                            : 0;
	uint64_t emulatedMilliseconds = _pair.presentedFrames
	                                    ? _pair.presentedFrames *
	                                          _pair.primary->frameCycles(
	                                              _pair.primary) *
	                                          UINT64_C(1000) /
	                                          _pair.primary->frequency(
	                                              _pair.primary)
	                                    : 0;
	uint64_t serialWordsPerSecondMilli = emulatedMilliseconds
	                                           ? (uint64_t) transfers * 2 *
	                                                 UINT64_C(1000000) /
	                                                 emulatedMilliseconds
	                                           : 0;
	mLOG(
	    STATUS, INFO,
	    "replicated-pair diagnostic: frames=%" PRIu64
	    " p0=%u p1=%u transfers=%u run-loops=%" PRIu64
	    " waits=%" PRIu64 "/%" PRIu64,
	    _pair.presentedFrames,
	    _pair.primary->frameCounter(_pair.primary),
	    _pair.shadow->frameCounter(_pair.shadow), transfers,
	    _pair.runLoops, _pair.users[0].sleeps,
	    _pair.users[1].sleeps);
	mLOG(
	    STATUS, INFO,
	    "replicated-pair performance: frames=%" PRIu64
	    " average-us=%" PRIu64 " maximum-us=%" PRIu64
	    " wall-ms=%" PRIu64,
	    _pair.presentedFrames,
	    averageUs, _pair.maximumFrameNanoseconds / 1000,
	    wallNanoseconds / UINT64_C(1000000));
	mLOG(
	    STATUS, INFO,
	    "replicated-pair rate: frames=%" PRIu64
	    " wall-fps-milli=%" PRIu64
	    " serial-words-per-emu-second-milli=%" PRIu64,
	    _pair.presentedFrames, wallFpsMilli,
	    serialWordsPerSecondMilli);
}

bool mLibretroReplicatedPairSpikeStart(struct mCore* primary) {
	if (_pair.active) {
		return _pair.primary == primary;
	}
	if (!primary || primary->platform(primary) != mPLATFORM_GBA) {
		mLOG(STATUS, ERROR,
		     "replicated-pair diagnostic requires a loaded GBA core");
		return false;
	}
	memset(&_pair, 0, sizeof(_pair));
	_pair.primary = primary;
	GBASIOLockstepCoordinatorInit(&_pair.coordinator);
	_pair.coordinatorInitialized = true;

	struct GBA* primaryGba = primary->board;
	_pair.shadow = GBACoreCreate();
	if (!_pair.shadow || !_pair.shadow->init(_pair.shadow)) {
		mLOG(STATUS, ERROR,
		     "replicated-pair diagnostic could not initialize shadow core");
		mLibretroReplicatedPairSpikeStop();
		return false;
	}
	_pair.shadowInitialized = true;
	mCoreInitConfig(_pair.shadow, NULL);
	_pair.shadowConfigInitialized = true;
	if (!_copyOptions(&_pair.shadow->opts, &primary->opts)) {
		mLOG(STATUS, ERROR,
		     "replicated-pair diagnostic could not clone core options");
		mLibretroReplicatedPairSpikeStop();
		return false;
	}
	mCoreLoadForeignConfig(_pair.shadow, &primary->config);
	size_t romSize = primary->romSize(primary);
	if (!_cloneEffectiveRom(primaryGba, romSize)) {
		mLOG(STATUS, ERROR,
		     "replicated-pair diagnostic could not clone effective ROM");
		mLibretroReplicatedPairSpikeStop();
		return false;
	}
	struct VFile* shadowRom = VFileFromMemory(
	    _pair.shadowRomData, romSize);
	if (!shadowRom ||
	    !_pair.shadow->loadROM(_pair.shadow, shadowRom)) {
		if (shadowRom) {
			shadowRom->close(shadowRom);
		}
		mLOG(STATUS, ERROR,
		     "replicated-pair diagnostic could not load cloned ROM");
		mLibretroReplicatedPairSpikeStop();
		return false;
	}
	if (primaryGba->biosVf) {
		struct VFile* shadowBios = VFileFromConstMemory(
		    primaryGba->memory.bios, GBA_SIZE_BIOS);
		if (!shadowBios || !_pair.shadow->loadBIOS(
		                       _pair.shadow, shadowBios, 0)) {
			if (shadowBios) {
				shadowBios->close(shadowBios);
			}
			mLOG(STATUS, ERROR,
			     "replicated-pair diagnostic could not clone BIOS");
			mLibretroReplicatedPairSpikeStop();
			return false;
		}
	}
	_pair.shadowSave = VFileMemChunk(NULL, 0);
	if (!_pair.shadowSave ||
	    !_pair.shadow->loadTemporarySave(
	        _pair.shadow, _pair.shadowSave)) {
		mLOG(STATUS, ERROR,
		     "replicated-pair diagnostic could not create shadow save");
		mLibretroReplicatedPairSpikeStop();
		return false;
	}
	_pair.shadowVideo = malloc(
	    PAIR_SPIKE_VIDEO_WIDTH * PAIR_SPIKE_VIDEO_HEIGHT *
	    sizeof(*_pair.shadowVideo));
	if (!_pair.shadowVideo) {
		mLOG(STATUS, ERROR,
		     "replicated-pair diagnostic could not allocate shadow video");
		mLibretroReplicatedPairSpikeStop();
		return false;
	}
	_pair.shadow->setVideoBuffer(
	    _pair.shadow, _pair.shadowVideo,
	    PAIR_SPIKE_VIDEO_WIDTH);
	_pair.shadow->setAudioBufferSize(
	    _pair.shadow, primary->getAudioBufferSize(primary));
	_pair.shadow->reset(_pair.shadow);

	_createDriver(0, _pair.primary);
	_createDriver(1, _pair.shadow);
	if (GBASIOLockstepCoordinatorAttached(
	        &_pair.coordinator) != 2 ||
	    _pair.drivers[0].d.deviceId(&_pair.drivers[0].d) != 0 ||
	    _pair.drivers[1].d.deviceId(&_pair.drivers[1].d) != 1) {
		mLOG(STATUS, ERROR,
		     "replicated-pair diagnostic could not assign local players");
		mLibretroReplicatedPairSpikeStop();
		return false;
	}
	_pair.active = true;
	_pair.startedAtNanoseconds = _monotonicNanoseconds();
	mLOG(
	    STATUS, INFO,
	    "replicated-pair diagnostic started with cooperative scheduling");
	return true;
}

bool mLibretroReplicatedPairSpikeRunFrame(uint16_t keys) {
	if (!_pair.active) {
		return false;
	}
	_pair.primary->setKeys(_pair.primary, keys);
	_pair.shadow->setKeys(_pair.shadow, keys);
	uint32_t target0 =
	    _pair.primary->frameCounter(_pair.primary) + 1;
	uint32_t target1 =
	    _pair.shadow->frameCounter(_pair.shadow) + 1;
	uint64_t startedAt = _monotonicNanoseconds();
	uint64_t frameRunLoops = 0;
	while (_pair.primary->frameCounter(_pair.primary) < target0 ||
	       _pair.shadow->frameCounter(_pair.shadow) < target1) {
		bool ran = false;
		struct mCore* cores[2] = {
			_pair.primary, _pair.shadow,
		};
		for (unsigned i = 0; i < 2; ++i) {
			if (_pair.users[i].asleep) {
				continue;
			}
			cores[i]->runLoop(cores[i]);
			++frameRunLoops;
			ran = true;
		}
		if (!ran || frameRunLoops >= PAIR_SPIKE_MAX_RUN_LOOPS) {
			mLOG(
			    STATUS, ERROR,
			    "replicated-pair diagnostic stalled at frame %u/%u",
			    _pair.primary->frameCounter(_pair.primary),
			    _pair.shadow->frameCounter(_pair.shadow));
			return false;
		}
	}
	uint64_t elapsed = _monotonicNanoseconds() - startedAt;
	_pair.runLoops += frameRunLoops;
	_pair.runNanoseconds += elapsed;
	if (elapsed > _pair.maximumFrameNanoseconds) {
		_pair.maximumFrameNanoseconds = elapsed;
	}
	++_pair.presentedFrames;
	mAudioBufferClear(_pair.shadow->getAudioBuffer(_pair.shadow));
	if (!(_pair.presentedFrames % 60)) {
		_logSummary();
	}
	return _pair.primary->frameCounter(_pair.primary) == target0 &&
	       _pair.shadow->frameCounter(_pair.shadow) == target1;
}

bool mLibretroReplicatedPairSpikeIsActive(void) {
	return _pair.active;
}

void mLibretroReplicatedPairSpikeStop(void) {
	if (_pair.active) {
		_logSummary();
	}
	if (_pair.coordinatorInitialized) {
		if (_pair.shadow && _pair.drivers[1].coordinator) {
			_pair.shadow->setPeripheral(
			    _pair.shadow, mPERIPH_GBA_LINK_PORT, NULL);
			GBASIOLockstepCoordinatorDetach(
			    &_pair.coordinator, &_pair.drivers[1]);
		}
		if (_pair.primary && _pair.drivers[0].coordinator) {
			_pair.primary->setPeripheral(
			    _pair.primary, mPERIPH_GBA_LINK_PORT, NULL);
			GBASIOLockstepCoordinatorDetach(
			    &_pair.coordinator, &_pair.drivers[0]);
		}
		GBASIOLockstepCoordinatorDeinit(&_pair.coordinator);
	}
	if (_pair.shadowConfigInitialized) {
		mCoreConfigDeinit(&_pair.shadow->config);
	}
	if (_pair.shadowInitialized) {
		_pair.shadow->deinit(_pair.shadow);
	} else {
		free(_pair.shadow);
	}
	if (_pair.shadowSave) {
		_pair.shadowSave->close(_pair.shadowSave);
	}
	free(_pair.shadowRomData);
	free(_pair.shadowVideo);
	memset(&_pair, 0, sizeof(_pair));
}
