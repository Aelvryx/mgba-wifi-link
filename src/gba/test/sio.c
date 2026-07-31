/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "util/test/suite.h"

#include <mgba/core/core.h>
#include <mgba/gba/core.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/io.h>
#include <mgba/internal/gba/sio.h>
#include <mgba/internal/gba/sio/lockstep.h>

struct TestSIODriver {
	struct GBASIODriver d;
	int setModeCalls;
	int startCalls;
	int connectedCalls;
	int deviceIdCalls;
	int writeSIOCNTCalls;
	int writeRCNTCalls;
	int finishMultiplayerCalls;
	int finishNormal8Calls;
	int finishNormal32Calls;
};

struct TransferObservation {
	uint16_t siocntBeforeCompletion;
	uint16_t rcntBeforeCompletion;
	int32_t cyclesUntilCompletion;
	enum GBASIOMode transferMode;
	uint16_t siocntAfterCompletion;
	uint16_t rcntAfterCompletion;
	uint16_t data[4];
	uint16_t interruptFlags;
};

struct ModeWriteObservation {
	enum GBASIOMode mode;
	uint16_t siocnt;
	uint16_t rcnt;
	uint16_t results[5];
	size_t resultCount;
	bool completionScheduled;
};

struct CharacterizationDriver {
	struct GBASIODriver d;
	int deviceId;
	int connectedDevices;
	int startCalls;
	int finishMultiplayerCalls;
};

struct ScheduledRemoteStart {
	struct mTimingEvent event;
	struct GBASIO* sio;
	bool handled;
};

static const int32_t _noPeerMultiCycles[4] = {
	31976,
	8378,
	5750,
	3140,
};

static bool _handlesMulti(struct GBASIODriver* driver, enum GBASIOMode mode) {
	UNUSED(driver);
	return mode == GBA_SIO_MULTI;
}

static void _setMode(struct GBASIODriver* driver, enum GBASIOMode mode) {
	UNUSED(mode);
	struct TestSIODriver* test = (struct TestSIODriver*) driver;
	++test->setModeCalls;
}

static struct GBASIOStartResult _start(struct GBASIODriver* driver) {
	struct TestSIODriver* test = (struct TestSIODriver*) driver;
	++test->startCalls;
	return (struct GBASIOStartResult) {
		.ownership = GBA_SIO_START_COMMON,
		.effectivePeerCount = 1,
	};
}

static int _connectedDevices(struct GBASIODriver* driver) {
	struct TestSIODriver* test = (struct TestSIODriver*) driver;
	++test->connectedCalls;
	return 1;
}

static int _deviceId(struct GBASIODriver* driver) {
	struct TestSIODriver* test = (struct TestSIODriver*) driver;
	++test->deviceIdCalls;
	return 1;
}

static uint16_t _writeSIOCNT(struct GBASIODriver* driver, uint16_t value) {
	struct TestSIODriver* test = (struct TestSIODriver*) driver;
	++test->writeSIOCNTCalls;
	return value ^ 0xFFFF;
}

static uint16_t _writeRCNT(struct GBASIODriver* driver, uint16_t value) {
	struct TestSIODriver* test = (struct TestSIODriver*) driver;
	++test->writeRCNTCalls;
	return value ^ 0xFFFF;
}

static void _finishMultiplayer(struct GBASIODriver* driver, uint16_t data[4]) {
	struct TestSIODriver* test = (struct TestSIODriver*) driver;
	++test->finishMultiplayerCalls;
	data[0] = 0x1234;
	data[1] = 0x5678;
	data[2] = 0x9ABC;
	data[3] = 0xDEF0;
}

static uint8_t _finishNormal8(struct GBASIODriver* driver) {
	struct TestSIODriver* test = (struct TestSIODriver*) driver;
	++test->finishNormal8Calls;
	return 0xA5;
}

static uint32_t _finishNormal32(struct GBASIODriver* driver) {
	struct TestSIODriver* test = (struct TestSIODriver*) driver;
	++test->finishNormal32Calls;
	return 0xA5A55A5A;
}

static void _createTestDriver(struct TestSIODriver* test) {
	memset(test, 0, sizeof(*test));
	test->d.setMode = _setMode;
	test->d.handlesMode = _handlesMulti;
	test->d.start = _start;
	test->d.connectedDevices = _connectedDevices;
	test->d.deviceId = _deviceId;
	test->d.writeSIOCNT = _writeSIOCNT;
	test->d.writeRCNT = _writeRCNT;
	test->d.finishMultiplayer = _finishMultiplayer;
	test->d.finishNormal8 = _finishNormal8;
	test->d.finishNormal32 = _finishNormal32;
}

static int _characterizationConnectedDevices(struct GBASIODriver* driver) {
	struct CharacterizationDriver* test = (struct CharacterizationDriver*) driver;
	return test->connectedDevices;
}

static int _characterizationDeviceId(struct GBASIODriver* driver) {
	struct CharacterizationDriver* test = (struct CharacterizationDriver*) driver;
	return test->deviceId;
}

static uint16_t _characterizationWriteSIOCNT(struct GBASIODriver* driver, uint16_t value) {
	UNUSED(driver);
	return GBASIOMultiplayerFillReady(value);
}

static struct GBASIOStartResult _characterizationStart(struct GBASIODriver* driver) {
	struct CharacterizationDriver* test = (struct CharacterizationDriver*) driver;
	++test->startCalls;
	return (struct GBASIOStartResult) {
		.ownership = GBA_SIO_START_COMMON,
		.effectivePeerCount = test->connectedDevices,
	};
}

static void _characterizationFinishMultiplayer(struct GBASIODriver* driver, uint16_t data[4]) {
	struct CharacterizationDriver* test = (struct CharacterizationDriver*) driver;
	++test->finishMultiplayerCalls;
	data[0] = 0x1111;
	data[1] = 0x2222;
	data[2] = 0xFFFF;
	data[3] = 0xFFFF;
}

static void _createCharacterizationDriver(struct CharacterizationDriver* test, int deviceId, int connectedDevices) {
	memset(test, 0, sizeof(*test));
	test->deviceId = deviceId;
	test->connectedDevices = connectedDevices;
	test->d.handlesMode = _handlesMulti;
	test->d.connectedDevices = _characterizationConnectedDevices;
	test->d.deviceId = _characterizationDeviceId;
	test->d.writeSIOCNT = _characterizationWriteSIOCNT;
	test->d.start = _characterizationStart;
	test->d.finishMultiplayer = _characterizationFinishMultiplayer;
}

static void _scheduledRemoteStart(struct mTiming* timing, void* user, uint32_t cyclesLate) {
	UNUSED(cyclesLate);
	struct ScheduledRemoteStart* start = user;
	start->handled = true;
	start->sio->siocnt = GBASIOMultiplayerFillBusy(start->sio->siocnt);
	start->sio->transferMode = GBA_SIO_MULTI;
	mTimingDeschedule(timing, &start->sio->completeEvent);
	mTimingSchedule(timing, &start->sio->completeEvent, 32);
}

static struct mCore* _createCore(void) {
	struct mCore* core = GBACoreCreate();
	assert_non_null(core);
	assert_true(core->init(core));
	mCoreInitConfig(core, NULL);
	core->reset(core);
	return core;
}

static void _destroyCore(struct mCore* core) {
	mCoreConfigDeinit(&core->config);
	core->deinit(core);
}

static struct TransferObservation _runNormalTransfer(enum GBASIOMode mode, struct TestSIODriver* test) {
	struct mCore* core = _createCore();
	struct GBA* gba = core->board;
	struct GBASIO* sio = &gba->sio;
	if (test) {
		GBASIOSetDriver(sio, &test->d);
	}

	GBASIOWriteRCNT(sio, 0);
	uint16_t modeBits = mode == GBA_SIO_NORMAL_32 ? 0x1000 : 0;
	GBASIOWriteSIOCNT(sio, modeBits | 0x0002);
	gba->memory.io[GBA_REG(SIODATA32_LO)] = 0x1357;
	gba->memory.io[GBA_REG(SIODATA32_HI)] = 0x2468;
	gba->memory.io[GBA_REG(IF)] = 0;
	GBASIOWriteSIOCNT(sio, modeBits | 0x4082);

	struct TransferObservation observation = {
		.siocntBeforeCompletion = sio->siocnt,
		.rcntBeforeCompletion = sio->rcnt,
		.cyclesUntilCompletion = mTimingUntil(&gba->timing, &sio->completeEvent),
		.transferMode = sio->transferMode,
	};
	assert_true(mTimingIsScheduled(&gba->timing, &sio->completeEvent));

	mTimingDeschedule(&gba->timing, &sio->completeEvent);
	sio->completeEvent.callback(&gba->timing, sio->completeEvent.context, 0);
	observation.siocntAfterCompletion = sio->siocnt;
	observation.rcntAfterCompletion = sio->rcnt;
	observation.data[0] = gba->memory.io[GBA_REG(SIODATA32_LO)];
	observation.data[1] = gba->memory.io[GBA_REG(SIODATA32_HI)];
	observation.data[2] = gba->memory.io[GBA_REG(SIOMULTI2)];
	observation.data[3] = gba->memory.io[GBA_REG(SIOMULTI3)];
	observation.interruptFlags = gba->memory.io[GBA_REG(IF)];

	_destroyCore(core);
	return observation;
}

static void _assertTransferEqual(const struct TransferObservation* expected, const struct TransferObservation* actual) {
	assert_int_equal(actual->siocntBeforeCompletion, expected->siocntBeforeCompletion);
	assert_int_equal(actual->rcntBeforeCompletion, expected->rcntBeforeCompletion);
	assert_int_equal(actual->cyclesUntilCompletion, expected->cyclesUntilCompletion);
	assert_int_equal(actual->transferMode, expected->transferMode);
	assert_int_equal(actual->siocntAfterCompletion, expected->siocntAfterCompletion);
	assert_int_equal(actual->rcntAfterCompletion, expected->rcntAfterCompletion);
	assert_memory_equal(actual->data, expected->data, sizeof(actual->data));
	assert_int_equal(actual->interruptFlags, expected->interruptFlags);
}

static void _assertNoModeSpecificCalls(const struct TestSIODriver* test) {
	assert_int_equal(test->startCalls, 0);
	assert_int_equal(test->connectedCalls, 0);
	assert_int_equal(test->deviceIdCalls, 0);
	assert_int_equal(test->writeSIOCNTCalls, 0);
	assert_int_equal(test->writeRCNTCalls, 0);
	assert_int_equal(test->finishMultiplayerCalls, 0);
	assert_int_equal(test->finishNormal8Calls, 0);
	assert_int_equal(test->finishNormal32Calls, 0);
}

static struct ModeWriteObservation _runModeWrites(enum GBASIOMode mode, struct TestSIODriver* test) {
	struct mCore* core = _createCore();
	struct GBA* gba = core->board;
	struct GBASIO* sio = &gba->sio;
	if (test) {
		GBASIOSetDriver(sio, &test->d);
	}

	uint16_t rcnt = 0;
	uint16_t siocnt = 0;
	switch (mode) {
	case GBA_SIO_UART:
		siocnt = 0x3000;
		break;
	case GBA_SIO_GPIO:
		rcnt = 0x8000;
		break;
	case GBA_SIO_JOYBUS:
		rcnt = 0xC000;
		break;
	default:
		fail_msg("Unsupported test mode");
	}
	GBASIOWriteRCNT(sio, rcnt | 0x01F0);
	GBASIOWriteSIOCNT(sio, siocnt | 0x0043);
	assert_int_equal(sio->mode, mode);

	gba->memory.io[GBA_REG(JOYCNT)] = 0x0007;
	gba->memory.io[GBA_REG(JOYSTAT)] = 0x0033;
	gba->memory.io[GBA_REG(SIODATA8)] = 0xBEEF;
	struct ModeWriteObservation observation = {
		.mode = sio->mode,
		.siocnt = sio->siocnt,
		.rcnt = sio->rcnt,
	};
	switch (mode) {
	case GBA_SIO_UART:
		observation.results[observation.resultCount++] =
		    GBASIOWriteRegister(sio, GBA_REG_SIODATA8, 0x1234);
		observation.results[observation.resultCount++] =
		    GBASIOWriteRegister(sio, GBA_REG_JOYCNT, 0x0047);
		break;
	case GBA_SIO_GPIO:
		observation.results[observation.resultCount++] =
		    GBASIOWriteRegister(sio, GBA_REG_SIODATA8, 0x1234);
		break;
	case GBA_SIO_JOYBUS:
		observation.results[observation.resultCount++] =
		    GBASIOWriteRegister(sio, GBA_REG_SIODATA8, 0x1234);
		observation.results[observation.resultCount++] =
		    GBASIOWriteRegister(sio, GBA_REG_JOYCNT, 0x0047);
		observation.results[observation.resultCount++] =
		    GBASIOWriteRegister(sio, GBA_REG_JOYSTAT, 0x0030);
		observation.results[observation.resultCount++] =
		    GBASIOWriteRegister(sio, GBA_REG_JOY_TRANS_LO, 0x5678);
		observation.results[observation.resultCount++] =
		    GBASIOWriteRegister(sio, GBA_REG_JOY_TRANS_HI, 0x9ABC);
		break;
	default:
		break;
	}
	observation.completionScheduled = mTimingIsScheduled(&gba->timing, &sio->completeEvent);

	_destroyCore(core);
	return observation;
}

static void _assertModeWriteEqual(const struct ModeWriteObservation* expected, const struct ModeWriteObservation* actual) {
	assert_int_equal(actual->mode, expected->mode);
	assert_int_equal(actual->siocnt, expected->siocnt);
	assert_int_equal(actual->rcnt, expected->rcnt);
	assert_int_equal(actual->resultCount, expected->resultCount);
	assert_memory_equal(actual->results, expected->results, sizeof(actual->results));
	assert_int_equal(actual->completionScheduled, expected->completionScheduled);
}

static void _lockstepSleep(struct mLockstepUser* user) {
	UNUSED(user);
}

static void _lockstepWake(struct mLockstepUser* user) {
	UNUSED(user);
}

M_TEST_DEFINE(missingDriverDoesNotHandleMode) {
	struct GBASIO sio = {0};
	assert_false(GBASIODriverHandlesMode(&sio, GBA_SIO_MULTI));
}

M_TEST_DEFINE(missingHookDoesNotHandleMode) {
	struct GBASIODriver driver = {0};
	struct GBASIO sio = {
		.driver = &driver,
	};
	assert_false(GBASIODriverHandlesMode(&sio, GBA_SIO_MULTI));
}

M_TEST_DEFINE(driverDecidesHandledModes) {
	struct GBASIODriver driver = {
		.handlesMode = _handlesMulti,
	};
	struct GBASIO sio = {
		.driver = &driver,
	};
	assert_true(GBASIODriverHandlesMode(&sio, GBA_SIO_MULTI));
	assert_false(GBASIODriverHandlesMode(&sio, GBA_SIO_NORMAL_8));
}

M_TEST_DEFINE(normal8MatchesNoDriver) {
	struct TransferObservation expected = _runNormalTransfer(GBA_SIO_NORMAL_8, NULL);
	struct TestSIODriver driver;
	_createTestDriver(&driver);
	struct TransferObservation actual = _runNormalTransfer(GBA_SIO_NORMAL_8, &driver);
	_assertTransferEqual(&expected, &actual);
	_assertNoModeSpecificCalls(&driver);
	assert_true(driver.setModeCalls > 0);
}

M_TEST_DEFINE(normal32MatchesNoDriver) {
	struct TransferObservation expected = _runNormalTransfer(GBA_SIO_NORMAL_32, NULL);
	struct TestSIODriver driver;
	_createTestDriver(&driver);
	struct TransferObservation actual = _runNormalTransfer(GBA_SIO_NORMAL_32, &driver);
	_assertTransferEqual(&expected, &actual);
	_assertNoModeSpecificCalls(&driver);
	assert_true(driver.setModeCalls > 0);
}

M_TEST_DEFINE(uartWritesMatchNoDriver) {
	struct ModeWriteObservation expected = _runModeWrites(GBA_SIO_UART, NULL);
	struct TestSIODriver driver;
	_createTestDriver(&driver);
	struct ModeWriteObservation actual = _runModeWrites(GBA_SIO_UART, &driver);
	_assertModeWriteEqual(&expected, &actual);
	_assertNoModeSpecificCalls(&driver);
	assert_true(driver.setModeCalls > 0);
}

M_TEST_DEFINE(gpioWritesMatchNoDriver) {
	struct ModeWriteObservation expected = _runModeWrites(GBA_SIO_GPIO, NULL);
	struct TestSIODriver driver;
	_createTestDriver(&driver);
	struct ModeWriteObservation actual = _runModeWrites(GBA_SIO_GPIO, &driver);
	_assertModeWriteEqual(&expected, &actual);
	_assertNoModeSpecificCalls(&driver);
}

M_TEST_DEFINE(joyBusWritesMatchNoDriver) {
	struct ModeWriteObservation expected = _runModeWrites(GBA_SIO_JOYBUS, NULL);
	struct TestSIODriver driver;
	_createTestDriver(&driver);
	struct ModeWriteObservation actual = _runModeWrites(GBA_SIO_JOYBUS, &driver);
	_assertModeWriteEqual(&expected, &actual);
	_assertNoModeSpecificCalls(&driver);
	assert_true(driver.setModeCalls > 0);
}

M_TEST_DEFINE(lockstepSinglePlayerStateRoundTrip) {
	struct mCore* core = _createCore();
	struct GBA* gba = core->board;
	struct GBASIOLockstepCoordinator coordinator;
	GBASIOLockstepCoordinatorInit(&coordinator);
	struct mLockstepUser user = {
		.sleep = _lockstepSleep,
		.wake = _lockstepWake,
	};
	struct GBASIOLockstepDriver driver;
	GBASIOLockstepDriverCreate(&driver, &user);
	GBASIOLockstepCoordinatorAttach(&coordinator, &driver);
	GBASIOSetDriver(&gba->sio, &driver.d);

	assert_int_equal(GBASIOLockstepCoordinatorAttached(&coordinator), 1);
	assert_true(GBASIODriverHandlesMode(&gba->sio, GBA_SIO_MULTI));
	assert_true(GBASIODriverHandlesMode(&gba->sio, GBA_SIO_NORMAL_32));
	assert_int_equal(driver.event.priority, gba->sio.completeEvent.priority);

	GBASIOWriteRCNT(&gba->sio, 0);
	GBASIOWriteSIOCNT(&gba->sio, 0x1002);
	GBASIOWriteSIOCNT(&gba->sio, 0x1082);
	assert_false(mTimingIsScheduled(&gba->timing, &gba->sio.completeEvent));

	void* serializedState = NULL;
	size_t stateSize = 0;
	driver.d.saveState(&driver.d, &serializedState, &stateSize);
	assert_non_null(serializedState);
	assert_true(stateSize > 0);
	mTimingDeschedule(&gba->timing, &driver.event);
	assert_true(driver.d.loadState(&driver.d, serializedState, stateSize));
	free(serializedState);

	GBASIOSetDriver(&gba->sio, NULL);
	assert_int_equal(GBASIOLockstepCoordinatorAttached(&coordinator), 0);
	GBASIOLockstepCoordinatorDetach(&coordinator, &driver);
	GBASIOLockstepCoordinatorDeinit(&coordinator);
	_destroyCore(core);
}

M_TEST_DEFINE(noPeerMultiCharacterization) {
	for (unsigned baud = 0; baud < 4; ++baud) {
		struct mCore* core = _createCore();
		struct GBA* gba = core->board;
		struct GBASIO* sio = &gba->sio;

		GBASIOWriteRCNT(sio, 0);
		GBASIOWriteSIOCNT(sio, 0x2000 | baud);
		gba->memory.io[GBA_REG(IF)] = 0;
		GBASIOWriteSIOCNT(sio, 0x6080 | baud);

		assert_int_equal(sio->mode, GBA_SIO_MULTI);
		assert_int_equal(sio->transferMode, GBA_SIO_MULTI);
		assert_true(mTimingIsScheduled(&gba->timing, &sio->completeEvent));
		assert_int_equal(mTimingUntil(&gba->timing, &sio->completeEvent), _noPeerMultiCycles[baud]);
		for (unsigned i = 0; i < 4; ++i) {
			assert_int_equal(gba->memory.io[GBA_REG(SIOMULTI0) + i], 0xFFFF);
		}
		assert_true(GBASIOMultiplayerIsBusy(sio->siocnt));
		assert_false(GBASIOMultiplayerIsError(sio->siocnt));
		assert_true(GBASIOMultiplayerIsReady(sio->siocnt));
		assert_true(GBASIOMultiplayerIsSlave(sio->siocnt));
		assert_int_equal(GBASIOMultiplayerGetId(sio->siocnt), 0);
		assert_false(GBASIORegisterRCNTIsSc(sio->rcnt));
		assert_int_equal(gba->memory.io[GBA_REG(IF)] & (1 << GBA_IRQ_SIO), 0);

		mTimingDeschedule(&gba->timing, &sio->completeEvent);
		sio->completeEvent.callback(&gba->timing, sio->completeEvent.context, 0);

		for (unsigned i = 0; i < 4; ++i) {
			assert_int_equal(gba->memory.io[GBA_REG(SIOMULTI0) + i], 0);
		}
		assert_false(GBASIOMultiplayerIsBusy(sio->siocnt));
		assert_false(GBASIOMultiplayerIsError(sio->siocnt));
		assert_true(GBASIOMultiplayerIsReady(sio->siocnt));
		assert_true(GBASIOMultiplayerIsSlave(sio->siocnt));
		assert_int_equal(GBASIOMultiplayerGetId(sio->siocnt), 0);
		assert_true(GBASIORegisterRCNTIsSc(sio->rcnt));
		assert_int_equal(gba->memory.io[GBA_REG(IF)] & (1 << GBA_IRQ_SIO), 1 << GBA_IRQ_SIO);
		assert_true(sio->transferMode == (enum GBASIOMode) -1);

		_destroyCore(core);
	}
}

M_TEST_DEFINE(secondaryStartWaitsForPrimary) {
	struct mCore* core = _createCore();
	struct GBA* gba = core->board;
	struct GBASIO* sio = &gba->sio;
	struct CharacterizationDriver driver;
	_createCharacterizationDriver(&driver, 1, 1);
	GBASIOSetDriver(sio, &driver.d);

	GBASIOWriteRCNT(sio, 0);
	GBASIOWriteSIOCNT(sio, 0x2000);
	const uint16_t words[] = { 0x1357, 0x2468, 0x9ABC, 0xDEF0 };
	memcpy(&gba->memory.io[GBA_REG(SIOMULTI0)], words, sizeof(words));
	gba->memory.io[GBA_REG(IF)] = 0;
	GBASIOWriteSIOCNT(sio, 0x6080);

	assert_int_equal(driver.startCalls, 0);
	assert_false(mTimingIsScheduled(&gba->timing, &sio->completeEvent));
	assert_memory_equal(&gba->memory.io[GBA_REG(SIOMULTI0)], words, sizeof(words));
	assert_true(GBASIOMultiplayerIsBusy(sio->siocnt));
	assert_false(GBASIOMultiplayerIsError(sio->siocnt));
	assert_true(GBASIOMultiplayerIsReady(sio->siocnt));
	assert_true(GBASIOMultiplayerIsSlave(sio->siocnt));
	assert_int_equal(GBASIOMultiplayerGetId(sio->siocnt), 1);
	assert_true(GBASIORegisterRCNTIsSc(sio->rcnt));
	assert_int_equal(gba->memory.io[GBA_REG(IF)] & (1 << GBA_IRQ_SIO), 0);

	GBASIOSetDriver(sio, NULL);
	_destroyCore(core);
}

M_TEST_DEFINE(characterizedIdleDetachState) {
	uint16_t siocnt = 0x4000;
	siocnt = GBASIOMultiplayerFillError(siocnt);
	siocnt = GBASIOMultiplayerFillReady(siocnt);
	siocnt = GBASIOMultiplayerFillSlave(siocnt);
	siocnt = GBASIOMultiplayerSetId(siocnt, 0);
	uint16_t rcnt = GBASIORegisterRCNTFillSc(0);
	const uint16_t words[] = { 0x1357, 0x2468, 0x9ABC, 0xDEF0 };
	const uint16_t expectedWords[] = { 0x1357, 0x2468, 0x9ABC, 0xDEF0 };

	assert_false(GBASIOMultiplayerIsBusy(siocnt));
	assert_true(GBASIOMultiplayerIsError(siocnt));
	assert_true(GBASIOMultiplayerIsReady(siocnt));
	assert_true(GBASIOMultiplayerIsSlave(siocnt));
	assert_int_equal(GBASIOMultiplayerGetId(siocnt), 0);
	assert_true(GBASIORegisterRCNTIsSc(rcnt));
	assert_memory_equal(words, expectedWords, sizeof(words));
}

M_TEST_DEFINE(characterizedPostStartErrorState) {
	uint16_t siocnt = 0x4080;
	siocnt = GBASIOMultiplayerClearBusy(siocnt);
	siocnt = GBASIOMultiplayerFillError(siocnt);
	siocnt = GBASIOMultiplayerFillReady(siocnt);
	siocnt = GBASIOMultiplayerFillSlave(siocnt);
	siocnt = GBASIOMultiplayerSetId(siocnt, 0);
	uint16_t rcnt = GBASIORegisterRCNTFillSc(0);
	const uint16_t words[] = { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF };

	assert_false(GBASIOMultiplayerIsBusy(siocnt));
	assert_true(GBASIOMultiplayerIsError(siocnt));
	assert_true(GBASIOMultiplayerIsReady(siocnt));
	assert_true(GBASIOMultiplayerIsSlave(siocnt));
	assert_int_equal(GBASIOMultiplayerGetId(siocnt), 0);
	assert_true(GBASIORegisterRCNTIsSc(rcnt));
	for (unsigned i = 0; i < 4; ++i) {
		assert_int_equal(words[i], 0xFFFF);
	}
}

M_TEST_DEFINE(remoteStartPrecedesSameCycleCpuModeWrite) {
	struct mCore* core = _createCore();
	struct GBA* gba = core->board;
	struct GBASIO* sio = &gba->sio;
	struct CharacterizationDriver driver;
	_createCharacterizationDriver(&driver, 1, 1);
	GBASIOSetDriver(sio, &driver.d);
	GBASIOWriteRCNT(sio, 0);
	GBASIOWriteSIOCNT(sio, 0x2000);

	struct ScheduledRemoteStart start = {
		.event = {
			.name = "Test remote SIO start",
			.callback = _scheduledRemoteStart,
			.priority = 0x80,
		},
		.sio = sio,
	};
	start.event.context = &start;
	int32_t relativeCycles = 0;
	int32_t nextEvent = INT_MAX;
	struct mTiming timing;
	mTimingInit(&timing, &relativeCycles, &nextEvent);
	mTimingSchedule(&timing, &start.event, 0);
	mTimingTick(&timing, 0);
	assert_true(start.handled);
	assert_int_equal(sio->transferMode, GBA_SIO_MULTI);
	assert_true(mTimingIsScheduled(&timing, &sio->completeEvent));

	GBASIOWriteSIOCNT(sio, 0x1000);
	assert_int_equal(sio->mode, GBA_SIO_NORMAL_32);
	assert_int_equal(sio->transferMode, GBA_SIO_MULTI);

	mTimingClear(&timing);
	mTimingDeinit(&timing);
	GBASIOSetDriver(sio, NULL);
	_destroyCore(core);
}

M_TEST_DEFINE(completionPrecedesSameCycleCpuModeWrite) {
	struct mCore* core = _createCore();
	struct GBA* gba = core->board;
	struct GBASIO* sio = &gba->sio;
	struct CharacterizationDriver driver;
	_createCharacterizationDriver(&driver, 0, 1);
	GBASIOSetDriver(sio, &driver.d);
	GBASIOWriteRCNT(sio, 0);
	GBASIOWriteSIOCNT(sio, 0x2000);
	GBASIOWriteSIOCNT(sio, 0x6080);
	assert_true(mTimingIsScheduled(&gba->timing, &sio->completeEvent));

	mTimingDeschedule(&gba->timing, &sio->completeEvent);
	int32_t relativeCycles = 0;
	int32_t nextEvent = INT_MAX;
	struct mTiming timing;
	mTimingInit(&timing, &relativeCycles, &nextEvent);
	mTimingSchedule(&timing, &sio->completeEvent, 0);
	mTimingTick(&timing, 0);
	assert_int_equal(driver.finishMultiplayerCalls, 1);
	assert_true(sio->transferMode == (enum GBASIOMode) -1);

	GBASIOWriteSIOCNT(sio, 0x1000);
	assert_int_equal(sio->mode, GBA_SIO_NORMAL_32);
	assert_int_equal(driver.finishMultiplayerCalls, 1);

	mTimingClear(&timing);
	mTimingDeinit(&timing);
	GBASIOSetDriver(sio, NULL);
	_destroyCore(core);
}

M_TEST_DEFINE(multiCompletionUsesLatchedMode) {
	struct mCore* core = _createCore();
	struct GBA* gba = core->board;
	struct GBASIO* sio = &gba->sio;
	struct CharacterizationDriver driver;
	_createCharacterizationDriver(&driver, 0, 1);
	GBASIOSetDriver(sio, &driver.d);
	GBASIOWriteRCNT(sio, 0);
	GBASIOWriteSIOCNT(sio, 0x2000);
	GBASIOWriteSIOCNT(sio, 0x6080);
	assert_true(mTimingIsScheduled(&gba->timing, &sio->completeEvent));

	GBASIOWriteSIOCNT(sio, 0x1000);
	assert_int_equal(sio->mode, GBA_SIO_NORMAL_32);
	assert_int_equal(sio->transferMode, GBA_SIO_MULTI);

	mTimingDeschedule(&gba->timing, &sio->completeEvent);
	sio->completeEvent.callback(&gba->timing, sio->completeEvent.context, 0);
	assert_int_equal(driver.finishMultiplayerCalls, 1);
	assert_int_equal(gba->memory.io[GBA_REG(SIOMULTI0)], 0x1111);
	assert_int_equal(gba->memory.io[GBA_REG(SIOMULTI1)], 0x2222);
	assert_int_equal(gba->memory.io[GBA_REG(SIOMULTI2)], 0xFFFF);
	assert_int_equal(gba->memory.io[GBA_REG(SIOMULTI3)], 0xFFFF);
	assert_int_equal(sio->mode, GBA_SIO_NORMAL_32);
	assert_true(sio->transferMode == (enum GBASIOMode) -1);

	GBASIOSetDriver(sio, NULL);
	_destroyCore(core);
}

M_TEST_SUITE_DEFINE(GBASIO,
	cmocka_unit_test(missingDriverDoesNotHandleMode),
	cmocka_unit_test(missingHookDoesNotHandleMode),
	cmocka_unit_test(driverDecidesHandledModes),
	cmocka_unit_test(normal8MatchesNoDriver),
	cmocka_unit_test(normal32MatchesNoDriver),
	cmocka_unit_test(uartWritesMatchNoDriver),
	cmocka_unit_test(gpioWritesMatchNoDriver),
	cmocka_unit_test(joyBusWritesMatchNoDriver),
	cmocka_unit_test(lockstepSinglePlayerStateRoundTrip),
	cmocka_unit_test(noPeerMultiCharacterization),
	cmocka_unit_test(secondaryStartWaitsForPrimary),
	cmocka_unit_test(characterizedIdleDetachState),
	cmocka_unit_test(characterizedPostStartErrorState),
	cmocka_unit_test(remoteStartPrecedesSameCycleCpuModeWrite),
	cmocka_unit_test(completionPrecedesSameCycleCpuModeWrite),
	cmocka_unit_test(multiCompletionUsesLatchedMode))
