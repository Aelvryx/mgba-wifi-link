/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "util/test/suite.h"

#include <mgba/internal/gba/serialize.h>
#include <mgba/internal/gba/sio/netplay/session-v2.h>
#include <mgba-util/sha256.h>

struct V2Endpoint {
	struct GBALinkTransport transport;
	struct GBALinkV2Session session;
	struct V2Endpoint* peer;
	uint64_t nowUs;
	bool quiescent;
	bool paused;
	bool installResult;
	bool pairInstalled;
	bool pairCommitted;
	bool pairDiscarded;
	enum GBALinkV2MessageType dropType;
	enum GBALinkV2MessageType stopType;
	enum GBALinkV2MessageType exhaustType;
	enum GBALinkV2MessageType semanticReplayType;
	enum GBALinkV2MessageType literalReplayType;
	bool corruptChunk;
	bool conflictReplay;
	bool replayed;
	bool stopDuringPoll;
	bool clockFail;
	uint32_t acceptAckDelayMs;
	uint32_t captureDelayMs;
	uint32_t ackDeliveryDelayUs;
	unsigned failClockCall;
	unsigned clockCalls;
	unsigned sentCounts[GBA_LINK_V2_MESSAGE_LATENCY_REPORT + 1];
	unsigned captureCalls;
	unsigned installCalls;
	unsigned commitCalls;
	unsigned discardCalls;
	unsigned failureCalls;
	unsigned stopCalls;
	uint8_t capturedPlayer;
	enum GBALinkV2Reason failureReason;
	uint8_t installedDigests[2][MGBA_SHA256_DIGEST_SIZE];
};

struct V2Pair {
	struct V2Endpoint host;
	struct V2Endpoint client;
};

static bool _sendReliable(
	void* context, const void* data, size_t size, bool flush) {
	struct V2Endpoint* endpoint = context;
	assert_true(flush || size > GBA_LINK_V2_HEADER_SIZE);
	struct GBALinkV2Packet packet;
	enum GBALinkDecodeStatus status = GBALinkV2PacketDecode(
	    data, size, endpoint->session.localRole, &packet);
	assert_int_equal(status, GBA_LINK_DECODE_OK);
	if (packet.header.type == endpoint->stopType) {
		GBALinkTransportInvalidate(
		    &endpoint->transport, GBA_LINK_REASON_TRANSPORT_STOP,
		    "test synchronous stop");
		return true;
	}
	if (packet.header.type == endpoint->dropType) {
		return true;
	}
	if (packet.header.type == GBA_LINK_V2_MESSAGE_ACCEPT_ACK) {
		endpoint->peer->nowUs += endpoint->acceptAckDelayMs * UINT64_C(1000);
	}
	if (packet.header.type == GBA_LINK_V2_MESSAGE_LATENCY_ACK) {
		endpoint->peer->nowUs += endpoint->ackDeliveryDelayUs;
	}
	if (packet.header.type <= GBA_LINK_V2_MESSAGE_LATENCY_REPORT) {
		++endpoint->sentCounts[packet.header.type];
	}
	if (packet.header.type == endpoint->exhaustType) {
		const uint8_t filler = 0xFF;
		while (endpoint->peer->transport.inbound.size <
		       GBA_LINK_MAX_COPIED_PACKETS) {
			assert_true(GBALinkTransportQueueInbound(
			    &endpoint->peer->transport,
			    endpoint->peer->transport.generation,
			    &filler, sizeof(filler)));
		}
		return false;
	}
	if (endpoint->corruptChunk &&
	    packet.header.type == GBA_LINK_V2_MESSAGE_REPLICA_CHUNK &&
	    packet.payload.replicaChunk.offset +
	            packet.payload.replicaChunk.size ==
	        endpoint->session.localBundle.manifest.encodedSize) {
		uint8_t* corrupt = malloc(size);
		assert_non_null(corrupt);
		memcpy(corrupt, data, size);
		corrupt[size - 1] ^= 1;
		bool queued = GBALinkTransportQueueInbound(
		    &endpoint->peer->transport,
		    endpoint->peer->transport.generation, corrupt, size);
		free(corrupt);
		endpoint->corruptChunk = false;
		return queued;
	}
	bool queued = GBALinkTransportQueueInbound(
	    &endpoint->peer->transport,
	    endpoint->peer->transport.generation, data, size);
	if (!queued || endpoint->replayed ||
	    (packet.header.type != endpoint->semanticReplayType &&
	     packet.header.type != endpoint->literalReplayType)) {
		return queued;
	}
	endpoint->replayed = true;
	if (packet.header.type == endpoint->literalReplayType) {
		return GBALinkTransportQueueInbound(
		    &endpoint->peer->transport,
		    endpoint->peer->transport.generation, data, size);
	}
	assert_true(endpoint->session.nextPacketSequence < UINT64_MAX);
	packet.header.packetSequence = endpoint->session.nextPacketSequence++;
	if (endpoint->conflictReplay) {
		switch (packet.header.type) {
		case GBA_LINK_V2_MESSAGE_HELLO:
			++packet.payload.hello.connectionNonce;
			break;
		case GBA_LINK_V2_MESSAGE_LATENCY_PROBE:
			packet.payload.latencyProbe.ordinal ^= 1;
			break;
		case GBA_LINK_V2_MESSAGE_LATENCY_ACK:
			packet.payload.latencyAck.ordinal ^= 1;
			break;
		case GBA_LINK_V2_MESSAGE_LATENCY_REPORT:
			packet.payload.latencyReport.samples[0] ^= 1;
			break;
		default:
			break;
		}
	}
	uint8_t replay[GBA_LINK_V2_MAX_PACKET_SIZE];
	size_t replaySize = 0;
	assert_true(GBALinkV2PacketEncode(
	    &packet, replay, sizeof(replay), &replaySize));
	return GBALinkTransportQueueInbound(
	    &endpoint->peer->transport,
	    endpoint->peer->transport.generation, replay, replaySize);
}

static bool _pollReceive(void* context) {
	struct V2Endpoint* endpoint = context;
	if (endpoint->stopDuringPoll) {
		GBALinkTransportInvalidate(
		    &endpoint->transport, GBA_LINK_REASON_TRANSPORT_STOP,
		    "test stop during receive poll");
	}
	return true;
}

static uint64_t _monotonicTimeMs(void* context) {
	return ((struct V2Endpoint*) context)->nowUs / 1000;
}

static bool _monotonicTimeUs(void* context, uint64_t* timestamp) {
	struct V2Endpoint* endpoint = context;
	++endpoint->clockCalls;
	if (!timestamp || endpoint->clockFail ||
	    (endpoint->failClockCall &&
	     endpoint->clockCalls == endpoint->failClockCall)) {
		return false;
	}
	*timestamp = endpoint->nowUs;
	return true;
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
	++((struct V2Endpoint*) context)->stopCalls;
}

static const struct GBALinkTransportVTable _transportVTable = {
	.sendReliable = _sendReliable,
	.pollReceive = _pollReceive,
	.monotonicTimeMs = _monotonicTimeMs,
	.monotonicTimeUs = _monotonicTimeUs,
	.diagnostic = _diagnostic,
	.stop = _stop,
};

static bool _quiescentBoundary(void* context) {
	return ((struct V2Endpoint*) context)->quiescent;
}

static void _setPaused(void* context, bool paused) {
	((struct V2Endpoint*) context)->paused = paused;
}

static enum GBAReplicaResult _captureReplica(
	void* context, uint8_t player, uint64_t generation,
	enum GBAReplicaEncoding encoding, uint32_t chunkSize,
	struct GBAReplicaBundle* bundle) {
	struct V2Endpoint* endpoint = context;
	++endpoint->captureCalls;
	endpoint->capturedPlayer = player;
	endpoint->nowUs += endpoint->captureDelayMs * UINT64_C(1000);
	if (endpoint->peer) {
		endpoint->peer->nowUs +=
		    endpoint->captureDelayMs * UINT64_C(1000);
	}
	if (encoding != GBA_REPLICA_ENCODING_NONE) {
		return GBA_REPLICA_UNSUPPORTED;
	}
	memset(bundle, 0, sizeof(*bundle));
	bundle->encodedSize = sizeof(struct GBASerializedState);
	bundle->encodedData = malloc(bundle->encodedSize);
	if (!bundle->encodedData) {
		return GBA_REPLICA_ALLOCATION_FAILED;
	}
	for (size_t i = 0; i < bundle->encodedSize; ++i) {
		bundle->encodedData[i] = player * 0x51 + i * 13;
	}
	struct GBAReplicaManifest* manifest = &bundle->manifest;
	manifest->formatVersion = GBA_REPLICA_FORMAT_VERSION;
	manifest->emulationCompatibilityVersion =
	    GBA_REPLICA_EMULATION_COMPATIBILITY_VERSION;
	manifest->generation = generation;
	manifest->player = player;
	manifest->encoding = encoding;
	manifest->stateVersion = GBASavestateMagic + GBASavestateVersion;
	manifest->stateSize = bundle->encodedSize;
	manifest->saveType = GBA_SAVEDATA_FORCE_NONE;
	manifest->rtcType = RTC_NO_OVERRIDE;
	manifest->frameCounter = 10 + player;
	manifest->globalCycles = 1000 + player;
	manifest->uncompressedSize = bundle->encodedSize;
	manifest->encodedSize = bundle->encodedSize;
	manifest->chunkSize = chunkSize;
	sha256Buffer(bundle->encodedData, bundle->encodedSize,
	    manifest->stateDigest);
	sha256Buffer(NULL, 0, manifest->saveDigest);
	memcpy(manifest->uncompressedDigest, manifest->stateDigest,
	    MGBA_SHA256_DIGEST_SIZE);
	memcpy(manifest->encodedDigest, manifest->stateDigest,
	    MGBA_SHA256_DIGEST_SIZE);
	return GBAReplicaManifestValidate(manifest, NULL);
}

static bool _installPair(
	void* context, const struct GBAReplicaManifest manifests[2],
	const struct GBAReplicaPayload payloads[2]) {
	struct V2Endpoint* endpoint = context;
	++endpoint->installCalls;
	assert_int_equal(manifests[0].player, 0);
	assert_int_equal(manifests[1].player, 1);
	assert_int_equal(payloads[0].size, sizeof(struct GBASerializedState));
	assert_int_equal(payloads[1].size, sizeof(struct GBASerializedState));
	for (unsigned i = 0; i < 2; ++i) {
		memcpy(endpoint->installedDigests[i],
		    manifests[i].uncompressedDigest, MGBA_SHA256_DIGEST_SIZE);
	}
	endpoint->pairInstalled = endpoint->installResult;
	return endpoint->installResult;
}

static bool _commitPair(void* context) {
	struct V2Endpoint* endpoint = context;
	assert_true(endpoint->pairInstalled);
	++endpoint->commitCalls;
	endpoint->pairCommitted = true;
	return true;
}

static void _discardPair(void* context, bool committed) {
	struct V2Endpoint* endpoint = context;
	++endpoint->discardCalls;
	endpoint->pairDiscarded = true;
	assert_int_equal(committed, endpoint->pairCommitted);
}

static bool _runtimePacket(
	void* context, const struct GBALinkV2Packet* packet) {
	UNUSED(context);
	return packet &&
	       (packet->header.type == GBA_LINK_V2_MESSAGE_INPUT_BATCH ||
	        packet->header.type == GBA_LINK_V2_MESSAGE_STATE_CHECK);
}

static void _failed(void* context, enum GBALinkV2Reason reason) {
	struct V2Endpoint* endpoint = context;
	++endpoint->failureCalls;
	endpoint->failureReason = reason;
}

static const struct GBALinkV2SessionCallbacks _callbacks = {
	.quiescentBoundary = _quiescentBoundary,
	.setPaused = _setPaused,
	.captureReplica = _captureReplica,
	.installPair = _installPair,
	.commitPair = _commitPair,
	.discardPair = _discardPair,
	.runtimePacket = _runtimePacket,
	.failed = _failed,
};

static struct GBALinkV2SessionConfig _config(
	struct V2Endpoint* endpoint, uint8_t romSeed) {
	struct GBALinkV2SessionConfig config;
	memset(&config, 0, sizeof(config));
	config.identity.romSize = 0x100000;
	for (unsigned i = 0; i < GBA_LINK_ROM_SHA1_SIZE; ++i) {
		config.identity.romSha1[i] = romSeed + i;
	}
	config.capabilities = GBA_LINK_V2_REQUIRED_CAPABILITIES;
	config.supportedEncodings = GBA_LINK_V2_ENCODING_NONE;
	config.emulationCompatibilityVersion =
	    GBA_REPLICA_EMULATION_COMPATIBILITY_VERSION;
	config.maxChunkSize = GBA_REPLICA_DEFAULT_CHUNK_SIZE;
	config.minimumInputDelay = 2;
	config.maximumInputDelay = 8;
	config.estimatedJitterMs = 5;
	config.experimentalRuntime = true;
	config.productPolicy = GBA_LINK_V2_PRODUCT_STABLE;
	struct GBALinkV2DeterminismProfileInput profile = {
		.biosMode = GBA_LINK_V2_BIOS_HLE,
		.emulationCompatibilityVersion = 1,
		.overclockQ16 = 0x10000,
		.idlePolicy = GBA_LINK_V2_IDLE_DETECT,
		.allowOpposingDirections = true,
		.rtcNormalizationPolicyVersion = 1,
		.fakeEpochArithmeticVersion = 1,
		.rtcSemanticsModelVersion = 1,
		.authoritativeInputFormatVersion = 1,
		.cartridgeRequiredInputMask = GBA_LINK_V2_INPUT_DIGITAL,
	};
	assert_true(GBALinkV2DeterminismProfileBuild(
	    &profile, &config.determinismProfile));
	config.deterministicCapabilities =
	    (struct GBALinkV2DeterminismCapabilities) {
		.supportedRtcSourceMask = GBA_LINK_V2_RTC_SOURCE_KNOWN_MASK,
		.timeSemanticsCapabilityMask =
		    GBA_LINK_V2_TIME_SIGNED_64BIT_TIME_T_V1,
		.authoritativePlayerRtcSource = RTC_NO_OVERRIDE,
		.synchronizedInputCapabilityMask = GBA_LINK_V2_INPUT_DIGITAL,
	};
	config.cartridgeRequiredInputMask = GBA_LINK_V2_INPUT_DIGITAL;
	GBALinkV2DeadlinePolicyInit(&config.deadlines);
	config.callbacks = &_callbacks;
	config.callbackContext = endpoint;
	return config;
}

static void _initEndpoint(
	struct V2Endpoint* endpoint, enum GBALinkRole role) {
	endpoint->quiescent = true;
	endpoint->installResult = true;
	GBALinkTransportInit(
	    &endpoint->transport, &_transportVTable, endpoint);
	assert_true(GBALinkTransportStart(&endpoint->transport, 1, role));
	GBALinkV2SessionInit(&endpoint->session, &endpoint->transport);
}

static void _initPair(struct V2Pair* pair) {
	memset(pair, 0, sizeof(*pair));
	pair->host.peer = &pair->client;
	pair->client.peer = &pair->host;
	_initEndpoint(&pair->host, GBA_LINK_ROLE_HOST);
	_initEndpoint(&pair->client, GBA_LINK_ROLE_CLIENT);
	struct GBALinkV2SessionConfig host = _config(&pair->host, 1);
	struct GBALinkV2SessionConfig client = _config(&pair->client, 1);
	assert_true(GBALinkV2SessionConfigure(&pair->host.session, &host));
	assert_true(GBALinkV2SessionConfigure(&pair->client.session, &client));
}

static void _startPair(struct V2Pair* pair) {
	assert_true(GBALinkV2SessionStart(
	    &pair->host.session, 1, GBA_LINK_ROLE_HOST));
	assert_true(GBALinkV2SessionStart(
	    &pair->client.session, 1, GBA_LINK_ROLE_CLIENT));
}

static void _pump(struct V2Pair* pair, unsigned count) {
	for (unsigned i = 0; i < count; ++i) {
		if (GBALinkV2SessionIsLive(&pair->host.session)) {
			GBALinkV2SessionUpdate(&pair->host.session, false);
		}
		if (GBALinkV2SessionIsLive(&pair->client.session)) {
			GBALinkV2SessionUpdate(&pair->client.session, false);
		}
	}
}

static void _advance(struct V2Pair* pair, uint64_t milliseconds) {
	pair->host.nowUs += milliseconds * UINT64_C(1000);
	pair->client.nowUs += milliseconds * UINT64_C(1000);
	_pump(pair, 2);
}

static void _deinitPair(struct V2Pair* pair) {
	GBALinkV2SessionDeinit(&pair->client.session);
	GBALinkV2SessionDeinit(&pair->host.session);
	GBALinkTransportDeinit(&pair->client.transport);
	GBALinkTransportDeinit(&pair->host.transport);
}

M_TEST_DEFINE(bilateralBundlesInstallInCanonicalOrderAndReleaseAtomically) {
	struct V2Pair pair;
	_initPair(&pair);
	_startPair(&pair);
	_pump(&pair, 64);
	assert_int_equal(pair.host.session.state, GBA_LINK_V2_SESSION_READY);
	assert_int_equal(pair.client.session.state, GBA_LINK_V2_SESSION_READY);
	assert_false(pair.host.paused);
	assert_false(pair.client.paused);
	assert_int_equal(pair.host.captureCalls, 1);
	assert_int_equal(pair.client.captureCalls, 1);
	assert_int_equal(pair.host.capturedPlayer, 0);
	assert_int_equal(pair.client.capturedPlayer, 1);
	assert_int_equal(pair.host.installCalls, 1);
	assert_int_equal(pair.client.installCalls, 1);
	assert_int_equal(pair.host.commitCalls, 1);
	assert_int_equal(pair.client.commitCalls, 1);
	assert_memory_equal(pair.host.installedDigests,
	    pair.client.installedDigests, sizeof(pair.host.installedDigests));
	assert_int_equal(pair.host.session.inputDelay, 2);
	assert_int_equal(pair.client.session.inputDelay, 2);
	_deinitPair(&pair);
}

M_TEST_DEFINE(acceptAckDelayDoesNotAffectCalibratedInputDelay) {
	struct V2Pair pair;
	_initPair(&pair);
	pair.client.acceptAckDelayMs = 40;
	_startPair(&pair);
	_pump(&pair, 64);
	assert_int_equal(pair.host.session.state, GBA_LINK_V2_SESSION_READY);
	assert_int_equal(pair.client.session.state, GBA_LINK_V2_SESSION_READY);
	assert_int_equal(pair.host.session.handshakeRoundTripMs, 0);
	assert_int_equal(pair.host.session.inputDelay, 2);
	assert_int_equal(pair.client.session.inputDelay, 2);
	assert_int_equal(pair.host.session.overlappingMinimumInputDelay, 2);
	assert_int_equal(pair.host.session.overlappingMaximumInputDelay, 8);
	_deinitPair(&pair);
}

M_TEST_DEFINE(replicaCaptureCannotInflateCalibratedInputDelay) {
	struct V2Pair pair;
	_initPair(&pair);
	pair.host.captureDelayMs = 20;
	pair.client.captureDelayMs = 20;
	_startPair(&pair);
	_pump(&pair, 64);
	assert_int_equal(pair.host.session.state, GBA_LINK_V2_SESSION_READY);
	assert_int_equal(pair.client.session.state, GBA_LINK_V2_SESSION_READY);
	assert_int_equal(pair.host.session.handshakeRoundTripMs, 0);
	assert_int_equal(pair.host.session.inputDelay, 2);
	assert_int_equal(pair.client.session.inputDelay, 2);
	_deinitPair(&pair);
}

M_TEST_DEFINE(calibrationSamplesBothEndpointCallbackDirections) {
	struct V2Pair pair;
	_initPair(&pair);
	pair.client.ackDeliveryDelayUs = 3000;
	pair.host.ackDeliveryDelayUs = 7000;
	_startPair(&pair);
	_pump(&pair, 64);
	assert_int_equal(pair.host.session.state, GBA_LINK_V2_SESSION_READY);
	assert_int_equal(pair.client.session.state, GBA_LINK_V2_SESSION_READY);
	for (unsigned i = 0; i < GBA_LINK_CALIBRATION_PROBES_PER_ROLE; ++i) {
		assert_int_equal(pair.host.session.calibration.samples[i], 3000);
		assert_int_equal(pair.client.session.calibration.samples[i], 3000);
		assert_int_equal(
		    pair.host.session.calibration.samples[
		        GBA_LINK_CALIBRATION_PROBES_PER_ROLE + i],
		    7000);
		assert_int_equal(
		    pair.client.session.calibration.samples[
		        GBA_LINK_CALIBRATION_PROBES_PER_ROLE + i],
		    7000);
	}
	assert_memory_equal(pair.host.session.selection.digest,
	    pair.client.session.selection.digest,
	    sizeof(pair.host.session.selection.digest));
	_deinitPair(&pair);
}

M_TEST_DEFINE(semanticHelloReplayDoesNotRestartCalibration) {
	struct V2Pair pair;
	_initPair(&pair);
	pair.client.semanticReplayType = GBA_LINK_V2_MESSAGE_HELLO;
	_startPair(&pair);
	_pump(&pair, 64);
	assert_int_equal(pair.host.session.state, GBA_LINK_V2_SESSION_READY);
	assert_int_equal(pair.client.session.state, GBA_LINK_V2_SESSION_READY);
	assert_int_equal(
	    pair.host.sentCounts[GBA_LINK_V2_MESSAGE_CALIBRATION_BEGIN], 1);
	assert_true(pair.client.replayed);
	_deinitPair(&pair);
}

M_TEST_DEFINE(semanticCalibrationReplayIsIdempotent) {
	const enum GBALinkV2MessageType types[] = {
		GBA_LINK_V2_MESSAGE_LATENCY_PROBE,
		GBA_LINK_V2_MESSAGE_LATENCY_ACK,
		GBA_LINK_V2_MESSAGE_LATENCY_REPORT,
	};
	for (unsigned i = 0; i < sizeof(types) / sizeof(*types); ++i) {
		struct V2Pair pair;
		_initPair(&pair);
		struct V2Endpoint* sender = types[i] == GBA_LINK_V2_MESSAGE_LATENCY_ACK
		    ? &pair.client
		    : &pair.host;
		sender->semanticReplayType = types[i];
		_startPair(&pair);
		_pump(&pair, 72);
		assert_true(sender->replayed);
		assert_int_equal(pair.host.session.state, GBA_LINK_V2_SESSION_READY);
		assert_int_equal(pair.client.session.state, GBA_LINK_V2_SESSION_READY);
		assert_int_equal(pair.host.captureCalls, 1);
		assert_int_equal(pair.client.captureCalls, 1);
		_deinitPair(&pair);
	}
}

M_TEST_DEFINE(literalOldPacketSequenceReplayFailsBeforeDispatch) {
	struct V2Pair pair;
	_initPair(&pair);
	pair.client.literalReplayType = GBA_LINK_V2_MESSAGE_HELLO;
	_startPair(&pair);
	_pump(&pair, 4);
	assert_int_equal(pair.host.session.state, GBA_LINK_V2_SESSION_FAILED);
	assert_int_equal(pair.host.failureReason, GBA_LINK_V2_REASON_SEQUENCE);
	assert_int_equal(pair.host.captureCalls + pair.client.captureCalls, 0);
	_deinitPair(&pair);
}

M_TEST_DEFINE(conflictingSemanticReplayFailsClosed) {
	struct V2Pair pair;
	_initPair(&pair);
	pair.client.semanticReplayType = GBA_LINK_V2_MESSAGE_HELLO;
	pair.client.conflictReplay = true;
	_startPair(&pair);
	_pump(&pair, 4);
	assert_int_equal(pair.host.session.state, GBA_LINK_V2_SESSION_FAILED);
	assert_int_equal(
	    pair.host.failureReason, GBA_LINK_V2_REASON_INVALID_TRANSITION);
	assert_int_equal(pair.host.captureCalls + pair.client.captureCalls, 0);
	_deinitPair(&pair);
}

M_TEST_DEFINE(calibrationDeadlineIsAbsoluteAndReportCannotRefreshIt) {
	struct V2Pair pair;
	_initPair(&pair);
	pair.client.dropType = GBA_LINK_V2_MESSAGE_LATENCY_REPORT;
	_startPair(&pair);
	_pump(&pair, 64);
	assert_int_equal(pair.host.session.state, GBA_LINK_V2_SESSION_CLIENT_PROBES);
	assert_int_equal(pair.client.session.state, GBA_LINK_V2_SESSION_WAIT_ACCEPT);
	_advance(&pair, 3000);
	assert_int_equal(pair.host.session.state, GBA_LINK_V2_SESSION_FAILED);
	assert_int_equal(
	    pair.host.failureReason, GBA_LINK_V2_REASON_CALIBRATION_TIMEOUT);
	assert_int_equal(pair.client.session.state, GBA_LINK_V2_SESSION_FAILED);
	assert_int_equal(pair.client.failureReason, GBA_LINK_V2_REASON_ACCEPT_TIMEOUT);
	assert_int_equal(pair.host.captureCalls + pair.client.captureCalls, 0);
	_deinitPair(&pair);
}

M_TEST_DEFINE(fallibleCalibrationClockDistinguishesDeadlineT0AndT1) {
	for (unsigned call = 1; call <= 3; ++call) {
		struct V2Pair pair;
		_initPair(&pair);
		pair.host.failClockCall = call;
		_startPair(&pair);
		_pump(&pair, 8);
		assert_int_equal(pair.host.session.state, GBA_LINK_V2_SESSION_FAILED);
		assert_int_equal(pair.host.failureReason,
		    GBA_LINK_V2_REASON_CALIBRATION_CLOCK_FAILURE);
		assert_int_equal(pair.host.captureCalls + pair.client.captureCalls, 0);
		_deinitPair(&pair);
	}
}

M_TEST_DEFINE(attachmentDeadlineBeginsBeforeQuiescentCapture) {
	struct V2Pair pair;
	_initPair(&pair);
	pair.client.quiescent = false;
	_startPair(&pair);
	assert_int_equal(pair.client.session.state,
	    GBA_LINK_V2_SESSION_WAIT_QUIESCENT);
	assert_false(pair.client.paused);
	assert_int_equal(pair.client.captureCalls, 0);
	_advance(&pair, 3001);
	assert_int_equal(pair.client.session.state, GBA_LINK_V2_SESSION_FAILED);
	assert_int_equal(pair.client.failureReason,
	    GBA_LINK_V2_REASON_ATTACHMENT_TIMEOUT);
	assert_int_equal(pair.client.captureCalls, 0);
	assert_int_equal(pair.client.installCalls, 0);
	_deinitPair(&pair);
}

M_TEST_DEFINE(identityMismatchPreservesBothOriginalCores) {
	struct V2Pair pair;
	_initPair(&pair);
	pair.client.session.config.identity.romSha1[0] ^= 1;
	_startPair(&pair);
	_pump(&pair, 3);
	assert_true(pair.host.failureCalls || pair.client.failureCalls);
	assert_int_equal(pair.host.installCalls + pair.client.installCalls, 0);
	assert_int_equal(pair.host.commitCalls + pair.client.commitCalls, 0);
	assert_int_equal(pair.host.discardCalls + pair.client.discardCalls, 0);
	_deinitPair(&pair);
}

M_TEST_DEFINE(experimentalPolicyMismatchFailsBeforeReplicaExchange) {
	struct V2Pair pair;
	_initPair(&pair);
	pair.client.session.config.experimentalRuntime = false;
	_startPair(&pair);
	_pump(&pair, 3);
	assert_true(pair.host.failureCalls || pair.client.failureCalls);
	assert_true(
	    pair.host.failureReason == GBA_LINK_V2_REASON_RUNTIME_MISMATCH ||
	    pair.client.failureReason == GBA_LINK_V2_REASON_RUNTIME_MISMATCH);
	assert_int_equal(pair.host.installCalls + pair.client.installCalls, 0);
	assert_int_equal(pair.host.captureCalls + pair.client.captureCalls, 0);
	_deinitPair(&pair);
}

M_TEST_DEFINE(profileMismatchFailsBeforeCalibrationOrReplicaExchange) {
	struct V2Pair pair;
	_initPair(&pair);
	pair.client.session.config.determinismProfile.records[3].digest[0] ^= 1;
	_startPair(&pair);
	_pump(&pair, 4);
	assert_int_equal(pair.host.session.state, GBA_LINK_V2_SESSION_FAILED);
	assert_int_equal(
	    pair.host.failureReason, GBA_LINK_V2_REASON_PROFILE_CATEGORY);
	assert_int_equal(pair.host.sentCounts[
	    GBA_LINK_V2_MESSAGE_CALIBRATION_BEGIN], 0);
	assert_int_equal(pair.host.captureCalls + pair.client.captureCalls, 0);
	_deinitPair(&pair);
}

M_TEST_DEFINE(requiredExternalInputMustBeSynchronizedByBothPeers) {
	struct V2Pair pair;
	_initPair(&pair);
	struct GBALinkV2DeterminismProfileInput profile = {
		.biosMode = GBA_LINK_V2_BIOS_HLE,
		.emulationCompatibilityVersion = 1,
		.overclockQ16 = 0x10000,
		.idlePolicy = GBA_LINK_V2_IDLE_DETECT,
		.allowOpposingDirections = true,
		.rtcNormalizationPolicyVersion = 1,
		.fakeEpochArithmeticVersion = 1,
		.rtcSemanticsModelVersion = 1,
		.authoritativeInputFormatVersion = 1,
		.cartridgeRequiredInputMask =
		    GBA_LINK_V2_INPUT_DIGITAL | GBA_LINK_V2_INPUT_TILT,
	};
	assert_true(GBALinkV2DeterminismProfileBuild(
	    &profile, &pair.host.session.config.determinismProfile));
	pair.client.session.config.determinismProfile =
	    pair.host.session.config.determinismProfile;
	pair.host.session.config.cartridgeRequiredInputMask =
	    profile.cartridgeRequiredInputMask;
	pair.client.session.config.cartridgeRequiredInputMask =
	    profile.cartridgeRequiredInputMask;
	pair.host.session.config.deterministicCapabilities
	    .synchronizedInputCapabilityMask = profile.cartridgeRequiredInputMask;
	_startPair(&pair);
	_pump(&pair, 4);
	assert_int_equal(pair.host.session.state, GBA_LINK_V2_SESSION_FAILED);
	assert_int_equal(
	    pair.host.failureReason, GBA_LINK_V2_REASON_EXTERNAL_INPUT);
	assert_int_equal(pair.host.captureCalls + pair.client.captureCalls, 0);
	_deinitPair(&pair);
}

M_TEST_DEFINE(rtcContentRequiresCommonTimeSemanticsBeforeCalibration) {
	struct V2Pair pair;
	_initPair(&pair);
	pair.host.session.config.deterministicCapabilities.contentRequiresRtc =
	    true;
	pair.client.session.config.deterministicCapabilities.contentRequiresRtc =
	    true;
	pair.client.session.config.deterministicCapabilities
	    .timeSemanticsCapabilityMask = 0;
	_startPair(&pair);
	_pump(&pair, 4);
	assert_int_equal(pair.host.session.state, GBA_LINK_V2_SESSION_FAILED);
	assert_int_equal(
	    pair.host.failureReason, GBA_LINK_V2_REASON_RTC_TIME_SEMANTICS);
	assert_int_equal(pair.host.captureCalls + pair.client.captureCalls, 0);
	_deinitPair(&pair);
}

M_TEST_DEFINE(productPolicyNegotiatesStricterFloorAndThenFreezes) {
	struct V2Pair pair;
	_initPair(&pair);
	pair.host.session.config.productPolicy = GBA_LINK_V2_PRODUCT_LOW_LATENCY;
	pair.host.session.config.minimumInputDelay = 1;
	pair.client.session.config.productPolicy = GBA_LINK_V2_PRODUCT_LOW_LATENCY;
	pair.client.session.config.minimumInputDelay = 1;
	_startPair(&pair);
	_pump(&pair, 64);
	assert_int_equal(pair.host.session.productPolicy,
	    GBA_LINK_V2_PRODUCT_LOW_LATENCY);
	assert_int_equal(pair.client.session.productPolicy,
	    GBA_LINK_V2_PRODUCT_LOW_LATENCY);
	assert_int_equal(pair.host.session.inputDelay, 1);
	assert_int_equal(pair.client.session.inputDelay, 1);
	pair.client.session.config.productPolicy = GBA_LINK_V2_PRODUCT_STABLE;
	pair.client.session.config.minimumInputDelay = 2;
	assert_int_equal(pair.client.session.inputDelay, 1);
	_deinitPair(&pair);

	_initPair(&pair);
	pair.host.session.config.productPolicy = GBA_LINK_V2_PRODUCT_LOW_LATENCY;
	pair.host.session.config.minimumInputDelay = 1;
	_startPair(&pair);
	_pump(&pair, 64);
	assert_int_equal(
	    pair.host.session.productPolicy, GBA_LINK_V2_PRODUCT_STABLE);
	assert_int_equal(pair.host.session.inputDelay, 2);
	assert_int_equal(pair.client.session.inputDelay, 2);
	_deinitPair(&pair);
}

M_TEST_DEFINE(corruptReplicaFailsBeforeProvisionalInstallation) {
	struct V2Pair pair;
	_initPair(&pair);
	pair.client.corruptChunk = true;
	_startPair(&pair);
	_pump(&pair, 64);
	assert_int_equal(pair.host.session.state, GBA_LINK_V2_SESSION_FAILED);
	assert_int_equal(pair.host.failureReason,
	    GBA_LINK_V2_REASON_REPLICA_INVALID);
	assert_int_equal(pair.host.installCalls, 0);
	assert_int_equal(pair.host.commitCalls, 0);
	_deinitPair(&pair);
}

M_TEST_DEFINE(missingFinalAckDiscardsOnlyProvisionalPairs) {
	struct V2Pair pair;
	_initPair(&pair);
	pair.client.dropType = GBA_LINK_V2_MESSAGE_SESSION_READY_ACK;
	_startPair(&pair);
	_pump(&pair, 64);
	assert_true(pair.host.pairInstalled);
	assert_true(pair.client.pairInstalled);
	assert_false(pair.host.pairCommitted);
	assert_false(pair.client.pairCommitted);
	_advance(&pair, 3001);
	assert_true(pair.host.pairDiscarded);
	assert_true(pair.client.pairDiscarded);
	assert_int_equal(pair.host.commitCalls + pair.client.commitCalls, 0);
	_deinitPair(&pair);
}

M_TEST_DEFINE(synchronousStopAtEveryReplicaBoundaryFailsClosed) {
	const enum GBALinkV2MessageType boundaries[] = {
		GBA_LINK_V2_MESSAGE_REPLICA_MANIFEST,
		GBA_LINK_V2_MESSAGE_REPLICA_CHUNK,
		GBA_LINK_V2_MESSAGE_REPLICA_INSTALLED,
		GBA_LINK_V2_MESSAGE_SESSION_READY,
		GBA_LINK_V2_MESSAGE_SESSION_READY_ACK,
		GBA_LINK_V2_MESSAGE_INPUT_WINDOW,
	};
	for (unsigned i = 0; i < sizeof(boundaries) / sizeof(*boundaries); ++i) {
		struct V2Pair pair;
		_initPair(&pair);
		struct V2Endpoint* sender =
		    boundaries[i] == GBA_LINK_V2_MESSAGE_SESSION_READY_ACK
		        ? &pair.client
		        : &pair.host;
		sender->stopType = boundaries[i];
		_startPair(&pair);
		_pump(&pair, 64);
		assert_true(sender->failureCalls);
		assert_false(sender->pairCommitted);
		_deinitPair(&pair);
	}
}

M_TEST_DEFINE(queueExhaustionDuringManifestSendFailsClosed) {
	struct V2Pair pair;
	_initPair(&pair);
	pair.client.exhaustType = GBA_LINK_V2_MESSAGE_REPLICA_MANIFEST;
	_startPair(&pair);
	_pump(&pair, 64);
	assert_int_equal(pair.client.session.state, GBA_LINK_V2_SESSION_FAILED);
	assert_int_equal(pair.client.failureReason,
	    GBA_LINK_V2_REASON_TRANSPORT_STOP);
	assert_int_equal(pair.client.commitCalls, 0);
	assert_int_equal(pair.host.commitCalls, 0);
	_deinitPair(&pair);
}

M_TEST_DEFINE(pairInstallationFailureRetainsOriginalAndStopsSession) {
	struct V2Pair pair;
	_initPair(&pair);
	pair.host.installResult = false;
	_startPair(&pair);
	_pump(&pair, 64);
	assert_int_equal(pair.host.session.state, GBA_LINK_V2_SESSION_FAILED);
	assert_int_equal(pair.host.failureReason,
	    GBA_LINK_V2_REASON_INSTALL_FAILED);
	assert_int_equal(pair.host.installCalls, 1);
	assert_int_equal(pair.host.commitCalls, 0);
	assert_int_equal(pair.host.discardCalls, 0);
	_deinitPair(&pair);
}

M_TEST_DEFINE(synchronousStopDuringReceivePollInvalidatesGeneration) {
	struct V2Pair pair;
	_initPair(&pair);
	_startPair(&pair);
	pair.host.stopDuringPoll = true;
	assert_false(GBALinkV2SessionUpdate(&pair.host.session, true));
	assert_int_equal(pair.host.session.state, GBA_LINK_V2_SESSION_FAILED);
	assert_int_equal(pair.host.failureReason,
	    GBA_LINK_V2_REASON_TRANSPORT_STOP);
	assert_false(pair.host.transport.active);
	assert_int_equal(pair.host.captureCalls, 0);
	assert_int_equal(pair.host.commitCalls, 0);
	_deinitPair(&pair);
}

M_TEST_DEFINE(runtimeInputDeadlineFailsWithSpecificReason) {
	struct V2Pair pair;
	_initPair(&pair);
	_startPair(&pair);
	_pump(&pair, 64);
	assert_int_equal(
	    pair.host.session.state, GBA_LINK_V2_SESSION_READY);

	struct GBALinkV2Packet packet;
	memset(&packet, 0, sizeof(packet));
	packet.header.type = GBA_LINK_V2_MESSAGE_INPUT_BATCH;
	packet.payload.inputBatch.snapshotGeneration =
	    pair.host.session.snapshotGeneration;
	packet.payload.inputBatch.player = 0;
	packet.payload.inputBatch.count = 1;
	packet.payload.inputBatch.records[0].frame =
	    pair.host.session.firstFrame;
	assert_true(GBALinkV2SessionSendRuntime(
	    &pair.host.session, &packet,
	    GBA_LINK_V2_DEADLINE_INPUT));
	pair.host.nowUs += UINT64_C(3001000);
	assert_false(GBALinkV2SessionUpdate(
	    &pair.host.session, false));
	assert_int_equal(
	    pair.host.session.state, GBA_LINK_V2_SESSION_FAILED);
	assert_int_equal(
	    pair.host.failureReason, GBA_LINK_V2_REASON_INPUT_TIMEOUT);
	_deinitPair(&pair);
}

M_TEST_SUITE_DEFINE(GBALinkSessionV2,
	cmocka_unit_test(bilateralBundlesInstallInCanonicalOrderAndReleaseAtomically),
	cmocka_unit_test(acceptAckDelayDoesNotAffectCalibratedInputDelay),
	cmocka_unit_test(replicaCaptureCannotInflateCalibratedInputDelay),
	cmocka_unit_test(calibrationSamplesBothEndpointCallbackDirections),
	cmocka_unit_test(semanticHelloReplayDoesNotRestartCalibration),
	cmocka_unit_test(semanticCalibrationReplayIsIdempotent),
	cmocka_unit_test(literalOldPacketSequenceReplayFailsBeforeDispatch),
	cmocka_unit_test(conflictingSemanticReplayFailsClosed),
	cmocka_unit_test(calibrationDeadlineIsAbsoluteAndReportCannotRefreshIt),
	cmocka_unit_test(fallibleCalibrationClockDistinguishesDeadlineT0AndT1),
	cmocka_unit_test(attachmentDeadlineBeginsBeforeQuiescentCapture),
	cmocka_unit_test(identityMismatchPreservesBothOriginalCores),
	cmocka_unit_test(
	    experimentalPolicyMismatchFailsBeforeReplicaExchange),
	cmocka_unit_test(profileMismatchFailsBeforeCalibrationOrReplicaExchange),
	cmocka_unit_test(requiredExternalInputMustBeSynchronizedByBothPeers),
	cmocka_unit_test(rtcContentRequiresCommonTimeSemanticsBeforeCalibration),
	cmocka_unit_test(productPolicyNegotiatesStricterFloorAndThenFreezes),
	cmocka_unit_test(corruptReplicaFailsBeforeProvisionalInstallation),
	cmocka_unit_test(missingFinalAckDiscardsOnlyProvisionalPairs),
	cmocka_unit_test(synchronousStopAtEveryReplicaBoundaryFailsClosed),
	cmocka_unit_test(queueExhaustionDuringManifestSendFailsClosed),
	cmocka_unit_test(pairInstallationFailureRetainsOriginalAndStopsSession),
	cmocka_unit_test(synchronousStopDuringReceivePollInvalidatesGeneration),
	cmocka_unit_test(runtimeInputDeadlineFailsWithSpecificReason))
