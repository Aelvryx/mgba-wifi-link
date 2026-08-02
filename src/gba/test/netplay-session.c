/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "util/test/suite.h"

#include <mgba/internal/gba/sio/netplay/identity-v1.h>
#include <mgba/internal/gba/sio/netplay/session.h>
#include <mgba/internal/gba/sio/netplay/timeline.h>
#include <mgba/internal/gba/sio/netplay/transport.h>

struct DelayedPacket {
	uint8_t data[GBA_LINK_MAX_PACKET_SIZE];
	size_t size;
	unsigned pumpsRemaining;
};

struct SessionEndpoint {
	struct GBALinkTransport transport;
	struct GBALinkSession session;
	struct GBALinkTimeline timeline;
	struct SessionEndpoint* peer;
	uint64_t now;
	bool quiescent;
	enum GBALinkWireMode mode;
	uint64_t cycle;
	bool paused;
	bool installed;
	bool observable;
	bool sendResult;
	bool stopDuringPoll;
	bool duplicateNextSend;
	bool conflictNextSend;
	bool yieldOnRuntime;
	bool timelineInitialized;
	bool executionLimitEnabled;
	uint64_t executionLimit;
	uint64_t rebasedLocalCycle;
	uint64_t rebasedCableCycle;
	uint64_t committedModeGeneration;
	enum GBALinkWireMode committedLocalMode;
	enum GBALinkWireMode committedRemoteMode;
	bool jointlyReady;
	enum GBALinkMessageType dropNextType;
	unsigned deliveryDelayPumps;
	struct DelayedPacket delayed[GBA_LINK_MAX_COPIED_PACKETS];
	size_t delayedSize;
	unsigned stopCalls;
	unsigned runtimePackets;
	unsigned attachmentCalls;
	unsigned failureCalls;
	enum GBALinkReason failureReason;
};

struct SessionPair {
	struct SessionEndpoint host;
	struct SessionEndpoint client;
};

static bool _sendReliable(
    void* context, const void* data, size_t size, bool flush) {
	struct SessionEndpoint* endpoint = context;
	assert_true(flush);
	if (!endpoint->sendResult) {
		return false;
	}
	struct GBALinkPacket decoded;
	bool packetDecoded =
	    GBALinkPacketDecode(
	        data, size,
	        endpoint->session.localRole,
	        &decoded) == GBA_LINK_DECODE_OK;
	if (packetDecoded &&
	    decoded.header.type == endpoint->dropNextType) {
		endpoint->dropNextType = 0;
		return true;
	}
	if (endpoint->deliveryDelayPumps) {
		if (endpoint->delayedSize >=
		    GBA_LINK_MAX_COPIED_PACKETS) {
			return false;
		}
		struct DelayedPacket* delayed =
		    &endpoint->delayed[endpoint->delayedSize++];
		memcpy(delayed->data, data, size);
		delayed->size = size;
		delayed->pumpsRemaining =
		    endpoint->deliveryDelayPumps;
		return true;
	}
	if (!GBALinkTransportQueueInbound(
	        &endpoint->peer->transport,
	        endpoint->peer->transport.generation, data, size)) {
		return false;
	}
	if (endpoint->duplicateNextSend || endpoint->conflictNextSend) {
		uint8_t duplicate[GBA_LINK_MAX_PACKET_SIZE];
		memcpy(duplicate, data, size);
		if (endpoint->conflictNextSend) {
			duplicate[size - 1] ^= 1;
		}
		endpoint->duplicateNextSend = false;
		endpoint->conflictNextSend = false;
		if (!GBALinkTransportQueueInbound(
		        &endpoint->peer->transport,
		        endpoint->peer->transport.generation,
		        duplicate, size)) {
			return false;
		}
	}
	return true;
}

static bool _pollReceive(void* context) {
	struct SessionEndpoint* endpoint = context;
	if (endpoint->stopDuringPoll) {
		GBALinkTransportInvalidate(
		    &endpoint->transport, GBA_LINK_REASON_TRANSPORT_STOP,
		    "test stop during poll");
	}
	return true;
}

static uint64_t _monotonicTimeMs(void* context) {
	struct SessionEndpoint* endpoint = context;
	return endpoint->now;
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
	struct SessionEndpoint* endpoint = context;
	++endpoint->stopCalls;
}

static const struct GBALinkTransportVTable _transportVTable = {
	.sendReliable = _sendReliable,
	.pollReceive = _pollReceive,
	.monotonicTimeMs = _monotonicTimeMs,
	.diagnostic = _diagnostic,
	.stop = _stop,
};

static bool _quiescentSnapshot(
    void* context, enum GBALinkWireMode* mode, uint64_t* localCycle) {
	struct SessionEndpoint* endpoint = context;
	if (!endpoint->quiescent) {
		return false;
	}
	*mode = endpoint->mode;
	*localCycle = endpoint->cycle;
	return true;
}

static void _setPaused(void* context, bool paused) {
	struct SessionEndpoint* endpoint = context;
	endpoint->paused = paused;
}

static void _setAttachment(
    void* context, bool installed, bool observable,
    uint64_t attachCycle) {
	struct SessionEndpoint* endpoint = context;
	endpoint->installed = installed;
	endpoint->observable = observable;
	endpoint->cycle = attachCycle;
	++endpoint->attachmentCalls;
}

static bool _runtimePacket(
    void* context, const struct GBALinkPacket* packet) {
	struct SessionEndpoint* endpoint = context;
	assert_non_null(packet);
	++endpoint->runtimePackets;
	if (endpoint->yieldOnRuntime) {
		endpoint->yieldOnRuntime = false;
		GBALinkSessionYieldInbound(&endpoint->session);
	}
	if (endpoint->timelineInitialized) {
		return GBALinkTimelineHandlePacket(
		    &endpoint->timeline, packet);
	}
	return true;
}

static void _failed(void* context, enum GBALinkReason reason) {
	struct SessionEndpoint* endpoint = context;
	++endpoint->failureCalls;
	endpoint->failureReason = reason;
}

static const struct GBALinkSessionCallbacks _sessionCallbacks = {
	.quiescentSnapshot = _quiescentSnapshot,
	.setPaused = _setPaused,
	.setAttachment = _setAttachment,
	.runtimePacket = _runtimePacket,
	.failed = _failed,
};

static uint64_t _localCycle(void* context) {
	struct SessionEndpoint* endpoint = context;
	return endpoint->cycle;
}

static void _setExecutionLimit(
    void* context, bool enabled, uint64_t localCycle) {
	struct SessionEndpoint* endpoint = context;
	endpoint->executionLimitEnabled = enabled;
	endpoint->executionLimit = localCycle;
}

static void _rebaseCableClock(
    void* context, uint64_t localCycle, uint64_t cableCycle) {
	struct SessionEndpoint* endpoint = context;
	endpoint->rebasedLocalCycle = localCycle;
	endpoint->rebasedCableCycle = cableCycle;
}

static void _modeCommitted(
    void* context, uint64_t generation,
    enum GBALinkWireMode localMode,
    enum GBALinkWireMode remoteMode, bool jointlyReady) {
	struct SessionEndpoint* endpoint = context;
	endpoint->committedModeGeneration = generation;
	endpoint->committedLocalMode = localMode;
	endpoint->committedRemoteMode = remoteMode;
	endpoint->jointlyReady = jointlyReady;
}

static const struct GBALinkTimelineCallbacks _timelineCallbacks = {
	.localCycle = _localCycle,
	.setPaused = _setPaused,
	.setExecutionLimit = _setExecutionLimit,
	.rebaseCableClock = _rebaseCableClock,
	.modeCommitted = _modeCommitted,
};

static struct GBALinkSessionConfig _config(
    struct SessionEndpoint* endpoint, uint8_t romSeed) {
	struct GBALinkSessionConfig config;
	memset(&config, 0, sizeof(config));
	config.identity.romSize = 0x200;
	for (unsigned i = 0; i < GBA_LINK_ROM_SHA1_SIZE; ++i) {
		config.identity.romSha1[i] = romSeed + i;
	}
	struct GBALinkDeterminismProfileInput profile = {
		.overclockQ16 = 0x10000,
	};
	assert_true(GBALinkDeterminismProfileBuild(
	    &profile, config.digests));
	config.capabilities = GBA_LINK_MVP_CAPABILITIES;
	config.supportedPolicies =
	    1U << GBA_LINK_COMPATIBILITY_EXACT_ROM;
	config.emulationCompatibilityVersion =
	    GBA_LINK_EMULATION_COMPATIBILITY_VERSION;
	GBALinkDeadlinePolicyInit(&config.deadlines);
	config.callbacks = &_sessionCallbacks;
	config.callbackContext = endpoint;
	return config;
}

static void _initEndpoint(
    struct SessionEndpoint* endpoint, enum GBALinkRole role,
    uint64_t cycle) {
	endpoint->quiescent = true;
	endpoint->mode = GBA_LINK_MODE_MULTI;
	endpoint->cycle = cycle;
	endpoint->sendResult = true;
	GBALinkTransportInit(
	    &endpoint->transport, &_transportVTable, endpoint);
	assert_true(GBALinkTransportStart(
	    &endpoint->transport, 1, role));
	GBALinkSessionInit(
	    &endpoint->session, &endpoint->transport);
}

static void _initPair(struct SessionPair* pair) {
	memset(pair, 0, sizeof(*pair));
	pair->host.peer = &pair->client;
	pair->client.peer = &pair->host;
	_initEndpoint(&pair->host, GBA_LINK_ROLE_HOST, 100);
	_initEndpoint(&pair->client, GBA_LINK_ROLE_CLIENT, 200);
	struct GBALinkSessionConfig hostConfig =
	    _config(&pair->host, 1);
	struct GBALinkSessionConfig clientConfig =
	    _config(&pair->client, 1);
	assert_true(GBALinkSessionConfigure(
	    &pair->host.session, &hostConfig));
	assert_true(GBALinkSessionConfigure(
	    &pair->client.session, &clientConfig));
}

static void _startPair(struct SessionPair* pair) {
	assert_true(GBALinkSessionStart(
	    &pair->host.session, 1, GBA_LINK_ROLE_HOST));
	assert_true(GBALinkSessionStart(
	    &pair->client.session, 1, GBA_LINK_ROLE_CLIENT));
}

static void _deliverDelayed(
    struct SessionEndpoint* endpoint) {
	size_t write = 0;
	for (size_t i = 0; i < endpoint->delayedSize; ++i) {
		struct DelayedPacket delayed =
		    endpoint->delayed[i];
		if (delayed.pumpsRemaining) {
			--delayed.pumpsRemaining;
		}
		if (!delayed.pumpsRemaining) {
			assert_true(GBALinkTransportQueueInbound(
			    &endpoint->peer->transport,
			    endpoint->peer->transport.generation,
			    delayed.data, delayed.size));
		} else {
			endpoint->delayed[write++] = delayed;
		}
	}
	endpoint->delayedSize = write;
}

static void _pump(struct SessionPair* pair, unsigned iterations) {
	for (unsigned i = 0; i < iterations; ++i) {
		_deliverDelayed(&pair->host);
		_deliverDelayed(&pair->client);
		if (GBALinkSessionIsLive(&pair->host.session)) {
			GBALinkSessionUpdate(&pair->host.session, false);
		}
		if (GBALinkSessionIsLive(&pair->client.session)) {
			GBALinkSessionUpdate(&pair->client.session, false);
		}
	}
}

static void _deinitPair(struct SessionPair* pair) {
	GBALinkSessionDeinit(&pair->client.session);
	GBALinkSessionDeinit(&pair->host.session);
	GBALinkTransportDeinit(&pair->client.transport);
	GBALinkTransportDeinit(&pair->host.transport);
}

static void _assertAtFinalAckBarrier(const struct SessionPair* pair) {
	assert_int_equal(
	    pair->host.session.state, GBA_LINK_SESSION_READY);
	assert_int_equal(
	    pair->client.session.state,
	    GBA_LINK_SESSION_ATTACH_BARRIER);
	assert_true(pair->host.installed);
	assert_true(pair->host.observable);
	assert_true(pair->client.installed);
	assert_false(pair->client.observable);
	assert_true(pair->host.paused);
	assert_true(pair->client.paused);
	assert_int_equal(
	    pair->host.session.sessionId,
	    pair->client.session.sessionId);
	assert_true(pair->host.session.sessionId != 0);
	assert_int_equal(pair->host.session.attachCycle, 100);
	assert_int_equal(pair->client.session.attachCycle, 100);
}

static void _initTimelines(struct SessionPair* pair) {
	assert_true(GBALinkTimelineInit(
	    &pair->host.timeline, &pair->host.session,
	    &_timelineCallbacks, &pair->host, pair->host.cycle,
	    pair->host.session.attachCycle,
	    pair->host.session.localHello.initialMode,
	    pair->host.session.remoteHello.initialMode));
	pair->host.timelineInitialized = true;
	assert_true(GBALinkTimelineInit(
	    &pair->client.timeline, &pair->client.session,
	    &_timelineCallbacks, &pair->client, pair->client.cycle,
	    pair->client.session.attachCycle,
	    pair->client.session.localHello.initialMode,
	    pair->client.session.remoteHello.initialMode));
	pair->client.timelineInitialized = true;
}

static void _commitInitialModes(struct SessionPair* pair) {
	_initTimelines(pair);
	assert_true(GBALinkTimelineHostCommitInitialModes(
	    &pair->host.timeline));
	_pump(pair, 32);
	assert_int_equal(
	    pair->host.timeline.committedModeGeneration, 1);
	assert_int_equal(
	    pair->client.timeline.committedModeGeneration, 1);
	assert_true(pair->host.jointlyReady);
	assert_true(pair->client.jointlyReady);
	assert_int_equal(
	    pair->client.session.state, GBA_LINK_SESSION_READY);
}

M_TEST_DEFINE(atomicHandshakeStopsAtFinalAckBarrier) {
	struct SessionPair pair;
	_initPair(&pair);
	_startPair(&pair);
	_pump(&pair, 8);
	_assertAtFinalAckBarrier(&pair);
	_deinitPair(&pair);
}

M_TEST_DEFINE(clientBecomesObservableOnlyOnPostAttachmentHostEvent) {
	struct SessionPair pair;
	_initPair(&pair);
	_startPair(&pair);
	_pump(&pair, 8);
	_assertAtFinalAckBarrier(&pair);

	struct GBALinkPacket packet;
	memset(&packet, 0, sizeof(packet));
	packet.header.type = GBA_LINK_MESSAGE_MODE_COMMIT;
	packet.header.sessionId = pair.host.session.sessionId;
	packet.header.packetSequence = 4;
	packet.payload.modeCommit.modeGeneration =
	    pair.host.session.initialModeGeneration;
	packet.payload.modeCommit.commitCycle =
	    pair.host.session.attachCycle;
	packet.payload.modeCommit.hostMode = GBA_LINK_MODE_MULTI;
	packet.payload.modeCommit.clientMode = GBA_LINK_MODE_MULTI;
	packet.payload.modeCommit.jointlyReady = true;
	uint8_t bytes[GBA_LINK_MAX_PACKET_SIZE];
	size_t size = 0;
	assert_true(GBALinkPacketEncode(
	    &packet, bytes, sizeof(bytes), &size));
	assert_true(GBALinkTransportQueueInbound(
	    &pair.client.transport, 1, bytes, size));
	assert_true(GBALinkSessionUpdate(
	    &pair.client.session, false));
	assert_int_equal(
	    pair.client.session.state, GBA_LINK_SESSION_READY);
	assert_true(pair.client.observable);
	assert_true(pair.client.paused);
	assert_int_equal(pair.client.runtimePackets, 1);
	_deinitPair(&pair);
}

M_TEST_DEFINE(runtimeYieldDefersCoalescedFollowingPacket) {
	struct SessionPair pair;
	_initPair(&pair);
	_startPair(&pair);
	_pump(&pair, 8);
	_assertAtFinalAckBarrier(&pair);

	struct GBALinkPacket packets[2];
	memset(packets, 0, sizeof(packets));
	packets[0].header.type = GBA_LINK_MESSAGE_MODE_COMMIT;
	packets[0].header.sessionId = pair.host.session.sessionId;
	packets[0].header.packetSequence = 4;
	packets[0].payload.modeCommit.modeGeneration =
	    pair.host.session.initialModeGeneration;
	packets[0].payload.modeCommit.commitCycle =
	    pair.host.session.attachCycle;
	packets[0].payload.modeCommit.hostMode = GBA_LINK_MODE_MULTI;
	packets[0].payload.modeCommit.clientMode = GBA_LINK_MODE_MULTI;
	packets[0].payload.modeCommit.jointlyReady = true;
	packets[1].header.type = GBA_LINK_MESSAGE_MODE_ACK;
	packets[1].header.sessionId = pair.host.session.sessionId;
	packets[1].header.packetSequence = 5;
	packets[1].payload.modeAck.modeGeneration =
	    pair.host.session.initialModeGeneration;
	packets[1].payload.modeAck.commitCycle =
	    pair.host.session.attachCycle;

	for (size_t i = 0; i < 2; ++i) {
		uint8_t bytes[GBA_LINK_MAX_PACKET_SIZE];
		size_t size = 0;
		assert_true(GBALinkPacketEncode(
		    &packets[i], bytes, sizeof(bytes), &size));
		assert_true(GBALinkTransportQueueInbound(
		    &pair.client.transport, 1, bytes, size));
	}
	pair.client.yieldOnRuntime = true;
	assert_true(GBALinkSessionUpdate(
	    &pair.client.session, false));
	assert_int_equal(pair.client.runtimePackets, 1);
	assert_int_equal(pair.client.transport.inbound.size, 1);
	assert_true(GBALinkSessionUpdate(
	    &pair.client.session, false));
	assert_int_equal(pair.client.runtimePackets, 2);
	assert_int_equal(pair.client.transport.inbound.size, 0);
	_deinitPair(&pair);
}

M_TEST_DEFINE(quiescentRendezvousWaitsWithoutProcessingPeerHello) {
	struct SessionPair pair;
	_initPair(&pair);
	pair.host.quiescent = false;
	_startPair(&pair);
	assert_false(pair.host.session.localHelloSent);
	assert_true(pair.client.session.localHelloSent);
	assert_true(GBALinkSessionUpdate(
	    &pair.host.session, false));
	assert_int_equal(
	    pair.host.session.state,
	    GBA_LINK_SESSION_TRANSPORT_STARTED);
	assert_false(pair.host.session.remoteHelloReceived);

	pair.host.quiescent = true;
	_pump(&pair, 8);
	_assertAtFinalAckBarrier(&pair);
	_deinitPair(&pair);
}

M_TEST_DEFINE(alreadyUnequalModesAreCapturedWithoutLaterWrites) {
	struct SessionPair pair;
	_initPair(&pair);
	pair.client.mode = GBA_LINK_MODE_NORMAL_32;
	_startPair(&pair);
	_pump(&pair, 8);
	_assertAtFinalAckBarrier(&pair);
	assert_int_equal(
	    pair.host.session.remoteHello.initialMode,
	    GBA_LINK_MODE_NORMAL_32);
	assert_int_equal(
	    pair.client.session.remoteHello.initialMode,
	    GBA_LINK_MODE_MULTI);
	_deinitPair(&pair);
}

M_TEST_DEFINE(neitherPeerInitiallyInMultiRemainsNotReady) {
	struct SessionPair pair;
	_initPair(&pair);
	pair.host.mode = GBA_LINK_MODE_NORMAL_8;
	pair.client.mode = GBA_LINK_MODE_NORMAL_32;
	_startPair(&pair);
	_pump(&pair, 8);
	_assertAtFinalAckBarrier(&pair);
	_initTimelines(&pair);
	assert_true(GBALinkTimelineHostCommitInitialModes(
	    &pair.host.timeline));
	_pump(&pair, 32);
	assert_int_equal(
	    pair.host.timeline.committedModeGeneration, 1);
	assert_int_equal(
	    pair.client.timeline.committedModeGeneration, 1);
	assert_false(pair.host.jointlyReady);
	assert_false(pair.client.jointlyReady);
	assert_int_equal(
	    pair.host.committedLocalMode,
	    GBA_LINK_MODE_NORMAL_8);
	assert_int_equal(
	    pair.client.committedLocalMode,
	    GBA_LINK_MODE_NORMAL_32);
	_deinitPair(&pair);
}

M_TEST_DEFINE(everyInterruptedHandshakePhaseTimesOutClosed) {
	const struct {
		enum GBALinkRole sender;
		enum GBALinkMessageType message;
	} failures[] = {
		{ GBA_LINK_ROLE_HOST, GBA_LINK_MESSAGE_HELLO },
		{ GBA_LINK_ROLE_CLIENT, GBA_LINK_MESSAGE_HELLO },
		{ GBA_LINK_ROLE_HOST, GBA_LINK_MESSAGE_ACCEPT },
		{ GBA_LINK_ROLE_CLIENT, GBA_LINK_MESSAGE_ACCEPT_ACK },
		{ GBA_LINK_ROLE_HOST, GBA_LINK_MESSAGE_SESSION_READY },
		{ GBA_LINK_ROLE_CLIENT, GBA_LINK_MESSAGE_SESSION_READY_ACK },
	};
	for (size_t i = 0;
	     i < sizeof(failures) / sizeof(failures[0]); ++i) {
		struct SessionPair pair;
		_initPair(&pair);
		struct SessionEndpoint* sender =
		    failures[i].sender == GBA_LINK_ROLE_HOST
		        ? &pair.host : &pair.client;
		sender->dropNextType = failures[i].message;
		_startPair(&pair);
		_pump(&pair, 32);
		pair.host.now = GBA_LINK_MAX_WAIT_MS + 1;
		pair.client.now = GBA_LINK_MAX_WAIT_MS + 1;
		if (GBALinkSessionIsLive(&pair.host.session)) {
			GBALinkSessionUpdate(
			    &pair.host.session, false);
		}
		if (GBALinkSessionIsLive(&pair.client.session)) {
			GBALinkSessionUpdate(
			    &pair.client.session, false);
		}
		assert_true(
		    pair.host.session.state ==
		        GBA_LINK_SESSION_FAILED ||
		    pair.client.session.state ==
		        GBA_LINK_SESSION_FAILED);
		assert_false(
		    pair.host.observable &&
		    pair.client.observable);
		_deinitPair(&pair);
	}
}

M_TEST_DEFINE(exactLatestDuplicateReplaysResponseIdempotently) {
	struct SessionPair pair;
	_initPair(&pair);
	pair.client.duplicateNextSend = true;
	_startPair(&pair);
	_pump(&pair, 12);
	_assertAtFinalAckBarrier(&pair);
	assert_int_equal(pair.host.failureCalls, 0);
	assert_int_equal(pair.client.failureCalls, 0);
	_deinitPair(&pair);
}

M_TEST_DEFINE(conflictingDuplicateFailsClosed) {
	struct SessionPair pair;
	_initPair(&pair);
	pair.client.conflictNextSend = true;
	_startPair(&pair);
	_pump(&pair, 4);
	assert_int_equal(
	    pair.host.session.state, GBA_LINK_SESSION_FAILED);
	assert_int_equal(
	    pair.host.failureReason, GBA_LINK_REASON_MALFORMED_PACKET);
	assert_false(pair.host.transport.active);
	_deinitPair(&pair);
}

M_TEST_DEFINE(romMismatchIsNamedAndRejected) {
	struct SessionPair pair;
	_initPair(&pair);
	struct GBALinkSessionConfig clientConfig =
	    _config(&pair.client, 2);
	assert_true(GBALinkSessionConfigure(
	    &pair.client.session, &clientConfig));
	_startPair(&pair);
	_pump(&pair, 4);
	assert_true(
	    pair.host.failureReason == GBA_LINK_REASON_ROM_MISMATCH ||
	    pair.client.failureReason == GBA_LINK_REASON_ROM_MISMATCH);
	assert_false(pair.host.transport.active);
	assert_false(pair.client.transport.active);
	_deinitPair(&pair);
}

M_TEST_DEFINE(profileMismatchIdentifiesCategory) {
	struct SessionPair pair;
	_initPair(&pair);
	struct GBALinkSessionConfig clientConfig =
	    _config(&pair.client, 1);
	clientConfig.digests[3].digest[0] ^= 1;
	assert_true(GBALinkSessionConfigure(
	    &pair.client.session, &clientConfig));
	_startPair(&pair);
	_pump(&pair, 4);
	assert_true(
	    pair.host.failureReason ==
	        GBA_LINK_REASON_DETERMINISM_MISMATCH ||
	    pair.client.failureReason ==
	        GBA_LINK_REASON_DETERMINISM_MISMATCH);
	_deinitPair(&pair);
}

M_TEST_DEFINE(stopDuringPollInvalidatesBeforeFurtherProcessing) {
	struct SessionPair pair;
	_initPair(&pair);
	_startPair(&pair);
	pair.host.stopDuringPoll = true;
	assert_false(GBALinkSessionUpdate(
	    &pair.host.session, true));
	assert_int_equal(
	    pair.host.session.state, GBA_LINK_SESSION_FAILED);
	assert_int_equal(
	    pair.host.failureReason, GBA_LINK_REASON_TRANSPORT_STOP);
	assert_false(pair.host.transport.active);
	_deinitPair(&pair);
}

M_TEST_DEFINE(operationDeadlineIsSpecificAndBounded) {
	struct SessionPair pair;
	_initPair(&pair);
	pair.host.quiescent = false;
	_startPair(&pair);
	pair.host.now = 3000;
	assert_false(GBALinkSessionUpdate(
	    &pair.host.session, false));
	assert_int_equal(
	    pair.host.failureReason, GBA_LINK_REASON_ATTACHMENT_TIMEOUT);
	assert_int_equal(
	    pair.host.session.state, GBA_LINK_SESSION_FAILED);
	_deinitPair(&pair);
}

M_TEST_DEFINE(staleAndFuturePacketSequencesFailClosed) {
	struct SessionPair pair;
	_initPair(&pair);
	_startPair(&pair);

	struct GBALinkPacket packet;
	memset(&packet, 0, sizeof(packet));
	packet.header.type = GBA_LINK_MESSAGE_HELLO;
	packet.header.packetSequence = 3;
	packet.payload.hello = pair.client.session.localHello;
	uint8_t bytes[GBA_LINK_MAX_PACKET_SIZE];
	size_t size = 0;
	assert_true(GBALinkPacketEncode(
	    &packet, bytes, sizeof(bytes), &size));
	assert_true(GBALinkTransportQueueInbound(
	    &pair.host.transport, 1, bytes, size));
	assert_false(GBALinkSessionUpdate(
	    &pair.host.session, false));
	assert_int_equal(
	    pair.host.failureReason, GBA_LINK_REASON_MALFORMED_PACKET);
	_deinitPair(&pair);
}

M_TEST_DEFINE(activeCheatsCannotConfigureMvpSession) {
	struct SessionPair pair;
	_initPair(&pair);
	struct GBALinkSessionConfig config = _config(&pair.host, 1);
	assert_true(GBALinkSessionConfigure(
	    &pair.host.session, &config));
	config.cheatsEnabled = true;
	assert_false(GBALinkSessionConfigure(
	    &pair.host.session, &config));
	_deinitPair(&pair);
}

M_TEST_DEFINE(unsupportedPolicyAndCompatibilityVersionRejectCleanly) {
	struct SessionPair pair;
	_initPair(&pair);
	struct GBALinkSessionConfig config =
	    _config(&pair.client, 1);
	config.supportedPolicies =
	    1U << GBA_LINK_COMPATIBILITY_GROUP;
	assert_false(GBALinkSessionConfigure(
	    &pair.client.session, &config));

	config = _config(&pair.client, 1);
	++config.emulationCompatibilityVersion;
	assert_true(GBALinkSessionConfigure(
	    &pair.client.session, &config));
	_startPair(&pair);
	_pump(&pair, 8);
	assert_true(
	    pair.host.failureReason ==
	        GBA_LINK_REASON_COMPATIBILITY_MISMATCH ||
	    pair.client.failureReason ==
	        GBA_LINK_REASON_COMPATIBILITY_MISMATCH);
	_deinitPair(&pair);
}

M_TEST_DEFINE(initialModeBarrierUsesBilateralAcknowledgements) {
	struct SessionPair pair;
	_initPair(&pair);
	_startPair(&pair);
	_pump(&pair, 8);
	_assertAtFinalAckBarrier(&pair);
	_commitInitialModes(&pair);
	assert_false(pair.host.timeline.modeBarrier);
	assert_false(pair.client.timeline.modeBarrier);
	assert_false(pair.host.paused);
	assert_true(pair.client.paused);
	assert_int_equal(pair.host.runtimePackets, 1);
	assert_int_equal(pair.client.runtimePackets, 2);
	_deinitPair(&pair);
}

M_TEST_DEFINE(hostLeadingGrantIsSingleFlight) {
	struct SessionPair pair;
	_initPair(&pair);
	_startPair(&pair);
	_pump(&pair, 8);
	_commitInitialModes(&pair);

	pair.host.cycle = 200;
	assert_false(GBALinkTimelineHostReachHorizon(
	    &pair.host.timeline, 300));
	assert_true(GBALinkTimelineHostReachHorizon(
	    &pair.host.timeline, 200));
	assert_false(GBALinkTimelineHostReachHorizon(
	    &pair.host.timeline, 300));
	assert_true(pair.host.paused);
	assert_int_equal(pair.host.timeline.currentCableCycle, 200);
	_pump(&pair, 1);
	assert_true(pair.client.timeline.grantOutstanding);
	assert_true(pair.client.executionLimitEnabled);
	assert_int_equal(pair.client.executionLimit, 200);
	assert_false(pair.client.paused);

	pair.client.cycle = 200;
	assert_true(GBALinkTimelineClientReachGrant(
	    &pair.client.timeline, 200));
	assert_true(pair.client.paused);
	assert_false(pair.client.executionLimitEnabled);
	_pump(&pair, 2);
	assert_false(pair.host.timeline.grantOutstanding);
	assert_false(pair.host.paused);
	assert_int_equal(pair.host.timeline.lastGrantSequence, 1);
	assert_int_equal(pair.client.timeline.lastGrantSequence, 1);
	_deinitPair(&pair);
}

M_TEST_DEFINE(rejectedModeWriteDoesNotMutateCommittedTimelineState) {
	struct SessionPair pair;
	_initPair(&pair);
	_startPair(&pair);
	_pump(&pair, 8);
	_commitInitialModes(&pair);
	assert_false(
	    pair.client.timeline.grantOutstanding);
	assert_false(GBALinkTimelineLocalModeWrite(
	    &pair.client.timeline,
	    GBA_LINK_MODE_NORMAL_32,
	    pair.client.cycle));
	assert_int_equal(
	    pair.client.timeline.localMode,
	    GBA_LINK_MODE_MULTI);
	assert_true(pair.client.paused);
	_deinitPair(&pair);
}

M_TEST_DEFINE(missingGrantAcknowledgementUsesGrantDeadline) {
	struct SessionPair pair;
	_initPair(&pair);
	_startPair(&pair);
	_pump(&pair, 8);
	_commitInitialModes(&pair);

	pair.host.cycle = 200;
	assert_true(GBALinkTimelineHostReachHorizon(
	    &pair.host.timeline, 200));
	_pump(&pair, 1);
	pair.client.dropNextType =
	    GBA_LINK_MESSAGE_GRANT_ACK;
	pair.client.cycle = 200;
	assert_true(GBALinkTimelineClientReachGrant(
	    &pair.client.timeline, 200));
	_pump(&pair, 2);
	assert_true(pair.host.timeline.grantOutstanding);
	pair.host.now = GBA_LINK_MAX_WAIT_MS;
	assert_false(GBALinkSessionUpdate(
	    &pair.host.session, false));
	assert_int_equal(
	    pair.host.failureReason,
	    GBA_LINK_REASON_GRANT_TIMEOUT);
	_deinitPair(&pair);
}

struct LatencyResult {
	uint64_t hostModeGeneration;
	uint64_t clientModeGeneration;
	uint64_t hostCableCycle;
	uint64_t clientCableCycle;
	uint64_t clientLocalAnchor;
	uint64_t clientCableAnchor;
	bool hostReady;
	bool clientReady;
};

static struct LatencyResult _runLatencyScenario(
    unsigned delayPumps) {
	struct SessionPair pair;
	_initPair(&pair);
	pair.host.deliveryDelayPumps = delayPumps;
	pair.client.deliveryDelayPumps = delayPumps;
	_startPair(&pair);
	_pump(&pair, 64);
	_assertAtFinalAckBarrier(&pair);
	_commitInitialModes(&pair);

	pair.host.cycle = 300;
	assert_true(GBALinkTimelineHostReachHorizon(
	    &pair.host.timeline, 300));
	_pump(&pair, 16);
	assert_true(pair.client.timeline.grantOutstanding);
	assert_int_equal(pair.client.executionLimit, 300);
	pair.client.cycle = 250;
	assert_true(GBALinkTimelineLocalModeWrite(
	    &pair.client.timeline,
	    GBA_LINK_MODE_NORMAL_32, 250));
	_pump(&pair, 64);

	struct LatencyResult result = {
		.hostModeGeneration =
		    pair.host.timeline.committedModeGeneration,
		.clientModeGeneration =
		    pair.client.timeline.committedModeGeneration,
		.hostCableCycle =
		    pair.host.timeline.currentCableCycle,
		.clientCableCycle =
		    pair.client.timeline.currentCableCycle,
		.clientLocalAnchor =
		    pair.client.timeline.clock.localAnchor,
		.clientCableAnchor =
		    pair.client.timeline.clock.cableAnchor,
		.hostReady = pair.host.jointlyReady,
		.clientReady = pair.client.jointlyReady,
	};
	_deinitPair(&pair);
	return result;
}

M_TEST_DEFINE(deliveryLatencyDoesNotChangeModeCommitBoundary) {
	struct LatencyResult immediate =
	    _runLatencyScenario(0);
	for (unsigned delay = 1; delay <= 4; ++delay) {
		struct LatencyResult delayed =
		    _runLatencyScenario(delay);
		assert_int_equal(
		    delayed.hostModeGeneration,
		    immediate.hostModeGeneration);
		assert_int_equal(
		    delayed.clientModeGeneration,
		    immediate.clientModeGeneration);
		assert_int_equal(
		    delayed.hostCableCycle,
		    immediate.hostCableCycle);
		assert_int_equal(
		    delayed.clientCableCycle,
		    immediate.clientCableCycle);
		assert_int_equal(
		    delayed.clientLocalAnchor,
		    immediate.clientLocalAnchor);
		assert_int_equal(
		    delayed.clientCableAnchor,
		    immediate.clientCableAnchor);
		assert_int_equal(
		    delayed.hostReady,
		    immediate.hostReady);
		assert_int_equal(
		    delayed.clientReady,
		    immediate.clientReady);
	}
	assert_int_equal(
	    immediate.hostModeGeneration, 2);
	assert_int_equal(
	    immediate.clientModeGeneration, 2);
	assert_int_equal(
	    immediate.hostCableCycle, 300);
	assert_int_equal(
	    immediate.clientCableCycle, 300);
	assert_int_equal(
	    immediate.clientLocalAnchor, 250);
	assert_int_equal(
	    immediate.clientCableAnchor, 300);
	assert_false(immediate.hostReady);
	assert_false(immediate.clientReady);
}

M_TEST_DEFINE(clientModeIntentCommitsAtUnpassedHostBoundary) {
	struct SessionPair pair;
	_initPair(&pair);
	_startPair(&pair);
	_pump(&pair, 8);
	_commitInitialModes(&pair);

	pair.host.cycle = 300;
	assert_true(GBALinkTimelineHostReachHorizon(
	    &pair.host.timeline, 300));
	_pump(&pair, 1);
	pair.client.cycle = 250;
	assert_true(GBALinkTimelineLocalModeWrite(
	    &pair.client.timeline,
	    GBA_LINK_MODE_NORMAL_32, 250));
	assert_true(pair.client.paused);
	_pump(&pair, 6);

	assert_int_equal(
	    pair.host.timeline.committedModeGeneration, 2);
	assert_int_equal(
	    pair.client.timeline.committedModeGeneration, 2);
	assert_int_equal(
	    pair.host.committedRemoteMode,
	    GBA_LINK_MODE_NORMAL_32);
	assert_int_equal(
	    pair.client.committedLocalMode,
	    GBA_LINK_MODE_NORMAL_32);
	assert_false(pair.host.jointlyReady);
	assert_false(pair.client.jointlyReady);
	assert_int_equal(pair.client.rebasedLocalCycle, 250);
	assert_int_equal(pair.client.rebasedCableCycle, 300);
	uint64_t mapped = 0;
	assert_true(GBALinkClockLocalToCable(
	    &pair.client.timeline.clock, 250, &mapped));
	assert_int_equal(mapped, 300);
	assert_false(pair.host.paused);
	assert_true(pair.client.paused);
	_deinitPair(&pair);
}

M_TEST_DEFINE(hostModeIntentFirstGrantsCatchupBoundary) {
	struct SessionPair pair;
	_initPair(&pair);
	_startPair(&pair);
	_pump(&pair, 8);
	_commitInitialModes(&pair);

	pair.host.cycle = 150;
	assert_true(GBALinkTimelineLocalModeWrite(
	    &pair.host.timeline,
	    GBA_LINK_MODE_NORMAL_32, 150));
	assert_true(pair.host.timeline.grantOutstanding);
	assert_true(pair.host.timeline.pendingHostMode);
	_pump(&pair, 1);
	assert_int_equal(pair.client.executionLimit, 150);
	pair.client.cycle = 150;
	assert_true(GBALinkTimelineClientReachGrant(
	    &pair.client.timeline, 150));
	_pump(&pair, 6);
	assert_int_equal(
	    pair.host.committedLocalMode,
	    GBA_LINK_MODE_NORMAL_32);
	assert_int_equal(
	    pair.client.committedRemoteMode,
	    GBA_LINK_MODE_NORMAL_32);
	assert_int_equal(
	    pair.host.timeline.committedModeGeneration, 2);
	assert_int_equal(
	    pair.client.timeline.committedModeGeneration, 2);
	assert_false(pair.host.jointlyReady);
	assert_false(pair.client.jointlyReady);
	_deinitPair(&pair);
}

M_TEST_DEFINE(clockMappingRejectsPastAndOverflow) {
	struct GBALinkClockMapping mapping = {
		.localAnchor = 100,
		.cableAnchor = 200,
	};
	uint64_t value = 0;
	assert_false(GBALinkClockLocalToCable(
	    &mapping, 99, &value));
	assert_false(GBALinkClockCableToLocal(
	    &mapping, 199, &value));
	assert_true(GBALinkClockLocalToCable(
	    &mapping, 150, &value));
	assert_int_equal(value, 250);
	assert_true(GBALinkClockCableToLocal(
	    &mapping, 250, &value));
	assert_int_equal(value, 150);
	mapping.cableAnchor = UINT64_MAX;
	assert_false(GBALinkClockLocalToCable(
	    &mapping, 101, &value));
}

M_TEST_DEFINE(timingClockExtendsMtimingWrapMonotonically) {
	struct GBALinkTimingClock clock;
	memset(&clock, 0, sizeof(clock));
	GBALinkTimingClockInit(&clock, (int32_t) 0xFFFFFFF0U);
	uint64_t cycle = 0;
	assert_true(GBALinkTimingClockUpdate(
	    &clock, (int32_t) 0x00000020U, &cycle));
	assert_int_equal(cycle, UINT64_C(0x100000020));
	assert_true(GBALinkTimingClockUpdate(
	    &clock, (int32_t) 0x00001020U, &cycle));
	assert_int_equal(cycle, UINT64_C(0x100001020));
	clock.cycle = UINT64_MAX;
	assert_false(GBALinkTimingClockUpdate(
	    &clock, (int32_t) 0x00001021U, &cycle));
}

M_TEST_DEFINE(timingPoliciesSeparateSchedulerGrantAndHealth) {
	struct GBALinkTimingPolicy policy;
	GBALinkTimingPolicyInit(&policy);
	assert_true(GBALinkTimingPolicyValidate(&policy));
	assert_int_equal(policy.localSchedulerQuantum, 4096);
	assert_int_equal(policy.candidateHorizonCycles, 280896);
	assert_int_equal(policy.healthBarrierCycles, 0);

	policy.localSchedulerQuantum = 0;
	assert_false(GBALinkTimingPolicyValidate(&policy));
	policy.localSchedulerQuantum = 1;
	policy.candidateHorizonCycles = 0;
	assert_false(GBALinkTimingPolicyValidate(&policy));
}

M_TEST_SUITE_DEFINE(GBALinkSession,
	cmocka_unit_test(atomicHandshakeStopsAtFinalAckBarrier),
	cmocka_unit_test(clientBecomesObservableOnlyOnPostAttachmentHostEvent),
	cmocka_unit_test(runtimeYieldDefersCoalescedFollowingPacket),
	cmocka_unit_test(quiescentRendezvousWaitsWithoutProcessingPeerHello),
	cmocka_unit_test(alreadyUnequalModesAreCapturedWithoutLaterWrites),
	cmocka_unit_test(neitherPeerInitiallyInMultiRemainsNotReady),
	cmocka_unit_test(everyInterruptedHandshakePhaseTimesOutClosed),
	cmocka_unit_test(exactLatestDuplicateReplaysResponseIdempotently),
	cmocka_unit_test(conflictingDuplicateFailsClosed),
	cmocka_unit_test(romMismatchIsNamedAndRejected),
	cmocka_unit_test(profileMismatchIdentifiesCategory),
	cmocka_unit_test(stopDuringPollInvalidatesBeforeFurtherProcessing),
	cmocka_unit_test(operationDeadlineIsSpecificAndBounded),
	cmocka_unit_test(staleAndFuturePacketSequencesFailClosed),
	cmocka_unit_test(activeCheatsCannotConfigureMvpSession),
	cmocka_unit_test(unsupportedPolicyAndCompatibilityVersionRejectCleanly),
	cmocka_unit_test(initialModeBarrierUsesBilateralAcknowledgements),
	cmocka_unit_test(hostLeadingGrantIsSingleFlight),
	cmocka_unit_test(rejectedModeWriteDoesNotMutateCommittedTimelineState),
	cmocka_unit_test(missingGrantAcknowledgementUsesGrantDeadline),
	cmocka_unit_test(deliveryLatencyDoesNotChangeModeCommitBoundary),
	cmocka_unit_test(clientModeIntentCommitsAtUnpassedHostBoundary),
	cmocka_unit_test(hostModeIntentFirstGrantsCatchupBoundary),
	cmocka_unit_test(clockMappingRejectsPastAndOverflow),
	cmocka_unit_test(timingClockExtendsMtimingWrapMonotonically),
	cmocka_unit_test(timingPoliciesSeparateSchedulerGrantAndHealth))
