/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/gba/sio/netplay/driver.h>

#define GBA_SIO_NETPLAY_TIMING_ACCOUNTING_SLOP 4

#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/io.h>
#include <mgba/internal/gba/sio.h>

#define DRIVER_ID 0x5074654E

static void _scheduleEvent(
    struct GBASIONetplayDriver* driver);

static bool _flushQueuedModeIntent(
    struct GBASIONetplayDriver* driver);

static void _fillErrorWords(uint16_t words[4]) {
	for (unsigned i = 0; i < 4; ++i) {
		words[i] = 0xFFFF;
	}
}

static bool _transferInProgress(
    const struct GBASIONetplayDriver* driver) {
	return driver->transfer.state !=
	           GBA_SIO_NETPLAY_TRANSFER_IDLE &&
	       driver->transfer.state !=
	           GBA_SIO_NETPLAY_TRANSFER_FINISHED;
}

static enum GBALinkWireMode _wireMode(enum GBASIOMode mode) {
	switch (mode) {
	case GBA_SIO_NORMAL_8:
		return GBA_LINK_MODE_NORMAL_8;
	case GBA_SIO_NORMAL_32:
		return GBA_LINK_MODE_NORMAL_32;
	case GBA_SIO_MULTI:
		return GBA_LINK_MODE_MULTI;
	case GBA_SIO_UART:
		return GBA_LINK_MODE_UART;
	case GBA_SIO_GPIO:
		return GBA_LINK_MODE_GPIO;
	case GBA_SIO_JOYBUS:
		return GBA_LINK_MODE_JOYBUS;
	}
	return GBA_LINK_MODE_GPIO;
}

static bool _updateLocalCycle(
    struct GBASIONetplayDriver* driver) {
	if (!driver || !driver->sio || !driver->sio->p) {
		return false;
	}
	return GBALinkTimingClockUpdate(
	    &driver->timingClock,
	    mTimingCurrentTime(&driver->sio->p->timing),
	    &driver->localCycle);
}

static void _setPaused(
    struct GBASIONetplayDriver* driver, bool paused) {
	if (!driver || driver->paused == paused) {
		return;
	}
	driver->paused = paused;
	if (paused && driver->sio && driver->sio->p) {
		/*
		 * A pause can be raised from inside an mTiming callback. Merely
		 * setting the flag is not enough there: the current ARM event
		 * pass may otherwise continue through events that were already
		 * queued after the new cable boundary. The local lockstep driver
		 * uses the same interrupt pattern when a player goes to sleep.
		 */
		driver->sio->p->cpu->nextEvent = 0;
		GBAInterrupt(driver->sio->p);
	}
}

static void _applyLineState(
    struct GBASIONetplayDriver* driver) {
	if (!driver || !driver->sio ||
	    driver->sio->mode != GBA_SIO_MULTI) {
		return;
	}
	int id =
	    driver->attached &&
	            driver->session->localRole == GBA_LINK_ROLE_CLIENT
	        ? 1
	        : 0;
	if (driver->attached && driver->observable &&
	    driver->jointlyReady) {
		driver->sio->siocnt =
		    GBASIOMultiplayerFillReady(driver->sio->siocnt);
		driver->sio->siocnt =
		    GBASIOMultiplayerSetSlave(
		        driver->sio->siocnt, id != 0);
		driver->sio->siocnt =
		    GBASIOMultiplayerSetId(driver->sio->siocnt, id);
	} else {
		driver->sio->siocnt =
		    driver->attached && driver->observable
		        ? GBASIOMultiplayerClearReady(
		              driver->sio->siocnt)
		        : GBASIOMultiplayerFillReady(
		              driver->sio->siocnt);
		driver->sio->siocnt =
		    GBASIOMultiplayerFillSlave(driver->sio->siocnt);
		driver->sio->siocnt =
		    GBASIOMultiplayerSetId(driver->sio->siocnt, 0);
	}
	driver->sio->rcnt =
	    GBASIORegisterRCNTFillSc(driver->sio->rcnt);
}

static void _setExecutionBoundary(
    struct GBASIONetplayDriver* driver,
    enum GBASIONetplayBoundary boundary,
    uint64_t cableCycle) {
	uint64_t localCycle = 0;
	if (!GBALinkClockCableToLocal(
	        &driver->timeline.clock, cableCycle,
	        &localCycle)) {
		GBALinkSessionFail(
		    driver->session,
		    GBA_LINK_REASON_INVALID_TRANSITION,
		    "network SIO boundary cannot map to local time");
		return;
	}
	driver->executionLimitEnabled = true;
	driver->executionLimit = localCycle;
	driver->boundary = boundary;
	_setPaused(driver, false);
	_scheduleEvent(driver);
}

static void _abortActiveTransfer(
    struct GBASIONetplayDriver* driver,
    enum GBALinkReason reason) {
	if (!_transferInProgress(driver) ||
	    (!driver->transfer.startEmitted &&
	     !driver->transfer.startAccepted)) {
		return;
	}
	if (driver->session->localRole == GBA_LINK_ROLE_HOST &&
	    (driver->transfer.state ==
	         GBA_SIO_NETPLAY_TRANSFER_HOST_WAIT_DECISION_ACK ||
	     driver->transfer.state ==
	         GBA_SIO_NETPLAY_TRANSFER_DECIDED) &&
	    driver->transfer.decisionAccepted) {
		/*
		 * The authoritative outcome was already committed locally.
		 * Losing the terminal acknowledgement closes the session so no
		 * later transfer can start, but does not rewrite that committed
		 * host observation.
		 */
		driver->transfer.state =
		    GBA_SIO_NETPLAY_TRANSFER_DECIDED;
		driver->paused = false;
		return;
	}
	driver->transfer.abortPending = true;
	driver->transfer.abortReason =
	    reason ? reason : GBA_LINK_REASON_TRANSPORT_STOP;
	driver->transfer.outcome = GBA_LINK_OUTCOME_ERROR;
	_fillErrorWords(driver->transfer.pendingWords);
	_fillErrorWords(driver->transfer.decidedWords);

	switch (driver->transfer.state) {
	case GBA_SIO_NETPLAY_TRANSFER_HOST_WAIT_READY:
		driver->transfer.state =
		    GBA_SIO_NETPLAY_TRANSFER_COMMITTED;
		driver->paused = false;
		break;
	case GBA_SIO_NETPLAY_TRANSFER_CLIENT_CATCHUP_START:
		/* Reach START first so the normal client completion event exists. */
		break;
	case GBA_SIO_NETPLAY_TRANSFER_CLIENT_WAIT_COMMIT:
	case GBA_SIO_NETPLAY_TRANSFER_COMMITTED:
		if (driver->session->localRole ==
		        GBA_LINK_ROLE_CLIENT &&
		    !GBALinkSessionIsLive(driver->session)) {
			_setExecutionBoundary(
			    driver,
			    GBA_SIO_NETPLAY_BOUNDARY_COMPLETION,
			    driver->transfer.completionCycle);
		}
		break;
	case GBA_SIO_NETPLAY_TRANSFER_HOST_WAIT_COMPLETION_READY:
	case GBA_SIO_NETPLAY_TRANSFER_HOST_WAIT_DECISION_ACK:
	case GBA_SIO_NETPLAY_TRANSFER_CLIENT_WAIT_DECISION:
		if (!GBALinkSessionIsLive(driver->session)) {
			driver->transfer.state =
			    GBA_SIO_NETPLAY_TRANSFER_DECIDED;
			driver->paused = false;
		}
		break;
	default:
		break;
	}
}

static bool _sendTransferAbort(
    struct GBASIONetplayDriver* driver,
    enum GBALinkReason reason) {
	if (!GBALinkSessionIsLive(driver->session) ||
	    !driver->transfer.sequence ||
	    !driver->transfer.completionCycle ||
	    driver->transfer.abortSent) {
		return false;
	}
	struct GBALinkPacket packet;
	memset(&packet, 0, sizeof(packet));
	packet.header.type = GBA_LINK_MESSAGE_TRANSFER_ABORT;
	packet.payload.transferAbort.transferSequence =
	    driver->transfer.sequence;
	packet.payload.transferAbort.completionCycle =
	    driver->transfer.completionCycle;
	packet.payload.transferAbort.reason = reason;
	if (!GBALinkSessionSendRuntime(
	    driver->session, &packet,
	    GBA_LINK_DEADLINE_TRANSFER_COMMIT)) {
		return false;
	}
	driver->transfer.abortSent = true;
	return true;
}

static bool _waitForTransferState(
    struct GBASIONetplayDriver* driver,
    enum GBASIONetplayTransferState waiting) {
	while (driver->transfer.state == waiting &&
	       GBALinkSessionIsLive(driver->session)) {
		/*
		 * A frontend receive poll can copy the message resolving this
		 * synchronous SIO barrier and the next transaction together.
		 * Process through the resolving packet, unwind the common SIO
		 * hook, then admit later packets from the copied queue.
		 */
		GBALinkSessionYieldInbound(driver->session);
		if (!GBALinkSessionUpdate(
		        driver->session, true)) {
			break;
		}
	}
	return driver->transfer.state != waiting;
}

static void _scheduleEvent(
    struct GBASIONetplayDriver* driver) {
	if (!driver->attached || !driver->sio || !driver->sio->p ||
	    driver->processingEvent) {
		return;
	}
	struct mTiming* timing = &driver->sio->p->timing;
	mTimingDeschedule(timing, &driver->schedulerEvent);
	uint64_t delay = driver->timingPolicy.localSchedulerQuantum;
	if (driver->executionLimitEnabled) {
		if (!_updateLocalCycle(driver)) {
			GBALinkSessionFail(
			    driver->session,
			    GBA_LINK_REASON_SEQUENCE_EXHAUSTED,
			    "local timing clock exhausted");
			return;
		}
		if (driver->executionLimit <= driver->localCycle) {
			delay = 0;
		} else {
			uint64_t until = driver->executionLimit -
			                 driver->localCycle;
			if (until < delay) {
				delay = until;
			}
		}
	}
	if (delay > INT32_MAX) {
		delay = INT32_MAX;
	}
	mTimingSchedule(
	    timing, &driver->schedulerEvent, (int32_t) delay);
}

static uint64_t _timelineLocalCycle(void* context) {
	struct GBASIONetplayDriver* driver = context;
	if (!_updateLocalCycle(driver)) {
		return driver ? driver->localCycle : 0;
	}
	return driver->localCycle;
}

static void _timelineSetPaused(
    void* context, bool paused) {
	struct GBASIONetplayDriver* driver = context;
	_setPaused(
	    driver,
	    paused || driver->queuedLocalModeIntent);
}

static void _timelineSetExecutionLimit(
    void* context, bool enabled, uint64_t localCycle) {
	struct GBASIONetplayDriver* driver = context;
	driver->executionLimitEnabled = enabled;
	driver->executionLimit = localCycle;
	driver->boundary =
	    enabled ? GBA_SIO_NETPLAY_BOUNDARY_GRANT
	            : GBA_SIO_NETPLAY_BOUNDARY_NONE;
	_scheduleEvent(driver);
}

static void _timelineRebase(
    void* context, uint64_t localCycle, uint64_t cableCycle) {
	UNUSED(cableCycle);
	struct GBASIONetplayDriver* driver = context;
	driver->localCycle = localCycle;
}

static void _timelineModeCommitted(
    void* context, uint64_t generation,
    enum GBALinkWireMode localMode,
    enum GBALinkWireMode remoteMode, bool jointlyReady) {
	struct GBASIONetplayDriver* driver = context;
	driver->committedModeGeneration = generation;
	driver->committedLocalMode = localMode;
	driver->committedRemoteMode = remoteMode;
	driver->jointlyReady = jointlyReady;
	_applyLineState(driver);
	if (driver->session->localRole ==
	        GBA_LINK_ROLE_HOST &&
	    driver->transfer.state ==
	        GBA_SIO_NETPLAY_TRANSFER_HOST_WAIT_READY &&
	    driver->transfer.abortPending) {
		_sendTransferAbort(
		    driver, driver->transfer.abortReason);
		driver->transfer.state =
		    GBA_SIO_NETPLAY_TRANSFER_COMMITTED;
		driver->paused = false;
	}
}

static const struct GBALinkTimelineCallbacks _timelineCallbacks = {
	.localCycle = _timelineLocalCycle,
	.setPaused = _timelineSetPaused,
	.setExecutionLimit = _timelineSetExecutionLimit,
	.rebaseCableClock = _timelineRebase,
	.modeCommitted = _timelineModeCommitted,
};

static bool _clientReachTransferStart(
    struct GBASIONetplayDriver* driver,
    uint32_t cyclesLate) {
	if (driver->session->localRole != GBA_LINK_ROLE_CLIENT ||
	    driver->transfer.state !=
	        GBA_SIO_NETPLAY_TRANSFER_CLIENT_CATCHUP_START) {
		return false;
	}
	uint64_t cableCycle = 0;
	if (!GBALinkClockLocalToCable(
	        &driver->timeline.clock, driver->localCycle,
	        &cableCycle) ||
	    cableCycle < driver->transfer.startCycle ||
	    cableCycle - driver->transfer.startCycle >
	        cyclesLate) {
		return false;
	}
	if (cableCycle != driver->transfer.startCycle) {
		GBALinkClockRebase(
		    &driver->timeline.clock,
		    driver->localCycle,
		    driver->transfer.startCycle);
		driver->timeline.currentCableCycle =
		    driver->transfer.startCycle;
	}
	driver->executionLimitEnabled = false;
	driver->boundary = GBA_SIO_NETPLAY_BOUNDARY_NONE;
	_setPaused(driver, true);
	driver->sio->siocnt =
	    GBASIOMultiplayerFillBusy(driver->sio->siocnt);
	driver->sio->transferMode = GBA_SIO_MULTI;
	driver->transfer.localWord =
	    driver->sio->p->memory.io[GBA_REG(SIOMLT_SEND)];

	uint64_t duration = driver->transfer.completionCycle -
	                    driver->transfer.startCycle;
	if (!duration || duration > INT32_MAX) {
		_abortActiveTransfer(
		    driver, GBA_LINK_REASON_INVALID_TRANSITION);
		return false;
	}
	mTimingDeschedule(
	    &driver->sio->p->timing,
	    &driver->sio->completeEvent);
	mTimingSchedule(
	    &driver->sio->p->timing,
	    &driver->sio->completeEvent, (int32_t) duration);

	if (driver->transfer.abortPending ||
	    !GBALinkSessionIsLive(driver->session)) {
		driver->transfer.state =
		    GBA_SIO_NETPLAY_TRANSFER_COMMITTED;
		_setExecutionBoundary(
		    driver, GBA_SIO_NETPLAY_BOUNDARY_COMPLETION,
		    driver->transfer.completionCycle);
		return true;
	}

	struct GBALinkPacket ready;
	memset(&ready, 0, sizeof(ready));
	ready.header.type = GBA_LINK_MESSAGE_TRANSFER_READY;
	ready.payload.transferStart.transferSequence =
	    driver->transfer.sequence;
	ready.payload.transferStart.startCycle =
	    driver->transfer.startCycle;
	ready.payload.transferStart.completionCycle =
	    driver->transfer.completionCycle;
	ready.payload.transferStart.outgoingWord =
	    driver->transfer.localWord;
	ready.payload.transferStart.siocnt =
	    driver->transfer.startSIOCNT;
	driver->transfer.state =
	    GBA_SIO_NETPLAY_TRANSFER_CLIENT_WAIT_COMMIT;
	if (!GBALinkSessionSendRuntime(
	        driver->session, &ready,
	        GBA_LINK_DEADLINE_TRANSFER_COMMIT)) {
		_abortActiveTransfer(
		    driver, GBA_LINK_REASON_SEND_FAILURE);
		return false;
	}
	return true;
}

static void _schedulerEvent(
    struct mTiming* timing, void* context,
    uint32_t cyclesLate) {
	UNUSED(timing);
	UNUSED(cyclesLate);
	struct GBASIONetplayDriver* driver = context;
	driver->processingEvent = true;
	if (!_updateLocalCycle(driver)) {
		GBALinkSessionFail(
		    driver->session, GBA_LINK_REASON_SEQUENCE_EXHAUSTED,
		    "local timing clock exhausted");
		driver->processingEvent = false;
		return;
	}
	if (driver->executionLimitEnabled &&
	    driver->localCycle >= driver->executionLimit) {
		switch (driver->boundary) {
		case GBA_SIO_NETPLAY_BOUNDARY_GRANT: {
			uint64_t cableCycle = 0;
			uint64_t toleratedLate =
			    (uint64_t) cyclesLate +
			    GBA_SIO_NETPLAY_TIMING_ACCOUNTING_SLOP;
			if (GBALinkClockLocalToCable(
			        &driver->timeline.clock,
			        driver->localCycle,
			        &cableCycle) &&
			    cableCycle >
			        driver->timeline.grantHorizon &&
			    cableCycle -
			            driver->timeline.grantHorizon <=
			        toleratedLate) {
				GBALinkClockRebase(
				    &driver->timeline.clock,
				    driver->localCycle,
				    driver->timeline.grantHorizon);
			}
			GBALinkTimelineClientReachGrant(
			    &driver->timeline, driver->localCycle);
			break;
		}
		case GBA_SIO_NETPLAY_BOUNDARY_TRANSFER_START:
			_clientReachTransferStart(
			    driver, cyclesLate);
			break;
		case GBA_SIO_NETPLAY_BOUNDARY_COMPLETION:
			driver->executionLimitEnabled = false;
			driver->boundary =
			    GBA_SIO_NETPLAY_BOUNDARY_NONE;
			break;
		default:
			break;
		}
	}
	if (GBALinkSessionIsLive(driver->session)) {
		GBALinkSessionUpdate(driver->session, false);
	}
	driver->processingEvent = false;
	_scheduleEvent(driver);
}

static bool _driverInit(struct GBASIODriver* base) {
	struct GBASIONetplayDriver* driver =
	    (struct GBASIONetplayDriver*) base;
	driver->sio = base->p;
	GBALinkTimingClockInit(
	    &driver->timingClock,
	    mTimingCurrentTime(&driver->sio->p->timing));
	if (!_updateLocalCycle(driver)) {
		return false;
	}
	driver->attached = true;
	driver->topologicalPeerCount = 1;
	driver->committedLocalMode =
	    driver->session->localHello.initialMode;
	driver->committedRemoteMode =
	    driver->session->remoteHello.initialMode;
	if (!GBALinkTimelineInit(
	        &driver->timeline, driver->session,
	        &_timelineCallbacks, driver, driver->localCycle,
	        driver->session->attachCycle,
	        driver->committedLocalMode,
	        driver->committedRemoteMode)) {
		return false;
	}
	driver->timelineInitialized = true;
	_applyLineState(driver);
	_scheduleEvent(driver);
	return true;
}

static void _driverDeinit(struct GBASIODriver* base) {
	struct GBASIONetplayDriver* driver =
	    (struct GBASIONetplayDriver*) base;
	if (driver->sio && driver->sio->p) {
		mTimingDeschedule(
		    &driver->sio->p->timing, &driver->schedulerEvent);
	}
	driver->attached = false;
	driver->observable = false;
	driver->jointlyReady = false;
	driver->timelineInitialized = false;
	driver->topologicalPeerCount = 0;
	driver->executionLimitEnabled = false;
	driver->paused = false;
	driver->boundary = GBA_SIO_NETPLAY_BOUNDARY_NONE;
	driver->secondaryStartPending = false;
}

static void _driverReset(struct GBASIODriver* base) {
	struct GBASIONetplayDriver* driver =
	    (struct GBASIONetplayDriver*) base;
	driver->resetting = true;
	if (driver->sio && driver->sio->p) {
		mTimingDeschedule(
		    &driver->sio->p->timing, &driver->schedulerEvent);
	}
	driver->paused = false;
	driver->executionLimitEnabled = false;
}

static uint32_t _driverId(
    const struct GBASIODriver* base) {
	UNUSED(base);
	return DRIVER_ID;
}

static void _driverSetMode(
    struct GBASIODriver* base, enum GBASIOMode mode) {
	struct GBASIONetplayDriver* driver =
	    (struct GBASIONetplayDriver*) base;
	if (!driver->timelineInitialized ||
	    !GBALinkSessionIsLive(driver->session)) {
		return;
	}
	if (!_updateLocalCycle(driver) ||
	    !driver->timelineInitialized) {
		return;
	}
	uint64_t cableCycle = 0;
	if (!GBALinkClockLocalToCable(
	        &driver->timeline.clock, driver->localCycle,
	        &cableCycle)) {
		return;
	}
	if (_transferInProgress(driver) &&
	    (driver->transfer.startEmitted ||
	     driver->transfer.startAccepted) &&
	    cableCycle >= driver->transfer.startCycle) {
		driver->transfer.localDeferredMode = true;
		driver->transfer.localDeferredCycle = cableCycle;
		driver->transfer.localDeferredWireMode =
		    _wireMode(mode);
		_abortActiveTransfer(
		    driver, GBA_LINK_REASON_MODE_DEPARTURE);
		if (GBALinkSessionIsLive(driver->session)) {
			struct GBALinkPacket intent;
			memset(&intent, 0, sizeof(intent));
			intent.header.type =
			    GBA_LINK_MESSAGE_MODE_INTENT;
			intent.payload.modeIntent.modeGeneration =
			    driver->timeline.committedModeGeneration + 1;
			intent.payload.modeIntent.localCycle = cableCycle;
			intent.payload.modeIntent.localMode =
			    _wireMode(mode);
			intent.payload.modeIntent.deferred = true;
			if (GBALinkSessionSendRuntime(
			        driver->session, &intent,
			        GBA_LINK_DEADLINE_COMPLETION_DECISION)) {
				_sendTransferAbort(
				    driver,
				    GBA_LINK_REASON_MODE_DEPARTURE);
			}
		}
		return;
	}
	if (driver->timeline.modeBarrier) {
		driver->queuedLocalModeIntent = true;
		driver->queuedLocalMode = _wireMode(mode);
		driver->queuedLocalModeCycle = cableCycle;
		_setPaused(driver, true);
		return;
	}
	if (driver->session->localRole ==
	        GBA_LINK_ROLE_CLIENT &&
	    driver->transfer.state ==
	        GBA_SIO_NETPLAY_TRANSFER_CLIENT_CATCHUP_START &&
	    cableCycle < driver->transfer.startCycle) {
		driver->timeline.localMode = _wireMode(mode);
		_setPaused(driver, true);
		driver->executionLimitEnabled = false;
		driver->boundary =
		    GBA_SIO_NETPLAY_BOUNDARY_NONE;
		driver->transfer.abortPending = true;
		driver->transfer.abortReason =
		    GBA_LINK_REASON_MODE_DEPARTURE;
		struct GBALinkPacket intent;
		memset(&intent, 0, sizeof(intent));
		intent.header.type = GBA_LINK_MESSAGE_MODE_INTENT;
		intent.payload.modeIntent.modeGeneration =
		    driver->timeline.committedModeGeneration + 1;
		intent.payload.modeIntent.localCycle = cableCycle;
		intent.payload.modeIntent.localMode = _wireMode(mode);
		intent.payload.modeIntent.deferred = false;
		GBALinkSessionSendRuntime(
		    driver->session, &intent,
		    GBA_LINK_DEADLINE_MODE);
		return;
	}
	if (!GBALinkTimelineLocalModeWrite(
	        &driver->timeline, _wireMode(mode),
	        driver->localCycle) &&
	    _wireMode(mode) != driver->timeline.localMode) {
		/*
		 * A second SIO write can complete in the same CPU run block
		 * after the first write has consumed the active grant. Keep
		 * its local effect, but do not expose it to the peer until a
		 * new host-led boundary is available.
		 */
		driver->queuedLocalModeIntent = true;
		driver->queuedLocalMode = _wireMode(mode);
		driver->queuedLocalModeCycle = cableCycle;
		_setPaused(driver, true);
	}
}

static bool _flushQueuedModeIntent(
    struct GBASIONetplayDriver* driver) {
	if (!driver || !driver->queuedLocalModeIntent ||
	    !driver->timelineInitialized ||
	    driver->timeline.modeBarrier ||
	    _transferInProgress(driver) ||
	    !GBALinkSessionIsLive(driver->session)) {
		return false;
	}
	if (driver->queuedLocalMode ==
	    driver->timeline.localMode) {
		driver->queuedLocalModeIntent = false;
		_setPaused(driver, driver->timeline.paused);
		return true;
	}
	if (driver->session->localRole ==
	        GBA_LINK_ROLE_CLIENT &&
	    !driver->timeline.grantOutstanding) {
		_setPaused(driver, true);
		return false;
	}
	if (!_updateLocalCycle(driver) ||
	    !GBALinkTimelineLocalModeWrite(
	        &driver->timeline,
	        driver->queuedLocalMode,
	        driver->localCycle)) {
		_setPaused(driver, true);
		return false;
	}
	driver->queuedLocalModeIntent = false;
	return true;
}

static bool _driverHandlesMode(
    struct GBASIODriver* base, enum GBASIOMode mode) {
	UNUSED(base);
	return mode == GBA_SIO_MULTI;
}

static bool _driverIsExecutionBlocked(
    struct GBASIODriver* base) {
	struct GBASIONetplayDriver* driver =
	    (struct GBASIONetplayDriver*) base;
	return driver->paused;
}

static int _driverConnectedDevices(
    struct GBASIODriver* base) {
	struct GBASIONetplayDriver* driver =
	    (struct GBASIONetplayDriver*) base;
	return driver->attached && driver->observable &&
	               driver->jointlyReady
	           ? 1
	           : 0;
}

static int _driverDeviceId(
    struct GBASIODriver* base) {
	struct GBASIONetplayDriver* driver =
	    (struct GBASIONetplayDriver*) base;
	if (driver->transfer.state ==
	        GBA_SIO_NETPLAY_TRANSFER_FINISHED &&
	    (driver->transfer.abortPending ||
	     driver->transfer.outcome ==
	         GBA_LINK_OUTCOME_ERROR)) {
		return 0;
	}
	return driver->attached && driver->observable &&
	               driver->topologicalPeerCount == 1 &&
	               driver->session->localRole ==
	                   GBA_LINK_ROLE_CLIENT
	           ? 1
	           : 0;
}

static uint16_t _driverWriteSIOCNT(
    struct GBASIODriver* base, uint16_t value) {
	struct GBASIONetplayDriver* driver =
	    (struct GBASIONetplayDriver*) base;
	int connected = _driverConnectedDevices(base);
	int id = _driverDeviceId(base);
	bool secondaryStart =
	    driver->session->localRole == GBA_LINK_ROLE_CLIENT &&
	    id == 1 &&
	    GBASIOMultiplayerIsBusy(value) &&
	    !GBASIOMultiplayerIsBusy(driver->sio->siocnt) &&
	    !_transferInProgress(driver) &&
	    (!driver->sio->p ||
	     !mTimingIsScheduled(
	         &driver->sio->p->timing,
	         &driver->sio->completeEvent));
	value = connected
	            ? GBASIOMultiplayerFillReady(value)
	            : driver->attached && driver->observable
	                  ? GBASIOMultiplayerClearReady(value)
	                  : GBASIOMultiplayerFillReady(value);
	value = GBASIOMultiplayerSetSlave(
	    value, id || !connected);
	value = GBASIOMultiplayerSetId(value, id);
	if (secondaryStart) {
		driver->secondaryStartPending = true;
	}
	return value;
}

static uint16_t _driverWriteRCNT(
    struct GBASIODriver* base, uint16_t value) {
	UNUSED(base);
	return GBASIORegisterRCNTFillSc(value);
}

static struct GBASIOStartResult _driverStart(
    struct GBASIODriver* base) {
	struct GBASIONetplayDriver* driver =
	    (struct GBASIONetplayDriver*) base;
	if (driver->session->localRole == GBA_LINK_ROLE_CLIENT) {
		return (struct GBASIOStartResult) {
			.ownership = GBA_SIO_START_DRIVER,
			.effectivePeerCount = 0,
		};
	}
	if (driver->transfer.state ==
	    GBA_SIO_NETPLAY_TRANSFER_FINISHED) {
		memset(
		    &driver->transfer, 0,
		    sizeof(driver->transfer));
	}
	if (!driver->attached || !driver->observable ||
	    !driver->jointlyReady ||
	    driver->session->state != GBA_LINK_SESSION_READY ||
	    driver->timeline.modeBarrier ||
	    driver->timeline.grantOutstanding ||
	    driver->transfer.state !=
	        GBA_SIO_NETPLAY_TRANSFER_IDLE ||
	    !_updateLocalCycle(driver)) {
		return (struct GBASIOStartResult) {
			.ownership = GBA_SIO_START_COMMON,
			.effectivePeerCount = 0,
		};
	}

	uint64_t startCycle = 0;
	if (!GBALinkClockLocalToCable(
	        &driver->timeline.clock, driver->localCycle,
	        &startCycle)) {
		return (struct GBASIOStartResult) {
			.ownership = GBA_SIO_START_COMMON,
			.effectivePeerCount = 0,
		};
	}
	int32_t duration = GBASIOTransferCycles(
	    GBA_SIO_MULTI, driver->sio->siocnt, 1);
	if (duration <= 0 ||
	    UINT64_MAX - startCycle < (uint32_t) duration) {
		return (struct GBASIOStartResult) {
			.ownership = GBA_SIO_START_COMMON,
			.effectivePeerCount = 0,
		};
	}
	uint64_t sequence = 0;
	if (!GBALinkSequenceTake(
	        &driver->session->sequences,
	        GBA_LINK_SEQUENCE_TRANSFER, &sequence)) {
		GBALinkSessionFail(
		    driver->session,
		    GBA_LINK_REASON_SEQUENCE_EXHAUSTED,
		    "transfer sequence exhausted");
		return (struct GBASIOStartResult) {
			.ownership = GBA_SIO_START_COMMON,
			.effectivePeerCount = 0,
		};
	}
	memset(
	    &driver->transfer, 0,
	    sizeof(driver->transfer));
	driver->transfer.state =
	    GBA_SIO_NETPLAY_TRANSFER_HOST_WAIT_READY;
	driver->transfer.sequence = sequence;
	driver->transfer.startCycle = startCycle;
	driver->transfer.completionCycle =
	    startCycle + (uint32_t) duration;
	driver->transfer.startSIOCNT = driver->sio->siocnt;
	driver->transfer.localWord =
	    driver->sio->p->memory.io[GBA_REG(SIOMLT_SEND)];
	driver->transfer.outcome = GBA_LINK_OUTCOME_SUCCESS;
	driver->transfer.pendingWords[0] =
	    driver->transfer.localWord;
	driver->transfer.pendingWords[2] = 0xFFFF;
	driver->transfer.pendingWords[3] = 0xFFFF;
	driver->session->state = GBA_LINK_SESSION_TRANSFERRING;
	driver->timeline.currentCableCycle = startCycle;
	_setPaused(driver, true);

	struct GBALinkPacket packet;
	memset(&packet, 0, sizeof(packet));
	packet.header.type = GBA_LINK_MESSAGE_TRANSFER_START;
	packet.payload.transferStart.transferSequence = sequence;
	packet.payload.transferStart.startCycle = startCycle;
	packet.payload.transferStart.completionCycle =
	    driver->transfer.completionCycle;
	packet.payload.transferStart.outgoingWord =
	    driver->transfer.localWord;
	packet.payload.transferStart.siocnt =
	    driver->transfer.startSIOCNT;
	if (!GBALinkSessionSendRuntime(
	        driver->session, &packet,
	        GBA_LINK_DEADLINE_TRANSFER_READINESS)) {
		memset(
		    &driver->transfer, 0,
		    sizeof(driver->transfer));
		driver->paused = false;
		return (struct GBASIOStartResult) {
			.ownership = GBA_SIO_START_COMMON,
			.effectivePeerCount = 0,
		};
	}
	driver->transfer.startEmitted = true;
	_waitForTransferState(
	    driver,
	    GBA_SIO_NETPLAY_TRANSFER_HOST_WAIT_READY);
	if (!driver->transfer.startEmitted) {
		return (struct GBASIOStartResult) {
			.ownership = GBA_SIO_START_COMMON,
			.effectivePeerCount = 0,
		};
	}
	return (struct GBASIOStartResult) {
		.ownership = GBA_SIO_START_COMMON,
		.effectivePeerCount = 1,
	};
}

static void _finishWithError(
    struct GBASIONetplayDriver* driver,
    uint16_t data[4]) {
	_fillErrorWords(data);
	driver->sio->siocnt =
	    GBASIOMultiplayerFillError(
	        driver->sio->siocnt);
	driver->sio->siocnt =
	    GBASIOMultiplayerFillReady(
	        driver->sio->siocnt);
	driver->sio->siocnt =
	    GBASIOMultiplayerFillSlave(
	        driver->sio->siocnt);
	driver->sio->siocnt =
	    GBASIOMultiplayerSetId(
	        driver->sio->siocnt, 0);
	driver->sio->rcnt =
	    GBASIORegisterRCNTFillSc(
	        driver->sio->rcnt);
	driver->jointlyReady = false;
	if (!GBALinkSessionIsLive(driver->session)) {
		driver->observable = false;
		driver->topologicalPeerCount = 0;
	}
}

static void _driverFinishMultiplayer(
    struct GBASIODriver* base, uint16_t data[4]) {
	struct GBASIONetplayDriver* driver =
	    (struct GBASIONetplayDriver*) base;
	if (!_transferInProgress(driver) &&
	    driver->transfer.state !=
	        GBA_SIO_NETPLAY_TRANSFER_DECIDED) {
		return;
	}

	if (driver->session->localRole ==
	    GBA_LINK_ROLE_HOST) {
		if (!driver->transfer.completionSequence &&
		    GBALinkSessionIsLive(driver->session)) {
			if (!GBALinkSequenceTake(
			        &driver->session->sequences,
			        GBA_LINK_SEQUENCE_COMPLETION,
			        &driver->transfer.completionSequence)) {
				_abortActiveTransfer(
				    driver,
				    GBA_LINK_REASON_SEQUENCE_EXHAUSTED);
			} else {
				struct GBALinkPacket catchup;
				memset(&catchup, 0, sizeof(catchup));
				catchup.header.type =
				    GBA_LINK_MESSAGE_COMPLETION_CATCHUP;
				catchup.payload.completionCatchup
				    .transferSequence =
				    driver->transfer.sequence;
				catchup.payload.completionCatchup
				    .completionSequence =
				    driver->transfer.completionSequence;
				catchup.payload.completionCatchup
				    .completionCycle =
				    driver->transfer.completionCycle;
				catchup.payload.completionCatchup
				    .pendingOutcome =
				    driver->transfer.abortPending
				        ? GBA_LINK_OUTCOME_ERROR
				        : GBA_LINK_OUTCOME_SUCCESS;
				driver->transfer.state =
				    GBA_SIO_NETPLAY_TRANSFER_HOST_WAIT_COMPLETION_READY;
				_setPaused(driver, true);
				if (!GBALinkSessionSendRuntime(
				        driver->session, &catchup,
				        GBA_LINK_DEADLINE_COMPLETION_READINESS)) {
					_abortActiveTransfer(
					    driver,
					    GBA_LINK_REASON_SEND_FAILURE);
				}
			}
		}
		if (driver->transfer.state ==
		    GBA_SIO_NETPLAY_TRANSFER_HOST_WAIT_COMPLETION_READY) {
			_waitForTransferState(
			    driver,
			    GBA_SIO_NETPLAY_TRANSFER_HOST_WAIT_COMPLETION_READY);
		}
		if (driver->transfer.state ==
		    GBA_SIO_NETPLAY_TRANSFER_HOST_WAIT_DECISION_ACK) {
			_waitForTransferState(
			    driver,
			    GBA_SIO_NETPLAY_TRANSFER_HOST_WAIT_DECISION_ACK);
		}
	} else if (driver->transfer.state ==
	               GBA_SIO_NETPLAY_TRANSFER_CLIENT_CATCHUP_COMPLETION ||
	           driver->transfer.state ==
	               GBA_SIO_NETPLAY_TRANSFER_COMMITTED) {
		driver->transfer.state =
		    GBA_SIO_NETPLAY_TRANSFER_CLIENT_WAIT_DECISION;
		_setPaused(driver, true);
		if (GBALinkSessionIsLive(driver->session)) {
			struct GBALinkPacket ready;
			memset(&ready, 0, sizeof(ready));
			ready.header.type =
			    GBA_LINK_MESSAGE_COMPLETION_READY;
			ready.payload.completionReady
			    .transferSequence =
			    driver->transfer.sequence;
			ready.payload.completionReady
			    .completionSequence =
			    driver->transfer.completionSequence;
			ready.payload.completionReady
			    .completionCycle =
			    driver->transfer.completionCycle;
			ready.payload.completionReady.abortReason =
			    driver->transfer.abortPending
			        ? driver->transfer.abortReason
			        : 0;
			ready.payload.completionReady.hasDeferredMode =
			    driver->transfer.localDeferredMode;
			if (!GBALinkSessionSendRuntime(
			        driver->session, &ready,
			        GBA_LINK_DEADLINE_COMPLETION_DECISION)) {
				_abortActiveTransfer(
				    driver,
				    GBA_LINK_REASON_SEND_FAILURE);
			}
		}
		if (driver->transfer.state ==
		    GBA_SIO_NETPLAY_TRANSFER_CLIENT_WAIT_DECISION) {
			_waitForTransferState(
			    driver,
			    GBA_SIO_NETPLAY_TRANSFER_CLIENT_WAIT_DECISION);
		}
	}

	if (driver->session->localRole ==
	        GBA_LINK_ROLE_CLIENT &&
	    driver->transfer.decisionAckPending &&
	    GBALinkSessionIsLive(driver->session)) {
		struct GBALinkPacket ack;
		memset(&ack, 0, sizeof(ack));
		ack.header.type =
		    GBA_LINK_MESSAGE_COMPLETION_DECISION_ACK;
		ack.payload.completionDecisionAck.transferSequence =
		    driver->transfer.sequence;
		ack.payload.completionDecisionAck.completionSequence =
		    driver->transfer.completionSequence;
		ack.payload.completionDecisionAck.completionCycle =
		    driver->transfer.completionCycle;
		ack.payload.completionDecisionAck.outcome =
		    driver->transfer.outcome;
		driver->transfer.decisionAckPending = false;
		if (!GBALinkSessionSendRuntime(
		        driver->session, &ack,
		        GBA_LINK_DEADLINE_NONE)) {
			_abortActiveTransfer(
			    driver, GBA_LINK_REASON_SEND_FAILURE);
		}
	}
	if (driver->transfer.outcome ==
	        GBA_LINK_OUTCOME_SUCCESS &&
	    driver->transfer.decisionAccepted) {
		memcpy(
		    data, driver->transfer.decidedWords,
		    sizeof(driver->transfer.decidedWords));
	} else {
		_finishWithError(driver, data);
	}
	/*
	 * mTiming may invoke the completion event a small number of cycles
	 * late. The distributed cable clock nevertheless completes exactly
	 * at the announced cycle. Rebase before either peer executes again
	 * so scheduler lateness cannot consume the next transaction's
	 * client catch-up interval.
	 */
	if (_updateLocalCycle(driver)) {
		GBALinkClockRebase(
		    &driver->timeline.clock,
		    driver->localCycle,
		    driver->transfer.completionCycle);
		driver->timeline.currentCableCycle =
		    driver->transfer.completionCycle;
	}
	if (driver->session->localRole ==
	        GBA_LINK_ROLE_HOST &&
	    GBALinkSessionIsLive(driver->session) &&
	    (driver->transfer.localDeferredMode ||
	     driver->transfer.remoteDeferredMode)) {
		enum GBALinkWireMode hostMode =
		    driver->transfer.localDeferredMode
		        ? driver->transfer.localDeferredWireMode
		        : driver->timeline.localMode;
		enum GBALinkWireMode clientMode =
		    driver->transfer.remoteDeferredMode
		        ? driver->transfer.remoteDeferredWireMode
		        : driver->timeline.remoteMode;
		GBALinkTimelineHostCommitModesAtCurrentBoundary(
		    &driver->timeline,
		    driver->timeline.committedModeGeneration + 1,
		    hostMode, clientMode);
	}
	_setPaused(
	    driver,
	    GBALinkSessionIsLive(driver->session) &&
	        (driver->timeline.paused ||
	         driver->timeline.modeBarrier));
	driver->executionLimitEnabled = false;
	driver->boundary = GBA_SIO_NETPLAY_BOUNDARY_NONE;
	driver->transfer.state =
	    GBA_SIO_NETPLAY_TRANSFER_FINISHED;
	if (GBALinkSessionIsLive(driver->session)) {
		driver->session->state =
		    GBA_LINK_SESSION_READY;
		GBALinkSessionSetDeadline(
		    driver->session, GBA_LINK_DEADLINE_NONE);
	}
}

static bool _sessionQuiescentSnapshot(
    void* context, enum GBALinkWireMode* mode,
    uint64_t* localCycle) {
	return GBASIONetplayDriverQuiescentSnapshot(
	    context, mode, localCycle);
}

static void _sessionSetPaused(
    void* context, bool paused) {
	struct GBASIONetplayDriver* driver = context;
	_setPaused(
	    driver,
	    paused || driver->queuedLocalModeIntent);
}

static void _sessionSetAttachment(
    void* context, bool installed, bool observable,
    uint64_t attachCycle) {
	UNUSED(attachCycle);
	struct GBASIONetplayDriver* driver = context;
	if (!installed) {
		if (_transferInProgress(driver) &&
		    (driver->transfer.startEmitted ||
		     driver->transfer.startAccepted)) {
			driver->observable = false;
			driver->jointlyReady = false;
			driver->topologicalPeerCount = 0;
			_abortActiveTransfer(
			    driver,
			    GBA_LINK_REASON_TRANSPORT_STOP);
			_applyLineState(driver);
		} else {
			GBASIONetplayDriverDetach(driver);
		}
		return;
	}
	if (!driver->attached) {
		GBASIOSetDriver(driver->sio, &driver->d);
	}
	driver->observable = observable;
	_applyLineState(driver);
	if (driver->session->localRole == GBA_LINK_ROLE_HOST &&
	    observable && driver->timelineInitialized &&
	    !driver->timeline.committedModeGeneration) {
		GBALinkTimelineHostCommitInitialModes(
		    &driver->timeline);
	}
}

static bool _handleTransferStart(
    struct GBASIONetplayDriver* driver,
    const struct GBALinkPacket* packet) {
	const struct GBALinkTransferStart* start =
	    &packet->payload.transferStart;
	bool resumingFinishedTransfer =
	    driver->transfer.state ==
	        GBA_SIO_NETPLAY_TRANSFER_FINISHED;
	uint64_t previousCompletionCycle =
	    driver->transfer.completionCycle;
	if (driver->session->localRole != GBA_LINK_ROLE_CLIENT ||
	    driver->session->state != GBA_LINK_SESSION_READY ||
	    (driver->transfer.state !=
	         GBA_SIO_NETPLAY_TRANSFER_IDLE &&
	     driver->transfer.state !=
	         GBA_SIO_NETPLAY_TRANSFER_FINISHED) ||
	    start->transferSequence !=
	        driver->lastRemoteTransferSequence + 1 ||
	    !_updateLocalCycle(driver)) {
		return false;
	}
	if (resumingFinishedTransfer && driver->paused &&
	    previousCompletionCycle) {
		/*
		 * The ARM run loop can account a few trailing scheduler cycles
		 * after finishMultiplayer requests a pause. No guest
		 * instruction executes in that interval, so anchor the next
		 * host-led catch-up at the previous authoritative completion.
		 */
		GBALinkClockRebase(
		    &driver->timeline.clock,
		    driver->localCycle,
		    previousCompletionCycle);
		driver->timeline.currentCableCycle =
		    previousCompletionCycle;
	}
	uint64_t currentCableCycle = 0;
	if (!GBALinkClockLocalToCable(
	        &driver->timeline.clock, driver->localCycle,
	        &currentCableCycle) ||
	    start->startCycle < currentCableCycle) {
		return false;
	}
	int32_t duration = GBASIOTransferCycles(
	    GBA_SIO_MULTI, start->siocnt, 1);
	if (duration <= 0 ||
	    start->completionCycle - start->startCycle !=
	        (uint32_t) duration) {
		return false;
	}
	driver->secondaryStartPending = false;
	memset(
	    &driver->transfer, 0,
	    sizeof(driver->transfer));
	driver->transfer.state =
	    GBA_SIO_NETPLAY_TRANSFER_CLIENT_CATCHUP_START;
	driver->transfer.sequence = start->transferSequence;
	driver->transfer.startCycle = start->startCycle;
	driver->transfer.completionCycle =
	    start->completionCycle;
	driver->transfer.startSIOCNT = start->siocnt;
	driver->transfer.remoteWord = start->outgoingWord;
	driver->transfer.startAccepted = true;
	driver->transfer.outcome = GBA_LINK_OUTCOME_SUCCESS;
	driver->lastRemoteTransferSequence =
	    start->transferSequence;
	driver->session->state =
	    GBA_LINK_SESSION_TRANSFERRING;
	_setExecutionBoundary(
	    driver, GBA_SIO_NETPLAY_BOUNDARY_TRANSFER_START,
	    start->startCycle);
	GBALinkSessionSetDeadline(
	    driver->session,
	    GBA_LINK_DEADLINE_TRANSFER_COMMIT);
	return true;
}

static bool _handleTransferReady(
    struct GBASIONetplayDriver* driver,
    const struct GBALinkPacket* packet) {
	const struct GBALinkTransferStart* ready =
	    &packet->payload.transferStart;
	if (driver->session->localRole != GBA_LINK_ROLE_HOST ||
	    driver->transfer.state !=
	        GBA_SIO_NETPLAY_TRANSFER_HOST_WAIT_READY ||
	    ready->transferSequence != driver->transfer.sequence ||
	    ready->startCycle != driver->transfer.startCycle ||
	    ready->completionCycle !=
	        driver->transfer.completionCycle ||
	    ready->siocnt != driver->transfer.startSIOCNT) {
		return false;
	}
	driver->transfer.remoteWord = ready->outgoingWord;
	driver->transfer.pendingWords[0] =
	    driver->transfer.localWord;
	driver->transfer.pendingWords[1] =
	    driver->transfer.remoteWord;
	driver->transfer.pendingWords[2] = 0xFFFF;
	driver->transfer.pendingWords[3] = 0xFFFF;

	struct GBALinkPacket commit;
	memset(&commit, 0, sizeof(commit));
	commit.header.type = GBA_LINK_MESSAGE_TRANSFER_COMMIT;
	commit.payload.transferCommit.transferSequence =
	    driver->transfer.sequence;
	commit.payload.transferCommit.startCycle =
	    driver->transfer.startCycle;
	commit.payload.transferCommit.completionCycle =
	    driver->transfer.completionCycle;
	memcpy(
	    commit.payload.transferCommit.words,
	    driver->transfer.pendingWords,
	    sizeof(driver->transfer.pendingWords));
	if (!GBALinkSessionSendRuntime(
	        driver->session, &commit,
	        GBA_LINK_DEADLINE_COMPLETION_CATCHUP)) {
		_abortActiveTransfer(
		    driver, GBA_LINK_REASON_SEND_FAILURE);
		return true;
	}
	driver->transfer.commitAccepted = true;
	driver->transfer.state =
	    GBA_SIO_NETPLAY_TRANSFER_COMMITTED;
	driver->paused = false;
	return true;
}

static bool _handleTransferCommit(
    struct GBASIONetplayDriver* driver,
    const struct GBALinkPacket* packet) {
	const struct GBALinkTransferCommit* commit =
	    &packet->payload.transferCommit;
	if (driver->session->localRole != GBA_LINK_ROLE_CLIENT ||
	    driver->transfer.state !=
	        GBA_SIO_NETPLAY_TRANSFER_CLIENT_WAIT_COMMIT ||
	    commit->transferSequence != driver->transfer.sequence ||
	    commit->startCycle != driver->transfer.startCycle ||
	    commit->completionCycle !=
	        driver->transfer.completionCycle ||
	    commit->words[0] != driver->transfer.remoteWord ||
	    commit->words[1] != driver->transfer.localWord) {
		return false;
	}
	memcpy(
	    driver->transfer.pendingWords, commit->words,
	    sizeof(driver->transfer.pendingWords));
	driver->transfer.commitAccepted = true;
	driver->transfer.state =
	    GBA_SIO_NETPLAY_TRANSFER_COMMITTED;
	_setPaused(driver, true);
	GBALinkSessionSetDeadline(
	    driver->session,
	    GBA_LINK_DEADLINE_COMPLETION_CATCHUP);
	return true;
}

static bool _handleTransferAbort(
    struct GBASIONetplayDriver* driver,
    const struct GBALinkPacket* packet) {
	const struct GBALinkTransferAbort* abort =
	    &packet->payload.transferAbort;
	if (!_transferInProgress(driver) ||
	    abort->transferSequence != driver->transfer.sequence ||
	    abort->completionCycle !=
	        driver->transfer.completionCycle) {
		return false;
	}
	_abortActiveTransfer(driver, abort->reason);
	if (driver->session->localRole ==
	        GBA_LINK_ROLE_CLIENT &&
	    driver->transfer.state ==
	        GBA_SIO_NETPLAY_TRANSFER_CLIENT_CATCHUP_START) {
		_updateLocalCycle(driver);
		_clientReachTransferStart(driver, 0);
	}
	return true;
}

static bool _handleCompletionCatchup(
    struct GBASIONetplayDriver* driver,
    const struct GBALinkPacket* packet) {
	const struct GBALinkCompletionCatchup* catchup =
	    &packet->payload.completionCatchup;
	if (driver->session->localRole != GBA_LINK_ROLE_CLIENT ||
	    driver->transfer.state !=
	        GBA_SIO_NETPLAY_TRANSFER_COMMITTED ||
	    catchup->transferSequence != driver->transfer.sequence ||
	    catchup->completionSequence !=
	        driver->lastRemoteCompletionSequence + 1 ||
	    catchup->completionCycle !=
	        driver->transfer.completionCycle) {
		return false;
	}
	driver->lastRemoteCompletionSequence =
	    catchup->completionSequence;
	driver->transfer.completionSequence =
	    catchup->completionSequence;
	if (catchup->pendingOutcome ==
	    GBA_LINK_OUTCOME_ERROR) {
		_abortActiveTransfer(
		    driver,
		    driver->transfer.abortReason
		        ? driver->transfer.abortReason
		        : GBA_LINK_REASON_PEER_DETACH);
	}
	driver->transfer.state =
	    GBA_SIO_NETPLAY_TRANSFER_CLIENT_CATCHUP_COMPLETION;
	_setExecutionBoundary(
	    driver, GBA_SIO_NETPLAY_BOUNDARY_COMPLETION,
	    driver->transfer.completionCycle);
	GBALinkSessionSetDeadline(
	    driver->session,
	    GBA_LINK_DEADLINE_COMPLETION_DECISION);
	return true;
}

static bool _handleCompletionReady(
    struct GBASIONetplayDriver* driver,
    const struct GBALinkPacket* packet) {
	const struct GBALinkCompletionReady* ready =
	    &packet->payload.completionReady;
	if (driver->session->localRole != GBA_LINK_ROLE_HOST ||
	    driver->transfer.state !=
	        GBA_SIO_NETPLAY_TRANSFER_HOST_WAIT_COMPLETION_READY ||
	    ready->transferSequence != driver->transfer.sequence ||
	    ready->completionSequence !=
	        driver->transfer.completionSequence ||
	    ready->completionCycle !=
	        driver->transfer.completionCycle) {
		return false;
	}
	if (ready->abortReason || ready->hasDeferredMode) {
		_abortActiveTransfer(
		    driver,
		    ready->abortReason
		        ? ready->abortReason
		        : GBA_LINK_REASON_MODE_DEPARTURE);
	}
	struct GBALinkPacket decision;
	memset(&decision, 0, sizeof(decision));
	decision.header.type =
	    GBA_LINK_MESSAGE_COMPLETION_DECISION;
	decision.payload.completionDecision.transferSequence =
	    driver->transfer.sequence;
	decision.payload.completionDecision.completionSequence =
	    driver->transfer.completionSequence;
	decision.payload.completionDecision.completionCycle =
	    driver->transfer.completionCycle;
	decision.payload.completionDecision.outcome =
	    driver->transfer.abortPending
	        ? GBA_LINK_OUTCOME_ERROR
	        : GBA_LINK_OUTCOME_SUCCESS;
	decision.payload.completionDecision.reason =
	    driver->transfer.abortPending
	        ? driver->transfer.abortReason
	        : 0;
	if (driver->transfer.abortPending) {
		_fillErrorWords(
		    decision.payload.completionDecision.words);
	} else {
		memcpy(
		    decision.payload.completionDecision.words,
		    driver->transfer.pendingWords,
		    sizeof(driver->transfer.pendingWords));
	}
	if (!GBALinkSessionSendRuntime(
	        driver->session, &decision,
	        GBA_LINK_DEADLINE_COMPLETION_DECISION)) {
		_abortActiveTransfer(
		    driver, GBA_LINK_REASON_SEND_FAILURE);
		driver->transfer.state =
		    GBA_SIO_NETPLAY_TRANSFER_DECIDED;
		return true;
	}
	driver->transfer.outcome =
	    decision.payload.completionDecision.outcome;
	driver->transfer.decisionAccepted = true;
	memcpy(
	    driver->transfer.decidedWords,
	    decision.payload.completionDecision.words,
	    sizeof(driver->transfer.decidedWords));
	driver->transfer.state =
	    GBA_SIO_NETPLAY_TRANSFER_HOST_WAIT_DECISION_ACK;
	_setPaused(driver, true);
	return true;
}

static bool _handleCompletionDecision(
    struct GBASIONetplayDriver* driver,
    const struct GBALinkPacket* packet) {
	const struct GBALinkCompletionDecision* decision =
	    &packet->payload.completionDecision;
	if (driver->session->localRole != GBA_LINK_ROLE_CLIENT ||
	    driver->transfer.state !=
	        GBA_SIO_NETPLAY_TRANSFER_CLIENT_WAIT_DECISION ||
	    decision->transferSequence != driver->transfer.sequence ||
	    decision->completionSequence !=
	        driver->transfer.completionSequence ||
	    decision->completionCycle !=
	        driver->transfer.completionCycle) {
		return false;
	}
	driver->transfer.outcome = decision->outcome;
	driver->transfer.abortReason = decision->reason;
	driver->transfer.abortPending =
	    decision->outcome == GBA_LINK_OUTCOME_ERROR;
	driver->transfer.decisionAccepted = true;
	memcpy(
	    driver->transfer.decidedWords, decision->words,
	    sizeof(driver->transfer.decidedWords));
	driver->transfer.decisionAckPending = true;
	driver->transfer.state =
	    GBA_SIO_NETPLAY_TRANSFER_DECIDED;
	driver->paused = false;
	GBALinkSessionSetDeadline(
	    driver->session, GBA_LINK_DEADLINE_NONE);
	return true;
}

static bool _handleCompletionDecisionAck(
    struct GBASIONetplayDriver* driver,
    const struct GBALinkPacket* packet) {
	const struct GBALinkCompletionDecisionAck* ack =
	    &packet->payload.completionDecisionAck;
	if (driver->session->localRole != GBA_LINK_ROLE_HOST ||
	    driver->transfer.state !=
	        GBA_SIO_NETPLAY_TRANSFER_HOST_WAIT_DECISION_ACK ||
	    ack->transferSequence != driver->transfer.sequence ||
	    ack->completionSequence !=
	        driver->transfer.completionSequence ||
	    ack->completionCycle !=
	        driver->transfer.completionCycle ||
	    ack->outcome != driver->transfer.outcome) {
		return false;
	}
	driver->transfer.state =
	    GBA_SIO_NETPLAY_TRANSFER_DECIDED;
	driver->paused = false;
	GBALinkSessionSetDeadline(
	    driver->session, GBA_LINK_DEADLINE_NONE);
	return true;
}

static bool _handleDeferredModeIntent(
    struct GBASIONetplayDriver* driver,
    const struct GBALinkPacket* packet) {
	const struct GBALinkModeIntent* intent =
	    &packet->payload.modeIntent;
	if (!intent->deferred || !_transferInProgress(driver) ||
	    intent->modeGeneration !=
	        driver->timeline.committedModeGeneration + 1 ||
	    intent->localCycle < driver->transfer.startCycle ||
	    intent->localCycle > driver->transfer.completionCycle) {
		return false;
	}
	driver->transfer.remoteDeferredMode = true;
	driver->transfer.remoteDeferredCycle =
	    intent->localCycle;
	driver->transfer.remoteDeferredWireMode =
	    intent->localMode;
	_abortActiveTransfer(
	    driver, GBA_LINK_REASON_MODE_DEPARTURE);
	return true;
}

static bool _handlePreStartModeIntent(
    struct GBASIONetplayDriver* driver,
    const struct GBALinkPacket* packet) {
	const struct GBALinkModeIntent* intent =
	    &packet->payload.modeIntent;
	if (driver->session->localRole != GBA_LINK_ROLE_HOST ||
	    driver->transfer.state !=
	        GBA_SIO_NETPLAY_TRANSFER_HOST_WAIT_READY ||
	    intent->deferred ||
	    intent->modeGeneration !=
	        driver->timeline.committedModeGeneration + 1 ||
	    intent->localCycle >= driver->transfer.startCycle) {
		return false;
	}
	driver->transfer.abortPending = true;
	driver->transfer.abortReason =
	    GBA_LINK_REASON_MODE_DEPARTURE;
	driver->timeline.remoteMode = intent->localMode;
	return GBALinkTimelineHostCommitModesAtCurrentBoundary(
	    &driver->timeline, intent->modeGeneration,
	    driver->timeline.localMode, intent->localMode);
}

static bool _sessionRuntimePacket(
    void* context, const struct GBALinkPacket* packet) {
	struct GBASIONetplayDriver* driver = context;
	if (!driver->timelineInitialized) {
		return false;
	}
	switch (packet->header.type) {
	case GBA_LINK_MESSAGE_TRANSFER_START:
		return _handleTransferStart(driver, packet);
	case GBA_LINK_MESSAGE_TRANSFER_READY:
		return _handleTransferReady(driver, packet);
	case GBA_LINK_MESSAGE_TRANSFER_COMMIT:
		return _handleTransferCommit(driver, packet);
	case GBA_LINK_MESSAGE_TRANSFER_ABORT:
		return _handleTransferAbort(driver, packet);
	case GBA_LINK_MESSAGE_COMPLETION_CATCHUP:
		return _handleCompletionCatchup(driver, packet);
	case GBA_LINK_MESSAGE_COMPLETION_READY:
		return _handleCompletionReady(driver, packet);
	case GBA_LINK_MESSAGE_COMPLETION_DECISION:
		return _handleCompletionDecision(driver, packet);
	case GBA_LINK_MESSAGE_COMPLETION_DECISION_ACK:
		return _handleCompletionDecisionAck(driver, packet);
	case GBA_LINK_MESSAGE_MODE_INTENT:
		if (packet->payload.modeIntent.deferred) {
			return _handleDeferredModeIntent(
			    driver, packet);
		}
		if (driver->transfer.state ==
		    GBA_SIO_NETPLAY_TRANSFER_HOST_WAIT_READY) {
			return _handlePreStartModeIntent(
			    driver, packet);
		}
		/* Fall through */
	default:
		return GBALinkTimelineHandlePacket(
		    &driver->timeline, packet);
	}
}

static void _sessionFailed(
    void* context, enum GBALinkReason reason) {
	struct GBASIONetplayDriver* driver = context;
	if (_transferInProgress(driver) &&
	    (driver->transfer.startEmitted ||
	     driver->transfer.startAccepted)) {
		_abortActiveTransfer(driver, reason);
	} else {
		GBASIONetplayDriverDetach(driver);
	}
}

static const struct GBALinkSessionCallbacks _sessionCallbacks = {
	.quiescentSnapshot = _sessionQuiescentSnapshot,
	.setPaused = _sessionSetPaused,
	.setAttachment = _sessionSetAttachment,
	.runtimePacket = _sessionRuntimePacket,
	.failed = _sessionFailed,
};

void GBASIONetplayDriverCreate(
    struct GBASIONetplayDriver* driver, struct GBASIO* sio,
    struct GBALinkSession* session) {
	memset(driver, 0, sizeof(*driver));
	driver->sio = sio;
	driver->session = session;
	driver->d.p = sio;
	driver->d.init = _driverInit;
	driver->d.deinit = _driverDeinit;
	driver->d.reset = _driverReset;
	driver->d.driverId = _driverId;
	driver->d.setMode = _driverSetMode;
	driver->d.handlesMode = _driverHandlesMode;
	driver->d.isExecutionBlocked =
	    _driverIsExecutionBlocked;
	driver->d.connectedDevices = _driverConnectedDevices;
	driver->d.deviceId = _driverDeviceId;
	driver->d.writeSIOCNT = _driverWriteSIOCNT;
	driver->d.writeRCNT = _driverWriteRCNT;
	driver->d.start = _driverStart;
	driver->d.finishMultiplayer =
	    _driverFinishMultiplayer;
	driver->schedulerEvent.context = driver;
	driver->schedulerEvent.callback = _schedulerEvent;
	driver->schedulerEvent.name = "GBA SIO Netplay";
	driver->schedulerEvent.priority = 0x80;
	GBALinkTimingPolicyInit(&driver->timingPolicy);
	if (sio && sio->p) {
		GBALinkTimingClockInit(
		    &driver->timingClock,
		    mTimingCurrentTime(&sio->p->timing));
		_updateLocalCycle(driver);
	}
}

void GBASIONetplayDriverSetTimingPolicy(
    struct GBASIONetplayDriver* driver,
    const struct GBALinkTimingPolicy* policy) {
	if (!driver || !GBALinkTimingPolicyValidate(policy)) {
		return;
	}
	driver->timingPolicy = *policy;
	_scheduleEvent(driver);
}

const struct GBALinkSessionCallbacks*
GBASIONetplayDriverSessionCallbacks(void) {
	return &_sessionCallbacks;
}

bool GBASIONetplayDriverQuiescentSnapshot(
    struct GBASIONetplayDriver* driver,
    enum GBALinkWireMode* mode, uint64_t* localCycle) {
	if (!driver || !driver->sio || !driver->sio->p ||
	    !mode || !localCycle || driver->resetting ||
	    driver->unloading ||
	    mTimingIsScheduled(
	        &driver->sio->p->timing,
	        &driver->sio->completeEvent) ||
	    (driver->sio->mode == GBA_SIO_MULTI &&
	     GBASIOMultiplayerIsBusy(driver->sio->siocnt)) ||
	    !_updateLocalCycle(driver)) {
		return false;
	}
	*mode = _wireMode(driver->sio->mode);
	*localCycle = driver->localCycle;
	return true;
}

bool GBASIONetplayDriverPump(
    struct GBASIONetplayDriver* driver, bool pollReceive) {
	if (!driver || !GBALinkSessionIsLive(driver->session)) {
		return false;
	}
	bool updated = GBALinkSessionUpdate(
	    driver->session, pollReceive);
	if (updated) {
		_flushQueuedModeIntent(driver);
	}
	return updated;
}

bool GBASIONetplayDriverHostFrameBoundary(
    struct GBASIONetplayDriver* driver) {
	if (!driver || !driver->timelineInitialized ||
	    driver->session->localRole != GBA_LINK_ROLE_HOST ||
	    driver->paused || !_updateLocalCycle(driver)) {
		return false;
	}
	uint64_t cableCycle = 0;
	if (!GBALinkClockLocalToCable(
	        &driver->timeline.clock, driver->localCycle,
	        &cableCycle)) {
		return false;
	}
	return GBALinkTimelineHostReachHorizon(
	    &driver->timeline, cableCycle);
}

void GBASIONetplayAbortTransfer(
    struct GBASIONetplayDriver* driver,
    enum GBALinkReason reason) {
	if (!driver) {
		return;
	}
	bool postStart =
	    _transferInProgress(driver) &&
	    (driver->transfer.startEmitted ||
	     driver->transfer.startAccepted);
	_abortActiveTransfer(driver, reason);
	if (postStart &&
	    GBALinkSessionIsLive(driver->session)) {
		_sendTransferAbort(
		    driver,
		    reason ? reason
		           : GBA_LINK_REASON_TRANSPORT_STOP);
	}
}

void GBASIONetplayDriverCancel(
    struct GBASIONetplayDriver* driver,
    enum GBALinkReason reason) {
	if (!driver || !driver->sio ||
	    (reason != GBA_LINK_REASON_RESET &&
	     reason != GBA_LINK_REASON_UNLOAD)) {
		return;
	}
	driver->resetting =
	    reason == GBA_LINK_REASON_RESET;
	driver->unloading =
	    reason == GBA_LINK_REASON_UNLOAD;
	if (driver->sio->p) {
		mTimingDeschedule(
		    &driver->sio->p->timing,
		    &driver->schedulerEvent);
		mTimingDeschedule(
		    &driver->sio->p->timing,
		    &driver->sio->completeEvent);
	}
	driver->sio->transferMode = -1;
	memset(
	    &driver->transfer, 0,
	    sizeof(driver->transfer));
	driver->paused = false;
	driver->executionLimitEnabled = false;
	driver->boundary = GBA_SIO_NETPLAY_BOUNDARY_NONE;
	driver->secondaryStartPending = false;
	if (GBALinkSessionIsLive(driver->session)) {
		GBALinkSessionFail(
		    driver->session, reason,
		    reason == GBA_LINK_REASON_RESET
		        ? "link session cancelled for reset"
		        : "link session cancelled for unload");
	} else {
		GBASIONetplayDriverDetach(driver);
	}
}

void GBASIONetplayDriverDetach(
    struct GBASIONetplayDriver* driver) {
	if (!driver || !driver->sio) {
		return;
	}
	bool orphanedSecondaryWait =
	    driver->secondaryStartPending &&
	    driver->sio->mode == GBA_SIO_MULTI &&
	    GBASIOMultiplayerIsBusy(driver->sio->siocnt) &&
	    !_transferInProgress(driver) &&
	    (!driver->sio->p ||
	     !mTimingIsScheduled(
	         &driver->sio->p->timing,
	         &driver->sio->completeEvent));
	if (orphanedSecondaryWait) {
		driver->sio->siocnt =
		    GBASIOMultiplayerClearBusy(
		        driver->sio->siocnt);
	}
	driver->secondaryStartPending = false;
	if (driver->sio->mode == GBA_SIO_MULTI &&
	    !GBASIOMultiplayerIsBusy(driver->sio->siocnt)) {
		driver->sio->siocnt =
		    GBASIOMultiplayerFillReady(
		        driver->sio->siocnt);
		driver->sio->siocnt =
		    GBASIOMultiplayerFillSlave(
		        driver->sio->siocnt);
		driver->sio->siocnt =
		    GBASIOMultiplayerSetId(
		        driver->sio->siocnt, 0);
		driver->sio->rcnt =
		    GBASIORegisterRCNTFillSc(
		        driver->sio->rcnt);
	}
	if (driver->sio->driver == &driver->d) {
		GBASIOSetDriver(driver->sio, NULL);
	} else {
		_driverDeinit(&driver->d);
	}
}

bool GBASIONetplayDriverIsAttached(
    const struct GBASIONetplayDriver* driver) {
	return driver && driver->attached;
}

bool GBASIONetplayDriverIsObservable(
    const struct GBASIONetplayDriver* driver) {
	return driver && driver->observable;
}

bool GBASIONetplayDriverIsPaused(
    const struct GBASIONetplayDriver* driver) {
	return driver && driver->paused;
}
