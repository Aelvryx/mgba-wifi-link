/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef GBA_SIO_NETPLAY_PROTOCOL_H
#define GBA_SIO_NETPLAY_PROTOCOL_H

#include <mgba-util/common.h>

CXX_GUARD_START

#define GBA_LINK_PROTOCOL_NAME "mgba-gba-link-netplay-v1"
#define GBA_LINK_PROTOCOL_MAGIC 0x4E4C474D
#define GBA_LINK_PROTOCOL_VERSION 1
#define GBA_LINK_HEADER_SIZE 32
#define GBA_LINK_MAX_PACKET_SIZE 512
#define GBA_LINK_MAX_PAYLOAD_SIZE (GBA_LINK_MAX_PACKET_SIZE - GBA_LINK_HEADER_SIZE)
#define GBA_LINK_MAX_COPIED_PACKETS 64
#define GBA_LINK_ROM_SHA1_SIZE 20
#define GBA_LINK_DIGEST_SIZE 20
#define GBA_LINK_MAX_DETERMINISM_DIGESTS 5
#define GBA_LINK_MAX_WAIT_MS 3000

enum GBALinkRole {
	GBA_LINK_ROLE_HOST = 0,
	GBA_LINK_ROLE_CLIENT = 1,
};

enum GBALinkMessageType {
	GBA_LINK_MESSAGE_HELLO = 1,
	GBA_LINK_MESSAGE_ACCEPT = 2,
	GBA_LINK_MESSAGE_ACCEPT_ACK = 3,
	GBA_LINK_MESSAGE_SESSION_READY = 4,
	GBA_LINK_MESSAGE_SESSION_READY_ACK = 5,
	GBA_LINK_MESSAGE_REJECT = 6,
	GBA_LINK_MESSAGE_DETACH = 7,
	GBA_LINK_MESSAGE_DETACH_ACK = 8,
	GBA_LINK_MESSAGE_EXECUTION_GRANT = 9,
	GBA_LINK_MESSAGE_GRANT_ACK = 10,
	GBA_LINK_MESSAGE_MODE_INTENT = 11,
	GBA_LINK_MESSAGE_MODE_COMMIT = 12,
	GBA_LINK_MESSAGE_MODE_ACK = 13,
	GBA_LINK_MESSAGE_TRANSFER_START = 14,
	GBA_LINK_MESSAGE_TRANSFER_READY = 15,
	GBA_LINK_MESSAGE_TRANSFER_COMMIT = 16,
	GBA_LINK_MESSAGE_TRANSFER_ABORT = 17,
	GBA_LINK_MESSAGE_COMPLETION_CATCHUP = 18,
	GBA_LINK_MESSAGE_COMPLETION_READY = 19,
	GBA_LINK_MESSAGE_COMPLETION_DECISION = 20,
	GBA_LINK_MESSAGE_COMPLETION_DECISION_ACK = 21,
};

enum GBALinkCapability {
	GBA_LINK_CAPABILITY_MULTI_2P = 1 << 0,
	GBA_LINK_CAPABILITY_EXACT_ROM = 1 << 1,
	GBA_LINK_CAPABILITY_PROFILE_DIGESTS = 1 << 2,
	GBA_LINK_CAPABILITY_HOST_LED_GRANTS = 1 << 3,
	GBA_LINK_CAPABILITY_COMPLETION_DECISION = 1 << 4,
};

enum {
	GBA_LINK_MVP_CAPABILITIES =
	    GBA_LINK_CAPABILITY_MULTI_2P |
	    GBA_LINK_CAPABILITY_EXACT_ROM |
	    GBA_LINK_CAPABILITY_PROFILE_DIGESTS |
	    GBA_LINK_CAPABILITY_HOST_LED_GRANTS |
	    GBA_LINK_CAPABILITY_COMPLETION_DECISION,
};

enum GBALinkCompatibilityPolicy {
	GBA_LINK_COMPATIBILITY_EXACT_ROM = 1,
	GBA_LINK_COMPATIBILITY_GROUP = 2,
};

enum GBALinkDeterminismCategory {
	GBA_LINK_DETERMINISM_BIOS = 1,
	GBA_LINK_DETERMINISM_CPU_TIMING = 2,
	GBA_LINK_DETERMINISM_IDLE_OPTIMIZATION = 3,
	GBA_LINK_DETERMINISM_RTC_OVERRIDE = 4,
	GBA_LINK_DETERMINISM_CHEATS = 5,
};

enum GBALinkReason {
	GBA_LINK_REASON_PROTOCOL_MISMATCH = 1,
	GBA_LINK_REASON_CAPABILITY_MISMATCH = 2,
	GBA_LINK_REASON_ROM_MISMATCH = 3,
	GBA_LINK_REASON_POLICY_MISMATCH = 4,
	GBA_LINK_REASON_COMPATIBILITY_MISMATCH = 5,
	GBA_LINK_REASON_DETERMINISM_MISMATCH = 6,
	GBA_LINK_REASON_SIO_NOT_QUIESCENT = 7,
	GBA_LINK_REASON_THIRD_PLAYER = 8,
	GBA_LINK_REASON_MALFORMED_PACKET = 9,
	GBA_LINK_REASON_TRANSPORT_STOP = 10,
	GBA_LINK_REASON_PEER_DETACH = 11,
	GBA_LINK_REASON_HANDSHAKE_TIMEOUT = 12,
	GBA_LINK_REASON_ATTACHMENT_TIMEOUT = 13,
	GBA_LINK_REASON_MODE_TIMEOUT = 14,
	GBA_LINK_REASON_TRANSFER_READY_TIMEOUT = 15,
	GBA_LINK_REASON_TRANSFER_COMMIT_TIMEOUT = 16,
	GBA_LINK_REASON_COMPLETION_CATCHUP_TIMEOUT = 17,
	GBA_LINK_REASON_COMPLETION_READY_TIMEOUT = 18,
	GBA_LINK_REASON_COMPLETION_DECISION_TIMEOUT = 19,
	GBA_LINK_REASON_DETACH_TIMEOUT = 20,
	GBA_LINK_REASON_MODE_DEPARTURE = 21,
	GBA_LINK_REASON_QUEUE_EXHAUSTED = 22,
	GBA_LINK_REASON_OVERSIZED_PACKET = 23,
	GBA_LINK_REASON_SEND_FAILURE = 24,
	GBA_LINK_REASON_SEQUENCE_EXHAUSTED = 25,
	GBA_LINK_REASON_INVALID_TRANSITION = 26,
	GBA_LINK_REASON_RESET = 27,
	GBA_LINK_REASON_UNLOAD = 28,
	GBA_LINK_REASON_USER_DISCONNECT = 29,
	GBA_LINK_REASON_GRANT_TIMEOUT = 30,
};

enum GBALinkTransferOutcome {
	GBA_LINK_OUTCOME_SUCCESS = 0,
	GBA_LINK_OUTCOME_ERROR = 1,
};

enum GBALinkWireMode {
	GBA_LINK_MODE_NORMAL_8 = 0,
	GBA_LINK_MODE_NORMAL_32 = 1,
	GBA_LINK_MODE_MULTI = 2,
	GBA_LINK_MODE_UART = 3,
	GBA_LINK_MODE_GPIO = 8,
	GBA_LINK_MODE_JOYBUS = 12,
};

enum GBALinkDecodeStatus {
	GBA_LINK_DECODE_OK = 0,
	GBA_LINK_DECODE_NULL,
	GBA_LINK_DECODE_TRUNCATED,
	GBA_LINK_DECODE_OVERSIZED,
	GBA_LINK_DECODE_MAGIC,
	GBA_LINK_DECODE_VERSION,
	GBA_LINK_DECODE_TYPE,
	GBA_LINK_DECODE_LENGTH,
	GBA_LINK_DECODE_RESERVED,
	GBA_LINK_DECODE_ROLE,
	GBA_LINK_DECODE_SESSION,
	GBA_LINK_DECODE_SEQUENCE,
	GBA_LINK_DECODE_FIELD,
};

struct GBALinkPacketHeader {
	enum GBALinkMessageType type;
	uint64_t sessionId;
	uint64_t packetSequence;
};

struct GBALinkDeterminismDigest {
	enum GBALinkDeterminismCategory category;
	uint8_t digest[GBA_LINK_DIGEST_SIZE];
};

struct GBALinkHello {
	uint64_t capabilities;
	uint64_t romSize;
	uint8_t romSha1[GBA_LINK_ROM_SHA1_SIZE];
	uint32_t supportedPolicies;
	uint32_t emulationCompatibilityVersion;
	enum GBALinkWireMode initialMode;
	uint8_t digestCount;
	uint64_t rendezvousCycle;
	struct GBALinkDeterminismDigest digests[GBA_LINK_MAX_DETERMINISM_DIGESTS];
};

struct GBALinkAccept {
	uint64_t proposedSessionId;
	uint16_t hostTransportId;
	uint16_t clientTransportId;
	enum GBALinkCompatibilityPolicy policy;
	uint64_t compatibilityGroup;
	uint64_t attachCycle;
	uint64_t initialModeGeneration;
};

struct GBALinkSessionId {
	uint64_t acceptedSessionId;
};

struct GBALinkSessionReady {
	uint64_t attachCycle;
	uint64_t initialModeGeneration;
};

struct GBALinkReasonPayload {
	enum GBALinkReason reason;
	enum GBALinkDeterminismCategory category;
};

struct GBALinkGrant {
	uint64_t grantSequence;
	uint64_t horizon;
};

struct GBALinkModeIntent {
	uint64_t modeGeneration;
	uint64_t localCycle;
	enum GBALinkWireMode localMode;
	bool deferred;
};

struct GBALinkModeCommit {
	uint64_t modeGeneration;
	uint64_t commitCycle;
	enum GBALinkWireMode hostMode;
	enum GBALinkWireMode clientMode;
	bool jointlyReady;
};

struct GBALinkModeAck {
	uint64_t modeGeneration;
	uint64_t commitCycle;
};

struct GBALinkTransferStart {
	uint64_t transferSequence;
	uint64_t startCycle;
	uint64_t completionCycle;
	uint16_t outgoingWord;
	uint16_t siocnt;
};

struct GBALinkTransferCommit {
	uint64_t transferSequence;
	uint64_t startCycle;
	uint64_t completionCycle;
	uint16_t words[4];
};

struct GBALinkTransferAbort {
	uint64_t transferSequence;
	uint64_t completionCycle;
	enum GBALinkReason reason;
};

struct GBALinkCompletionCatchup {
	uint64_t transferSequence;
	uint64_t completionSequence;
	uint64_t completionCycle;
	enum GBALinkTransferOutcome pendingOutcome;
};

struct GBALinkCompletionReady {
	uint64_t transferSequence;
	uint64_t completionSequence;
	uint64_t completionCycle;
	enum GBALinkReason abortReason;
	bool hasDeferredMode;
};

struct GBALinkCompletionDecision {
	uint64_t transferSequence;
	uint64_t completionSequence;
	uint64_t completionCycle;
	enum GBALinkTransferOutcome outcome;
	enum GBALinkReason reason;
	uint16_t words[4];
};

struct GBALinkCompletionDecisionAck {
	uint64_t transferSequence;
	uint64_t completionSequence;
	uint64_t completionCycle;
	enum GBALinkTransferOutcome outcome;
};

struct GBALinkPacket {
	struct GBALinkPacketHeader header;
	union {
		struct GBALinkHello hello;
		struct GBALinkAccept accept;
		struct GBALinkSessionId sessionId;
		struct GBALinkSessionReady sessionReady;
		struct GBALinkReasonPayload reason;
		struct GBALinkGrant grant;
		struct GBALinkModeIntent modeIntent;
		struct GBALinkModeCommit modeCommit;
		struct GBALinkModeAck modeAck;
		struct GBALinkTransferStart transferStart;
		struct GBALinkTransferCommit transferCommit;
		struct GBALinkTransferAbort transferAbort;
		struct GBALinkCompletionCatchup completionCatchup;
		struct GBALinkCompletionReady completionReady;
		struct GBALinkCompletionDecision completionDecision;
		struct GBALinkCompletionDecisionAck completionDecisionAck;
	} payload;
};

bool GBALinkMessageAllowsRole(enum GBALinkMessageType type, enum GBALinkRole role);
const char* GBALinkMessageTypeName(enum GBALinkMessageType type);
size_t GBALinkPacketEncodedSize(const struct GBALinkPacket* packet);
bool GBALinkPacketEncode(const struct GBALinkPacket* packet, void* data, size_t capacity, size_t* encodedSize);
enum GBALinkDecodeStatus GBALinkPacketDecode(
    const void* data, size_t size, enum GBALinkRole senderRole, struct GBALinkPacket* packet);
const char* GBALinkDecodeStatusName(enum GBALinkDecodeStatus status);

CXX_GUARD_END

#endif
