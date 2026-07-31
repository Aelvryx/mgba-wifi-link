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
#include <mgba/internal/gba/sio/netplay/driver.h>
#include <mgba/internal/gba/sio/netplay/identity.h>
#include <mgba/internal/gba/sio/netplay/transport.h>
#include <mgba-util/vfs.h>

#include <fcntl.h>

#ifndef GBA_LINK_TEST_ROM_PATH
#error "GBA_LINK_TEST_ROM_PATH must name the built link-test ROM"
#endif

#define LINK_RESULT_ADDRESS 0x02000000
#define LINK_RESULT_MAGIC 0x31544B4C
#define LINK_RESULT_PASS 4
#define LINK_RESULT_FAIL 0x80000000U
#define LINK_RESULT_TRANSFERS 16
#define INTEGRATION_MAX_DELAYED 256
#define INTEGRATION_MAX_TRACE 4096

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

struct IntegrationPacket {
	uint8_t data[GBA_LINK_MAX_PACKET_SIZE];
	size_t size;
	uint64_t deliveryTick;
};

struct IntegrationTraceEntry {
	enum GBALinkRole sender;
	enum GBALinkMessageType message;
	uint64_t transportGeneration;
	uint32_t localSchedulerQuantum;
	uint32_t candidateHorizonCycles;
	uint32_t healthBarrierCycles;
	int topologicalPeerCount;
	int effectiveTransferPeerCount;
	uint64_t sequenceNext[GBA_LINK_SEQUENCE_DOMAIN_COUNT];
	uint64_t packetSequence;
	uint64_t logicalSequence;
	uint64_t cableCycle;
	enum GBALinkWireMode firstMode;
	enum GBALinkWireMode secondMode;
	bool jointlyReady;
	enum GBALinkSessionState sessionState;
	enum GBASIONetplayTransferState transferState;
	enum GBALinkTransferOutcome outcome;
	bool outcomeCommitted;
	bool localDeferredMode;
	bool remoteDeferredMode;
	uint16_t siocnt;
	uint16_t interruptFlags;
};

struct IntegrationPair;

struct IntegrationEndpoint {
	struct mCore* core;
	struct GBA* gba;
	struct GBALinkTransport transport;
	struct GBALinkSession session;
	struct GBASIONetplayDriver driver;
	struct IntegrationPair* pair;
	struct IntegrationEndpoint* peer;
	enum GBALinkRole role;
	struct IntegrationPacket delayed[INTEGRATION_MAX_DELAYED];
	size_t delayedSize;
	bool runningCore;
	unsigned sends;
	uint64_t lastDeliveryTick;
};

struct IntegrationPair {
	struct IntegrationEndpoint host;
	struct IntegrationEndpoint client;
	uint64_t tick;
	unsigned deliveryDelay;
	unsigned deliveryJitter;
	enum GBALinkMessageType dropMessage;
	enum GBALinkRole dropSender;
	bool droppedMessage;
	struct IntegrationTraceEntry trace[INTEGRATION_MAX_TRACE];
	size_t traceSize;
};

static uint64_t _packetLogicalSequence(
    const struct GBALinkPacket* packet) {
	switch (packet->header.type) {
	case GBA_LINK_MESSAGE_ACCEPT:
		return packet->payload.accept.proposedSessionId;
	case GBA_LINK_MESSAGE_EXECUTION_GRANT:
	case GBA_LINK_MESSAGE_GRANT_ACK:
		return packet->payload.grant.grantSequence;
	case GBA_LINK_MESSAGE_MODE_INTENT:
		return packet->payload.modeIntent.modeGeneration;
	case GBA_LINK_MESSAGE_MODE_COMMIT:
		return packet->payload.modeCommit.modeGeneration;
	case GBA_LINK_MESSAGE_MODE_ACK:
		return packet->payload.modeAck.modeGeneration;
	case GBA_LINK_MESSAGE_TRANSFER_START:
	case GBA_LINK_MESSAGE_TRANSFER_READY:
		return packet->payload.transferStart.transferSequence;
	case GBA_LINK_MESSAGE_TRANSFER_COMMIT:
		return packet->payload.transferCommit.transferSequence;
	case GBA_LINK_MESSAGE_TRANSFER_ABORT:
		return packet->payload.transferAbort.transferSequence;
	case GBA_LINK_MESSAGE_COMPLETION_CATCHUP:
		return packet->payload.completionCatchup.completionSequence;
	case GBA_LINK_MESSAGE_COMPLETION_READY:
		return packet->payload.completionReady.completionSequence;
	case GBA_LINK_MESSAGE_COMPLETION_DECISION:
		return packet->payload.completionDecision.completionSequence;
	case GBA_LINK_MESSAGE_COMPLETION_DECISION_ACK:
		return packet->payload.completionDecisionAck.completionSequence;
	default:
		return 0;
	}
}

static void _tracePacket(
    struct IntegrationEndpoint* endpoint,
    const struct GBALinkPacket* packet) {
	struct IntegrationPair* pair = endpoint->pair;
	assert_true(pair->traceSize < INTEGRATION_MAX_TRACE);
	struct IntegrationTraceEntry* entry =
	    &pair->trace[pair->traceSize++];
	memset(entry, 0, sizeof(*entry));
	entry->sender = endpoint->role;
	entry->message = packet->header.type;
	entry->transportGeneration =
	    endpoint->transport.generation;
	entry->localSchedulerQuantum =
	    endpoint->driver.timingPolicy.localSchedulerQuantum;
	entry->candidateHorizonCycles =
	    endpoint->driver.timingPolicy.candidateHorizonCycles;
	entry->healthBarrierCycles =
	    endpoint->driver.timingPolicy.healthBarrierCycles;
	entry->topologicalPeerCount =
	    endpoint->driver.topologicalPeerCount;
	entry->effectiveTransferPeerCount =
	    endpoint->driver.attached &&
	            endpoint->driver.d.connectedDevices
	        ? endpoint->driver.d.connectedDevices(
	              &endpoint->driver.d)
	        : 0;
	memcpy(
	    entry->sequenceNext,
	    endpoint->session.sequences.next,
	    sizeof(entry->sequenceNext));
	entry->packetSequence =
	    packet->header.packetSequence;
	entry->logicalSequence =
	    _packetLogicalSequence(packet);
	if (packet->header.type ==
	    GBA_LINK_MESSAGE_MODE_INTENT) {
		entry->firstMode =
		    packet->payload.modeIntent.localMode;
	} else if (packet->header.type ==
	           GBA_LINK_MESSAGE_MODE_COMMIT) {
		entry->firstMode =
		    packet->payload.modeCommit.hostMode;
		entry->secondMode =
		    packet->payload.modeCommit.clientMode;
		entry->jointlyReady =
		    packet->payload.modeCommit.jointlyReady;
	}
	entry->cableCycle =
	    endpoint->driver.timelineInitialized
	        ? endpoint->driver.timeline.currentCableCycle
	        : endpoint->session.attachCycle;
	entry->sessionState = endpoint->session.state;
	entry->transferState =
	    endpoint->driver.transfer.state;
	entry->outcome = endpoint->driver.transfer.outcome;
	entry->outcomeCommitted =
	    endpoint->driver.transfer.decisionAccepted;
	entry->localDeferredMode =
	    endpoint->driver.transfer.localDeferredMode;
	entry->remoteDeferredMode =
	    endpoint->driver.transfer.remoteDeferredMode;
	if (endpoint->gba) {
		entry->siocnt = endpoint->gba->sio.siocnt;
		entry->interruptFlags =
		    endpoint->gba->memory.io[GBA_REG(IF)];
	}
}

static void _deliverDue(struct IntegrationPair* pair) {
	struct IntegrationEndpoint* endpoints[] = {
		&pair->host, &pair->client,
	};
	for (unsigned endpointIndex = 0;
	     endpointIndex < 2; ++endpointIndex) {
		struct IntegrationEndpoint* endpoint =
		    endpoints[endpointIndex];
		size_t write = 0;
		for (size_t i = 0; i < endpoint->delayedSize; ++i) {
			struct IntegrationPacket packet =
			    endpoint->delayed[i];
			if (packet.deliveryTick <= pair->tick) {
				assert_true(
				    GBALinkTransportQueueInbound(
				        &endpoint->peer->transport,
				        endpoint->peer->transport.generation,
				        packet.data, packet.size));
			} else {
				endpoint->delayed[write++] = packet;
			}
		}
		endpoint->delayedSize = write;
	}
}

static bool _sendReliable(
    void* context, const void* data, size_t size, bool flush) {
	struct IntegrationEndpoint* endpoint = context;
	assert_true(flush);
	struct GBALinkPacket packet;
	assert_int_equal(
	    GBALinkPacketDecode(
	        data, size, endpoint->role, &packet),
	    GBA_LINK_DECODE_OK);
	_tracePacket(endpoint, &packet);
	++endpoint->sends;
	if (!endpoint->pair->droppedMessage &&
	    packet.header.type ==
	        endpoint->pair->dropMessage &&
	    endpoint->role ==
	        endpoint->pair->dropSender) {
		endpoint->pair->droppedMessage = true;
		return true;
	}

	unsigned delay = endpoint->pair->deliveryDelay;
	if (endpoint->pair->deliveryJitter) {
		delay +=
		    (endpoint->sends * 5U +
		     (unsigned) packet.header.type * 3U) %
		    (endpoint->pair->deliveryJitter + 1U);
	}
	if (!delay) {
		return GBALinkTransportQueueInbound(
		    &endpoint->peer->transport,
		    endpoint->peer->transport.generation,
		    data, size);
	}
	assert_true(
	    endpoint->delayedSize <
	    INTEGRATION_MAX_DELAYED);
	struct IntegrationPacket* delayed =
	    &endpoint->delayed[endpoint->delayedSize++];
	memcpy(delayed->data, data, size);
	delayed->size = size;
	delayed->deliveryTick =
	    endpoint->pair->tick + delay;
	if (delayed->deliveryTick <
	    endpoint->lastDeliveryTick) {
		delayed->deliveryTick =
		    endpoint->lastDeliveryTick;
	}
	endpoint->lastDeliveryTick =
	    delayed->deliveryTick;
	return true;
}

static void _runEndpoint(
    struct IntegrationEndpoint* endpoint);

static bool _pollReceive(void* context) {
	struct IntegrationEndpoint* endpoint = context;
	struct IntegrationPair* pair = endpoint->pair;
	++pair->tick;
	_deliverDue(pair);
	if (GBALinkSessionIsLive(&endpoint->peer->session)) {
		GBASIONetplayDriverPump(
		    &endpoint->peer->driver, false);
	}
	_runEndpoint(endpoint->peer);
	if (GBALinkSessionIsLive(&endpoint->peer->session)) {
		GBASIONetplayDriverPump(
		    &endpoint->peer->driver, false);
	}
	_deliverDue(pair);
	return true;
}

static void _yield(void* context) {
	UNUSED(context);
}

static uint64_t _monotonicTimeMs(void* context) {
	struct IntegrationEndpoint* endpoint = context;
	return endpoint->pair->tick;
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

static const struct GBALinkTransportVTable _transportVTable = {
	.sendReliable = _sendReliable,
	.pollReceive = _pollReceive,
	.yield = _yield,
	.monotonicTimeMs = _monotonicTimeMs,
	.diagnostic = _diagnostic,
	.stop = _stop,
};

static struct GBALinkSessionConfig _sessionConfig(
    struct IntegrationEndpoint* endpoint) {
	struct GBALinkSessionConfig config;
	memset(&config, 0, sizeof(config));
	assert_true(GBALinkContentIdentityFromCore(
	    endpoint->core, &config.identity));
	struct GBALinkDeterminismProfileInput profile = {
		.overclockQ16 = 0x10000,
	};
	assert_true(GBALinkDeterminismProfileBuild(
	    &profile, config.digests));
	config.capabilities =
	    GBA_LINK_MVP_CAPABILITIES;
	config.supportedPolicies =
	    1U << GBA_LINK_COMPATIBILITY_EXACT_ROM;
	config.emulationCompatibilityVersion =
	    GBA_LINK_EMULATION_COMPATIBILITY_VERSION;
	GBALinkDeadlinePolicyInit(&config.deadlines);
	config.callbacks =
	    GBASIONetplayDriverSessionCallbacks();
	config.callbackContext = &endpoint->driver;
	return config;
}

static void _initEndpoint(
    struct IntegrationEndpoint* endpoint,
    struct IntegrationPair* pair, enum GBALinkRole role) {
	memset(endpoint, 0, sizeof(*endpoint));
	endpoint->pair = pair;
	endpoint->role = role;
	endpoint->core = GBACoreCreate();
	assert_non_null(endpoint->core);
	assert_true(endpoint->core->init(endpoint->core));
	mCoreInitConfig(endpoint->core, NULL);
	assert_true(endpoint->core->loadROM(
	    endpoint->core,
	    VFileOpen(GBA_LINK_TEST_ROM_PATH, O_RDONLY)));
	endpoint->core->reset(endpoint->core);
	endpoint->gba = endpoint->core->board;

	GBALinkTransportInit(
	    &endpoint->transport, &_transportVTable,
	    endpoint);
	assert_true(GBALinkTransportStart(
	    &endpoint->transport, 1, role));
	GBALinkSessionInit(
	    &endpoint->session, &endpoint->transport);
	GBASIONetplayDriverCreate(
	    &endpoint->driver, &endpoint->gba->sio,
	    &endpoint->session);
	struct GBALinkSessionConfig config =
	    _sessionConfig(endpoint);
	assert_true(GBALinkSessionConfigure(
	    &endpoint->session, &config));
}

static void _initPair(
    struct IntegrationPair* pair,
    unsigned deliveryDelay, unsigned deliveryJitter) {
	memset(pair, 0, sizeof(*pair));
	pair->deliveryDelay = deliveryDelay;
	pair->deliveryJitter = deliveryJitter;
	_initEndpoint(
	    &pair->host, pair, GBA_LINK_ROLE_HOST);
	_initEndpoint(
	    &pair->client, pair, GBA_LINK_ROLE_CLIENT);
	pair->host.peer = &pair->client;
	pair->client.peer = &pair->host;
	assert_true(GBALinkSessionStart(
	    &pair->host.session, 1, GBA_LINK_ROLE_HOST));
	assert_true(GBALinkSessionStart(
	    &pair->client.session, 1, GBA_LINK_ROLE_CLIENT));
}

static void _runEndpoint(
    struct IntegrationEndpoint* endpoint) {
	if (!endpoint || endpoint->runningCore ||
	    !endpoint->driver.attached ||
	    GBASIONetplayDriverIsPaused(&endpoint->driver)) {
		return;
	}
	endpoint->runningCore = true;
	endpoint->core->runFrame(endpoint->core);
	endpoint->runningCore = false;
	if (endpoint->role == GBA_LINK_ROLE_HOST &&
	    GBALinkSessionIsLive(&endpoint->session) &&
	    !GBASIONetplayDriverIsPaused(&endpoint->driver)) {
		GBASIONetplayDriverHostFrameBoundary(
		    &endpoint->driver);
	}
}

static uint32_t _resultWord(
    struct IntegrationEndpoint* endpoint, unsigned index) {
	return endpoint->core->rawRead32(
	    endpoint->core,
	    LINK_RESULT_ADDRESS + index * sizeof(uint32_t),
	    -1);
}

static void _readResult(
    struct IntegrationEndpoint* endpoint,
    struct LinkTestResult* result) {
	memset(result, 0, sizeof(*result));
	uint8_t* bytes = (uint8_t*) result;
	for (size_t offset = 0;
	     offset < sizeof(*result); offset += sizeof(uint32_t)) {
		uint32_t word = endpoint->core->rawRead32(
		    endpoint->core,
		    LINK_RESULT_ADDRESS + (uint32_t) offset, -1);
		size_t remaining = sizeof(*result) - offset;
		memcpy(
		    &bytes[offset], &word,
		    remaining < sizeof(word)
		        ? remaining : sizeof(word));
	}
}

static void _pumpPair(struct IntegrationPair* pair) {
	++pair->tick;
	_deliverDue(pair);
	if (GBALinkSessionIsLive(&pair->host.session)) {
		GBASIONetplayDriverPump(
		    &pair->host.driver, false);
	}
	if (GBALinkSessionIsLive(&pair->client.session)) {
		GBASIONetplayDriverPump(
		    &pair->client.driver, false);
	}
	_runEndpoint(&pair->host);
	_runEndpoint(&pair->client);
	_deliverDue(pair);
}

static void _runPairToCompletion(
    struct IntegrationPair* pair,
    struct LinkTestResult* hostResult,
    struct LinkTestResult* clientResult) {
	for (unsigned iteration = 0;
	     iteration < 4096; ++iteration) {
		_pumpPair(pair);
		uint32_t hostStatus = _resultWord(
		    &pair->host, 2);
		uint32_t clientStatus = _resultWord(
		    &pair->client, 2);
		if ((hostStatus & LINK_RESULT_FAIL) ||
		    (clientStatus & LINK_RESULT_FAIL)) {
			struct LinkTestResult failedHost;
			struct LinkTestResult failedClient;
			_readResult(&pair->host, &failedHost);
			_readResult(&pair->client, &failedClient);
			fprintf(
			    stderr,
			    "link ROM failed: tick=%" PRIu64
			    " host(status=%08X transfers=%u errors=%u missed=%u"
			    " duplicate=%u timeouts=%u siocnt=%04X)"
			    " client(status=%08X transfers=%u errors=%u missed=%u"
			    " duplicate=%u timeouts=%u siocnt=%04X)"
			    " host-words=%04X/%04X/%04X/%04X"
			    " expected=%04X/%04X/%04X/%04X\n",
			    pair->tick,
			    failedHost.status, failedHost.transfers,
			    failedHost.dataErrors, failedHost.missedIrqs,
			    failedHost.duplicateIrqs, failedHost.timeouts,
			    failedHost.lastSIOCNT,
			    failedClient.status, failedClient.transfers,
			    failedClient.dataErrors, failedClient.missedIrqs,
			    failedClient.duplicateIrqs, failedClient.timeouts,
			    failedClient.lastSIOCNT,
			    failedHost.received[0],
			    failedHost.received[1],
			    failedHost.received[2],
			    failedHost.received[3],
			    failedHost.expected[0],
			    failedHost.expected[1],
			    failedHost.expected[2],
			    failedHost.expected[3]);
			size_t first =
			    pair->traceSize > 32
			        ? pair->traceSize - 32
			        : 0;
			for (size_t i = first;
			     i < pair->traceSize; ++i) {
				const struct IntegrationTraceEntry* entry =
				    &pair->trace[i];
				fprintf(
				    stderr,
				    "trace %zu role=%d type=%s logical=%" PRIu64
				    " cycle=%" PRIu64 " session=%d transfer=%d"
				    " outcome=%d committed=%d\n",
				    i, entry->sender,
				    GBALinkMessageTypeName(entry->message),
				    entry->logicalSequence,
				    entry->cableCycle,
				    entry->sessionState,
				    entry->transferState,
				    entry->outcome,
				    entry->outcomeCommitted);
			}
		}
		assert_false(hostStatus & LINK_RESULT_FAIL);
		assert_false(clientStatus & LINK_RESULT_FAIL);
		if (hostStatus == LINK_RESULT_PASS &&
		    clientStatus == LINK_RESULT_PASS) {
			break;
		}
	}
	_readResult(&pair->host, hostResult);
	_readResult(&pair->client, clientResult);
	if (hostResult->status != LINK_RESULT_PASS ||
	    clientResult->status != LINK_RESULT_PASS) {
		unsigned messageCounts[
		    GBA_LINK_MESSAGE_COMPLETION_DECISION_ACK + 1] = {0};
		for (size_t i = 0; i < pair->traceSize; ++i) {
			++messageCounts[pair->trace[i].message];
		}
		fprintf(
		    stderr,
		    "integration stalled: tick=%" PRIu64
		    " host(status=%08X session=%d paused=%d transfer=%d cycle=%" PRIu64
		    " grant=%d mode=%d ready=%d modes=%d/%d timeline=%d/%d"
		    " generation=%" PRIu64 " queued=%d/%d siocnt=%04X)"
		    " client(status=%08X session=%d paused=%d"
		    " transfer=%d cycle=%" PRIu64
		    " grant=%d mode=%d ready=%d modes=%d/%d timeline=%d/%d"
		    " generation=%" PRIu64 " queued=%d/%d siocnt=%04X)"
		    " trace=%zu\n",
		    pair->tick,
		    hostResult->status, pair->host.session.state,
		    pair->host.driver.paused,
		    pair->host.driver.transfer.state,
		    pair->host.driver.localCycle,
		    pair->host.driver.timeline.grantOutstanding,
		    pair->host.driver.timeline.modeBarrier,
		    pair->host.driver.jointlyReady,
		    pair->host.driver.committedLocalMode,
		    pair->host.driver.committedRemoteMode,
		    pair->host.driver.timeline.localMode,
		    pair->host.driver.timeline.remoteMode,
		    pair->host.driver.committedModeGeneration,
		    pair->host.driver.queuedLocalModeIntent,
		    pair->host.driver.queuedLocalMode,
		    pair->host.gba->sio.siocnt,
		    clientResult->status, pair->client.session.state,
		    pair->client.driver.paused,
		    pair->client.driver.transfer.state,
		    pair->client.driver.localCycle,
		    pair->client.driver.timeline.grantOutstanding,
		    pair->client.driver.timeline.modeBarrier,
		    pair->client.driver.jointlyReady,
		    pair->client.driver.committedLocalMode,
		    pair->client.driver.committedRemoteMode,
		    pair->client.driver.timeline.localMode,
		    pair->client.driver.timeline.remoteMode,
		    pair->client.driver.committedModeGeneration,
		    pair->client.driver.queuedLocalModeIntent,
		    pair->client.driver.queuedLocalMode,
		    pair->client.gba->sio.siocnt,
		    pair->traceSize);
		fprintf(
		    stderr,
		    "messages: grant=%u ack=%u intent=%u commit=%u"
		    " mode-ack=%u start=%u ready=%u transfer-commit=%u\n",
		    messageCounts[GBA_LINK_MESSAGE_EXECUTION_GRANT],
		    messageCounts[GBA_LINK_MESSAGE_GRANT_ACK],
		    messageCounts[GBA_LINK_MESSAGE_MODE_INTENT],
		    messageCounts[GBA_LINK_MESSAGE_MODE_COMMIT],
		    messageCounts[GBA_LINK_MESSAGE_MODE_ACK],
		    messageCounts[GBA_LINK_MESSAGE_TRANSFER_START],
		    messageCounts[GBA_LINK_MESSAGE_TRANSFER_READY],
		    messageCounts[GBA_LINK_MESSAGE_TRANSFER_COMMIT]);
		for (size_t i = 0; i < pair->traceSize; ++i) {
			const struct IntegrationTraceEntry* entry =
			    &pair->trace[i];
			if (entry->message >=
			        GBA_LINK_MESSAGE_MODE_INTENT &&
			    entry->message <=
			        GBA_LINK_MESSAGE_MODE_ACK) {
				fprintf(
				    stderr,
				    "mode trace %zu: role=%d type=%d packet=%" PRIu64
				    " logical=%" PRIu64 " cable=%" PRIu64
				    " state=%d top=%d effective=%d modes=%d/%d"
				    " joint=%d siocnt=%04X\n",
				    i, entry->sender, entry->message,
				    entry->packetSequence,
				    entry->logicalSequence,
				    entry->cableCycle,
				    entry->sessionState,
				    entry->topologicalPeerCount,
				    entry->effectiveTransferPeerCount,
				    entry->firstMode,
				    entry->secondMode,
				    entry->jointlyReady,
				    entry->siocnt);
			}
		}
	}
}

static void _assertResult(
    const struct LinkTestResult* result,
    unsigned playerId) {
	assert_int_equal(result->magic, LINK_RESULT_MAGIC);
	assert_int_equal(result->version, 1);
	assert_int_equal(result->status, LINK_RESULT_PASS);
	assert_int_equal(result->playerId, playerId);
	assert_int_equal(result->observableAttachments, 1);
	assert_int_equal(result->effectiveParticipants, 2);
	assert_int_equal(result->transfers, LINK_RESULT_TRANSFERS);
	assert_int_equal(result->baudMask, 0xF);
	assert_int_equal(result->dataErrors, 0);
	assert_int_equal(result->missedIrqs, 0);
	assert_int_equal(result->duplicateIrqs, 0);
	assert_int_equal(
	    result->busyObservations,
	    LINK_RESULT_TRANSFERS);
	assert_int_equal(result->timeouts, 0);
}

static void _deinitEndpoint(
    struct IntegrationEndpoint* endpoint) {
	if (endpoint->driver.attached) {
		GBASIONetplayDriverDetach(
		    &endpoint->driver);
	}
	endpoint->session.state =
	    GBA_LINK_SESSION_DISCONNECTED;
	GBALinkSessionDeinit(&endpoint->session);
	GBALinkTransportInvalidate(
	    &endpoint->transport,
	    GBA_LINK_REASON_USER_DISCONNECT, NULL);
	GBALinkTransportDeinit(&endpoint->transport);
	endpoint->core->unloadROM(endpoint->core);
	mCoreConfigDeinit(&endpoint->core->config);
	endpoint->core->deinit(endpoint->core);
}

static void _deinitPair(
    struct IntegrationPair* pair) {
	_deinitEndpoint(&pair->client);
	_deinitEndpoint(&pair->host);
}

static void _runScenario(
    unsigned deliveryDelay, unsigned deliveryJitter,
    struct IntegrationPair* pair,
    struct LinkTestResult* hostResult,
    struct LinkTestResult* clientResult) {
	_initPair(
	    pair, deliveryDelay, deliveryJitter);
	_runPairToCompletion(
	    pair, hostResult, clientResult);
	_assertResult(hostResult, 0);
	_assertResult(clientResult, 1);
	assert_int_equal(
	    pair->host.session.state,
	    GBA_LINK_SESSION_READY);
	assert_int_equal(
	    pair->client.session.state,
	    GBA_LINK_SESSION_READY);
}

static void _runReadyLossScenario(
    unsigned deliveryDelay, unsigned deliveryJitter,
    struct IntegrationPair* pair,
    struct LinkTestResult* hostResult,
    struct LinkTestResult* clientResult) {
	_initPair(
	    pair, deliveryDelay, deliveryJitter);
	pair->dropMessage =
	    GBA_LINK_MESSAGE_TRANSFER_READY;
	pair->dropSender =
	    GBA_LINK_ROLE_CLIENT;
	for (unsigned iteration = 0;
	     iteration < 512; ++iteration) {
		_pumpPair(pair);
		uint32_t hostStatus =
		    _resultWord(&pair->host, 2);
		uint32_t clientStatus =
		    _resultWord(&pair->client, 2);
		if ((hostStatus & LINK_RESULT_FAIL) &&
		    (clientStatus & LINK_RESULT_FAIL)) {
			break;
		}
	}
	_readResult(&pair->host, hostResult);
	_readResult(&pair->client, clientResult);
	if (!(hostResult->status & LINK_RESULT_FAIL) ||
	    !(clientResult->status & LINK_RESULT_FAIL)) {
		fprintf(
		    stderr,
		    "ready-loss unsettled: tick=%" PRIu64
		    " host=%08X/%d/%d/%d client=%08X/%d/%d/%d"
		    " client-boundary=%d limit=%" PRIu64
		    " local=%" PRIu64 "\n",
		    pair->tick,
		    hostResult->status,
		    pair->host.session.state,
		    pair->host.driver.transfer.state,
		    pair->host.driver.paused,
		    clientResult->status,
		    pair->client.session.state,
		    pair->client.driver.transfer.state,
		    pair->client.driver.paused,
		    pair->client.driver.boundary,
		    pair->client.driver.executionLimit,
		    pair->client.driver.localCycle);
	}
	assert_true(pair->droppedMessage);
	assert_true(
	    hostResult->status & LINK_RESULT_FAIL);
	assert_true(
	    clientResult->status & LINK_RESULT_FAIL);
	assert_false(GBASIOMultiplayerIsBusy(
	    pair->host.gba->sio.siocnt));
	assert_false(GBASIOMultiplayerIsBusy(
	    pair->client.gba->sio.siocnt));
	assert_true(GBASIOMultiplayerIsError(
	    pair->host.gba->sio.siocnt));
	assert_true(GBASIOMultiplayerIsError(
	    pair->client.gba->sio.siocnt));
	for (unsigned i = 0; i < 4; ++i) {
		assert_int_equal(
		    hostResult->received[i], 0xFFFF);
		assert_int_equal(
		    clientResult->received[i], 0xFFFF);
	}
	assert_int_equal(hostResult->missedIrqs, 0);
	assert_int_equal(clientResult->missedIrqs, 0);
}

M_TEST_DEFINE(twoCoresBootLinkRomAndCompleteAllBauds) {
	struct IntegrationPair* pair =
	    calloc(1, sizeof(*pair));
	assert_non_null(pair);
	struct LinkTestResult hostResult;
	struct LinkTestResult clientResult;
	_runScenario(
	    0, 0, pair,
	    &hostResult, &clientResult);
	assert_true(pair->traceSize > 100);
	_deinitPair(pair);
	free(pair);
}

M_TEST_DEFINE(boundedLatencyAndJitterPreserveLogicalTrace) {
	struct IntegrationPair* immediate =
	    calloc(1, sizeof(*immediate));
	struct IntegrationPair* delayed =
	    calloc(1, sizeof(*delayed));
	assert_non_null(immediate);
	assert_non_null(delayed);
	struct LinkTestResult immediateHost;
	struct LinkTestResult immediateClient;
	struct LinkTestResult delayedHost;
	struct LinkTestResult delayedClient;
	_runScenario(
	    0, 0, immediate,
	    &immediateHost, &immediateClient);
	_runScenario(
	    2, 3, delayed,
	    &delayedHost, &delayedClient);
	assert_memory_equal(
	    &delayedHost, &immediateHost,
	    sizeof(immediateHost));
	assert_memory_equal(
	    &delayedClient, &immediateClient,
	    sizeof(immediateClient));
	assert_int_equal(
	    delayed->traceSize, immediate->traceSize);
	assert_memory_equal(
	    delayed->trace, immediate->trace,
	    immediate->traceSize *
	        sizeof(immediate->trace[0]));
	_deinitPair(delayed);
	_deinitPair(immediate);
	free(delayed);
	free(immediate);
}

M_TEST_DEFINE(postStartReadyLossReplaysAtTheSameErrorCycle) {
	struct IntegrationPair* immediate =
	    calloc(1, sizeof(*immediate));
	struct IntegrationPair* delayed =
	    calloc(1, sizeof(*delayed));
	assert_non_null(immediate);
	assert_non_null(delayed);
	struct LinkTestResult immediateHost;
	struct LinkTestResult immediateClient;
	struct LinkTestResult delayedHost;
	struct LinkTestResult delayedClient;
	_runReadyLossScenario(
	    0, 0, immediate,
	    &immediateHost, &immediateClient);
	_runReadyLossScenario(
	    2, 3, delayed,
	    &delayedHost, &delayedClient);
	assert_memory_equal(
	    &delayedHost, &immediateHost,
	    sizeof(immediateHost));
	assert_memory_equal(
	    &delayedClient, &immediateClient,
	    sizeof(immediateClient));
	assert_int_equal(
	    delayed->host.driver.transfer.completionCycle,
	    immediate->host.driver.transfer.completionCycle);
	assert_int_equal(
	    delayed->client.driver.transfer.completionCycle,
	    immediate->client.driver.transfer.completionCycle);
	assert_int_equal(
	    delayed->traceSize, immediate->traceSize);
	assert_memory_equal(
	    delayed->trace, immediate->trace,
	    immediate->traceSize *
	        sizeof(immediate->trace[0]));
	_deinitPair(delayed);
	_deinitPair(immediate);
	free(delayed);
	free(immediate);
}

M_TEST_SUITE_DEFINE(GBALinkIntegration,
	cmocka_unit_test(
	    twoCoresBootLinkRomAndCompleteAllBauds),
	cmocka_unit_test(
	    boundedLatencyAndJitterPreserveLogicalTrace),
	cmocka_unit_test(
	    postStartReadyLossReplaysAtTheSameErrorCycle))
