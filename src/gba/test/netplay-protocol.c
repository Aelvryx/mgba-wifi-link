/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "util/test/suite.h"

#include <mgba/internal/gba/sio/netplay/protocol.h>

static bool _isPreSession(enum GBALinkMessageType type) {
	return type == GBA_LINK_MESSAGE_HELLO ||
	       type == GBA_LINK_MESSAGE_ACCEPT ||
	       type == GBA_LINK_MESSAGE_REJECT;
}

static enum GBALinkRole _senderRole(enum GBALinkMessageType type) {
	switch (type) {
	case GBA_LINK_MESSAGE_ACCEPT_ACK:
	case GBA_LINK_MESSAGE_SESSION_READY_ACK:
	case GBA_LINK_MESSAGE_GRANT_ACK:
	case GBA_LINK_MESSAGE_TRANSFER_READY:
	case GBA_LINK_MESSAGE_COMPLETION_READY:
	case GBA_LINK_MESSAGE_COMPLETION_DECISION_ACK:
		return GBA_LINK_ROLE_CLIENT;
	default:
		return GBA_LINK_ROLE_HOST;
	}
}

static struct GBALinkPacket _packet(enum GBALinkMessageType type) {
	struct GBALinkPacket packet;
	memset(&packet, 0, sizeof(packet));
	packet.header.type = type;
	packet.header.sessionId = _isPreSession(type) ? 0 : 0x0102030405060708;
	packet.header.packetSequence = 0x1112131415161718;

	switch (type) {
	case GBA_LINK_MESSAGE_HELLO:
		packet.payload.hello.capabilities = GBA_LINK_MVP_CAPABILITIES;
		packet.payload.hello.romSize = 0x02000000;
		for (unsigned i = 0; i < GBA_LINK_ROM_SHA1_SIZE; ++i) {
			packet.payload.hello.romSha1[i] = i;
		}
		packet.payload.hello.supportedPolicies = 1U << GBA_LINK_COMPATIBILITY_EXACT_ROM;
		packet.payload.hello.emulationCompatibilityVersion = 1;
		packet.payload.hello.initialMode = GBA_LINK_MODE_MULTI;
		packet.payload.hello.digestCount = GBA_LINK_MAX_DETERMINISM_DIGESTS;
		packet.payload.hello.rendezvousCycle = 0x2122232425262728;
		for (unsigned i = 0; i < GBA_LINK_MAX_DETERMINISM_DIGESTS; ++i) {
			packet.payload.hello.digests[i].category = i + 1;
			for (unsigned j = 0; j < GBA_LINK_DIGEST_SIZE; ++j) {
				packet.payload.hello.digests[i].digest[j] = i * GBA_LINK_DIGEST_SIZE + j;
			}
		}
		break;
	case GBA_LINK_MESSAGE_ACCEPT:
		packet.payload.accept.proposedSessionId = 0x0102030405060708;
		packet.payload.accept.hostTransportId = 0;
		packet.payload.accept.clientTransportId = 1;
		packet.payload.accept.policy = GBA_LINK_COMPATIBILITY_EXACT_ROM;
		packet.payload.accept.attachCycle = 0x3132333435363738;
		packet.payload.accept.initialModeGeneration = 1;
		break;
	case GBA_LINK_MESSAGE_ACCEPT_ACK:
		packet.payload.sessionId.acceptedSessionId = packet.header.sessionId;
		break;
	case GBA_LINK_MESSAGE_SESSION_READY:
	case GBA_LINK_MESSAGE_SESSION_READY_ACK:
		packet.payload.sessionReady.attachCycle = 0x3132333435363738;
		packet.payload.sessionReady.initialModeGeneration = 1;
		break;
	case GBA_LINK_MESSAGE_REJECT:
	case GBA_LINK_MESSAGE_DETACH:
	case GBA_LINK_MESSAGE_DETACH_ACK:
		packet.payload.reason.reason = GBA_LINK_REASON_USER_DISCONNECT;
		break;
	case GBA_LINK_MESSAGE_EXECUTION_GRANT:
	case GBA_LINK_MESSAGE_GRANT_ACK:
		packet.payload.grant.grantSequence = 0x2122232425262728;
		packet.payload.grant.horizon = 0x3132333435363738;
		break;
	case GBA_LINK_MESSAGE_MODE_INTENT:
		packet.payload.modeIntent.modeGeneration = 2;
		packet.payload.modeIntent.localCycle = 0x3132333435363738;
		packet.payload.modeIntent.localMode = GBA_LINK_MODE_MULTI;
		packet.payload.modeIntent.deferred = true;
		break;
	case GBA_LINK_MESSAGE_MODE_COMMIT:
		packet.payload.modeCommit.modeGeneration = 2;
		packet.payload.modeCommit.commitCycle = 0x3132333435363738;
		packet.payload.modeCommit.hostMode = GBA_LINK_MODE_MULTI;
		packet.payload.modeCommit.clientMode = GBA_LINK_MODE_MULTI;
		packet.payload.modeCommit.jointlyReady = true;
		break;
	case GBA_LINK_MESSAGE_MODE_ACK:
		packet.payload.modeAck.modeGeneration = 2;
		packet.payload.modeAck.commitCycle = 0x3132333435363738;
		break;
	case GBA_LINK_MESSAGE_TRANSFER_START:
	case GBA_LINK_MESSAGE_TRANSFER_READY:
		packet.payload.transferStart.transferSequence = 3;
		packet.payload.transferStart.startCycle = 100;
		packet.payload.transferStart.completionCycle = 200;
		packet.payload.transferStart.outgoingWord = 0x1234;
		packet.payload.transferStart.siocnt = 0x6083;
		break;
	case GBA_LINK_MESSAGE_TRANSFER_COMMIT:
		packet.payload.transferCommit.transferSequence = 3;
		packet.payload.transferCommit.startCycle = 100;
		packet.payload.transferCommit.completionCycle = 200;
		packet.payload.transferCommit.words[0] = 0x1234;
		packet.payload.transferCommit.words[1] = 0x5678;
		packet.payload.transferCommit.words[2] = 0xFFFF;
		packet.payload.transferCommit.words[3] = 0xFFFF;
		break;
	case GBA_LINK_MESSAGE_TRANSFER_ABORT:
		packet.payload.transferAbort.transferSequence = 3;
		packet.payload.transferAbort.completionCycle = 200;
		packet.payload.transferAbort.reason = GBA_LINK_REASON_SEND_FAILURE;
		break;
	case GBA_LINK_MESSAGE_COMPLETION_CATCHUP:
		packet.payload.completionCatchup.transferSequence = 3;
		packet.payload.completionCatchup.completionSequence = 4;
		packet.payload.completionCatchup.completionCycle = 200;
		packet.payload.completionCatchup.pendingOutcome = GBA_LINK_OUTCOME_SUCCESS;
		break;
	case GBA_LINK_MESSAGE_COMPLETION_READY:
		packet.payload.completionReady.transferSequence = 3;
		packet.payload.completionReady.completionSequence = 4;
		packet.payload.completionReady.completionCycle = 200;
		packet.payload.completionReady.hasDeferredMode = true;
		break;
	case GBA_LINK_MESSAGE_COMPLETION_DECISION:
		packet.payload.completionDecision.transferSequence = 3;
		packet.payload.completionDecision.completionSequence = 4;
		packet.payload.completionDecision.completionCycle = 200;
		packet.payload.completionDecision.outcome = GBA_LINK_OUTCOME_SUCCESS;
		packet.payload.completionDecision.words[0] = 0x1234;
		packet.payload.completionDecision.words[1] = 0x5678;
		packet.payload.completionDecision.words[2] = 0xFFFF;
		packet.payload.completionDecision.words[3] = 0xFFFF;
		break;
	case GBA_LINK_MESSAGE_COMPLETION_DECISION_ACK:
		packet.payload.completionDecisionAck.transferSequence = 3;
		packet.payload.completionDecisionAck.completionSequence = 4;
		packet.payload.completionDecisionAck.completionCycle = 200;
		packet.payload.completionDecisionAck.outcome =
		    GBA_LINK_OUTCOME_SUCCESS;
		break;
	}
	return packet;
}

static void _assertRoundTrip(enum GBALinkMessageType type) {
	struct GBALinkPacket expected = _packet(type);
	uint8_t buffer[GBA_LINK_MAX_PACKET_SIZE];
	memset(buffer, 0xA5, sizeof(buffer));
	size_t encodedSize = 0;
	assert_true(GBALinkPacketEncode(&expected, buffer, sizeof(buffer), &encodedSize));
	assert_int_equal(encodedSize, GBALinkPacketEncodedSize(&expected));
	assert_true(encodedSize <= GBA_LINK_MAX_PACKET_SIZE);
	assert_int_equal(buffer[encodedSize], 0xA5);

	struct GBALinkPacket actual;
	memset(&actual, 0x5A, sizeof(actual));
	assert_int_equal(
	    GBALinkPacketDecode(buffer, encodedSize, _senderRole(type), &actual),
	    GBA_LINK_DECODE_OK);
	assert_memory_equal(&actual, &expected, sizeof(actual));
}

M_TEST_DEFINE(allMessagesRoundTrip) {
	for (enum GBALinkMessageType type = GBA_LINK_MESSAGE_HELLO;
	     type <= GBA_LINK_MESSAGE_COMPLETION_DECISION_ACK;
	     type = (enum GBALinkMessageType) (type + 1)) {
		_assertRoundTrip(type);
	}
}

M_TEST_DEFINE(executionGrantGoldenVector) {
	struct GBALinkPacket packet = _packet(GBA_LINK_MESSAGE_EXECUTION_GRANT);
	const uint8_t expected[] = {
		0x4D, 0x47, 0x4C, 0x4E,
		0x01, 0x00,
		0x09, 0x00,
		0x10, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
		0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11,
		0x28, 0x27, 0x26, 0x25, 0x24, 0x23, 0x22, 0x21,
		0x38, 0x37, 0x36, 0x35, 0x34, 0x33, 0x32, 0x31,
	};
	uint8_t actual[sizeof(expected)];
	size_t encodedSize = 0;
	assert_true(GBALinkPacketEncode(&packet, actual, sizeof(actual), &encodedSize));
	assert_int_equal(encodedSize, sizeof(expected));
	assert_memory_equal(actual, expected, sizeof(expected));
}

M_TEST_DEFINE(truncationAndTrailingDataFailWithoutMutation) {
	struct GBALinkPacket packet = _packet(GBA_LINK_MESSAGE_HELLO);
	uint8_t buffer[GBA_LINK_MAX_PACKET_SIZE + 1];
	size_t encodedSize = 0;
	assert_true(GBALinkPacketEncode(&packet, buffer, sizeof(buffer), &encodedSize));

	struct GBALinkPacket sentinel;
	memset(&sentinel, 0xA5, sizeof(sentinel));
	for (size_t size = 0; size < encodedSize; ++size) {
		struct GBALinkPacket output = sentinel;
		assert_int_not_equal(
		    GBALinkPacketDecode(buffer, size, GBA_LINK_ROLE_HOST, &output),
		    GBA_LINK_DECODE_OK);
		assert_memory_equal(&output, &sentinel, sizeof(output));
	}

	buffer[encodedSize] = 0;
	struct GBALinkPacket output = sentinel;
	assert_int_equal(
	    GBALinkPacketDecode(buffer, encodedSize + 1, GBA_LINK_ROLE_HOST, &output),
	    GBA_LINK_DECODE_LENGTH);
	assert_memory_equal(&output, &sentinel, sizeof(output));
}

M_TEST_DEFINE(oversizedAndReservedDataFail) {
	uint8_t oversized[GBA_LINK_MAX_PACKET_SIZE + 1] = {0};
	struct GBALinkPacket sentinel;
	memset(&sentinel, 0xA5, sizeof(sentinel));
	struct GBALinkPacket output = sentinel;
	assert_int_equal(
	    GBALinkPacketDecode(oversized, sizeof(oversized), GBA_LINK_ROLE_HOST, &output),
	    GBA_LINK_DECODE_OVERSIZED);
	assert_memory_equal(&output, &sentinel, sizeof(output));

	struct GBALinkPacket packet = _packet(GBA_LINK_MESSAGE_EXECUTION_GRANT);
	uint8_t buffer[GBA_LINK_MAX_PACKET_SIZE];
	size_t encodedSize = 0;
	assert_true(GBALinkPacketEncode(&packet, buffer, sizeof(buffer), &encodedSize));
	buffer[12] = 1;
	output = sentinel;
	assert_int_equal(
	    GBALinkPacketDecode(buffer, encodedSize, GBA_LINK_ROLE_HOST, &output),
	    GBA_LINK_DECODE_RESERVED);
	assert_memory_equal(&output, &sentinel, sizeof(output));
}

M_TEST_DEFINE(invalidEnumsIdsAndRolesFail) {
	struct GBALinkPacket packet = _packet(GBA_LINK_MESSAGE_ACCEPT);
	packet.payload.accept.clientTransportId = 2;
	assert_int_equal(GBALinkPacketEncodedSize(&packet), 0);

	packet = _packet(GBA_LINK_MESSAGE_MODE_INTENT);
	packet.payload.modeIntent.localMode = 7;
	assert_int_equal(GBALinkPacketEncodedSize(&packet), 0);

	packet = _packet(GBA_LINK_MESSAGE_COMPLETION_DECISION);
	packet.payload.completionDecision.outcome = 2;
	assert_int_equal(GBALinkPacketEncodedSize(&packet), 0);

	packet = _packet(GBA_LINK_MESSAGE_EXECUTION_GRANT);
	uint8_t buffer[GBA_LINK_MAX_PACKET_SIZE];
	size_t encodedSize = 0;
	assert_true(GBALinkPacketEncode(&packet, buffer, sizeof(buffer), &encodedSize));
	struct GBALinkPacket output;
	assert_int_equal(
	    GBALinkPacketDecode(buffer, encodedSize, GBA_LINK_ROLE_CLIENT, &output),
	    GBA_LINK_DECODE_ROLE);
}

M_TEST_DEFINE(noncanonicalBooleanBytesFailWithoutMutation) {
	const struct {
		enum GBALinkMessageType type;
		size_t payloadOffset;
	} cases[] = {
		{
			GBA_LINK_MESSAGE_MODE_INTENT,
			17,
		},
		{
			GBA_LINK_MESSAGE_MODE_COMMIT,
			18,
		},
		{
			GBA_LINK_MESSAGE_COMPLETION_READY,
			26,
		},
	};
	struct GBALinkPacket sentinel;
	memset(&sentinel, 0xA5, sizeof(sentinel));
	for (size_t i = 0;
	     i < sizeof(cases) / sizeof(cases[0]); ++i) {
		struct GBALinkPacket packet =
		    _packet(cases[i].type);
		uint8_t buffer[GBA_LINK_MAX_PACKET_SIZE];
		size_t encodedSize = 0;
		assert_true(GBALinkPacketEncode(
		    &packet, buffer, sizeof(buffer),
		    &encodedSize));
		buffer[GBA_LINK_HEADER_SIZE +
		       cases[i].payloadOffset] = 2;
		struct GBALinkPacket output = sentinel;
		assert_int_equal(
		    GBALinkPacketDecode(
		        buffer, encodedSize,
		        _senderRole(cases[i].type),
		        &output),
		    GBA_LINK_DECODE_FIELD);
		assert_memory_equal(
		    &output, &sentinel, sizeof(output));
	}
}

M_TEST_DEFINE(counterBoundaryIsRepresentableWithoutWrap) {
	struct GBALinkPacket packet = _packet(GBA_LINK_MESSAGE_EXECUTION_GRANT);
	packet.header.packetSequence = UINT64_MAX;
	packet.payload.grant.grantSequence = UINT64_MAX;
	packet.payload.grant.horizon = UINT64_MAX;
	uint8_t buffer[GBA_LINK_MAX_PACKET_SIZE];
	size_t encodedSize = 0;
	assert_true(GBALinkPacketEncode(&packet, buffer, sizeof(buffer), &encodedSize));

	struct GBALinkPacket decoded;
	assert_int_equal(
	    GBALinkPacketDecode(buffer, encodedSize, GBA_LINK_ROLE_HOST, &decoded),
	    GBA_LINK_DECODE_OK);
	assert_int_equal(decoded.header.packetSequence, UINT64_MAX);
	assert_int_equal(decoded.payload.grant.grantSequence, UINT64_MAX);
	assert_int_equal(decoded.payload.grant.horizon, UINT64_MAX);
}

M_TEST_DEFINE(conflictingDuplicateHasDistinctCanonicalBytes) {
	struct GBALinkPacket first = _packet(GBA_LINK_MESSAGE_EXECUTION_GRANT);
	struct GBALinkPacket conflicting = first;
	++conflicting.payload.grant.horizon;
	uint8_t firstBytes[GBA_LINK_MAX_PACKET_SIZE];
	uint8_t conflictingBytes[GBA_LINK_MAX_PACKET_SIZE];
	size_t firstSize = 0;
	size_t conflictingSize = 0;
	assert_true(GBALinkPacketEncode(&first, firstBytes, sizeof(firstBytes), &firstSize));
	assert_true(GBALinkPacketEncode(
	    &conflicting, conflictingBytes, sizeof(conflictingBytes), &conflictingSize));
	assert_int_equal(firstSize, conflictingSize);
	assert_memory_not_equal(firstBytes, conflictingBytes, firstSize);
}

static uint32_t _randomState = 0xC001D00D;

static uint32_t _random(void) {
	_randomState = _randomState * 1664525 + 1013904223;
	return _randomState;
}

M_TEST_DEFINE(randomInputNeverPartiallyMutatesOutput) {
	uint8_t buffer[GBA_LINK_MAX_PACKET_SIZE + 64];
	struct GBALinkPacket sentinel;
	memset(&sentinel, 0xA5, sizeof(sentinel));
	for (unsigned iteration = 0; iteration < 50000; ++iteration) {
		size_t size = _random() % sizeof(buffer);
		for (size_t i = 0; i < size; ++i) {
			buffer[i] = _random();
		}
		struct GBALinkPacket output = sentinel;
		enum GBALinkDecodeStatus status = GBALinkPacketDecode(
		    buffer, size, _random() & 1, &output);
		if (status != GBA_LINK_DECODE_OK) {
			assert_memory_equal(&output, &sentinel, sizeof(output));
		} else {
			uint8_t canonical[GBA_LINK_MAX_PACKET_SIZE];
			size_t canonicalSize = 0;
			assert_true(GBALinkPacketEncode(
			    &output, canonical, sizeof(canonical), &canonicalSize));
			assert_int_equal(canonicalSize, size);
		}
	}
}

M_TEST_DEFINE(validPacketsWithSingleByteMutationsAreBounded) {
	for (enum GBALinkMessageType type = GBA_LINK_MESSAGE_HELLO;
	     type <= GBA_LINK_MESSAGE_COMPLETION_DECISION_ACK;
	     type = (enum GBALinkMessageType) (type + 1)) {
		struct GBALinkPacket packet = _packet(type);
		uint8_t buffer[GBA_LINK_MAX_PACKET_SIZE];
		size_t encodedSize = 0;
		assert_true(GBALinkPacketEncode(&packet, buffer, sizeof(buffer), &encodedSize));
		for (size_t i = 0; i < encodedSize; ++i) {
			uint8_t original = buffer[i];
			buffer[i] ^= 0x80;
			struct GBALinkPacket sentinel;
			memset(&sentinel, 0xA5, sizeof(sentinel));
			struct GBALinkPacket output = sentinel;
			enum GBALinkDecodeStatus status = GBALinkPacketDecode(
			    buffer, encodedSize, _senderRole(type), &output);
			if (status != GBA_LINK_DECODE_OK) {
				assert_memory_equal(&output, &sentinel, sizeof(output));
			}
			buffer[i] = original;
		}
	}
}

M_TEST_SUITE_DEFINE(GBALinkProtocol,
	cmocka_unit_test(allMessagesRoundTrip),
	cmocka_unit_test(executionGrantGoldenVector),
	cmocka_unit_test(truncationAndTrailingDataFailWithoutMutation),
	cmocka_unit_test(oversizedAndReservedDataFail),
	cmocka_unit_test(invalidEnumsIdsAndRolesFail),
	cmocka_unit_test(
	    noncanonicalBooleanBytesFailWithoutMutation),
	cmocka_unit_test(counterBoundaryIsRepresentableWithoutWrap),
	cmocka_unit_test(conflictingDuplicateHasDistinctCanonicalBytes),
	cmocka_unit_test(randomInputNeverPartiallyMutatesOutput),
	cmocka_unit_test(validPacketsWithSingleByteMutationsAreBounded))
