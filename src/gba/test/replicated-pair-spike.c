/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "util/test/suite.h"

#include <mgba/core/core.h>
#include <mgba/core/thread.h>
#include <mgba/gba/core.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/sio/lockstep.h>
#include <mgba-util/threading.h>
#include <mgba-util/vfs.h>

#include <fcntl.h>
#include <sys/time.h>

#ifndef GBA_LINK_CONTINUOUS_ROM_PATH
#error "GBA_LINK_CONTINUOUS_ROM_PATH must name the continuous link-test ROM"
#endif

#define LINK_RESULT_ADDRESS 0x02000000
#define LINK_RESULT_RUNNING 3
#define LINK_RESULT_FAIL 0x80000000U
#define SPIKE_TARGET_FRAMES 600
#define SPIKE_TIMEOUT_MS 10000
#define FNV64_OFFSET UINT64_C(14695981039346656037)
#define FNV64_PRIME UINT64_C(1099511628211)

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

struct ReplicatedPairSpike;

struct CooperativeUser {
	struct mLockstepUser d;
	int requestedId;
	bool asleep;
	uint64_t sleeps;
	uint64_t wakes;
};

#ifndef DISABLE_THREADING
struct ThreadUser {
	struct mLockstepThreadUser d;
	int requestedId;
	struct ReplicatedPairSpike* pair;
	unsigned index;
};
#endif

struct PairPlayer {
	struct ReplicatedPairSpike* pair;
	unsigned index;
	struct mCore* core;
	struct GBA* gba;
	struct VFile* temporarySave;
	struct GBASIOLockstepDriver driver;
	struct CooperativeUser cooperativeUser;
#ifndef DISABLE_THREADING
	struct mCoreThread thread;
	struct ThreadUser threadUser;
#endif
};

struct ReplicatedPairSpike {
	struct GBASIOLockstepCoordinator coordinator;
	struct PairPlayer players[2];
#ifndef DISABLE_THREADING
	Mutex metricsMutex;
	Condition metricsChanged;
	uint32_t workerFrames[2];
	uint64_t sleeps[2];
	uint64_t wakes[2];
	uint64_t traceSamples[2];
	uint64_t traceHashes[2];
#endif
};

static struct mLogger _silentLogger;
static bool _cooperativeEvidenceRecorded;
static uint64_t _cooperativeTraceHashes[2];
static uint64_t _cooperativeRunLoops;
static uint32_t _cooperativeTransfers[2];

static void _discardLog(
	struct mLogger* logger, int category,
	enum mLogLevel level, const char* format,
	va_list args) {
	UNUSED(logger);
	UNUSED(category);
	UNUSED(level);
	UNUSED(format);
	UNUSED(args);
}

static uint64_t _monotonicMs(void) {
	struct timeval now;
	gettimeofday(&now, NULL);
	return (uint64_t) now.tv_sec * 1000 +
	       (uint64_t) now.tv_usec / 1000;
}

static int _cooperativeRequestedId(struct mLockstepUser* user) {
	struct CooperativeUser* cooperative =
	    (struct CooperativeUser*) user;
	return cooperative->requestedId;
}

static void _cooperativeSleep(struct mLockstepUser* user) {
	struct CooperativeUser* cooperative =
	    (struct CooperativeUser*) user;
	cooperative->asleep = true;
	++cooperative->sleeps;
}

static void _cooperativeWake(struct mLockstepUser* user) {
	struct CooperativeUser* cooperative =
	    (struct CooperativeUser*) user;
	cooperative->asleep = false;
	++cooperative->wakes;
}

static void _noopUser(struct mLockstepUser* user) {
	UNUSED(user);
}

static uint64_t _traceWord(uint64_t hash, uint32_t word) {
	for (unsigned i = 0; i < sizeof(word); ++i) {
		hash ^= word & 0xFF;
		hash *= FNV64_PRIME;
		word >>= 8;
	}
	return hash;
}

static uint64_t _recordFrameTrace(
	struct PairPlayer* player, uint64_t hash) {
	hash = _traceWord(
	    hash, player->core->frameCounter(player->core));
	hash = _traceWord(
	    hash, player->core->rawRead32(
	              player->core, LINK_RESULT_ADDRESS +
	                                offsetof(struct LinkTestResult, transfers),
	              -1));
	hash = _traceWord(hash, player->gba->sio.siocnt);
	hash = _traceWord(
	    hash, (uint32_t) mTimingCurrentTime(&player->gba->timing));
	return hash;
}

#ifndef DISABLE_THREADING
static int _threadRequestedId(struct mLockstepUser* user) {
	struct ThreadUser* thread = (struct ThreadUser*) user;
	return thread->requestedId;
}

static void _threadSleep(struct mLockstepUser* user) {
	struct ThreadUser* thread = (struct ThreadUser*) user;
	MutexLock(&thread->pair->metricsMutex);
	++thread->pair->sleeps[thread->index];
	MutexUnlock(&thread->pair->metricsMutex);
	mCoreThreadWaitFromThread(thread->d.thread);
}

static void _threadWake(struct mLockstepUser* user) {
	struct ThreadUser* thread = (struct ThreadUser*) user;
	MutexLock(&thread->pair->metricsMutex);
	++thread->pair->wakes[thread->index];
	MutexUnlock(&thread->pair->metricsMutex);
	mCoreThreadStopWaiting(thread->d.thread);
}

static void _workerFrame(struct mCoreThread* thread) {
	struct PairPlayer* player = thread->userData;
	struct ReplicatedPairSpike* pair = player->pair;
	MutexLock(&pair->metricsMutex);
	pair->workerFrames[player->index] =
	    player->core->frameCounter(player->core);
	pair->traceHashes[player->index] = _recordFrameTrace(
	    player, pair->traceHashes[player->index]);
	++pair->traceSamples[player->index];
	ConditionWake(&pair->metricsChanged);
	MutexUnlock(&pair->metricsMutex);
}
#endif

static void _initPlayer(
	struct ReplicatedPairSpike* pair, unsigned index,
	bool threaded) {
	struct PairPlayer* player = &pair->players[index];
	memset(player, 0, sizeof(*player));
	player->pair = pair;
	player->index = index;
	player->core = GBACoreCreate();
	assert_non_null(player->core);
	assert_true(player->core->init(player->core));
	mCoreInitConfig(player->core, NULL);
	assert_true(player->core->loadROM(
	    player->core,
	    VFileOpen(GBA_LINK_CONTINUOUS_ROM_PATH, O_RDONLY)));
	player->temporarySave = VFileMemChunk(NULL, 0);
	assert_non_null(player->temporarySave);
	assert_true(player->core->loadTemporarySave(
	    player->core, player->temporarySave));
	player->core->reset(player->core);
	player->gba = player->core->board;

	struct mLockstepUser* user;
#ifndef DISABLE_THREADING
	if (threaded) {
		player->thread.core = player->core;
		player->thread.frameCallback = _workerFrame;
		player->thread.userData = player;
		mLockstepThreadUserInit(
		    &player->threadUser.d, &player->thread);
		player->threadUser.requestedId = (int) index;
		player->threadUser.pair = pair;
		player->threadUser.index = index;
		player->threadUser.d.d.requestedId =
		    _threadRequestedId;
		player->threadUser.d.d.sleep = _threadSleep;
		player->threadUser.d.d.wake = _threadWake;
		player->thread.logger.logger = &_silentLogger;
		user = &player->threadUser.d.d;
	} else
#else
	UNUSED(threaded);
#endif
	{
		player->cooperativeUser.requestedId = (int) index;
		player->cooperativeUser.d.requestedId =
		    _cooperativeRequestedId;
		player->cooperativeUser.d.sleep =
		    _cooperativeSleep;
		player->cooperativeUser.d.wake =
		    _cooperativeWake;
		user = &player->cooperativeUser.d;
	}

	GBASIOLockstepDriverCreate(&player->driver, user);
	GBASIOLockstepCoordinatorAttach(
	    &pair->coordinator, &player->driver);
	player->core->setPeripheral(
	    player->core, mPERIPH_GBA_LINK_PORT,
	    &player->driver.d);
}

static void _initPair(
	struct ReplicatedPairSpike* pair, bool threaded) {
	memset(pair, 0, sizeof(*pair));
	GBASIOLockstepCoordinatorInit(&pair->coordinator);
#ifndef DISABLE_THREADING
	MutexInit(&pair->metricsMutex);
	ConditionInit(&pair->metricsChanged);
	pair->traceHashes[0] = FNV64_OFFSET;
	pair->traceHashes[1] = FNV64_OFFSET;
#endif
	_initPlayer(pair, 0, threaded);
	_initPlayer(pair, 1, threaded);
	assert_int_equal(
	    GBASIOLockstepCoordinatorAttached(
	        &pair->coordinator),
	    2);
	assert_ptr_not_equal(
	    pair->players[0].gba->memory.savedata.vf,
	    pair->players[1].gba->memory.savedata.vf);
	assert_int_equal(
	    pair->players[0].driver.d.deviceId(
	        &pair->players[0].driver.d),
	    0);
	assert_int_equal(
	    pair->players[1].driver.d.deviceId(
	        &pair->players[1].driver.d),
	    1);
}

static void _readResult(
	struct PairPlayer* player,
	struct LinkTestResult* result) {
	memset(result, 0, sizeof(*result));
	uint8_t* bytes = (uint8_t*) result;
	for (size_t offset = 0; offset < sizeof(*result);
	     offset += sizeof(uint32_t)) {
		uint32_t word = player->core->rawRead32(
		    player->core,
		    LINK_RESULT_ADDRESS + (uint32_t) offset, -1);
		size_t remaining = sizeof(*result) - offset;
		memcpy(
		    &bytes[offset], &word,
		    remaining < sizeof(word)
		        ? remaining
		        : sizeof(word));
	}
}

static void _assertHealthyResult(
	const struct LinkTestResult* result, unsigned playerId) {
	assert_false(result->status & LINK_RESULT_FAIL);
	assert_int_equal(result->status, LINK_RESULT_RUNNING);
	assert_int_equal(result->playerId, playerId);
	assert_int_equal(result->observableAttachments, 1);
	assert_int_equal(result->baudMask, 0xF);
	assert_true(result->transfers >= 16);
	assert_int_equal(result->dataErrors, 0);
	assert_int_equal(result->missedIrqs, 0);
	assert_int_equal(result->duplicateIrqs, 0);
	assert_int_equal(result->timeouts, 0);
}

static void _detachPlayers(
	struct ReplicatedPairSpike* pair, bool threaded) {
#ifndef DISABLE_THREADING
	if (threaded) {
		for (unsigned i = 0; i < 2; ++i) {
			pair->players[i].threadUser.d.d.sleep =
			    _noopUser;
			pair->players[i].threadUser.d.d.wake =
			    _noopUser;
		}
	}
#else
	UNUSED(threaded);
#endif
	for (int i = 1; i >= 0; --i) {
		struct PairPlayer* player = &pair->players[i];
		player->core->setPeripheral(
		    player->core, mPERIPH_GBA_LINK_PORT, NULL);
		GBASIOLockstepCoordinatorDetach(
		    &pair->coordinator, &player->driver);
	}
	assert_int_equal(
	    GBASIOLockstepCoordinatorAttached(
	        &pair->coordinator),
	    0);
}

static void _deinitPair(
	struct ReplicatedPairSpike* pair, bool threaded) {
	_detachPlayers(pair, threaded);
	for (unsigned i = 0; i < 2; ++i) {
		struct mCore* core = pair->players[i].core;
		mCoreConfigDeinit(&core->config);
		core->deinit(core);
		assert_true(pair->players[i].temporarySave->close(
		    pair->players[i].temporarySave));
	}
#ifndef DISABLE_THREADING
	ConditionDeinit(&pair->metricsChanged);
	MutexDeinit(&pair->metricsMutex);
#endif
	GBASIOLockstepCoordinatorDeinit(&pair->coordinator);
}

enum CooperativeMilestone {
	MILESTONE_TRANSFER_START,
	MILESTONE_TRANSFER_COMPLETION_PENDING,
	MILESTONE_TRANSFER_IDLE,
};

static bool _milestoneReached(
	struct ReplicatedPairSpike* pair,
	enum CooperativeMilestone milestone) {
	bool completion0 = mTimingIsScheduled(
	    &pair->players[0].gba->timing,
	    &pair->players[0].gba->sio.completeEvent);
	bool completion1 = mTimingIsScheduled(
	    &pair->players[1].gba->timing,
	    &pair->players[1].gba->sio.completeEvent);
	uint32_t transfers0 = pair->players[0].core->rawRead32(
	    pair->players[0].core,
	    LINK_RESULT_ADDRESS +
	        offsetof(struct LinkTestResult, transfers),
	    -1);
	uint32_t transfers1 = pair->players[1].core->rawRead32(
	    pair->players[1].core,
	    LINK_RESULT_ADDRESS +
	        offsetof(struct LinkTestResult, transfers),
	    -1);
	switch (milestone) {
	case MILESTONE_TRANSFER_START:
		return pair->coordinator.transferActive;
	case MILESTONE_TRANSFER_COMPLETION_PENDING:
		return !pair->coordinator.transferActive &&
		       completion0 && completion1;
	case MILESTONE_TRANSFER_IDLE:
		return transfers0 && transfers1 &&
		       !pair->coordinator.transferActive &&
		       !completion0 && !completion1;
	}
	return false;
}

static void _runCooperativeToMilestone(
	struct ReplicatedPairSpike* pair,
	enum CooperativeMilestone milestone) {
	for (uint64_t iteration = 0; iteration < 2000000;
	     ++iteration) {
		for (unsigned i = 0; i < 2; ++i) {
			struct PairPlayer* player = &pair->players[i];
			if (!player->cooperativeUser.asleep) {
				player->core->runLoop(player->core);
			}
			if (_milestoneReached(pair, milestone)) {
				return;
			}
		}
	}
	fail_msg("cooperative pair did not reach milestone %u", milestone);
}

M_TEST_DEFINE(cooperativePairTeardownAtCriticalBoundaries) {
	UNUSED(state);
	struct ReplicatedPairSpike pair;

	_initPair(&pair, false);
	_runCooperativeToMilestone(&pair, MILESTONE_TRANSFER_IDLE);
	_deinitPair(&pair, false);

	_initPair(&pair, false);
	assert_int_equal((uintptr_t) pair.players[0].gba %
	    _Alignof(struct GBA), 0);
	GBASIOWriteRCNT(&pair.players[0].gba->sio, 0);
	assert_int_equal((uintptr_t) pair.players[0].gba %
	    _Alignof(struct GBA), 0);
	GBASIOWriteSIOCNT(&pair.players[0].gba->sio, 0x6000);
	assert_true(pair.coordinator.waiting != 0);
	_deinitPair(&pair, false);

	_initPair(&pair, false);
	_runCooperativeToMilestone(&pair, MILESTONE_TRANSFER_START);
	_deinitPair(&pair, false);

	_initPair(&pair, false);
	_runCooperativeToMilestone(
	    &pair, MILESTONE_TRANSFER_COMPLETION_PENDING);
	_deinitPair(&pair, false);

	_initPair(&pair, false);
	_runCooperativeToMilestone(&pair, MILESTONE_TRANSFER_START);
	pair.players[0].core->reset(pair.players[0].core);
	pair.players[1].core->reset(pair.players[1].core);
	_deinitPair(&pair, false);
}

M_TEST_DEFINE(partialPairConstructionTearsDownCleanly) {
	UNUSED(state);
	struct ReplicatedPairSpike pair;
	memset(&pair, 0, sizeof(pair));
	GBASIOLockstepCoordinatorInit(&pair.coordinator);
#ifndef DISABLE_THREADING
	MutexInit(&pair.metricsMutex);
	ConditionInit(&pair.metricsChanged);
#endif
	_initPlayer(&pair, 0, false);
	assert_int_equal(
	    GBASIOLockstepCoordinatorAttached(&pair.coordinator), 1);
	pair.players[0].core->setPeripheral(
	    pair.players[0].core, mPERIPH_GBA_LINK_PORT, NULL);
	GBASIOLockstepCoordinatorDetach(
	    &pair.coordinator, &pair.players[0].driver);
	mCoreConfigDeinit(&pair.players[0].core->config);
	pair.players[0].core->deinit(pair.players[0].core);
	assert_true(pair.players[0].temporarySave->close(
	    pair.players[0].temporarySave));
#ifndef DISABLE_THREADING
	ConditionDeinit(&pair.metricsChanged);
	MutexDeinit(&pair.metricsMutex);
#endif
	assert_int_equal(
	    GBASIOLockstepCoordinatorAttached(&pair.coordinator), 0);
	GBASIOLockstepCoordinatorDeinit(&pair.coordinator);
}

M_TEST_DEFINE(cooperativePairRunsContinuousLinkForTenSeconds) {
	UNUSED(state);
	struct ReplicatedPairSpike pair;
	_initPair(&pair, false);
	uint64_t startedAtMs = _monotonicMs();
	uint64_t runLoops = 0;
	uint32_t tracedFrames[2] = {0};
	uint64_t traceHashes[2] = {
		FNV64_OFFSET, FNV64_OFFSET,
	};
	while (pair.players[0].core->frameCounter(
	           pair.players[0].core) <
	           SPIKE_TARGET_FRAMES ||
	       pair.players[1].core->frameCounter(
	           pair.players[1].core) <
	           SPIKE_TARGET_FRAMES) {
		bool ran = false;
		for (unsigned i = 0; i < 2; ++i) {
			struct PairPlayer* player = &pair.players[i];
			if (player->cooperativeUser.asleep) {
				continue;
			}
			player->core->runLoop(player->core);
			++runLoops;
			uint32_t frame = player->core->frameCounter(
			    player->core);
			if (frame != tracedFrames[i]) {
				assert_int_equal(frame, tracedFrames[i] + 1);
				traceHashes[i] = _recordFrameTrace(
				    player, traceHashes[i]);
				tracedFrames[i] = frame;
			}
			ran = true;
		}
		assert_true(ran);
		assert_true(
		    _monotonicMs() - startedAtMs <
		    SPIKE_TIMEOUT_MS);
	}

	struct LinkTestResult results[2];
	for (unsigned i = 0; i < 2; ++i) {
		_readResult(&pair.players[i], &results[i]);
		_assertHealthyResult(&results[i], i);
		assert_true(pair.players[i].cooperativeUser.sleeps > 0);
		assert_true(pair.players[i].cooperativeUser.wakes > 0);
		assert_int_equal(tracedFrames[i], SPIKE_TARGET_FRAMES);
		assert_true(traceHashes[i] != FNV64_OFFSET);
	}
	assert_true(
	    results[0].transfers == results[1].transfers ||
	    results[0].transfers + 1 == results[1].transfers ||
	    results[1].transfers + 1 == results[0].transfers);
	if (_cooperativeEvidenceRecorded) {
		assert_memory_equal(
		    traceHashes, _cooperativeTraceHashes,
		    sizeof(traceHashes));
		assert_int_equal(runLoops, _cooperativeRunLoops);
		assert_int_equal(
		    results[0].transfers,
		    _cooperativeTransfers[0]);
		assert_int_equal(
		    results[1].transfers,
		    _cooperativeTransfers[1]);
	} else {
		memcpy(
		    _cooperativeTraceHashes, traceHashes,
		    sizeof(traceHashes));
		_cooperativeRunLoops = runLoops;
		_cooperativeTransfers[0] = results[0].transfers;
		_cooperativeTransfers[1] = results[1].transfers;
		_cooperativeEvidenceRecorded = true;
	}
	fprintf(
	    stderr,
	    "cooperative pair: frames=%u/%u transfers=%u/%u"
	    " waits=%" PRIu64 "/%" PRIu64
	    " trace=%016" PRIx64 "/%016" PRIx64
	    " run-loops=%" PRIu64 " wall-ms=%" PRIu64 "\n",
	    pair.players[0].core->frameCounter(pair.players[0].core),
	    pair.players[1].core->frameCounter(pair.players[1].core),
	    results[0].transfers, results[1].transfers,
	    pair.players[0].cooperativeUser.sleeps,
	    pair.players[1].cooperativeUser.sleeps,
	    traceHashes[0], traceHashes[1],
	    runLoops, _monotonicMs() - startedAtMs);
	_deinitPair(&pair, false);
}

M_TEST_DEFINE(cooperativePairTraceIsRepeatable) {
	cooperativePairRunsContinuousLinkForTenSeconds(state);
}

#ifndef DISABLE_THREADING
M_TEST_DEFINE(twoWorkerPairRunsContinuousLinkForTenSeconds) {
	UNUSED(state);
	struct ReplicatedPairSpike pair;
	_initPair(&pair, true);
	uint64_t startedAtMs = _monotonicMs();
	assert_true(mCoreThreadStart(&pair.players[0].thread));
	assert_true(mCoreThreadStart(&pair.players[1].thread));

	MutexLock(&pair.metricsMutex);
	while (pair.workerFrames[0] < SPIKE_TARGET_FRAMES ||
	       pair.workerFrames[1] < SPIKE_TARGET_FRAMES) {
		assert_true(
		    _monotonicMs() - startedAtMs <
		    SPIKE_TIMEOUT_MS);
		ConditionWaitTimed(
		    &pair.metricsChanged, &pair.metricsMutex, 100);
	}
	uint32_t frames[2] = {
		pair.workerFrames[0], pair.workerFrames[1],
	};
	uint64_t sleeps[2] = {
		pair.sleeps[0], pair.sleeps[1],
	};
	uint64_t traces[2] = {
		pair.traceHashes[0], pair.traceHashes[1],
	};
	uint64_t traceSamples[2] = {
		pair.traceSamples[0], pair.traceSamples[1],
	};
	MutexUnlock(&pair.metricsMutex);

	mCoreThreadEnd(&pair.players[0].thread);
	mCoreThreadEnd(&pair.players[1].thread);
	mCoreThreadJoin(&pair.players[0].thread);
	mCoreThreadJoin(&pair.players[1].thread);

	struct LinkTestResult results[2];
	for (unsigned i = 0; i < 2; ++i) {
		_readResult(&pair.players[i], &results[i]);
		_assertHealthyResult(&results[i], i);
		assert_true(sleeps[i] > 0);
		assert_true(traces[i] != FNV64_OFFSET);
		assert_true(traceSamples[i] >= SPIKE_TARGET_FRAMES);
	}
	assert_true(
	    results[0].transfers == results[1].transfers ||
	    results[0].transfers + 1 == results[1].transfers ||
	    results[1].transfers + 1 == results[0].transfers);
	fprintf(
	    stderr,
	    "two-worker pair: frames=%u/%u transfers=%u/%u"
	    " waits=%" PRIu64 "/%" PRIu64
	    " trace=%016" PRIx64 "/%016" PRIx64
	    " wall-ms=%" PRIu64 "\n",
	    frames[0], frames[1], results[0].transfers,
	    results[1].transfers, sleeps[0], sleeps[1],
	    traces[0], traces[1], _monotonicMs() - startedAtMs);
	_deinitPair(&pair, true);
}
#endif

int main(void) {
	_silentLogger.log = _discardLog;
	mLogSetDefaultLogger(&_silentLogger);
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(
		    partialPairConstructionTearsDownCleanly),
		cmocka_unit_test(
		    cooperativePairTeardownAtCriticalBoundaries),
		cmocka_unit_test(
		    cooperativePairRunsContinuousLinkForTenSeconds),
		cmocka_unit_test(
		    cooperativePairTraceIsRepeatable),
#ifndef DISABLE_THREADING
		cmocka_unit_test(
		    twoWorkerPairRunsContinuousLinkForTenSeconds),
#endif
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
