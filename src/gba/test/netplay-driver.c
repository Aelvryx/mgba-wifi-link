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
#include <mgba/internal/gba/sio/netplay/driver.h>
#include <mgba/internal/gba/sio/netplay/transport.h>

struct DriverDelayedPacket {
	uint8_t data[GBA_LINK_MAX_PACKET_SIZE];
	size_t size;
	unsigned pollsRemaining;
};

struct DriverFixture {
	struct mCore* core;
	struct GBA* gba;
	struct GBALinkTransport transport;
	struct GBALinkSession session;
	struct GBASIONetplayDriver driver;
	struct DriverFixture* peer;
	bool injectModeBeforeTransferStart;
	bool injectModeDuringCompletionCatchup;
	enum GBALinkMessageType duplicateMessageType;
	enum GBALinkMessageType conflictMessageType;
	enum GBALinkMessageType dropMessageType;
	enum GBALinkMessageType failMessageType;
	enum GBALinkMessageType stopPeerAfterMessageType;
	unsigned messageSends[
	    GBA_LINK_MESSAGE_COMPLETION_DECISION_ACK + 1];
	unsigned deliveryDelayPolls;
	struct DriverDelayedPacket delayed[GBA_LINK_MAX_COPIED_PACKETS];
	size_t delayedSize;
	uint32_t advancePollMs;
	unsigned sends;
	uint64_t now;
};

static void _driveExecutionBoundary(
    struct DriverFixture* fixture);

static bool _send(
    void* context, const void* data, size_t size, bool flush) {
	UNUSED(flush);
	struct DriverFixture* fixture = context;
	++fixture->sends;
	struct GBALinkPacket packet;
	bool decoded = GBALinkPacketDecode(
	        data, size, fixture->session.localRole,
	        &packet) == GBA_LINK_DECODE_OK;
	if (decoded) {
		++fixture->messageSends[packet.header.type];
		if (packet.header.type ==
		    fixture->failMessageType) {
			fixture->failMessageType = 0;
			return false;
		}
		if (packet.header.type ==
		    fixture->dropMessageType) {
			fixture->dropMessageType = 0;
			return true;
		}
	}
	if (fixture->deliveryDelayPolls) {
		assert_true(
		    fixture->delayedSize <
		    GBA_LINK_MAX_COPIED_PACKETS);
		struct DriverDelayedPacket* delayed =
		    &fixture->delayed[fixture->delayedSize++];
		memcpy(delayed->data, data, size);
		delayed->size = size;
		delayed->pollsRemaining =
		    fixture->deliveryDelayPolls;
		return true;
	}
	if (fixture->peer) {
		if (!GBALinkTransportQueueInbound(
		    &fixture->peer->transport,
		    fixture->peer->transport.generation,
		    data, size)) {
			return false;
		}
		if (decoded &&
		    packet.header.type ==
		        fixture->duplicateMessageType) {
			fixture->duplicateMessageType = 0;
			return GBALinkTransportQueueInbound(
			    &fixture->peer->transport,
			    fixture->peer->transport.generation,
			    data, size);
		}
		if (decoded && packet.header.type ==
		    fixture->conflictMessageType) {
			fixture->conflictMessageType = 0;
			if (packet.header.type ==
			    GBA_LINK_MESSAGE_TRANSFER_COMMIT) {
				packet.payload.transferCommit.words[0] ^= 1;
			}
			uint8_t conflicting[GBA_LINK_MAX_PACKET_SIZE];
			size_t conflictingSize = 0;
			assert_true(GBALinkPacketEncode(
			    &packet, conflicting,
			    sizeof(conflicting),
			    &conflictingSize));
			return GBALinkTransportQueueInbound(
			    &fixture->peer->transport,
			    fixture->peer->transport.generation,
			    conflicting, conflictingSize);
		}
		if (decoded &&
		    packet.header.type ==
		        fixture->stopPeerAfterMessageType) {
			fixture->stopPeerAfterMessageType = 0;
			GBALinkTransportInvalidate(
			    &fixture->peer->transport,
			    GBA_LINK_REASON_TRANSPORT_STOP,
			    NULL);
			GBALinkSessionFail(
			    &fixture->peer->session,
			    GBA_LINK_REASON_TRANSPORT_STOP,
			    "injected terminal delivery failure");
		}
	}
	return true;
}

static bool _poll(void* context) {
	struct DriverFixture* fixture = context;
	struct DriverFixture* endpoints[] = {
		fixture, fixture->peer,
	};
	for (unsigned endpointIndex = 0;
	     endpointIndex < 2; ++endpointIndex) {
		struct DriverFixture* endpoint =
		    endpoints[endpointIndex];
		if (!endpoint || !endpoint->peer) {
			continue;
		}
		size_t write = 0;
		for (size_t i = 0;
		     i < endpoint->delayedSize; ++i) {
			struct DriverDelayedPacket delayed =
			    endpoint->delayed[i];
			if (delayed.pollsRemaining) {
				--delayed.pollsRemaining;
			}
			if (!delayed.pollsRemaining) {
				assert_true(
				    GBALinkTransportQueueInbound(
				        &endpoint->peer->transport,
				        endpoint->peer->transport.generation,
				        delayed.data, delayed.size));
			} else {
				endpoint->delayed[write++] =
				    delayed;
			}
		}
		endpoint->delayedSize = write;
	}
	if (fixture->peer &&
	    GBALinkSessionIsLive(&fixture->peer->session)) {
		GBALinkSessionUpdate(
		    &fixture->peer->session, false);
		if (fixture->peer->injectModeBeforeTransferStart &&
		    fixture->peer->driver.transfer.state ==
		        GBA_SIO_NETPLAY_TRANSFER_CLIENT_CATCHUP_START &&
		    fixture->peer->driver.executionLimitEnabled) {
			fixture->peer->injectModeBeforeTransferStart =
			    false;
			uint64_t remaining =
			    fixture->peer->driver.executionLimit -
			    fixture->peer->driver.localCycle;
			assert_true(remaining > 1);
			fixture->peer->gba->timing.masterCycles +=
			    (uint32_t) (remaining / 2);
			GBASIOWriteSIOCNT(
			    &fixture->peer->gba->sio, 0x1000);
		}
		if (fixture->peer->injectModeDuringCompletionCatchup &&
		    fixture->peer->driver.transfer.state ==
		        GBA_SIO_NETPLAY_TRANSFER_CLIENT_CATCHUP_COMPLETION &&
		    fixture->peer->driver.executionLimitEnabled) {
			fixture->peer->injectModeDuringCompletionCatchup =
			    false;
			uint64_t remaining =
			    fixture->peer->driver.executionLimit -
			    fixture->peer->driver.localCycle;
			assert_true(remaining > 1);
			fixture->peer->gba->timing.masterCycles +=
			    (uint32_t) (remaining / 2);
			GBASIOWriteSIOCNT(
			    &fixture->peer->gba->sio, 0x1000);
		}
		_driveExecutionBoundary(fixture->peer);
	}
	fixture->now += fixture->advancePollMs;
	return true;
}

static uint64_t _now(void* context) {
	struct DriverFixture* fixture = context;
	return fixture->now;
}

static void _diagnostic(
    void* context, enum GBALinkDiagnosticLevel level,
    enum GBALinkReason reason, const char* message) {
	UNUSED(context);
	UNUSED(level);
	UNUSED(reason);
	assert_non_null(message);
}

static void _stop(void* context) {
	UNUSED(context);
}

static const struct GBALinkTransportVTable _vtable = {
	.sendReliable = _send,
	.pollReceive = _poll,
	.monotonicTimeMs = _now,
	.diagnostic = _diagnostic,
	.stop = _stop,
};

static void _initFixture(
    struct DriverFixture* fixture, enum GBALinkRole role) {
	memset(fixture, 0, sizeof(*fixture));
	fixture->core = GBACoreCreate();
	assert_non_null(fixture->core);
	assert_true(fixture->core->init(fixture->core));
	mCoreInitConfig(fixture->core, NULL);
	fixture->core->reset(fixture->core);
	fixture->gba = fixture->core->board;
	GBASIOWriteRCNT(&fixture->gba->sio, 0);
	GBASIOWriteSIOCNT(&fixture->gba->sio, 0x2000);

	GBALinkTransportInit(
	    &fixture->transport, &_vtable, fixture);
	assert_true(GBALinkTransportStart(
	    &fixture->transport, 1, role));
	GBALinkSessionInit(
	    &fixture->session, &fixture->transport);
	fixture->session.state =
	    role == GBA_LINK_ROLE_HOST
	        ? GBA_LINK_SESSION_READY
	        : GBA_LINK_SESSION_ATTACH_BARRIER;
	fixture->session.localRole = role;
	fixture->session.transportGeneration = 1;
	fixture->session.nextRemotePacketSequence = 1;
	fixture->session.sessionId = 1;
	fixture->session.attachCycle = 0;
	fixture->session.initialModeGeneration = 1;
	fixture->session.localHelloSent = true;
	fixture->session.remoteHelloReceived = true;
	fixture->session.localHello.initialMode =
	    GBA_LINK_MODE_MULTI;
	fixture->session.remoteHello.initialMode =
	    GBA_LINK_MODE_MULTI;
	GBALinkDeadlinePolicyInit(
	    &fixture->session.config.deadlines);
	GBASIONetplayDriverCreate(
	    &fixture->driver, &fixture->gba->sio,
	    &fixture->session);
	fixture->session.config.callbacks =
	    GBASIONetplayDriverSessionCallbacks();
	fixture->session.config.callbackContext =
	    &fixture->driver;
}

static void _deinitFixture(
    struct DriverFixture* fixture) {
	GBASIONetplayDriverDetach(&fixture->driver);
	fixture->session.state = GBA_LINK_SESSION_DISCONNECTED;
	GBALinkSessionDeinit(&fixture->session);
	GBALinkTransportInvalidate(
	    &fixture->transport, GBA_LINK_REASON_USER_DISCONNECT,
	    NULL);
	GBALinkTransportDeinit(&fixture->transport);
	mCoreConfigDeinit(&fixture->core->config);
	fixture->core->deinit(fixture->core);
}

static void _attachHostAndCommit(
    struct DriverFixture* fixture) {
	const struct GBALinkSessionCallbacks* callbacks =
	    GBASIONetplayDriverSessionCallbacks();
	callbacks->setAttachment(
	    &fixture->driver, true, true, 0);
	assert_true(fixture->driver.timeline.modeBarrier);

	struct GBALinkPacket ack;
	memset(&ack, 0, sizeof(ack));
	ack.header.type = GBA_LINK_MESSAGE_MODE_ACK;
	ack.header.sessionId = 1;
	ack.payload.modeAck.modeGeneration = 1;
	ack.payload.modeAck.commitCycle = 0;
	assert_true(callbacks->runtimePacket(
	    &fixture->driver, &ack));
}

static void _driveExecutionBoundary(
    struct DriverFixture* fixture) {
	struct GBASIONetplayDriver* driver = &fixture->driver;
	if (!driver->executionLimitEnabled ||
	    !driver->attached) {
		return;
	}
	if (driver->executionLimit > driver->localCycle) {
		uint64_t delta =
		    driver->executionLimit - driver->localCycle;
		assert_true(delta <= INT32_MAX);
		fixture->gba->timing.masterCycles +=
		    (uint32_t) delta;
	}
	mTimingDeschedule(
	    &fixture->gba->timing,
	    &driver->schedulerEvent);
	driver->schedulerEvent.callback(
	    &fixture->gba->timing,
	    driver->schedulerEvent.context, 0);
	if (mTimingIsScheduled(
	        &fixture->gba->timing,
	        &fixture->gba->sio.completeEvent) &&
	    mTimingUntil(
	        &fixture->gba->timing,
	        &fixture->gba->sio.completeEvent) <= 0) {
		mTimingDeschedule(
		    &fixture->gba->timing,
		    &fixture->gba->sio.completeEvent);
		fixture->gba->sio.completeEvent.callback(
		    &fixture->gba->timing,
		    fixture->gba->sio.completeEvent.context, 0);
	}
}

static void _initAttachedPair(
    struct DriverFixture* host,
    struct DriverFixture* client) {
	_initFixture(host, GBA_LINK_ROLE_HOST);
	_initFixture(client, GBA_LINK_ROLE_CLIENT);
	host->peer = client;
	client->peer = host;
	const struct GBALinkSessionCallbacks* callbacks =
	    GBASIONetplayDriverSessionCallbacks();
	callbacks->setPaused(&client->driver, true);
	callbacks->setAttachment(
	    &client->driver, true, false, 0);
	callbacks->setAttachment(
	    &host->driver, true, true, 0);
	assert_true(GBALinkSessionUpdate(
	    &client->session, false));
	assert_true(GBALinkSessionUpdate(
	    &client->session, false));
	assert_true(GBALinkSessionUpdate(
	    &host->session, false));
	assert_true(host->driver.jointlyReady);
	assert_true(client->driver.jointlyReady);
	assert_false(host->driver.paused);
	assert_true(client->driver.paused);
}

static void _configureTransfer(
    struct DriverFixture* host,
    struct DriverFixture* client, bool irq) {
	host->gba->memory.io[GBA_REG(SIOMLT_SEND)] =
	    0x1234;
	client->gba->memory.io[GBA_REG(SIOMLT_SEND)] =
	    0x5678;
	host->gba->memory.io[GBA_REG(IF)] = 0;
	client->gba->memory.io[GBA_REG(IF)] = 0;
	uint16_t control = irq ? 0x6000 : 0x2000;
	GBASIOWriteSIOCNT(&host->gba->sio, control);
	GBASIOWriteSIOCNT(&client->gba->sio, control);
}

static void _startHostTransfer(
    struct DriverFixture* host, bool irq) {
	GBASIOWriteSIOCNT(
	    &host->gba->sio,
	    (irq ? 0x6000 : 0x2000) | 0x80);
}

static void _completeHostTransfer(
    struct DriverFixture* host) {
	assert_true(mTimingIsScheduled(
	    &host->gba->timing,
	    &host->gba->sio.completeEvent));
	int32_t duration = mTimingUntil(
	    &host->gba->timing,
	    &host->gba->sio.completeEvent);
	assert_true(duration >= 0);
	host->gba->timing.masterCycles +=
	    (uint32_t) duration;
	mTimingDeschedule(
	    &host->gba->timing,
	    &host->gba->sio.completeEvent);
	host->gba->sio.completeEvent.callback(
	    &host->gba->timing,
	    host->gba->sio.completeEvent.context, 0);
}

static void _assertErrorCompletion(
    const struct DriverFixture* fixture, bool irq) {
	const uint16_t errors[] = {
		0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
	};
	assert_memory_equal(
	    &fixture->gba->memory.io[GBA_REG(SIOMULTI0)],
	    errors, sizeof(errors));
	assert_false(GBASIOMultiplayerIsBusy(
	    fixture->gba->sio.siocnt));
	assert_true(GBASIOMultiplayerIsError(
	    fixture->gba->sio.siocnt));
	assert_true(GBASIOMultiplayerIsReady(
	    fixture->gba->sio.siocnt));
	assert_true(GBASIOMultiplayerIsSlave(
	    fixture->gba->sio.siocnt));
	assert_int_equal(
	    GBASIOMultiplayerGetId(
	        fixture->gba->sio.siocnt), 0);
	assert_int_equal(
	    !!(fixture->gba->memory.io[GBA_REG(IF)] &
	       (1 << GBA_IRQ_SIO)),
	    irq);
}

M_TEST_DEFINE(quiescentAttachRejectsPendingSioCompletion) {
	struct DriverFixture fixture;
	_initFixture(&fixture, GBA_LINK_ROLE_HOST);
	enum GBALinkWireMode mode;
	uint64_t cycle;
	assert_true(GBASIONetplayDriverQuiescentSnapshot(
	    &fixture.driver, &mode, &cycle));
	assert_int_equal(mode, GBA_LINK_MODE_MULTI);

	mTimingSchedule(
	    &fixture.gba->timing,
	    &fixture.gba->sio.completeEvent, 16);
	assert_false(GBASIONetplayDriverQuiescentSnapshot(
	    &fixture.driver, &mode, &cycle));
	mTimingDeschedule(
	    &fixture.gba->timing,
	    &fixture.gba->sio.completeEvent);
	fixture.gba->sio.siocnt =
	    GBASIOMultiplayerFillBusy(
	        fixture.gba->sio.siocnt);
	assert_false(GBASIONetplayDriverQuiescentSnapshot(
	    &fixture.driver, &mode, &cycle));
	fixture.gba->sio.siocnt =
	    GBASIOMultiplayerClearBusy(
	        fixture.gba->sio.siocnt);
	_deinitFixture(&fixture);
}

M_TEST_DEFINE(hostAttachSeparatesTopologyAndEffectiveCount) {
	struct DriverFixture fixture;
	_initFixture(&fixture, GBA_LINK_ROLE_HOST);
	_attachHostAndCommit(&fixture);

	assert_true(GBASIONetplayDriverIsAttached(
	    &fixture.driver));
	assert_true(GBASIONetplayDriverIsObservable(
	    &fixture.driver));
	assert_int_equal(
	    fixture.driver.topologicalPeerCount, 1);
	assert_true(fixture.driver.jointlyReady);
	assert_int_equal(
	    fixture.driver.d.connectedDevices(
	        &fixture.driver.d), 1);
	assert_int_equal(
	    fixture.driver.d.deviceId(&fixture.driver.d), 0);
	assert_true(GBASIOMultiplayerIsReady(
	    fixture.gba->sio.siocnt));
	assert_false(GBASIOMultiplayerIsSlave(
	    fixture.gba->sio.siocnt));
	_deinitFixture(&fixture);
}

M_TEST_DEFINE(clientAttachmentRemainsHiddenUntilHostRelease) {
	struct DriverFixture fixture;
	_initFixture(&fixture, GBA_LINK_ROLE_CLIENT);
	const struct GBALinkSessionCallbacks* callbacks =
	    GBASIONetplayDriverSessionCallbacks();
	callbacks->setPaused(&fixture.driver, true);
	callbacks->setAttachment(
	    &fixture.driver, true, false, 0);
	assert_true(GBASIONetplayDriverIsAttached(
	    &fixture.driver));
	assert_false(GBASIONetplayDriverIsObservable(
	    &fixture.driver));
	assert_int_equal(
	    fixture.driver.topologicalPeerCount, 1);
	assert_int_equal(
	    fixture.driver.d.connectedDevices(
	        &fixture.driver.d), 0);

	callbacks->setAttachment(
	    &fixture.driver, true, true, 0);
	struct GBALinkPacket commit;
	memset(&commit, 0, sizeof(commit));
	commit.header.type = GBA_LINK_MESSAGE_MODE_COMMIT;
	commit.header.sessionId = 1;
	commit.payload.modeCommit.modeGeneration = 1;
	commit.payload.modeCommit.commitCycle = 0;
	commit.payload.modeCommit.hostMode = GBA_LINK_MODE_MULTI;
	commit.payload.modeCommit.clientMode = GBA_LINK_MODE_MULTI;
	commit.payload.modeCommit.jointlyReady = true;
	assert_true(callbacks->runtimePacket(
	    &fixture.driver, &commit));
	struct GBALinkPacket ack;
	memset(&ack, 0, sizeof(ack));
	ack.header.type = GBA_LINK_MESSAGE_MODE_ACK;
	ack.header.sessionId = 1;
	ack.payload.modeAck.modeGeneration = 1;
	ack.payload.modeAck.commitCycle = 0;
	assert_true(callbacks->runtimePacket(
	    &fixture.driver, &ack));

	assert_true(fixture.driver.jointlyReady);
	assert_int_equal(
	    fixture.driver.d.deviceId(&fixture.driver.d), 1);
	assert_int_equal(
	    fixture.driver.d.connectedDevices(
	        &fixture.driver.d), 1);
	assert_true(GBASIOMultiplayerIsSlave(
	    fixture.gba->sio.siocnt));
	assert_int_equal(
	    GBASIOMultiplayerGetId(
	        fixture.gba->sio.siocnt), 1);
	assert_true(GBASIONetplayDriverIsPaused(
	    &fixture.driver));
	_deinitFixture(&fixture);
}

M_TEST_DEFINE(localSchedulerQuantumDoesNotEmitPacket) {
	struct DriverFixture fixture;
	_initFixture(&fixture, GBA_LINK_ROLE_HOST);
	_attachHostAndCommit(&fixture);
	unsigned sends = fixture.sends;
	assert_true(mTimingIsScheduled(
	    &fixture.gba->timing,
	    &fixture.driver.schedulerEvent));
	mTimingDeschedule(
	    &fixture.gba->timing,
	    &fixture.driver.schedulerEvent);
	fixture.driver.schedulerEvent.callback(
	    &fixture.gba->timing,
	    fixture.driver.schedulerEvent.context, 0);
	assert_int_equal(fixture.sends, sends);
	_deinitFixture(&fixture);
}

M_TEST_DEFINE(grantBoundaryAcceptsBoundedTimingAccountingSlop) {
	struct DriverFixture host;
	struct DriverFixture client;
	_initAttachedPair(&host, &client);
	host.gba->timing.masterCycles += 100;
	assert_true(GBASIONetplayDriverHostFrameBoundary(
	    &host.driver));
	assert_true(GBALinkSessionUpdate(
	    &client.session, false));
	assert_true(client.driver.executionLimitEnabled);
	assert_int_equal(
	    client.driver.boundary,
	    GBA_SIO_NETPLAY_BOUNDARY_GRANT);

	uint64_t advance =
	    client.driver.executionLimit -
	    client.driver.localCycle + 2;
	assert_true(advance <= UINT32_MAX);
	client.gba->timing.masterCycles +=
	    (uint32_t) advance;
	mTimingDeschedule(
	    &client.gba->timing,
	    &client.driver.schedulerEvent);
	client.driver.schedulerEvent.callback(
	    &client.gba->timing,
	    client.driver.schedulerEvent.context, 0);

	assert_false(client.driver.timeline.grantOutstanding);
	assert_false(client.driver.executionLimitEnabled);
	assert_int_equal(
	    client.messageSends[
	        GBA_LINK_MESSAGE_GRANT_ACK],
	    1);
	assert_true(GBALinkSessionUpdate(
	    &host.session, false));
	assert_false(host.driver.timeline.grantOutstanding);
	_deinitFixture(&client);
	_deinitFixture(&host);
}

M_TEST_DEFINE(clientStartPauseInterruptsBeforeUncommittedCompletion) {
	struct DriverFixture host;
	struct DriverFixture client;
	_initAttachedPair(&host, &client);
	client.gba->earlyExit = false;

	struct GBALinkPacket start;
	memset(&start, 0, sizeof(start));
	start.header.type = GBA_LINK_MESSAGE_TRANSFER_START;
	start.header.sessionId = client.session.sessionId;
	start.payload.transferStart.transferSequence = 1;
	start.payload.transferStart.startCycle =
	    client.driver.timeline.currentCableCycle + 64;
	start.payload.transferStart.siocnt = 0x6000;
	start.payload.transferStart.completionCycle =
	    start.payload.transferStart.startCycle +
	    GBASIOTransferCycles(
	        GBA_SIO_MULTI,
	        start.payload.transferStart.siocnt, 1);
	start.payload.transferStart.outgoingWord = 0x1234;
	assert_true(
	    GBASIONetplayDriverSessionCallbacks()->runtimePacket(
	        &client.driver, &start));
	assert_false(client.driver.paused);

	_driveExecutionBoundary(&client);
	assert_true(client.driver.paused);
	assert_true(client.gba->earlyExit);
	assert_int_equal(
	    client.driver.transfer.state,
	    GBA_SIO_NETPLAY_TRANSFER_CLIENT_WAIT_COMMIT);
	assert_true(mTimingIsScheduled(
	    &client.gba->timing,
	    &client.gba->sio.completeEvent));
	assert_true(GBASIOMultiplayerIsBusy(
	    client.gba->sio.siocnt));

	/*
	 * The pause interrupt moves later timing events out of the active
	 * event pass. Re-rooting that queue must not consume the pending SIO
	 * completion before COMMIT and COMPLETION_CATCHUP arrive.
	 */
	mTimingTick(&client.gba->timing, 0);
	assert_true(mTimingIsScheduled(
	    &client.gba->timing,
	    &client.gba->sio.completeEvent));
	assert_int_equal(
	    client.driver.transfer.state,
	    GBA_SIO_NETPLAY_TRANSFER_CLIENT_WAIT_COMMIT);
	assert_true(GBASIOMultiplayerIsBusy(
	    client.gba->sio.siocnt));

	GBASIONetplayDriverCancel(
	    &client.driver, GBA_LINK_REASON_UNLOAD);
	_deinitFixture(&client);
	_deinitFixture(&host);
}

M_TEST_DEFINE(idleDetachCleansLinesWithoutTouchingDataOrError) {
	struct DriverFixture fixture;
	_initFixture(&fixture, GBA_LINK_ROLE_HOST);
	_attachHostAndCommit(&fixture);
	uint16_t words[] = {
		0x1357, 0x2468, 0x9ABC, 0xDEF0,
	};
	memcpy(
	    &fixture.gba->memory.io[GBA_REG(SIOMULTI0)],
	    words, sizeof(words));
	fixture.gba->sio.siocnt =
	    GBASIOMultiplayerFillError(
	        fixture.gba->sio.siocnt);
	GBASIONetplayDriverDetach(&fixture.driver);

	assert_null(fixture.gba->sio.driver);
	assert_memory_equal(
	    &fixture.gba->memory.io[GBA_REG(SIOMULTI0)],
	    words, sizeof(words));
	assert_false(GBASIOMultiplayerIsBusy(
	    fixture.gba->sio.siocnt));
	assert_true(GBASIOMultiplayerIsError(
	    fixture.gba->sio.siocnt));
	assert_true(GBASIOMultiplayerIsReady(
	    fixture.gba->sio.siocnt));
	assert_true(GBASIOMultiplayerIsSlave(
	    fixture.gba->sio.siocnt));
	assert_int_equal(
	    GBASIOMultiplayerGetId(
	        fixture.gba->sio.siocnt), 0);
	assert_true(GBASIORegisterRCNTIsSc(
	    fixture.gba->sio.rcnt));
	_deinitFixture(&fixture);
}

M_TEST_DEFINE(twoDriverTransferUsesCommonCompletionAtEveryBaud) {
	for (unsigned baud = 0; baud < 4; ++baud) {
		struct DriverFixture host;
		struct DriverFixture client;
		_initAttachedPair(&host, &client);
		host.gba->memory.io[GBA_REG(SIOMLT_SEND)] =
		    0x1200 | baud;
		client.gba->memory.io[GBA_REG(SIOMLT_SEND)] =
		    0x3400 | baud;
		host.gba->memory.io[GBA_REG(IF)] = 0;
		client.gba->memory.io[GBA_REG(IF)] = 0;
		GBASIOWriteSIOCNT(
		    &host.gba->sio, 0x6000 | baud);
		GBASIOWriteSIOCNT(
		    &client.gba->sio, 0x6000 | baud);

		GBASIOWriteSIOCNT(
		    &host.gba->sio, 0x6080 | baud);
		assert_true(mTimingIsScheduled(
		    &host.gba->timing,
		    &host.gba->sio.completeEvent));
		assert_int_equal(
		    mTimingUntil(
		        &host.gba->timing,
		        &host.gba->sio.completeEvent),
		    GBASIOTransferCycles(
		        GBA_SIO_MULTI,
		        host.gba->sio.siocnt, 1));
		assert_int_equal(
		    host.driver.transfer.state,
		    GBA_SIO_NETPLAY_TRANSFER_COMMITTED);
		assert_int_equal(
		    client.driver.transfer.state,
		    GBA_SIO_NETPLAY_TRANSFER_CLIENT_WAIT_COMMIT);
		assert_true(GBALinkSessionUpdate(
		    &client.session, false));
		assert_int_equal(
		    client.driver.transfer.state,
		    GBA_SIO_NETPLAY_TRANSFER_COMMITTED);
		assert_true(client.driver.paused);
		assert_true(mTimingIsScheduled(
		    &client.gba->timing,
		    &client.gba->sio.completeEvent));

		int32_t hostDuration = mTimingUntil(
		    &host.gba->timing,
		    &host.gba->sio.completeEvent);
		host.gba->timing.masterCycles +=
		    (uint32_t) hostDuration;
		mTimingDeschedule(
		    &host.gba->timing,
		    &host.gba->sio.completeEvent);
		host.gba->sio.completeEvent.callback(
		    &host.gba->timing,
		    host.gba->sio.completeEvent.context, 0);

		const uint16_t expected[] = {
			(uint16_t) (0x1200 | baud),
			(uint16_t) (0x3400 | baud),
			0xFFFF,
			0xFFFF,
		};
		assert_memory_equal(
		    &host.gba->memory.io[GBA_REG(SIOMULTI0)],
		    expected, sizeof(expected));
		assert_memory_equal(
		    &client.gba->memory.io[GBA_REG(SIOMULTI0)],
		    expected, sizeof(expected));
		assert_false(GBASIOMultiplayerIsBusy(
		    host.gba->sio.siocnt));
		assert_false(GBASIOMultiplayerIsBusy(
		    client.gba->sio.siocnt));
		assert_int_equal(
		    GBASIOMultiplayerGetId(
		        host.gba->sio.siocnt), 0);
		assert_int_equal(
		    GBASIOMultiplayerGetId(
		        client.gba->sio.siocnt), 1);
		assert_true(
		    host.gba->memory.io[GBA_REG(IF)] &
		    (1 << GBA_IRQ_SIO));
		assert_true(
		    client.gba->memory.io[GBA_REG(IF)] &
		    (1 << GBA_IRQ_SIO));
		assert_int_equal(
		    host.driver.transfer.state,
		    GBA_SIO_NETPLAY_TRANSFER_FINISHED);
		assert_int_equal(
		    client.driver.transfer.state,
		    GBA_SIO_NETPLAY_TRANSFER_FINISHED);
		_deinitFixture(&client);
		_deinitFixture(&host);
	}
}

M_TEST_DEFINE(successfulTransferHonorsDisabledIrq) {
	struct DriverFixture host;
	struct DriverFixture client;
	_initAttachedPair(&host, &client);
	_configureTransfer(&host, &client, false);
	_startHostTransfer(&host, false);
	assert_true(GBALinkSessionUpdate(
	    &client.session, false));
	_completeHostTransfer(&host);
	const uint16_t expected[] = {
		0x1234, 0x5678, 0xFFFF, 0xFFFF,
	};
	assert_memory_equal(
	    &host.gba->memory.io[GBA_REG(SIOMULTI0)],
	    expected, sizeof(expected));
	assert_memory_equal(
	    &client.gba->memory.io[GBA_REG(SIOMULTI0)],
	    expected, sizeof(expected));
	assert_int_equal(
	    host.gba->memory.io[GBA_REG(IF)] &
	        (1 << GBA_IRQ_SIO),
	    0);
	assert_int_equal(
	    client.gba->memory.io[GBA_REG(IF)] &
	        (1 << GBA_IRQ_SIO),
	    0);
	_deinitFixture(&client);
	_deinitFixture(&host);
}

struct DriverLatencyResult {
	uint64_t hostCompletionCycle;
	uint64_t clientCompletionCycle;
	uint16_t hostWords[4];
	uint16_t clientWords[4];
	uint16_t hostSIOCNT;
	uint16_t clientSIOCNT;
	unsigned hostIrq;
	unsigned clientIrq;
};

static struct DriverLatencyResult _runTransferLatency(
    unsigned delayPolls) {
	struct DriverFixture host;
	struct DriverFixture client;
	_initAttachedPair(&host, &client);
	host.deliveryDelayPolls = delayPolls;
	client.deliveryDelayPolls = delayPolls;
	_configureTransfer(&host, &client, true);
	_startHostTransfer(&host, true);
	for (unsigned i = 0; i < 32 &&
	     client.driver.transfer.state !=
	         GBA_SIO_NETPLAY_TRANSFER_COMMITTED; ++i) {
		_poll(&host);
		if (GBALinkSessionIsLive(
		        &client.session)) {
			GBALinkSessionUpdate(
			    &client.session, false);
		}
	}
	assert_int_equal(
	    client.driver.transfer.state,
	    GBA_SIO_NETPLAY_TRANSFER_COMMITTED);
	uint64_t completionCycle =
	    host.driver.transfer.completionCycle;
	_completeHostTransfer(&host);
	struct DriverLatencyResult result = {
		.hostCompletionCycle = completionCycle,
		.clientCompletionCycle =
		    client.driver.transfer.completionCycle,
		.hostSIOCNT = host.gba->sio.siocnt,
		.clientSIOCNT = client.gba->sio.siocnt,
		.hostIrq =
		    !!(host.gba->memory.io[GBA_REG(IF)] &
		       (1 << GBA_IRQ_SIO)),
		.clientIrq =
		    !!(client.gba->memory.io[GBA_REG(IF)] &
		       (1 << GBA_IRQ_SIO)),
	};
	memcpy(
	    result.hostWords,
	    &host.gba->memory.io[GBA_REG(SIOMULTI0)],
	    sizeof(result.hostWords));
	memcpy(
	    result.clientWords,
	    &client.gba->memory.io[GBA_REG(SIOMULTI0)],
	    sizeof(result.clientWords));
	_deinitFixture(&client);
	_deinitFixture(&host);
	return result;
}

M_TEST_DEFINE(deliveryLatencyDoesNotChangeTransferOutcome) {
	struct DriverLatencyResult immediate =
	    _runTransferLatency(0);
	for (unsigned delay = 1; delay <= 4; ++delay) {
		struct DriverLatencyResult delayed =
		    _runTransferLatency(delay);
		assert_int_equal(
		    delayed.hostCompletionCycle,
		    immediate.hostCompletionCycle);
		assert_int_equal(
		    delayed.clientCompletionCycle,
		    immediate.clientCompletionCycle);
		assert_memory_equal(
		    delayed.hostWords,
		    immediate.hostWords,
		    sizeof(immediate.hostWords));
		assert_memory_equal(
		    delayed.clientWords,
		    immediate.clientWords,
		    sizeof(immediate.clientWords));
		assert_int_equal(
		    delayed.hostSIOCNT,
		    immediate.hostSIOCNT);
		assert_int_equal(
		    delayed.clientSIOCNT,
		    immediate.clientSIOCNT);
		assert_int_equal(
		    delayed.hostIrq,
		    immediate.hostIrq);
		assert_int_equal(
		    delayed.clientIrq,
		    immediate.clientIrq);
	}
}

M_TEST_DEFINE(notReadyHostStartUsesOrdinaryNoPeerPath) {
	struct DriverFixture fixture;
	_initFixture(&fixture, GBA_LINK_ROLE_HOST);
	const struct GBALinkSessionCallbacks* callbacks =
	    GBASIONetplayDriverSessionCallbacks();
	callbacks->setAttachment(
	    &fixture.driver, true, true, 0);
	assert_false(fixture.driver.jointlyReady);
	unsigned sends = fixture.sends;
	GBASIOWriteSIOCNT(
	    &fixture.gba->sio, 0x2000);
	GBASIOWriteSIOCNT(
	    &fixture.gba->sio, 0x2080);
	assert_int_equal(fixture.sends, sends);
	assert_int_equal(
	    fixture.driver.transfer.state,
	    GBA_SIO_NETPLAY_TRANSFER_IDLE);
	assert_true(mTimingIsScheduled(
	    &fixture.gba->timing,
	    &fixture.gba->sio.completeEvent));
	assert_int_equal(
	    mTimingUntil(
	        &fixture.gba->timing,
	        &fixture.gba->sio.completeEvent),
	    GBASIOTransferCycles(
	        GBA_SIO_MULTI,
	        fixture.gba->sio.siocnt, 0));
	_deinitFixture(&fixture);
}

M_TEST_DEFINE(clientIndependentStartWaitsForPrimary) {
	struct DriverFixture host;
	struct DriverFixture client;
	_initAttachedPair(&host, &client);
	unsigned sends = client.sends;
	GBASIOWriteSIOCNT(
	    &client.gba->sio, 0x6000);
	GBASIOWriteSIOCNT(
	    &client.gba->sio, 0x6080);
	assert_int_equal(client.sends, sends);
	assert_true(GBASIOMultiplayerIsBusy(
	    client.gba->sio.siocnt));
	assert_false(mTimingIsScheduled(
	    &client.gba->timing,
	    &client.gba->sio.completeEvent));
	assert_int_equal(
	    client.driver.transfer.state,
	    GBA_SIO_NETPLAY_TRANSFER_IDLE);
	assert_true(client.driver.secondaryStartPending);
	_deinitFixture(&client);
	_deinitFixture(&host);
}

M_TEST_DEFINE(clientIndependentStartThenDisconnectRestoresIdleLines) {
	struct DriverFixture host;
	struct DriverFixture client;
	_initAttachedPair(&host, &client);
	const uint16_t words[] = {
		0x1357, 0x2468, 0x9ABC, 0xDEF0,
	};
	memcpy(
	    &client.gba->memory.io[GBA_REG(SIOMULTI0)],
	    words, sizeof(words));
	client.gba->memory.io[GBA_REG(IF)] = 0;
	client.gba->sio.siocnt =
	    GBASIOMultiplayerFillError(
	        client.gba->sio.siocnt);

	GBASIOWriteSIOCNT(
	    &client.gba->sio, 0x6080);
	assert_true(client.driver.secondaryStartPending);
	assert_true(GBASIOMultiplayerIsBusy(
	    client.gba->sio.siocnt));
	assert_false(mTimingIsScheduled(
	    &client.gba->timing,
	    &client.gba->sio.completeEvent));

	GBALinkSessionFail(
	    &client.session, GBA_LINK_REASON_PEER_DETACH,
	    "injected disconnect during secondary wait");

	assert_null(client.gba->sio.driver);
	assert_false(client.driver.secondaryStartPending);
	assert_false(GBASIOMultiplayerIsBusy(
	    client.gba->sio.siocnt));
	assert_false(mTimingIsScheduled(
	    &client.gba->timing,
	    &client.gba->sio.completeEvent));
	assert_false(
	    client.gba->memory.io[GBA_REG(IF)] &
	    (1 << GBA_IRQ_SIO));
	assert_memory_equal(
	    &client.gba->memory.io[GBA_REG(SIOMULTI0)],
	    words, sizeof(words));
	assert_true(GBASIOMultiplayerIsError(
	    client.gba->sio.siocnt));
	assert_true(GBASIOMultiplayerIsReady(
	    client.gba->sio.siocnt));
	assert_true(GBASIOMultiplayerIsSlave(
	    client.gba->sio.siocnt));
	assert_int_equal(
	    GBASIOMultiplayerGetId(
	        client.gba->sio.siocnt), 0);
	assert_true(GBASIORegisterRCNTIsSc(
	    client.gba->sio.rcnt));
	_deinitFixture(&client);
	_deinitFixture(&host);
}

M_TEST_DEFINE(remoteStartConsumesClientIndependentStart) {
	struct DriverFixture host;
	struct DriverFixture client;
	_initAttachedPair(&host, &client);
	_configureTransfer(&host, &client, true);

	GBASIOWriteSIOCNT(
	    &client.gba->sio, 0x6080);
	assert_true(client.driver.secondaryStartPending);
	_startHostTransfer(&host, true);

	assert_false(client.driver.secondaryStartPending);
	assert_true(client.driver.transfer.startAccepted);
	assert_true(mTimingIsScheduled(
	    &client.gba->timing,
	    &client.gba->sio.completeEvent));
	_completeHostTransfer(&host);
	assert_false(GBASIOMultiplayerIsBusy(
	    client.gba->sio.siocnt));
	assert_int_equal(
	    client.gba->memory.io[GBA_REG(SIOMULTI0)],
	    0x1234);
	assert_int_equal(
	    client.gba->memory.io[GBA_REG(SIOMULTI1)],
	    0x5678);
	_deinitFixture(&client);
	_deinitFixture(&host);
}

M_TEST_DEFINE(stopAfterCommitErrorCompletesAtAnnouncedCycle) {
	struct DriverFixture host;
	struct DriverFixture client;
	_initAttachedPair(&host, &client);
	host.gba->memory.io[GBA_REG(SIOMLT_SEND)] = 0x1234;
	client.gba->memory.io[GBA_REG(SIOMLT_SEND)] = 0x5678;
	host.gba->memory.io[GBA_REG(IF)] = 0;
	client.gba->memory.io[GBA_REG(IF)] = 0;
	GBASIOWriteSIOCNT(&host.gba->sio, 0x6000);
	GBASIOWriteSIOCNT(&client.gba->sio, 0x6000);
	GBASIOWriteSIOCNT(&host.gba->sio, 0x6080);
	assert_true(GBALinkSessionUpdate(
	    &client.session, false));
	uint64_t completionCycle =
	    host.driver.transfer.completionCycle;
	assert_int_equal(
	    completionCycle,
	    client.driver.transfer.completionCycle);

	GBALinkSessionFail(
	    &host.session, GBA_LINK_REASON_TRANSPORT_STOP,
	    "injected stop after commit");
	GBALinkSessionFail(
	    &client.session, GBA_LINK_REASON_TRANSPORT_STOP,
	    "injected stop after commit");
	assert_true(host.driver.transfer.abortPending);
	assert_true(client.driver.transfer.abortPending);

	int32_t hostDuration = mTimingUntil(
	    &host.gba->timing,
	    &host.gba->sio.completeEvent);
	host.gba->timing.masterCycles +=
	    (uint32_t) hostDuration;
	mTimingDeschedule(
	    &host.gba->timing,
	    &host.gba->sio.completeEvent);
	host.gba->sio.completeEvent.callback(
	    &host.gba->timing,
	    host.gba->sio.completeEvent.context, 0);
	_driveExecutionBoundary(&client);

	const uint16_t errors[] = {
		0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
	};
	assert_memory_equal(
	    &host.gba->memory.io[GBA_REG(SIOMULTI0)],
	    errors, sizeof(errors));
	assert_memory_equal(
	    &client.gba->memory.io[GBA_REG(SIOMULTI0)],
	    errors, sizeof(errors));
	assert_true(GBASIOMultiplayerIsError(
	    host.gba->sio.siocnt));
	assert_true(GBASIOMultiplayerIsError(
	    client.gba->sio.siocnt));
	assert_false(GBASIOMultiplayerIsBusy(
	    host.gba->sio.siocnt));
	assert_false(GBASIOMultiplayerIsBusy(
	    client.gba->sio.siocnt));
	assert_true(GBASIOMultiplayerIsReady(
	    host.gba->sio.siocnt));
	assert_true(GBASIOMultiplayerIsSlave(
	    host.gba->sio.siocnt));
	assert_int_equal(
	    host.driver.topologicalPeerCount, 0);
	assert_int_equal(
	    client.driver.topologicalPeerCount, 0);
	assert_true(
	    host.gba->memory.io[GBA_REG(IF)] &
	    (1 << GBA_IRQ_SIO));
	assert_true(
	    client.gba->memory.io[GBA_REG(IF)] &
	    (1 << GBA_IRQ_SIO));

	GBASIOWriteSIOCNT(&host.gba->sio, 0x2000);
	GBASIOWriteSIOCNT(&host.gba->sio, 0x2080);
	assert_true(mTimingIsScheduled(
	    &host.gba->timing,
	    &host.gba->sio.completeEvent));
	assert_int_equal(
	    mTimingUntil(
	        &host.gba->timing,
	        &host.gba->sio.completeEvent),
	    GBASIOTransferCycles(
	        GBA_SIO_MULTI,
	        host.gba->sio.siocnt, 0));
	_deinitFixture(&client);
	_deinitFixture(&host);
}

M_TEST_DEFINE(startAndReadinessLossPreservePointOfNoReturn) {
	for (unsigned scenario = 0; scenario < 2; ++scenario) {
		struct DriverFixture host;
		struct DriverFixture client;
		_initAttachedPair(&host, &client);
		_configureTransfer(&host, &client, true);
		host.advancePollMs = GBA_LINK_MAX_WAIT_MS;
		if (scenario == 0) {
			host.dropMessageType =
			    GBA_LINK_MESSAGE_TRANSFER_START;
		} else {
			client.dropMessageType =
			    GBA_LINK_MESSAGE_TRANSFER_READY;
		}
		_startHostTransfer(&host, true);
		assert_true(host.driver.transfer.startEmitted);
		uint64_t completionCycle =
		    host.driver.transfer.completionCycle;
		assert_true(completionCycle > 0);
		assert_int_equal(
		    host.session.state,
		    GBA_LINK_SESSION_FAILED);
		assert_true(mTimingIsScheduled(
		    &host.gba->timing,
		    &host.gba->sio.completeEvent));
		assert_int_equal(
		    mTimingUntil(
		        &host.gba->timing,
		        &host.gba->sio.completeEvent),
		    (int64_t) completionCycle);

		if (scenario == 0) {
			assert_int_equal(
			    client.driver.transfer.state,
			    GBA_SIO_NETPLAY_TRANSFER_IDLE);
		} else {
			assert_true(
			    client.driver.transfer.startAccepted);
			assert_int_equal(
			    client.driver.transfer.completionCycle,
			    completionCycle);
			GBALinkSessionFail(
			    &client.session,
			    GBA_LINK_REASON_TRANSPORT_STOP,
			    "peer readiness delivery lost");
		}
		_completeHostTransfer(&host);
		if (scenario != 0) {
			_driveExecutionBoundary(&client);
			_assertErrorCompletion(&client, true);
		}
		_assertErrorCompletion(&host, true);
		_deinitFixture(&client);
		_deinitFixture(&host);
	}
}

M_TEST_DEFINE(preStartSendFailureUsesOrdinaryNoPeerTiming) {
	struct DriverFixture host;
	struct DriverFixture client;
	_initAttachedPair(&host, &client);
	_configureTransfer(&host, &client, true);
	host.failMessageType =
	    GBA_LINK_MESSAGE_TRANSFER_START;
	_startHostTransfer(&host, true);
	assert_false(host.driver.transfer.startEmitted);
	assert_null(host.gba->sio.driver);
	assert_true(mTimingIsScheduled(
	    &host.gba->timing,
	    &host.gba->sio.completeEvent));
	assert_int_equal(
	    mTimingUntil(
	        &host.gba->timing,
	        &host.gba->sio.completeEvent),
	    GBASIOTransferCycles(
	        GBA_SIO_MULTI,
	        host.gba->sio.siocnt, 0));
	assert_int_equal(
	    client.driver.transfer.state,
	    GBA_SIO_NETPLAY_TRANSFER_IDLE);
	_deinitFixture(&client);
	_deinitFixture(&host);
}

M_TEST_DEFINE(commitSendFailureUsesAnnouncedCompletionCycle) {
	struct DriverFixture host;
	struct DriverFixture client;
	_initAttachedPair(&host, &client);
	_configureTransfer(&host, &client, true);
	host.failMessageType =
	    GBA_LINK_MESSAGE_TRANSFER_COMMIT;
	_startHostTransfer(&host, true);
	assert_int_equal(
	    host.session.state,
	    GBA_LINK_SESSION_FAILED);
	assert_true(host.driver.transfer.startEmitted);
	assert_false(host.driver.transfer.commitAccepted);
	assert_true(client.driver.transfer.startAccepted);
	assert_int_equal(
	    client.driver.transfer.state,
	    GBA_SIO_NETPLAY_TRANSFER_CLIENT_WAIT_COMMIT);
	assert_int_equal(
	    host.driver.transfer.completionCycle,
	    client.driver.transfer.completionCycle);
	GBALinkSessionFail(
	    &client.session,
	    GBA_LINK_REASON_TRANSPORT_STOP,
	    "commit delivery failed");
	_completeHostTransfer(&host);
	_driveExecutionBoundary(&client);
	_assertErrorCompletion(&host, true);
	_assertErrorCompletion(&client, true);
	_deinitFixture(&client);
	_deinitFixture(&host);
}

M_TEST_DEFINE(completionMessageLossNeverLeavesBusySet) {
	const enum GBALinkMessageType losses[] = {
		GBA_LINK_MESSAGE_COMPLETION_CATCHUP,
		GBA_LINK_MESSAGE_COMPLETION_READY,
		GBA_LINK_MESSAGE_COMPLETION_DECISION,
	};
	for (unsigned scenario = 0;
	     scenario < sizeof(losses) / sizeof(losses[0]);
	     ++scenario) {
		struct DriverFixture host;
		struct DriverFixture client;
		_initAttachedPair(&host, &client);
		_configureTransfer(&host, &client, true);
		if (losses[scenario] ==
		    GBA_LINK_MESSAGE_COMPLETION_READY) {
			client.dropMessageType = losses[scenario];
		} else if (losses[scenario] ==
		           GBA_LINK_MESSAGE_COMPLETION_CATCHUP) {
			host.dropMessageType = losses[scenario];
		} else {
			host.failMessageType = losses[scenario];
		}
		host.advancePollMs = GBA_LINK_MAX_WAIT_MS;
		client.advancePollMs = GBA_LINK_MAX_WAIT_MS;
		_startHostTransfer(&host, true);
		assert_true(GBALinkSessionUpdate(
		    &client.session, false));
		_completeHostTransfer(&host);
		if (GBALinkSessionIsLive(
		        &client.session)) {
			GBALinkSessionFail(
			    &client.session,
			    GBA_LINK_REASON_TRANSPORT_STOP,
			    "completion message lost");
		}
		_driveExecutionBoundary(&client);
		_assertErrorCompletion(&host, true);
		_assertErrorCompletion(&client, true);
		_deinitFixture(&client);
		_deinitFixture(&host);
	}
}

M_TEST_DEFINE(finalDecisionDeliveryFailureHasScopedAsymmetry) {
	struct DriverFixture host;
	struct DriverFixture client;
	_initAttachedPair(&host, &client);
	_configureTransfer(&host, &client, true);
	host.stopPeerAfterMessageType =
	    GBA_LINK_MESSAGE_COMPLETION_DECISION;
	host.advancePollMs = GBA_LINK_MAX_WAIT_MS;
	_startHostTransfer(&host, true);
	assert_true(GBALinkSessionUpdate(
	    &client.session, false));
	_completeHostTransfer(&host);

	const uint16_t success[] = {
		0x1234, 0x5678, 0xFFFF, 0xFFFF,
	};
	assert_memory_equal(
	    &host.gba->memory.io[GBA_REG(SIOMULTI0)],
	    success, sizeof(success));
	assert_false(GBASIOMultiplayerIsError(
	    host.gba->sio.siocnt));
	_assertErrorCompletion(&client, true);
	assert_int_equal(
	    host.driver.transfer.state,
	    GBA_SIO_NETPLAY_TRANSFER_FINISHED);
	assert_int_equal(
	    client.driver.transfer.state,
	    GBA_SIO_NETPLAY_TRANSFER_FINISHED);
	_deinitFixture(&client);
	_deinitFixture(&host);
}

M_TEST_DEFINE(finalDecisionAckLossPreservesCommittedOutcomeAndClosesSession) {
	struct DriverFixture host;
	struct DriverFixture client;
	_initAttachedPair(&host, &client);
	_configureTransfer(&host, &client, true);
	client.dropMessageType =
	    GBA_LINK_MESSAGE_COMPLETION_DECISION_ACK;
	host.advancePollMs = GBA_LINK_MAX_WAIT_MS;
	_startHostTransfer(&host, true);
	assert_true(GBALinkSessionUpdate(
	    &client.session, false));
	_completeHostTransfer(&host);

	const uint16_t success[] = {
		0x1234, 0x5678, 0xFFFF, 0xFFFF,
	};
	assert_memory_equal(
	    &host.gba->memory.io[GBA_REG(SIOMULTI0)],
	    success, sizeof(success));
	assert_memory_equal(
	    &client.gba->memory.io[GBA_REG(SIOMULTI0)],
	    success, sizeof(success));
	assert_false(GBASIOMultiplayerIsError(
	    host.gba->sio.siocnt));
	assert_false(GBASIOMultiplayerIsError(
	    client.gba->sio.siocnt));
	assert_int_equal(
	    host.driver.transfer.state,
	    GBA_SIO_NETPLAY_TRANSFER_FINISHED);
	assert_int_equal(
	    client.driver.transfer.state,
	    GBA_SIO_NETPLAY_TRANSFER_FINISHED);
	assert_int_equal(
	    host.session.state,
	    GBA_LINK_SESSION_FAILED);
	assert_false(GBALinkSessionIsLive(
	    &host.session));
	assert_int_equal(
	    client.messageSends[
	        GBA_LINK_MESSAGE_COMPLETION_DECISION_ACK],
	    1);
	_deinitFixture(&client);
	_deinitFixture(&host);
}

M_TEST_DEFINE(explicitAbortIsReliableIdempotentAndCycleStable) {
	struct DriverFixture host;
	struct DriverFixture client;
	_initAttachedPair(&host, &client);
	_configureTransfer(&host, &client, true);
	_startHostTransfer(&host, true);
	assert_true(GBALinkSessionUpdate(
	    &client.session, false));
	uint64_t completionCycle =
	    host.driver.transfer.completionCycle;
	GBASIONetplayAbortTransfer(
	    &host.driver,
	    GBA_LINK_REASON_USER_DISCONNECT);
	assert_int_equal(
	    host.messageSends[
	        GBA_LINK_MESSAGE_TRANSFER_ABORT],
	    1);
	GBASIONetplayAbortTransfer(
	    &host.driver,
	    GBA_LINK_REASON_USER_DISCONNECT);
	assert_int_equal(
	    host.messageSends[
	        GBA_LINK_MESSAGE_TRANSFER_ABORT],
	    1);
	assert_true(GBALinkSessionUpdate(
	    &client.session, false));
	assert_true(client.driver.transfer.abortPending);
	assert_int_equal(
	    client.driver.transfer.completionCycle,
	    completionCycle);
	_completeHostTransfer(&host);
	_assertErrorCompletion(&host, true);
	_assertErrorCompletion(&client, true);
	_deinitFixture(&client);
	_deinitFixture(&host);
}

M_TEST_DEFINE(resetCancellationRemovesEventWithoutIrq) {
	struct DriverFixture host;
	struct DriverFixture client;
	_initAttachedPair(&host, &client);
	host.gba->memory.io[GBA_REG(SIOMLT_SEND)] = 0x1234;
	client.gba->memory.io[GBA_REG(SIOMLT_SEND)] = 0x5678;
	host.gba->memory.io[GBA_REG(IF)] = 0;
	GBASIOWriteSIOCNT(&host.gba->sio, 0x6000);
	GBASIOWriteSIOCNT(&client.gba->sio, 0x6000);
	GBASIOWriteSIOCNT(&host.gba->sio, 0x6080);
	assert_true(mTimingIsScheduled(
	    &host.gba->timing,
	    &host.gba->sio.completeEvent));
	GBASIONetplayDriverCancel(
	    &host.driver, GBA_LINK_REASON_RESET);
	assert_false(mTimingIsScheduled(
	    &host.gba->timing,
	    &host.gba->sio.completeEvent));
	assert_int_equal(
	    host.gba->memory.io[GBA_REG(IF)] &
	        (1 << GBA_IRQ_SIO),
	    0);
	assert_null(host.gba->sio.driver);
	_deinitFixture(&client);
	_deinitFixture(&host);
}

M_TEST_DEFINE(unloadCancellationRemovesBothPendingEventsWithoutIrq) {
	struct DriverFixture host;
	struct DriverFixture client;
	_initAttachedPair(&host, &client);
	_configureTransfer(&host, &client, true);
	_startHostTransfer(&host, true);
	assert_true(GBALinkSessionUpdate(
	    &client.session, false));
	assert_true(mTimingIsScheduled(
	    &host.gba->timing,
	    &host.gba->sio.completeEvent));
	assert_true(mTimingIsScheduled(
	    &client.gba->timing,
	    &client.gba->sio.completeEvent));
	GBASIONetplayDriverCancel(
	    &host.driver, GBA_LINK_REASON_UNLOAD);
	GBASIONetplayDriverCancel(
	    &client.driver, GBA_LINK_REASON_UNLOAD);
	assert_false(mTimingIsScheduled(
	    &host.gba->timing,
	    &host.gba->sio.completeEvent));
	assert_false(mTimingIsScheduled(
	    &client.gba->timing,
	    &client.gba->sio.completeEvent));
	assert_int_equal(
	    host.gba->memory.io[GBA_REG(IF)] &
	        (1 << GBA_IRQ_SIO),
	    0);
	assert_int_equal(
	    client.gba->memory.io[GBA_REG(IF)] &
	        (1 << GBA_IRQ_SIO),
	    0);
	assert_null(host.gba->sio.driver);
	assert_null(client.gba->sio.driver);
	_deinitFixture(&client);
	_deinitFixture(&host);
}

M_TEST_DEFINE(clientIntentBeforeStartCommitsBeforeErrorTransfer) {
	struct DriverFixture host;
	struct DriverFixture client;
	_initAttachedPair(&host, &client);
	host.gba->memory.io[GBA_REG(SIOMLT_SEND)] = 0x1234;
	client.gba->memory.io[GBA_REG(SIOMLT_SEND)] = 0x5678;
	host.gba->memory.io[GBA_REG(IF)] = 0;
	client.gba->memory.io[GBA_REG(IF)] = 0;
	GBASIOWriteSIOCNT(&host.gba->sio, 0x6000);
	GBASIOWriteSIOCNT(&client.gba->sio, 0x6000);
	host.gba->timing.masterCycles += 100;
	client.injectModeBeforeTransferStart = true;

	GBASIOWriteSIOCNT(&host.gba->sio, 0x6080);
	assert_true(host.driver.transfer.startEmitted);
	assert_false(host.driver.transfer.commitAccepted);
	assert_true(host.driver.transfer.abortPending);
	assert_int_equal(
	    host.driver.timeline.committedModeGeneration, 2);
	assert_int_equal(
	    client.driver.timeline.committedModeGeneration, 2);
	assert_int_equal(
	    client.driver.timeline.localMode,
	    GBA_LINK_MODE_NORMAL_32);
	assert_false(host.driver.jointlyReady);
	assert_false(client.driver.jointlyReady);
	assert_true(mTimingIsScheduled(
	    &host.gba->timing,
	    &host.gba->sio.completeEvent));

	assert_true(GBALinkSessionUpdate(
	    &client.session, false));
	assert_true(client.driver.transfer.abortPending);
	assert_true(mTimingIsScheduled(
	    &client.gba->timing,
	    &client.gba->sio.completeEvent));
	int32_t hostDuration = mTimingUntil(
	    &host.gba->timing,
	    &host.gba->sio.completeEvent);
	host.gba->timing.masterCycles +=
	    (uint32_t) hostDuration;
	mTimingDeschedule(
	    &host.gba->timing,
	    &host.gba->sio.completeEvent);
	host.gba->sio.completeEvent.callback(
	    &host.gba->timing,
	    host.gba->sio.completeEvent.context, 0);
	_driveExecutionBoundary(&client);

	assert_true(GBASIOMultiplayerIsError(
	    host.gba->sio.siocnt));
	assert_true(GBASIOMultiplayerIsError(
	    client.gba->sio.siocnt));
	assert_false(GBASIOMultiplayerIsBusy(
	    host.gba->sio.siocnt));
	assert_false(GBASIOMultiplayerIsBusy(
	    client.gba->sio.siocnt));
	_deinitFixture(&client);
	_deinitFixture(&host);
}

M_TEST_DEFINE(hostModeWriteAfterStartDefersUntilErrorCompletion) {
	struct DriverFixture host;
	struct DriverFixture client;
	_initAttachedPair(&host, &client);
	host.gba->memory.io[GBA_REG(SIOMLT_SEND)] = 0x1234;
	client.gba->memory.io[GBA_REG(SIOMLT_SEND)] = 0x5678;
	GBASIOWriteSIOCNT(&host.gba->sio, 0x6000);
	GBASIOWriteSIOCNT(&client.gba->sio, 0x6000);
	GBASIOWriteSIOCNT(&host.gba->sio, 0x6080);
	assert_true(GBALinkSessionUpdate(
	    &client.session, false));
	assert_true(client.driver.paused);

	host.gba->timing.masterCycles += 10;
	GBASIOWriteSIOCNT(&host.gba->sio, 0x1000);
	assert_true(host.driver.transfer.localDeferredMode);
	assert_true(host.driver.transfer.abortPending);
	assert_false(host.driver.paused);
	assert_true(GBALinkSessionUpdate(
	    &client.session, false));
	assert_true(GBALinkSessionUpdate(
	    &client.session, false));
	assert_true(client.driver.transfer.remoteDeferredMode);
	assert_true(client.driver.transfer.abortPending);
	assert_true(client.driver.paused);
	assert_false(client.driver.executionLimitEnabled);

	int32_t hostDuration = mTimingUntil(
	    &host.gba->timing,
	    &host.gba->sio.completeEvent);
	host.gba->timing.masterCycles +=
	    (uint32_t) hostDuration;
	mTimingDeschedule(
	    &host.gba->timing,
	    &host.gba->sio.completeEvent);
	host.gba->sio.completeEvent.callback(
	    &host.gba->timing,
	    host.gba->sio.completeEvent.context, 0);
	assert_true(GBASIOMultiplayerIsError(
	    host.gba->sio.siocnt));
	assert_true(GBASIOMultiplayerIsError(
	    client.gba->sio.siocnt));
	assert_true(host.driver.timeline.modeBarrier);
	assert_true(host.driver.paused);

	assert_true(GBALinkSessionUpdate(
	    &client.session, false));
	assert_true(GBALinkSessionUpdate(
	    &client.session, false));
	assert_true(GBALinkSessionUpdate(
	    &host.session, false));
	assert_int_equal(
	    host.driver.timeline.committedModeGeneration, 2);
	assert_int_equal(
	    client.driver.timeline.committedModeGeneration, 2);
	assert_int_equal(
	    host.driver.timeline.localMode,
	    GBA_LINK_MODE_NORMAL_32);
	assert_int_equal(
	    client.driver.timeline.remoteMode,
	    GBA_LINK_MODE_NORMAL_32);
	assert_false(host.driver.jointlyReady);
	assert_false(client.driver.jointlyReady);
	assert_false(host.driver.paused);
	assert_true(client.driver.paused);
	_deinitFixture(&client);
	_deinitFixture(&host);
}

M_TEST_DEFINE(clientModeWriteDuringCatchupDoesNotStrandCompletion) {
	struct DriverFixture host;
	struct DriverFixture client;
	_initAttachedPair(&host, &client);
	host.gba->memory.io[GBA_REG(SIOMLT_SEND)] = 0x1234;
	client.gba->memory.io[GBA_REG(SIOMLT_SEND)] = 0x5678;
	GBASIOWriteSIOCNT(&host.gba->sio, 0x6000);
	GBASIOWriteSIOCNT(&client.gba->sio, 0x6000);
	GBASIOWriteSIOCNT(&host.gba->sio, 0x6080);
	assert_true(GBALinkSessionUpdate(
	    &client.session, false));
	client.injectModeDuringCompletionCatchup = true;

	int32_t hostDuration = mTimingUntil(
	    &host.gba->timing,
	    &host.gba->sio.completeEvent);
	host.gba->timing.masterCycles +=
	    (uint32_t) hostDuration;
	mTimingDeschedule(
	    &host.gba->timing,
	    &host.gba->sio.completeEvent);
	host.gba->sio.completeEvent.callback(
	    &host.gba->timing,
	    host.gba->sio.completeEvent.context, 0);

	assert_true(client.driver.transfer.localDeferredMode);
	assert_true(host.driver.transfer.remoteDeferredMode);
	assert_int_equal(
	    host.driver.transfer.state,
	    GBA_SIO_NETPLAY_TRANSFER_FINISHED);
	assert_int_equal(
	    client.driver.transfer.state,
	    GBA_SIO_NETPLAY_TRANSFER_FINISHED);
	assert_true(GBASIOMultiplayerIsError(
	    host.gba->sio.siocnt));
	assert_true(GBASIOMultiplayerIsError(
	    client.gba->sio.siocnt));

	for (unsigned i = 0; i < 3; ++i) {
		GBALinkSessionUpdate(&client.session, false);
		GBALinkSessionUpdate(&host.session, false);
	}
	assert_int_equal(
	    host.driver.timeline.committedModeGeneration, 2);
	assert_int_equal(
	    client.driver.timeline.committedModeGeneration, 2);
	assert_int_equal(
	    host.driver.timeline.remoteMode,
	    GBA_LINK_MODE_NORMAL_32);
	assert_int_equal(
	    client.driver.timeline.localMode,
	    GBA_LINK_MODE_NORMAL_32);
	assert_false(host.driver.jointlyReady);
	assert_false(client.driver.jointlyReady);
	_deinitFixture(&client);
	_deinitFixture(&host);
}

M_TEST_DEFINE(exactDuplicateCommitIsIdempotent) {
	struct DriverFixture host;
	struct DriverFixture client;
	_initAttachedPair(&host, &client);
	host.duplicateMessageType =
	    GBA_LINK_MESSAGE_TRANSFER_COMMIT;
	host.gba->memory.io[GBA_REG(SIOMLT_SEND)] = 0x1234;
	client.gba->memory.io[GBA_REG(SIOMLT_SEND)] = 0x5678;
	GBASIOWriteSIOCNT(&host.gba->sio, 0x2000);
	GBASIOWriteSIOCNT(&client.gba->sio, 0x2000);
	GBASIOWriteSIOCNT(&host.gba->sio, 0x2080);
	assert_true(GBALinkSessionUpdate(
	    &client.session, false));
	assert_int_equal(
	    client.session.state,
	    GBA_LINK_SESSION_TRANSFERRING);
	assert_true(client.driver.transfer.commitAccepted);
	assert_int_equal(
	    client.driver.transfer.state,
	    GBA_SIO_NETPLAY_TRANSFER_COMMITTED);
	GBASIONetplayDriverCancel(
	    &host.driver, GBA_LINK_REASON_UNLOAD);
	GBASIONetplayDriverCancel(
	    &client.driver, GBA_LINK_REASON_UNLOAD);
	_deinitFixture(&client);
	_deinitFixture(&host);
}

M_TEST_DEFINE(conflictingDuplicateCommitFailsClosed) {
	struct DriverFixture host;
	struct DriverFixture client;
	_initAttachedPair(&host, &client);
	host.conflictMessageType =
	    GBA_LINK_MESSAGE_TRANSFER_COMMIT;
	host.gba->memory.io[GBA_REG(SIOMLT_SEND)] = 0x1234;
	client.gba->memory.io[GBA_REG(SIOMLT_SEND)] = 0x5678;
	GBASIOWriteSIOCNT(&host.gba->sio, 0x2000);
	GBASIOWriteSIOCNT(&client.gba->sio, 0x2000);
	GBASIOWriteSIOCNT(&host.gba->sio, 0x2080);
	assert_false(GBALinkSessionUpdate(
	    &client.session, false));
	assert_int_equal(
	    client.session.state, GBA_LINK_SESSION_FAILED);
	assert_true(client.driver.transfer.abortPending);
	assert_true(client.driver.transfer.commitAccepted);
	for (unsigned i = 0; i < 4; ++i) {
		assert_int_equal(
		    client.driver.transfer.pendingWords[i],
		    0xFFFF);
	}
	GBASIONetplayDriverCancel(
	    &host.driver, GBA_LINK_REASON_UNLOAD);
	GBASIONetplayDriverCancel(
	    &client.driver, GBA_LINK_REASON_UNLOAD);
	_deinitFixture(&client);
	_deinitFixture(&host);
}

M_TEST_DEFINE(futureTransferStartCannotMutateSio) {
	struct DriverFixture host;
	struct DriverFixture client;
	_initAttachedPair(&host, &client);
	struct GBALinkPacket start;
	memset(&start, 0, sizeof(start));
	start.header.type = GBA_LINK_MESSAGE_TRANSFER_START;
	start.header.sessionId = client.session.sessionId;
	start.header.packetSequence =
	    client.session.nextRemotePacketSequence;
	start.payload.transferStart.transferSequence = 2;
	start.payload.transferStart.startCycle = 0;
	start.payload.transferStart.completionCycle =
	    GBASIOTransferCycles(GBA_SIO_MULTI, 0x2000, 1);
	start.payload.transferStart.outgoingWord = 0x1234;
	start.payload.transferStart.siocnt = 0x2000;
	uint8_t encoded[GBA_LINK_MAX_PACKET_SIZE];
	size_t encodedSize = 0;
	assert_true(GBALinkPacketEncode(
	    &start, encoded, sizeof(encoded), &encodedSize));
	assert_true(GBALinkTransportQueueInbound(
	    &client.transport, client.transport.generation,
	    encoded, encodedSize));
	assert_false(GBALinkSessionUpdate(
	    &client.session, false));
	assert_int_equal(
	    client.driver.transfer.state,
	    GBA_SIO_NETPLAY_TRANSFER_IDLE);
	assert_false(GBASIOMultiplayerIsBusy(
	    client.gba->sio.siocnt));
	assert_false(mTimingIsScheduled(
	    &client.gba->timing,
	    &client.gba->sio.completeEvent));
	_deinitFixture(&client);
	_deinitFixture(&host);
}

M_TEST_SUITE_DEFINE(GBASIONetplayDriver,
	cmocka_unit_test(
	    quiescentAttachRejectsPendingSioCompletion),
	cmocka_unit_test(
	    hostAttachSeparatesTopologyAndEffectiveCount),
	cmocka_unit_test(
	    clientAttachmentRemainsHiddenUntilHostRelease),
	cmocka_unit_test(
	    localSchedulerQuantumDoesNotEmitPacket),
	cmocka_unit_test(
	    grantBoundaryAcceptsBoundedTimingAccountingSlop),
	cmocka_unit_test(
	    clientStartPauseInterruptsBeforeUncommittedCompletion),
	cmocka_unit_test(
	    idleDetachCleansLinesWithoutTouchingDataOrError),
	cmocka_unit_test(
	    twoDriverTransferUsesCommonCompletionAtEveryBaud),
	cmocka_unit_test(
	    successfulTransferHonorsDisabledIrq),
	cmocka_unit_test(
	    deliveryLatencyDoesNotChangeTransferOutcome),
	cmocka_unit_test(
	    notReadyHostStartUsesOrdinaryNoPeerPath),
	cmocka_unit_test(
	    clientIndependentStartWaitsForPrimary),
	cmocka_unit_test(
	    clientIndependentStartThenDisconnectRestoresIdleLines),
	cmocka_unit_test(
	    remoteStartConsumesClientIndependentStart),
	cmocka_unit_test(
	    stopAfterCommitErrorCompletesAtAnnouncedCycle),
	cmocka_unit_test(
	    startAndReadinessLossPreservePointOfNoReturn),
	cmocka_unit_test(
	    preStartSendFailureUsesOrdinaryNoPeerTiming),
	cmocka_unit_test(
	    commitSendFailureUsesAnnouncedCompletionCycle),
	cmocka_unit_test(
	    completionMessageLossNeverLeavesBusySet),
	cmocka_unit_test(
	    finalDecisionDeliveryFailureHasScopedAsymmetry),
	cmocka_unit_test(
	    finalDecisionAckLossPreservesCommittedOutcomeAndClosesSession),
	cmocka_unit_test(
	    explicitAbortIsReliableIdempotentAndCycleStable),
	cmocka_unit_test(
	    resetCancellationRemovesEventWithoutIrq),
	cmocka_unit_test(
	    unloadCancellationRemovesBothPendingEventsWithoutIrq),
	cmocka_unit_test(
	    clientIntentBeforeStartCommitsBeforeErrorTransfer),
	cmocka_unit_test(
	    hostModeWriteAfterStartDefersUntilErrorCompletion),
	cmocka_unit_test(
	    clientModeWriteDuringCatchupDoesNotStrandCompletion),
	cmocka_unit_test(
	    exactDuplicateCommitIsIdempotent),
	cmocka_unit_test(
	    conflictingDuplicateCommitFailsClosed),
	cmocka_unit_test(
	    futureTransferStartCannotMutateSio))
