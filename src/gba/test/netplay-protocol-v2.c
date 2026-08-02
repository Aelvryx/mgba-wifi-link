/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "util/test/suite.h"

#include <mgba/internal/gba/serialize.h>
#include <mgba/internal/gba/sio/netplay/protocol-v2.h>
#include <mgba/internal/gba/sio/netplay/transport.h>

enum {
	TEST_SESSION = 0x1234,
	TEST_GENERATION = 0x5678,
};

static struct GBALinkV2DeterminismProfile _profile(void) {
	struct GBALinkV2DeterminismProfileInput input = {
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
	struct GBALinkV2DeterminismProfile profile;
	assert_true(GBALinkV2DeterminismProfileBuild(&input, &profile));
	return profile;
}

static struct GBALinkV2CalibrationSummary _summary(void) {
	struct GBALinkV2CalibrationSummary summary = {
		.generation = TEST_GENERATION,
		.selectorPolicyVersion = GBA_LINK_INPUT_SELECTOR_POLICY_VERSION,
		.minimumRttUs = 1000,
		.p50RttUs = 2000,
		.p95RttUs = 3000,
		.maximumRttUs = 4000,
		.overlappingMinimum = 2,
		.overlappingMaximum = 8,
		.productionFloor = 2,
		.selectionReason = GBA_LINK_V2_SELECTION_RAISED_TO_MINIMUM,
	};
	memset(summary.vectorDigest, 0x45, sizeof(summary.vectorDigest));
	return summary;
}

static struct GBALinkV2Packet _packet(
	enum GBALinkV2MessageType type, enum GBALinkRole role) {
	struct GBALinkV2Packet packet;
	memset(&packet, 0, sizeof(packet));
	packet.header.type = type;
	packet.header.packetSequence = 7;
	if (type != GBA_LINK_V2_MESSAGE_HELLO &&
	    type != GBA_LINK_V2_MESSAGE_REJECT) {
		packet.header.sessionId = TEST_SESSION;
	}
	switch (type) {
	case GBA_LINK_V2_MESSAGE_HELLO:
		packet.payload.hello.capabilities =
		    GBA_LINK_V2_REQUIRED_CAPABILITIES;
		packet.payload.hello.requiredCapabilities =
		    GBA_LINK_V2_REQUIRED_CAPABILITIES;
		packet.payload.hello.romSize = 0x100000;
		memset(packet.payload.hello.romSha1, 0x21,
		    sizeof(packet.payload.hello.romSha1));
		packet.payload.hello.emulationCompatibilityVersion =
		    GBA_REPLICA_EMULATION_COMPATIBILITY_VERSION;
		packet.payload.hello.runtimeCompatibilityVersion =
		    GBA_LINK_V2_RUNTIME_COMPATIBILITY_VERSION;
		packet.payload.hello.maxChunkSize = GBA_REPLICA_DEFAULT_CHUNK_SIZE;
		packet.payload.hello.supportedEncodings = GBA_LINK_V2_ENCODING_NONE;
		packet.payload.hello.minimumInputDelay = 2;
		packet.payload.hello.maximumInputDelay = 8;
		packet.payload.hello.experimentalRuntime = true;
		packet.payload.hello.connectionNonce = 0x1111;
		packet.payload.hello.productPolicy = GBA_LINK_V2_PRODUCT_STABLE;
		packet.payload.hello.profile = _profile();
		packet.payload.hello.deterministicCapabilities =
		    (struct GBALinkV2DeterminismCapabilities) {
			.supportedRtcSourceMask = GBA_LINK_V2_RTC_SOURCE_KNOWN_MASK,
			.timeSemanticsCapabilityMask =
			    GBA_LINK_V2_TIME_SIGNED_64BIT_TIME_T_V1,
			.authoritativePlayerRtcSource = RTC_NO_OVERRIDE,
			.synchronizedInputCapabilityMask = GBA_LINK_V2_INPUT_DIGITAL,
		};
		break;
	case GBA_LINK_V2_MESSAGE_ACCEPT:
		packet.payload.accept.proposedSessionId = TEST_SESSION;
		packet.payload.accept.snapshotGeneration = TEST_GENERATION;
		packet.payload.accept.hostTransportId = 0;
		packet.payload.accept.clientTransportId = 1;
		packet.payload.accept.runtimeCompatibilityVersion =
		    GBA_LINK_V2_RUNTIME_COMPATIBILITY_VERSION;
		packet.payload.accept.selectedChunkSize =
		    GBA_REPLICA_DEFAULT_CHUNK_SIZE;
		packet.payload.accept.selectedEncoding = GBA_REPLICA_ENCODING_NONE;
		packet.payload.accept.inputDelay = 4;
		packet.payload.accept.productPolicy = GBA_LINK_V2_PRODUCT_STABLE;
		packet.payload.accept.calibration = _summary();
		break;
	case GBA_LINK_V2_MESSAGE_ACCEPT_ACK:
		packet.payload.acceptAck.acceptedSessionId = TEST_SESSION;
		packet.payload.acceptAck.snapshotGeneration = TEST_GENERATION;
		break;
	case GBA_LINK_V2_MESSAGE_REPLICA_MANIFEST: {
		struct GBAReplicaManifest manifest;
		memset(&manifest, 0, sizeof(manifest));
		manifest.formatVersion = GBA_REPLICA_FORMAT_VERSION;
		manifest.emulationCompatibilityVersion =
		    GBA_REPLICA_EMULATION_COMPATIBILITY_VERSION;
		manifest.generation = TEST_GENERATION;
		manifest.player = role == GBA_LINK_ROLE_HOST ? 0 : 1;
		manifest.encoding = GBA_REPLICA_ENCODING_NONE;
		manifest.stateVersion = GBASavestateMagic + GBASavestateVersion;
		manifest.stateSize = sizeof(struct GBASerializedState);
		manifest.saveType = GBA_SAVEDATA_FORCE_NONE;
		manifest.rtcType = RTC_NO_OVERRIDE;
		manifest.uncompressedSize = manifest.stateSize;
		manifest.encodedSize = manifest.stateSize;
		manifest.chunkSize = GBA_REPLICA_DEFAULT_CHUNK_SIZE;
		assert_int_equal(
		    GBAReplicaManifestEncode(
		        &manifest, packet.payload.replicaManifest.encoded),
		    GBA_REPLICA_OK);
		packet.payload.replicaManifest.snapshotGeneration = TEST_GENERATION;
		packet.payload.replicaManifest.player = manifest.player;
		break;
	}
	case GBA_LINK_V2_MESSAGE_REPLICA_CHUNK: {
		static const uint8_t bytes[] = { 1, 2, 3, 4, 5 };
		packet.payload.replicaChunk.snapshotGeneration = TEST_GENERATION;
		packet.payload.replicaChunk.player =
		    role == GBA_LINK_ROLE_HOST ? 0 : 1;
		packet.payload.replicaChunk.offset = 16;
		packet.payload.replicaChunk.size = sizeof(bytes);
		packet.payload.replicaChunk.data = bytes;
		break;
	}
	case GBA_LINK_V2_MESSAGE_REPLICA_INSTALLED:
		packet.payload.replicaInstalled.snapshotGeneration = TEST_GENERATION;
		packet.payload.replicaInstalled.installed = true;
		memset(packet.payload.replicaInstalled.playerDigests, 0x42,
		    sizeof(packet.payload.replicaInstalled.playerDigests));
		break;
	case GBA_LINK_V2_MESSAGE_SESSION_READY:
	case GBA_LINK_V2_MESSAGE_SESSION_READY_ACK:
		packet.payload.sessionReady.snapshotGeneration = TEST_GENERATION;
		packet.payload.sessionReady.firstFrame = 100;
		packet.payload.sessionReady.policy =
		    GBA_LINK_V2_READY_EXACT_ROM |
		    GBA_LINK_V2_READY_FIXED_DELAY |
		    GBA_LINK_V2_READY_BILATERAL_INSTALL;
		packet.payload.sessionReady.inputDelay = 4;
		packet.payload.sessionReady.productPolicy = GBA_LINK_V2_PRODUCT_STABLE;
		packet.payload.sessionReady.calibration = _summary();
		memset(packet.payload.sessionReady.playerDigests, 0x43,
		    sizeof(packet.payload.sessionReady.playerDigests));
		break;
	case GBA_LINK_V2_MESSAGE_INPUT_WINDOW:
		packet.payload.inputWindow.snapshotGeneration = TEST_GENERATION;
		packet.payload.inputWindow.firstFrame = 100;
		packet.payload.inputWindow.frameCount = 8;
		packet.payload.inputWindow.inputDelay = 4;
		break;
	case GBA_LINK_V2_MESSAGE_INPUT_BATCH:
		packet.payload.inputBatch.snapshotGeneration = TEST_GENERATION;
		packet.payload.inputBatch.player =
		    role == GBA_LINK_ROLE_HOST ? 0 : 1;
		packet.payload.inputBatch.count = 3;
		for (unsigned i = 0; i < packet.payload.inputBatch.count; ++i) {
			packet.payload.inputBatch.records[i].frame = 100 + i;
			packet.payload.inputBatch.records[i].keys = 1 << i;
		}
		break;
	case GBA_LINK_V2_MESSAGE_STATE_CHECK:
		packet.payload.stateCheck.snapshotGeneration = TEST_GENERATION;
		packet.payload.stateCheck.frame = 120;
		packet.payload.stateCheck.player =
		    role == GBA_LINK_ROLE_HOST ? 0 : 1;
		memset(packet.payload.stateCheck.playerDigests, 0x44,
		    sizeof(packet.payload.stateCheck.playerDigests));
		break;
	case GBA_LINK_V2_MESSAGE_DETACH:
	case GBA_LINK_V2_MESSAGE_DETACH_ACK:
		packet.payload.reason.snapshotGeneration = TEST_GENERATION;
		packet.payload.reason.reason = GBA_LINK_V2_REASON_USER_DISCONNECT;
		break;
	case GBA_LINK_V2_MESSAGE_REJECT:
		packet.payload.reason.reason = GBA_LINK_V2_REASON_ROM_MISMATCH;
		break;
	case GBA_LINK_V2_MESSAGE_CALIBRATION_BEGIN:
		packet.payload.calibrationBegin =
		    (struct GBALinkV2CalibrationBegin) {
			.generation = TEST_GENERATION,
			.hostConnectionNonce = 0x1111,
			.clientConnectionNonce = 0x2222,
			.probeCount = GBA_LINK_CALIBRATION_PROBES_PER_ROLE,
			.unit = GBA_LINK_INPUT_LATENCY_UNIT_MICROSECONDS,
			.calibrationPolicyVersion = GBA_LINK_CALIBRATION_POLICY_VERSION,
			.selectorPolicyVersion = GBA_LINK_INPUT_SELECTOR_POLICY_VERSION,
		};
		break;
	case GBA_LINK_V2_MESSAGE_LATENCY_PROBE:
		packet.payload.latencyProbe = (struct GBALinkV2LatencyProbe) {
			.generation = TEST_GENERATION,
			.originRole = role,
			.ordinal = 3,
			.unit = GBA_LINK_INPUT_LATENCY_UNIT_MICROSECONDS,
		};
		break;
	case GBA_LINK_V2_MESSAGE_LATENCY_ACK:
		packet.payload.latencyAck = (struct GBALinkV2LatencyProbe) {
			.generation = TEST_GENERATION,
			.originRole = role == GBA_LINK_ROLE_HOST
			    ? GBA_LINK_ROLE_CLIENT
			    : GBA_LINK_ROLE_HOST,
			.ordinal = 3,
			.unit = GBA_LINK_INPUT_LATENCY_UNIT_MICROSECONDS,
		};
		break;
	case GBA_LINK_V2_MESSAGE_LATENCY_REPORT:
		packet.payload.latencyReport = (struct GBALinkV2LatencyReport) {
			.generation = TEST_GENERATION,
			.originRole = role,
			.sampleCount = GBA_LINK_CALIBRATION_PROBES_PER_ROLE,
			.unit = GBA_LINK_INPUT_LATENCY_UNIT_MICROSECONDS,
			.calibrationPolicyVersion = GBA_LINK_CALIBRATION_POLICY_VERSION,
		};
		for (unsigned i = 0; i < GBA_LINK_CALIBRATION_PROBES_PER_ROLE; ++i) {
			packet.payload.latencyReport.samples[i] = i * 1000;
		}
		break;
	}
	return packet;
}

static enum GBALinkRole _role(enum GBALinkV2MessageType type) {
	switch (type) {
	case GBA_LINK_V2_MESSAGE_ACCEPT:
	case GBA_LINK_V2_MESSAGE_SESSION_READY:
	case GBA_LINK_V2_MESSAGE_INPUT_WINDOW:
	case GBA_LINK_V2_MESSAGE_CALIBRATION_BEGIN:
		return GBA_LINK_ROLE_HOST;
	case GBA_LINK_V2_MESSAGE_ACCEPT_ACK:
	case GBA_LINK_V2_MESSAGE_SESSION_READY_ACK:
		return GBA_LINK_ROLE_CLIENT;
	default:
		return GBA_LINK_ROLE_HOST;
	}
}

static size_t _encode(
	const struct GBALinkV2Packet* packet, uint8_t* data, size_t capacity) {
	size_t size = 0;
	assert_true(GBALinkV2PacketEncode(packet, data, capacity, &size));
	assert_true(size <= GBA_LINK_V2_MAX_PACKET_SIZE);
	return size;
}

M_TEST_DEFINE(allV2MessagesRoundTripCanonically) {
	for (enum GBALinkV2MessageType type = GBA_LINK_V2_MESSAGE_HELLO;
	     type <= GBA_LINK_V2_MESSAGE_LATENCY_REPORT;
	     type = (enum GBALinkV2MessageType) (type + 1)) {
		enum GBALinkRole role = _role(type);
		struct GBALinkV2Packet packet = _packet(type, role);
		uint8_t first[GBA_LINK_V2_MAX_PACKET_SIZE];
		uint8_t second[GBA_LINK_V2_MAX_PACKET_SIZE];
		size_t firstSize = _encode(&packet, first, sizeof(first));
		struct GBALinkV2Packet decoded;
		assert_int_equal(
		    GBALinkV2PacketDecode(first, firstSize, role, &decoded),
		    GBA_LINK_DECODE_OK);
		size_t secondSize = _encode(&decoded, second, sizeof(second));
		assert_int_equal(firstSize, secondSize);
		assert_memory_equal(first, second, firstSize);
		assert_string_equal(GBALinkV2MessageTypeName(type),
		    GBALinkV2MessageTypeName(decoded.header.type));
	}
}

M_TEST_DEFINE(calibrationPayloadsHaveGoldenLittleEndianBytes) {
	static const uint8_t beginExpected[40] = {
		0x78, 0x56, 0, 0, 0, 0, 0, 0,
		0x11, 0x11, 0, 0, 0, 0, 0, 0,
		0x22, 0x22, 0, 0, 0, 0, 0, 0,
		0x0c, 0, 1, 0, 1, 0, 0, 0,
		1, 0, 0, 0, 0, 0, 0, 0,
	};
	static const uint8_t probeExpected[16] = {
		0x78, 0x56, 0, 0, 0, 0, 0, 0,
		0, 3, 1, 0, 0, 0, 0, 0,
	};
	static const uint8_t ackExpected[16] = {
		0x78, 0x56, 0, 0, 0, 0, 0, 0,
		1, 3, 1, 0, 0, 0, 0, 0,
	};
	static const uint8_t reportExpected[68] = {
		0x78, 0x56, 0, 0, 0, 0, 0, 0,
		0, 0x0c, 1, 0, 1, 0, 0, 0,
		0, 0, 0, 0, 0xe8, 3, 0, 0,
		0xd0, 7, 0, 0, 0xb8, 0x0b, 0, 0,
		0xa0, 0x0f, 0, 0, 0x88, 0x13, 0, 0,
		0x70, 0x17, 0, 0, 0x58, 0x1b, 0, 0,
		0x40, 0x1f, 0, 0, 0x28, 0x23, 0, 0,
		0x10, 0x27, 0, 0, 0xf8, 0x2a, 0, 0,
		0, 0, 0, 0,
	};
	const struct {
		enum GBALinkV2MessageType type;
		const uint8_t* expected;
		size_t size;
	} vectors[] = {
		{ GBA_LINK_V2_MESSAGE_CALIBRATION_BEGIN,
		  beginExpected, sizeof(beginExpected) },
		{ GBA_LINK_V2_MESSAGE_LATENCY_PROBE,
		  probeExpected, sizeof(probeExpected) },
		{ GBA_LINK_V2_MESSAGE_LATENCY_ACK,
		  ackExpected, sizeof(ackExpected) },
		{ GBA_LINK_V2_MESSAGE_LATENCY_REPORT,
		  reportExpected, sizeof(reportExpected) },
	};
	for (unsigned i = 0; i < sizeof(vectors) / sizeof(*vectors); ++i) {
		struct GBALinkV2Packet packet = _packet(
		    vectors[i].type, GBA_LINK_ROLE_HOST);
		uint8_t encoded[GBA_LINK_V2_MAX_PACKET_SIZE];
		size_t size = _encode(&packet, encoded, sizeof(encoded));
		assert_int_equal(size,
		    GBA_LINK_V2_HEADER_SIZE + vectors[i].size);
		assert_memory_equal(encoded + GBA_LINK_V2_HEADER_SIZE,
		    vectors[i].expected, vectors[i].size);
	}
}

M_TEST_DEFINE(exactV2VersionAndCapabilitiesDoNotDowngrade) {
	struct GBALinkV2Packet packet =
	    _packet(GBA_LINK_V2_MESSAGE_HELLO, GBA_LINK_ROLE_HOST);
	uint8_t data[GBA_LINK_V2_MAX_PACKET_SIZE];
	size_t size = _encode(&packet, data, sizeof(data));
	struct GBALinkV2Packet decoded;
	data[4] = 1;
	assert_int_equal(
	    GBALinkV2PacketDecode(data, size, GBA_LINK_ROLE_HOST, &decoded),
	    GBA_LINK_DECODE_VERSION);
	data[4] = GBA_LINK_V2_PROTOCOL_VERSION;
	data[GBA_LINK_V2_HEADER_SIZE + 8] ^= 0x20;
	assert_int_equal(
	    GBALinkV2PacketDecode(data, size, GBA_LINK_ROLE_HOST, &decoded),
	    GBA_LINK_DECODE_FIELD);

	packet.payload.hello.runtimeCompatibilityVersion++;
	assert_int_equal(GBALinkV2PacketEncodedSize(&packet), 0);
}

M_TEST_DEFINE(rolesOwnersSessionsAndSequencesFailClosed) {
	struct GBALinkV2Packet packet =
	    _packet(GBA_LINK_V2_MESSAGE_INPUT_BATCH, GBA_LINK_ROLE_HOST);
	uint8_t data[GBA_LINK_V2_MAX_PACKET_SIZE];
	size_t size = _encode(&packet, data, sizeof(data));
	struct GBALinkV2Packet decoded;
	assert_int_equal(
	    GBALinkV2PacketDecode(data, size, GBA_LINK_ROLE_CLIENT, &decoded),
	    GBA_LINK_DECODE_FIELD);
	data[24] = 0;
	memset(&data[25], 0, 7);
	assert_int_equal(
	    GBALinkV2PacketDecode(data, size, GBA_LINK_ROLE_HOST, &decoded),
	    GBA_LINK_DECODE_SEQUENCE);

	packet = _packet(GBA_LINK_V2_MESSAGE_ACCEPT, GBA_LINK_ROLE_HOST);
	size = _encode(&packet, data, sizeof(data));
	memset(&data[16], 0, 8);
	assert_int_equal(
	    GBALinkV2PacketDecode(data, size, GBA_LINK_ROLE_HOST, &decoded),
	    GBA_LINK_DECODE_SESSION);
}

M_TEST_DEFINE(lengthReservedBooleanAndManifestAreCanonical) {
	struct GBALinkV2Packet packet =
	    _packet(GBA_LINK_V2_MESSAGE_REPLICA_INSTALLED, GBA_LINK_ROLE_HOST);
	uint8_t data[GBA_LINK_V2_MAX_PACKET_SIZE];
	size_t size = _encode(&packet, data, sizeof(data));
	struct GBALinkV2Packet decoded;
	data[GBA_LINK_V2_HEADER_SIZE + 8] = 2;
	assert_int_equal(
	    GBALinkV2PacketDecode(data, size, GBA_LINK_ROLE_HOST, &decoded),
	    GBA_LINK_DECODE_FIELD);
	data[GBA_LINK_V2_HEADER_SIZE + 8] = 1;
	data[GBA_LINK_V2_HEADER_SIZE + 9] = 1;
	assert_int_equal(
	    GBALinkV2PacketDecode(data, size, GBA_LINK_ROLE_HOST, &decoded),
	    GBA_LINK_DECODE_RESERVED);
	data[GBA_LINK_V2_HEADER_SIZE + 9] = 0;
	data[8]--;
	assert_int_equal(
	    GBALinkV2PacketDecode(data, size, GBA_LINK_ROLE_HOST, &decoded),
	    GBA_LINK_DECODE_LENGTH);

	packet = _packet(GBA_LINK_V2_MESSAGE_REPLICA_MANIFEST, GBA_LINK_ROLE_HOST);
	size = _encode(&packet, data, sizeof(data));
	data[GBA_LINK_V2_HEADER_SIZE + 16] ^= 1;
	assert_int_equal(
	    GBALinkV2PacketDecode(data, size, GBA_LINK_ROLE_HOST, &decoded),
	    GBA_LINK_DECODE_FIELD);
}

M_TEST_DEFINE(chunkAndInputRelationsAreBounded) {
	struct GBALinkV2Packet packet =
	    _packet(GBA_LINK_V2_MESSAGE_REPLICA_CHUNK, GBA_LINK_ROLE_CLIENT);
	packet.payload.replicaChunk.offset = GBA_REPLICA_MAX_ENCODED_SIZE - 2;
	packet.payload.replicaChunk.size = 5;
	assert_int_equal(GBALinkV2PacketEncodedSize(&packet), 0);

	packet = _packet(GBA_LINK_V2_MESSAGE_INPUT_BATCH, GBA_LINK_ROLE_CLIENT);
	packet.payload.inputBatch.records[1].frame += 2;
	assert_int_equal(GBALinkV2PacketEncodedSize(&packet), 0);
	packet = _packet(GBA_LINK_V2_MESSAGE_INPUT_BATCH, GBA_LINK_ROLE_CLIENT);
	packet.payload.inputBatch.records[1].keys = 0x400;
	assert_int_equal(GBALinkV2PacketEncodedSize(&packet), 0);
	packet = _packet(GBA_LINK_V2_MESSAGE_STATE_CHECK, GBA_LINK_ROLE_CLIENT);
	packet.payload.stateCheck.player = 0;
	uint8_t data[GBA_LINK_V2_MAX_PACKET_SIZE];
	size_t size = _encode(&packet, data, sizeof(data));
	struct GBALinkV2Packet decoded;
	assert_int_equal(
	    GBALinkV2PacketDecode(data, size, GBA_LINK_ROLE_CLIENT, &decoded),
	    GBA_LINK_DECODE_FIELD);
}

M_TEST_DEFINE(transportPacketCeilingCarriesMaximumReplicaChunk) {
	uint8_t* chunk = malloc(GBA_REPLICA_MAX_CHUNK_SIZE);
	uint8_t* packetData = malloc(GBA_LINK_V2_MAX_PACKET_SIZE);
	assert_non_null(chunk);
	assert_non_null(packetData);
	memset(chunk, 0xA5, GBA_REPLICA_MAX_CHUNK_SIZE);
	struct GBALinkV2Packet packet =
	    _packet(GBA_LINK_V2_MESSAGE_REPLICA_CHUNK, GBA_LINK_ROLE_HOST);
	packet.payload.replicaChunk.offset = 0;
	packet.payload.replicaChunk.size = GBA_REPLICA_MAX_CHUNK_SIZE;
	packet.payload.replicaChunk.data = chunk;
	size_t size = _encode(&packet, packetData, GBA_LINK_V2_MAX_PACKET_SIZE);
	assert_true(size <= GBA_LINK_TRANSPORT_MAX_PACKET_SIZE);
	struct GBALinkV2Packet decoded;
	assert_int_equal(
	    GBALinkV2PacketDecode(
	        packetData, size, GBA_LINK_ROLE_HOST, &decoded),
	    GBA_LINK_DECODE_OK);
	assert_memory_equal(decoded.payload.replicaChunk.data, chunk,
	    GBA_REPLICA_MAX_CHUNK_SIZE);
	free(packetData);
	free(chunk);
}

M_TEST_DEFINE(everyMessageRejectsTruncationAndExtension) {
	for (enum GBALinkV2MessageType type = GBA_LINK_V2_MESSAGE_HELLO;
	     type <= GBA_LINK_V2_MESSAGE_LATENCY_REPORT;
	     type = (enum GBALinkV2MessageType) (type + 1)) {
		enum GBALinkRole role = _role(type);
		struct GBALinkV2Packet packet = _packet(type, role);
		uint8_t data[GBA_LINK_V2_MAX_PACKET_SIZE + 1];
		size_t size = _encode(&packet, data, sizeof(data));
		struct GBALinkV2Packet decoded;
		for (size_t truncated = 0; truncated < size; ++truncated) {
			enum GBALinkDecodeStatus status = GBALinkV2PacketDecode(
			    data, truncated, role, &decoded);
			assert_true(status != GBA_LINK_DECODE_OK);
		}
		data[size] = 0;
		assert_int_equal(
		    GBALinkV2PacketDecode(data, size + 1, role, &decoded),
		    GBA_LINK_DECODE_LENGTH);
	}
}

M_TEST_DEFINE(calibrationFieldBoundariesAndRolesAreCanonical) {
	struct GBALinkV2Packet packet =
	    _packet(GBA_LINK_V2_MESSAGE_LATENCY_PROBE, GBA_LINK_ROLE_HOST);
	packet.payload.latencyProbe.ordinal = 0;
	assert_true(GBALinkV2PacketEncodedSize(&packet));
	packet.payload.latencyProbe.ordinal =
	    GBA_LINK_CALIBRATION_PROBES_PER_ROLE - 1;
	assert_true(GBALinkV2PacketEncodedSize(&packet));
	packet.payload.latencyProbe.ordinal =
	    GBA_LINK_CALIBRATION_PROBES_PER_ROLE;
	assert_int_equal(GBALinkV2PacketEncodedSize(&packet), 0);

	packet = _packet(GBA_LINK_V2_MESSAGE_LATENCY_ACK, GBA_LINK_ROLE_CLIENT);
	assert_true(GBALinkV2PacketEncodedSize(&packet));
	uint8_t data[GBA_LINK_V2_MAX_PACKET_SIZE];
	size_t size = _encode(&packet, data, sizeof(data));
	struct GBALinkV2Packet decoded;
	assert_int_equal(
	    GBALinkV2PacketDecode(data, size, GBA_LINK_ROLE_HOST, &decoded),
	    GBA_LINK_DECODE_FIELD);

	packet = _packet(GBA_LINK_V2_MESSAGE_LATENCY_REPORT, GBA_LINK_ROLE_HOST);
	memset(packet.payload.latencyReport.samples, 0,
	    sizeof(packet.payload.latencyReport.samples));
	packet.payload.latencyReport.samples[11] =
	    GBA_LINK_CALIBRATION_MAX_SAMPLE_US;
	assert_true(GBALinkV2PacketEncodedSize(&packet));
	packet.payload.latencyReport.samples[11]++;
	assert_int_equal(GBALinkV2PacketEncodedSize(&packet), 0);
	packet = _packet(GBA_LINK_V2_MESSAGE_LATENCY_REPORT, GBA_LINK_ROLE_HOST);
	packet.payload.latencyReport.sampleCount--;
	assert_int_equal(GBALinkV2PacketEncodedSize(&packet), 0);
	packet = _packet(GBA_LINK_V2_MESSAGE_CALIBRATION_BEGIN, GBA_LINK_ROLE_HOST);
	packet.payload.calibrationBegin.hostConnectionNonce = 0;
	assert_int_equal(GBALinkV2PacketEncodedSize(&packet), 0);
}

M_TEST_DEFINE(newPayloadReservedBytesReportReservedFailure) {
	struct ReservedCase {
		enum GBALinkV2MessageType type;
		size_t payloadOffset;
	};
	const struct ReservedCase cases[] = {
		{ GBA_LINK_V2_MESSAGE_ACCEPT, 99 },
		{ GBA_LINK_V2_MESSAGE_SESSION_READY, 91 },
		{ GBA_LINK_V2_MESSAGE_SESSION_READY_ACK, 91 },
		{ GBA_LINK_V2_MESSAGE_CALIBRATION_BEGIN, 36 },
		{ GBA_LINK_V2_MESSAGE_LATENCY_PROBE, 12 },
		{ GBA_LINK_V2_MESSAGE_LATENCY_ACK, 12 },
		{ GBA_LINK_V2_MESSAGE_LATENCY_REPORT, 64 },
	};
	for (unsigned i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
		enum GBALinkRole role = _role(cases[i].type);
		struct GBALinkV2Packet packet = _packet(cases[i].type, role);
		uint8_t data[GBA_LINK_V2_MAX_PACKET_SIZE];
		size_t size = _encode(&packet, data, sizeof(data));
		assert_true(GBA_LINK_V2_HEADER_SIZE + cases[i].payloadOffset < size);
		data[GBA_LINK_V2_HEADER_SIZE + cases[i].payloadOffset] = 1;
		struct GBALinkV2Packet decoded;
		assert_int_equal(
		    GBALinkV2PacketDecode(data, size, role, &decoded),
		    GBA_LINK_DECODE_RESERVED);
	}
}

M_TEST_DEFINE(profileAndCapabilityNoncanonicalFieldsFailClosed) {
	struct GBALinkV2Packet packet =
	    _packet(GBA_LINK_V2_MESSAGE_HELLO, GBA_LINK_ROLE_HOST);
	packet.payload.hello.deterministicCapabilities
	    .timeSemanticsCapabilityMask = 2;
	assert_int_equal(GBALinkV2PacketEncodedSize(&packet), 0);
	packet = _packet(GBA_LINK_V2_MESSAGE_HELLO, GBA_LINK_ROLE_HOST);
	packet.payload.hello.deterministicCapabilities
	    .synchronizedInputCapabilityMask |= UINT64_C(1) << 63;
	assert_int_equal(GBALinkV2PacketEncodedSize(&packet), 0);
	packet = _packet(GBA_LINK_V2_MESSAGE_HELLO, GBA_LINK_ROLE_HOST);
	packet.payload.hello.profile.records[0].flags = 2;
	assert_int_equal(GBALinkV2PacketEncodedSize(&packet), 0);
}

M_TEST_SUITE_DEFINE(GBALinkProtocolV2,
	cmocka_unit_test(allV2MessagesRoundTripCanonically),
	cmocka_unit_test(calibrationPayloadsHaveGoldenLittleEndianBytes),
	cmocka_unit_test(exactV2VersionAndCapabilitiesDoNotDowngrade),
	cmocka_unit_test(rolesOwnersSessionsAndSequencesFailClosed),
	cmocka_unit_test(lengthReservedBooleanAndManifestAreCanonical),
	cmocka_unit_test(chunkAndInputRelationsAreBounded),
	cmocka_unit_test(transportPacketCeilingCarriesMaximumReplicaChunk),
	cmocka_unit_test(everyMessageRejectsTruncationAndExtension),
	cmocka_unit_test(calibrationFieldBoundariesAndRolesAreCanonical),
	cmocka_unit_test(newPayloadReservedBytesReportReservedFailure),
	cmocka_unit_test(profileAndCapabilityNoncanonicalFieldsFailClosed))
