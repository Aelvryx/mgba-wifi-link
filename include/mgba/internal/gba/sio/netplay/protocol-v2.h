/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef GBA_SIO_NETPLAY_PROTOCOL_V2_H
#define GBA_SIO_NETPLAY_PROTOCOL_V2_H

#include <mgba-util/common.h>

#include <mgba/internal/gba/replica.h>
#include <mgba/internal/gba/sio/netplay/protocol.h>

CXX_GUARD_START

#define GBA_LINK_V2_PROTOCOL_NAME "mgba-gba-link-replicated-v2"
#define GBA_LINK_V2_PROTOCOL_MAGIC 0x32524C47
#define GBA_LINK_V2_PROTOCOL_VERSION 2
#define GBA_LINK_V2_RUNTIME_COMPATIBILITY_VERSION 1
#define GBA_LINK_V2_HEADER_SIZE 32
#define GBA_LINK_V2_MAX_PACKET_SIZE \
	(GBA_LINK_V2_HEADER_SIZE + 24 + GBA_REPLICA_MAX_CHUNK_SIZE)
#define GBA_LINK_V2_MAX_INPUT_RECORDS 16
#define GBA_LINK_V2_INPUT_KEY_MASK 0x03FF
#define GBA_LINK_V2_MAX_INPUT_DELAY 15

enum GBALinkV2Capability {
	GBA_LINK_V2_CAPABILITY_REPLICATED_PAIR = 1 << 0,
	GBA_LINK_V2_CAPABILITY_BILATERAL_REPLICA = 1 << 1,
	GBA_LINK_V2_CAPABILITY_FIXED_DELAY_INPUT = 1 << 2,
	GBA_LINK_V2_CAPABILITY_STATE_SHA256 = 1 << 3,
	GBA_LINK_V2_CAPABILITY_EXACT_ROM = 1 << 4,
	GBA_LINK_V2_CAPABILITY_ATOMIC_READY = 1 << 5,
};

enum {
	GBA_LINK_V2_REQUIRED_CAPABILITIES =
	    GBA_LINK_V2_CAPABILITY_REPLICATED_PAIR |
	    GBA_LINK_V2_CAPABILITY_BILATERAL_REPLICA |
	    GBA_LINK_V2_CAPABILITY_FIXED_DELAY_INPUT |
	    GBA_LINK_V2_CAPABILITY_STATE_SHA256 |
	    GBA_LINK_V2_CAPABILITY_EXACT_ROM |
	    GBA_LINK_V2_CAPABILITY_ATOMIC_READY,
};

enum GBALinkV2MessageType {
	GBA_LINK_V2_MESSAGE_HELLO = 1,
	GBA_LINK_V2_MESSAGE_ACCEPT = 2,
	GBA_LINK_V2_MESSAGE_ACCEPT_ACK = 3,
	GBA_LINK_V2_MESSAGE_REPLICA_MANIFEST = 4,
	GBA_LINK_V2_MESSAGE_REPLICA_CHUNK = 5,
	GBA_LINK_V2_MESSAGE_REPLICA_INSTALLED = 6,
	GBA_LINK_V2_MESSAGE_SESSION_READY = 7,
	GBA_LINK_V2_MESSAGE_SESSION_READY_ACK = 8,
	GBA_LINK_V2_MESSAGE_INPUT_WINDOW = 9,
	GBA_LINK_V2_MESSAGE_INPUT_BATCH = 10,
	GBA_LINK_V2_MESSAGE_STATE_CHECK = 11,
	GBA_LINK_V2_MESSAGE_DETACH = 12,
	GBA_LINK_V2_MESSAGE_DETACH_ACK = 13,
	GBA_LINK_V2_MESSAGE_REJECT = 14,
};

enum GBALinkV2EncodingMask {
	GBA_LINK_V2_ENCODING_NONE = 1 << GBA_REPLICA_ENCODING_NONE,
	GBA_LINK_V2_ENCODING_DEFLATE = 1 << GBA_REPLICA_ENCODING_DEFLATE,
};

enum GBALinkV2ReadyPolicy {
	GBA_LINK_V2_READY_EXACT_ROM = 1 << 0,
	GBA_LINK_V2_READY_FIXED_DELAY = 1 << 1,
	GBA_LINK_V2_READY_BILATERAL_INSTALL = 1 << 2,
};

enum GBALinkV2Reason {
	GBA_LINK_V2_REASON_PROTOCOL_MISMATCH = 1,
	GBA_LINK_V2_REASON_CAPABILITY_MISMATCH = 2,
	GBA_LINK_V2_REASON_ROM_MISMATCH = 3,
	GBA_LINK_V2_REASON_RUNTIME_MISMATCH = 4,
	GBA_LINK_V2_REASON_ATTACHMENT_TIMEOUT = 5,
	GBA_LINK_V2_REASON_REPLICA_INVALID = 6,
	GBA_LINK_V2_REASON_REPLICA_TIMEOUT = 7,
	GBA_LINK_V2_REASON_INSTALL_FAILED = 8,
	GBA_LINK_V2_REASON_READY_TIMEOUT = 9,
	GBA_LINK_V2_REASON_MALFORMED_PACKET = 10,
	GBA_LINK_V2_REASON_SEQUENCE = 11,
	GBA_LINK_V2_REASON_QUEUE_EXHAUSTED = 12,
	GBA_LINK_V2_REASON_TRANSPORT_STOP = 13,
	GBA_LINK_V2_REASON_PEER_DETACH = 14,
	GBA_LINK_V2_REASON_RESET = 15,
	GBA_LINK_V2_REASON_UNLOAD = 16,
	GBA_LINK_V2_REASON_USER_DISCONNECT = 17,
	GBA_LINK_V2_REASON_INVALID_TRANSITION = 18,
	GBA_LINK_V2_REASON_INPUT_TIMEOUT = 19,
	GBA_LINK_V2_REASON_INPUT_CONFLICT = 20,
	GBA_LINK_V2_REASON_VERIFICATION_TIMEOUT = 21,
	GBA_LINK_V2_REASON_DIVERGENCE = 22,
};

struct GBALinkV2Header {
	enum GBALinkV2MessageType type;
	uint64_t sessionId;
	uint64_t packetSequence;
};

struct GBALinkV2Hello {
	uint64_t capabilities;
	uint64_t requiredCapabilities;
	uint64_t romSize;
	uint8_t romSha1[GBA_LINK_ROM_SHA1_SIZE];
	uint32_t emulationCompatibilityVersion;
	uint32_t runtimeCompatibilityVersion;
	uint32_t maxChunkSize;
	uint16_t supportedEncodings;
	uint16_t minimumInputDelay;
	uint16_t maximumInputDelay;
	bool experimentalRuntime;
};

struct GBALinkV2Accept {
	uint64_t proposedSessionId;
	uint64_t snapshotGeneration;
	uint16_t hostTransportId;
	uint16_t clientTransportId;
	uint32_t runtimeCompatibilityVersion;
	uint32_t selectedChunkSize;
	enum GBAReplicaEncoding selectedEncoding;
	uint16_t inputDelay;
};

struct GBALinkV2AcceptAck {
	uint64_t acceptedSessionId;
	uint64_t snapshotGeneration;
};

struct GBALinkV2ReplicaManifest {
	uint64_t snapshotGeneration;
	uint8_t player;
	uint8_t encoded[GBA_REPLICA_MANIFEST_SIZE];
};

struct GBALinkV2ReplicaChunk {
	uint64_t snapshotGeneration;
	uint8_t player;
	uint32_t offset;
	uint32_t size;
	const uint8_t* data;
};

struct GBALinkV2ReplicaInstalled {
	uint64_t snapshotGeneration;
	bool installed;
	uint8_t playerDigests[2][MGBA_SHA256_DIGEST_SIZE];
};

struct GBALinkV2SessionReady {
	uint64_t snapshotGeneration;
	uint64_t firstFrame;
	uint32_t policy;
	uint16_t inputDelay;
	uint8_t playerDigests[2][MGBA_SHA256_DIGEST_SIZE];
};

struct GBALinkV2InputWindow {
	uint64_t snapshotGeneration;
	uint64_t firstFrame;
	uint16_t frameCount;
	uint16_t inputDelay;
};

struct GBALinkV2InputRecord {
	uint64_t frame;
	uint16_t keys;
};

struct GBALinkV2InputBatch {
	uint64_t snapshotGeneration;
	uint8_t player;
	uint8_t count;
	struct GBALinkV2InputRecord records[GBA_LINK_V2_MAX_INPUT_RECORDS];
};

struct GBALinkV2StateCheck {
	uint64_t snapshotGeneration;
	uint64_t frame;
	uint8_t player;
	uint8_t playerDigests[2][MGBA_SHA256_DIGEST_SIZE];
};

struct GBALinkV2ReasonPayload {
	uint64_t snapshotGeneration;
	enum GBALinkV2Reason reason;
};

struct GBALinkV2Packet {
	struct GBALinkV2Header header;
	union {
		struct GBALinkV2Hello hello;
		struct GBALinkV2Accept accept;
		struct GBALinkV2AcceptAck acceptAck;
		struct GBALinkV2ReplicaManifest replicaManifest;
		struct GBALinkV2ReplicaChunk replicaChunk;
		struct GBALinkV2ReplicaInstalled replicaInstalled;
		struct GBALinkV2SessionReady sessionReady;
		struct GBALinkV2InputWindow inputWindow;
		struct GBALinkV2InputBatch inputBatch;
		struct GBALinkV2StateCheck stateCheck;
		struct GBALinkV2ReasonPayload reason;
	} payload;
};

bool GBALinkV2MessageAllowsRole(
	enum GBALinkV2MessageType type, enum GBALinkRole role);
const char* GBALinkV2MessageTypeName(enum GBALinkV2MessageType type);
size_t GBALinkV2PacketEncodedSize(const struct GBALinkV2Packet* packet);
bool GBALinkV2PacketEncode(
	const struct GBALinkV2Packet* packet, void* data, size_t capacity,
	size_t* encodedSize);
enum GBALinkDecodeStatus GBALinkV2PacketDecode(
	const void* data, size_t size, enum GBALinkRole senderRole,
	struct GBALinkV2Packet* packet);

CXX_GUARD_END

#endif
