/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/gba/sio/netplay/protocol-v2.h>

struct GBALinkV2Writer {
	uint8_t* data;
	size_t capacity;
	size_t offset;
	bool valid;
};

struct GBALinkV2Reader {
	const uint8_t* data;
	size_t size;
	size_t offset;
	bool valid;
};

static void _write8(struct GBALinkV2Writer* writer, uint8_t value) {
	if (!writer->valid || writer->offset >= writer->capacity) {
		writer->valid = false;
		return;
	}
	writer->data[writer->offset++] = value;
}

static void _write16(struct GBALinkV2Writer* writer, uint16_t value) {
	_write8(writer, value);
	_write8(writer, value >> 8);
}

static void _write32(struct GBALinkV2Writer* writer, uint32_t value) {
	_write16(writer, value);
	_write16(writer, value >> 16);
}

static void _write64(struct GBALinkV2Writer* writer, uint64_t value) {
	_write32(writer, value);
	_write32(writer, value >> 32);
}

static void _writeBytes(
	struct GBALinkV2Writer* writer, const void* data, size_t size) {
	const uint8_t* bytes = data;
	for (size_t i = 0; i < size; ++i) {
		_write8(writer, bytes[i]);
	}
}

static void _writeZeroes(struct GBALinkV2Writer* writer, size_t size) {
	for (size_t i = 0; i < size; ++i) {
		_write8(writer, 0);
	}
}

static uint8_t _read8(struct GBALinkV2Reader* reader) {
	if (!reader->valid || reader->offset >= reader->size) {
		reader->valid = false;
		return 0;
	}
	return reader->data[reader->offset++];
}

static uint16_t _read16(struct GBALinkV2Reader* reader) {
	uint16_t value = _read8(reader);
	value |= (uint16_t) _read8(reader) << 8;
	return value;
}

static uint32_t _read32(struct GBALinkV2Reader* reader) {
	uint32_t value = _read16(reader);
	value |= (uint32_t) _read16(reader) << 16;
	return value;
}

static uint64_t _read64(struct GBALinkV2Reader* reader) {
	uint64_t value = _read32(reader);
	value |= (uint64_t) _read32(reader) << 32;
	return value;
}

static void _readBytes(
	struct GBALinkV2Reader* reader, void* data, size_t size) {
	uint8_t* bytes = data;
	for (size_t i = 0; i < size; ++i) {
		bytes[i] = _read8(reader);
	}
}

static bool _readZeroes(struct GBALinkV2Reader* reader, size_t size) {
	bool zero = true;
	for (size_t i = 0; i < size; ++i) {
		zero &= !_read8(reader);
	}
	return zero;
}

static bool _readBoolean(struct GBALinkV2Reader* reader, bool* value) {
	uint8_t encoded = _read8(reader);
	if (encoded > 1) {
		return false;
	}
	*value = encoded != 0;
	return true;
}

static bool _validType(enum GBALinkV2MessageType type) {
	return type >= GBA_LINK_V2_MESSAGE_HELLO &&
	       type <= GBA_LINK_V2_MESSAGE_REJECT;
}

static bool _validReason(enum GBALinkV2Reason reason) {
	return reason >= GBA_LINK_V2_REASON_PROTOCOL_MISMATCH &&
	       reason <= GBA_LINK_V2_REASON_INVALID_TRANSITION;
}

static bool _preSessionType(enum GBALinkV2MessageType type) {
	return type == GBA_LINK_V2_MESSAGE_HELLO ||
	       type == GBA_LINK_V2_MESSAGE_ACCEPT ||
	       type == GBA_LINK_V2_MESSAGE_REJECT;
}

bool GBALinkV2MessageAllowsRole(
	enum GBALinkV2MessageType type, enum GBALinkRole role) {
	if (role != GBA_LINK_ROLE_HOST && role != GBA_LINK_ROLE_CLIENT) {
		return false;
	}
	switch (type) {
	case GBA_LINK_V2_MESSAGE_HELLO:
	case GBA_LINK_V2_MESSAGE_REPLICA_MANIFEST:
	case GBA_LINK_V2_MESSAGE_REPLICA_CHUNK:
	case GBA_LINK_V2_MESSAGE_REPLICA_INSTALLED:
	case GBA_LINK_V2_MESSAGE_INPUT_BATCH:
	case GBA_LINK_V2_MESSAGE_STATE_CHECK:
	case GBA_LINK_V2_MESSAGE_DETACH:
	case GBA_LINK_V2_MESSAGE_DETACH_ACK:
	case GBA_LINK_V2_MESSAGE_REJECT:
		return true;
	case GBA_LINK_V2_MESSAGE_ACCEPT:
	case GBA_LINK_V2_MESSAGE_SESSION_READY:
	case GBA_LINK_V2_MESSAGE_INPUT_WINDOW:
		return role == GBA_LINK_ROLE_HOST;
	case GBA_LINK_V2_MESSAGE_ACCEPT_ACK:
	case GBA_LINK_V2_MESSAGE_SESSION_READY_ACK:
		return role == GBA_LINK_ROLE_CLIENT;
	}
	return false;
}

const char* GBALinkV2MessageTypeName(enum GBALinkV2MessageType type) {
	switch (type) {
	case GBA_LINK_V2_MESSAGE_HELLO: return "HELLO";
	case GBA_LINK_V2_MESSAGE_ACCEPT: return "ACCEPT";
	case GBA_LINK_V2_MESSAGE_ACCEPT_ACK: return "ACCEPT_ACK";
	case GBA_LINK_V2_MESSAGE_REPLICA_MANIFEST: return "REPLICA_MANIFEST";
	case GBA_LINK_V2_MESSAGE_REPLICA_CHUNK: return "REPLICA_CHUNK";
	case GBA_LINK_V2_MESSAGE_REPLICA_INSTALLED: return "REPLICA_INSTALLED";
	case GBA_LINK_V2_MESSAGE_SESSION_READY: return "SESSION_READY";
	case GBA_LINK_V2_MESSAGE_SESSION_READY_ACK: return "SESSION_READY_ACK";
	case GBA_LINK_V2_MESSAGE_INPUT_WINDOW: return "INPUT_WINDOW";
	case GBA_LINK_V2_MESSAGE_INPUT_BATCH: return "INPUT_BATCH";
	case GBA_LINK_V2_MESSAGE_STATE_CHECK: return "STATE_CHECK";
	case GBA_LINK_V2_MESSAGE_DETACH: return "DETACH";
	case GBA_LINK_V2_MESSAGE_DETACH_ACK: return "DETACH_ACK";
	case GBA_LINK_V2_MESSAGE_REJECT: return "REJECT";
	}
	return "(unknown)";
}

static size_t _payloadSize(const struct GBALinkV2Packet* packet) {
	switch (packet->header.type) {
	case GBA_LINK_V2_MESSAGE_HELLO: return 68;
	case GBA_LINK_V2_MESSAGE_ACCEPT: return 40;
	case GBA_LINK_V2_MESSAGE_ACCEPT_ACK: return 16;
	case GBA_LINK_V2_MESSAGE_REPLICA_MANIFEST:
		return 16 + GBA_REPLICA_MANIFEST_SIZE;
	case GBA_LINK_V2_MESSAGE_REPLICA_CHUNK:
		return 24 + packet->payload.replicaChunk.size;
	case GBA_LINK_V2_MESSAGE_REPLICA_INSTALLED: return 80;
	case GBA_LINK_V2_MESSAGE_SESSION_READY:
	case GBA_LINK_V2_MESSAGE_SESSION_READY_ACK: return 88;
	case GBA_LINK_V2_MESSAGE_INPUT_WINDOW: return 24;
	case GBA_LINK_V2_MESSAGE_INPUT_BATCH:
		return 16 + (size_t) packet->payload.inputBatch.count * 16;
	case GBA_LINK_V2_MESSAGE_STATE_CHECK: return 88;
	case GBA_LINK_V2_MESSAGE_DETACH:
	case GBA_LINK_V2_MESSAGE_DETACH_ACK:
	case GBA_LINK_V2_MESSAGE_REJECT: return 16;
	}
	return 0;
}

static bool _validHello(const struct GBALinkV2Hello* hello) {
	uint64_t known = GBA_LINK_V2_REQUIRED_CAPABILITIES;
	uint16_t encodings = GBA_LINK_V2_ENCODING_NONE |
	                     GBA_LINK_V2_ENCODING_DEFLATE;
	return hello->capabilities && !(hello->capabilities & ~known) &&
	       hello->requiredCapabilities == GBA_LINK_V2_REQUIRED_CAPABILITIES &&
	       (hello->capabilities & hello->requiredCapabilities) ==
	           hello->requiredCapabilities &&
	       hello->romSize && hello->emulationCompatibilityVersion &&
	       hello->runtimeCompatibilityVersion ==
	           GBA_LINK_V2_RUNTIME_COMPATIBILITY_VERSION &&
	       hello->maxChunkSize &&
	       hello->maxChunkSize <= GBA_REPLICA_MAX_CHUNK_SIZE &&
	       hello->supportedEncodings &&
	       !(hello->supportedEncodings & ~encodings) &&
	       hello->minimumInputDelay <= hello->maximumInputDelay &&
	       hello->maximumInputDelay <= GBA_LINK_V2_MAX_INPUT_DELAY;
}

static bool _validOwner(uint8_t player, enum GBALinkRole senderRole) {
	return player == (senderRole == GBA_LINK_ROLE_HOST ? 0 : 1);
}

static bool _validatePacket(
	const struct GBALinkV2Packet* packet, enum GBALinkRole senderRole) {
	if (!packet || !_validType(packet->header.type) ||
	    !packet->header.packetSequence ||
	    !GBALinkV2MessageAllowsRole(packet->header.type, senderRole)) {
		return false;
	}
	if (_preSessionType(packet->header.type) != !packet->header.sessionId) {
		return false;
	}
	switch (packet->header.type) {
	case GBA_LINK_V2_MESSAGE_HELLO:
		return _validHello(&packet->payload.hello);
	case GBA_LINK_V2_MESSAGE_ACCEPT: {
		const struct GBALinkV2Accept* accept = &packet->payload.accept;
		return accept->proposedSessionId && accept->snapshotGeneration &&
		       accept->hostTransportId == 0 && accept->clientTransportId == 1 &&
		       accept->runtimeCompatibilityVersion ==
		           GBA_LINK_V2_RUNTIME_COMPATIBILITY_VERSION &&
		       accept->selectedChunkSize &&
		       accept->selectedChunkSize <= GBA_REPLICA_MAX_CHUNK_SIZE &&
		       (accept->selectedEncoding == GBA_REPLICA_ENCODING_NONE ||
		        accept->selectedEncoding == GBA_REPLICA_ENCODING_DEFLATE) &&
		       accept->inputDelay <= GBA_LINK_V2_MAX_INPUT_DELAY;
	}
	case GBA_LINK_V2_MESSAGE_ACCEPT_ACK:
		return packet->payload.acceptAck.acceptedSessionId ==
		           packet->header.sessionId &&
		       packet->payload.acceptAck.snapshotGeneration;
	case GBA_LINK_V2_MESSAGE_REPLICA_MANIFEST:
		if (!packet->payload.replicaManifest.snapshotGeneration ||
		    !_validOwner(packet->payload.replicaManifest.player, senderRole)) {
			return false;
		}
		struct GBAReplicaManifest manifest;
		return GBAReplicaManifestDecode(
		           packet->payload.replicaManifest.encoded,
		           GBA_REPLICA_MANIFEST_SIZE, NULL, &manifest) ==
		           GBA_REPLICA_OK &&
		       manifest.generation ==
		           packet->payload.replicaManifest.snapshotGeneration &&
		       manifest.player == packet->payload.replicaManifest.player;
	case GBA_LINK_V2_MESSAGE_REPLICA_CHUNK: {
		const struct GBALinkV2ReplicaChunk* chunk =
		    &packet->payload.replicaChunk;
		return chunk->snapshotGeneration &&
		       _validOwner(chunk->player, senderRole) && chunk->data &&
		       chunk->size && chunk->size <= GBA_REPLICA_MAX_CHUNK_SIZE &&
		       chunk->offset < GBA_REPLICA_MAX_ENCODED_SIZE &&
		       chunk->size <= GBA_REPLICA_MAX_ENCODED_SIZE - chunk->offset;
	}
	case GBA_LINK_V2_MESSAGE_REPLICA_INSTALLED:
		return packet->payload.replicaInstalled.snapshotGeneration &&
		       packet->payload.replicaInstalled.installed;
	case GBA_LINK_V2_MESSAGE_SESSION_READY:
	case GBA_LINK_V2_MESSAGE_SESSION_READY_ACK: {
		const struct GBALinkV2SessionReady* ready =
		    &packet->payload.sessionReady;
		return ready->snapshotGeneration &&
		       ready->inputDelay <= GBA_LINK_V2_MAX_INPUT_DELAY &&
		       ready->policy ==
		           (GBA_LINK_V2_READY_EXACT_ROM |
		            GBA_LINK_V2_READY_FIXED_DELAY |
		            GBA_LINK_V2_READY_BILATERAL_INSTALL);
	}
	case GBA_LINK_V2_MESSAGE_INPUT_WINDOW:
		return packet->payload.inputWindow.snapshotGeneration &&
		       packet->payload.inputWindow.frameCount &&
		       packet->payload.inputWindow.inputDelay <=
		           GBA_LINK_V2_MAX_INPUT_DELAY;
	case GBA_LINK_V2_MESSAGE_INPUT_BATCH: {
		const struct GBALinkV2InputBatch* batch = &packet->payload.inputBatch;
		if (!batch->snapshotGeneration ||
		    !_validOwner(batch->player, senderRole) || !batch->count ||
		    batch->count > GBA_LINK_V2_MAX_INPUT_RECORDS) {
			return false;
		}
		for (unsigned i = 0; i < batch->count; ++i) {
			if ((batch->records[i].keys & ~GBA_LINK_V2_INPUT_KEY_MASK) ||
			    (i && (batch->records[i - 1].frame == UINT64_MAX ||
			           batch->records[i].frame !=
			              batch->records[i - 1].frame + 1))) {
				return false;
			}
		}
		return true;
	}
	case GBA_LINK_V2_MESSAGE_STATE_CHECK:
		return packet->payload.stateCheck.snapshotGeneration &&
		       _validOwner(packet->payload.stateCheck.player, senderRole);
	case GBA_LINK_V2_MESSAGE_DETACH:
	case GBA_LINK_V2_MESSAGE_DETACH_ACK:
	case GBA_LINK_V2_MESSAGE_REJECT:
		return _validReason(packet->payload.reason.reason);
	}
	return false;
}

size_t GBALinkV2PacketEncodedSize(const struct GBALinkV2Packet* packet) {
	if (!_validatePacket(packet, GBA_LINK_ROLE_HOST) &&
	    !_validatePacket(packet, GBA_LINK_ROLE_CLIENT)) {
		return 0;
	}
	size_t payload = _payloadSize(packet);
	if (!payload || payload > GBA_LINK_V2_MAX_PACKET_SIZE - GBA_LINK_V2_HEADER_SIZE) {
		return 0;
	}
	return GBA_LINK_V2_HEADER_SIZE + payload;
}

static void _encodePayload(
	struct GBALinkV2Writer* writer, const struct GBALinkV2Packet* packet) {
	switch (packet->header.type) {
	case GBA_LINK_V2_MESSAGE_HELLO: {
		const struct GBALinkV2Hello* value = &packet->payload.hello;
		_write64(writer, value->capabilities);
		_write64(writer, value->requiredCapabilities);
		_write64(writer, value->romSize);
		_writeBytes(writer, value->romSha1, sizeof(value->romSha1));
		_write32(writer, value->emulationCompatibilityVersion);
		_write32(writer, value->runtimeCompatibilityVersion);
		_write32(writer, value->maxChunkSize);
		_write16(writer, value->supportedEncodings);
		_write16(writer, value->minimumInputDelay);
		_write16(writer, value->maximumInputDelay);
		_write8(writer, value->experimentalRuntime);
		_writeZeroes(writer, 5);
		break;
	}
	case GBA_LINK_V2_MESSAGE_ACCEPT: {
		const struct GBALinkV2Accept* value = &packet->payload.accept;
		_write64(writer, value->proposedSessionId);
		_write64(writer, value->snapshotGeneration);
		_write16(writer, value->hostTransportId);
		_write16(writer, value->clientTransportId);
		_write32(writer, value->runtimeCompatibilityVersion);
		_write32(writer, value->selectedChunkSize);
		_write8(writer, value->selectedEncoding);
		_write8(writer, 0);
		_write16(writer, value->inputDelay);
		_writeZeroes(writer, 8);
		break;
	}
	case GBA_LINK_V2_MESSAGE_ACCEPT_ACK:
		_write64(writer, packet->payload.acceptAck.acceptedSessionId);
		_write64(writer, packet->payload.acceptAck.snapshotGeneration);
		break;
	case GBA_LINK_V2_MESSAGE_REPLICA_MANIFEST:
		_write64(writer, packet->payload.replicaManifest.snapshotGeneration);
		_write8(writer, packet->payload.replicaManifest.player);
		_writeZeroes(writer, 7);
		_writeBytes(writer, packet->payload.replicaManifest.encoded,
		    GBA_REPLICA_MANIFEST_SIZE);
		break;
	case GBA_LINK_V2_MESSAGE_REPLICA_CHUNK:
		_write64(writer, packet->payload.replicaChunk.snapshotGeneration);
		_write8(writer, packet->payload.replicaChunk.player);
		_writeZeroes(writer, 3);
		_write32(writer, packet->payload.replicaChunk.offset);
		_write32(writer, packet->payload.replicaChunk.size);
		_writeZeroes(writer, 4);
		_writeBytes(writer, packet->payload.replicaChunk.data,
		    packet->payload.replicaChunk.size);
		break;
	case GBA_LINK_V2_MESSAGE_REPLICA_INSTALLED:
		_write64(writer, packet->payload.replicaInstalled.snapshotGeneration);
		_write8(writer, packet->payload.replicaInstalled.installed);
		_writeZeroes(writer, 7);
		_writeBytes(writer, packet->payload.replicaInstalled.playerDigests,
		    sizeof(packet->payload.replicaInstalled.playerDigests));
		break;
	case GBA_LINK_V2_MESSAGE_SESSION_READY:
	case GBA_LINK_V2_MESSAGE_SESSION_READY_ACK:
		_write64(writer, packet->payload.sessionReady.snapshotGeneration);
		_write64(writer, packet->payload.sessionReady.firstFrame);
		_write32(writer, packet->payload.sessionReady.policy);
		_write16(writer, packet->payload.sessionReady.inputDelay);
		_writeZeroes(writer, 2);
		_writeBytes(writer, packet->payload.sessionReady.playerDigests,
		    sizeof(packet->payload.sessionReady.playerDigests));
		break;
	case GBA_LINK_V2_MESSAGE_INPUT_WINDOW:
		_write64(writer, packet->payload.inputWindow.snapshotGeneration);
		_write64(writer, packet->payload.inputWindow.firstFrame);
		_write16(writer, packet->payload.inputWindow.frameCount);
		_write16(writer, packet->payload.inputWindow.inputDelay);
		_writeZeroes(writer, 4);
		break;
	case GBA_LINK_V2_MESSAGE_INPUT_BATCH:
		_write64(writer, packet->payload.inputBatch.snapshotGeneration);
		_write8(writer, packet->payload.inputBatch.player);
		_write8(writer, packet->payload.inputBatch.count);
		_writeZeroes(writer, 6);
		for (unsigned i = 0; i < packet->payload.inputBatch.count; ++i) {
			_write64(writer, packet->payload.inputBatch.records[i].frame);
			_write16(writer, packet->payload.inputBatch.records[i].keys);
			_writeZeroes(writer, 6);
		}
		break;
	case GBA_LINK_V2_MESSAGE_STATE_CHECK:
		_write64(writer, packet->payload.stateCheck.snapshotGeneration);
		_write64(writer, packet->payload.stateCheck.frame);
		_write8(writer, packet->payload.stateCheck.player);
		_writeZeroes(writer, 7);
		_writeBytes(writer, packet->payload.stateCheck.playerDigests,
		    sizeof(packet->payload.stateCheck.playerDigests));
		break;
	case GBA_LINK_V2_MESSAGE_DETACH:
	case GBA_LINK_V2_MESSAGE_DETACH_ACK:
	case GBA_LINK_V2_MESSAGE_REJECT:
		_write64(writer, packet->payload.reason.snapshotGeneration);
		_write16(writer, packet->payload.reason.reason);
		_writeZeroes(writer, 6);
		break;
	}
}

bool GBALinkV2PacketEncode(
	const struct GBALinkV2Packet* packet, void* data, size_t capacity,
	size_t* encodedSize) {
	if (encodedSize) {
		*encodedSize = 0;
	}
	size_t size = GBALinkV2PacketEncodedSize(packet);
	if (!data || !size || capacity < size) {
		return false;
	}
	struct GBALinkV2Writer writer = {
		.data = data,
		.capacity = capacity,
		.valid = true,
	};
	_write32(&writer, GBA_LINK_V2_PROTOCOL_MAGIC);
	_write16(&writer, GBA_LINK_V2_PROTOCOL_VERSION);
	_write16(&writer, packet->header.type);
	_write32(&writer, size - GBA_LINK_V2_HEADER_SIZE);
	_write32(&writer, 0);
	_write64(&writer, packet->header.sessionId);
	_write64(&writer, packet->header.packetSequence);
	_encodePayload(&writer, packet);
	if (!writer.valid || writer.offset != size) {
		return false;
	}
	if (encodedSize) {
		*encodedSize = size;
	}
	return true;
}

static enum GBALinkDecodeStatus _decodePayload(
	struct GBALinkV2Reader* reader, struct GBALinkV2Packet* packet) {
	bool reserved = true;
	bool canonical = true;
	switch (packet->header.type) {
	case GBA_LINK_V2_MESSAGE_HELLO: {
		struct GBALinkV2Hello* value = &packet->payload.hello;
		value->capabilities = _read64(reader);
		value->requiredCapabilities = _read64(reader);
		value->romSize = _read64(reader);
		_readBytes(reader, value->romSha1, sizeof(value->romSha1));
		value->emulationCompatibilityVersion = _read32(reader);
		value->runtimeCompatibilityVersion = _read32(reader);
		value->maxChunkSize = _read32(reader);
		value->supportedEncodings = _read16(reader);
		value->minimumInputDelay = _read16(reader);
		value->maximumInputDelay = _read16(reader);
		canonical &= _readBoolean(reader, &value->experimentalRuntime);
		reserved &= _readZeroes(reader, 5);
		break;
	}
	case GBA_LINK_V2_MESSAGE_ACCEPT: {
		struct GBALinkV2Accept* value = &packet->payload.accept;
		value->proposedSessionId = _read64(reader);
		value->snapshotGeneration = _read64(reader);
		value->hostTransportId = _read16(reader);
		value->clientTransportId = _read16(reader);
		value->runtimeCompatibilityVersion = _read32(reader);
		value->selectedChunkSize = _read32(reader);
		value->selectedEncoding = _read8(reader);
		reserved &= !_read8(reader);
		value->inputDelay = _read16(reader);
		reserved &= _readZeroes(reader, 8);
		break;
	}
	case GBA_LINK_V2_MESSAGE_ACCEPT_ACK:
		packet->payload.acceptAck.acceptedSessionId = _read64(reader);
		packet->payload.acceptAck.snapshotGeneration = _read64(reader);
		break;
	case GBA_LINK_V2_MESSAGE_REPLICA_MANIFEST:
		packet->payload.replicaManifest.snapshotGeneration = _read64(reader);
		packet->payload.replicaManifest.player = _read8(reader);
		reserved &= _readZeroes(reader, 7);
		_readBytes(reader, packet->payload.replicaManifest.encoded,
		    GBA_REPLICA_MANIFEST_SIZE);
		break;
	case GBA_LINK_V2_MESSAGE_REPLICA_CHUNK:
		packet->payload.replicaChunk.snapshotGeneration = _read64(reader);
		packet->payload.replicaChunk.player = _read8(reader);
		reserved &= _readZeroes(reader, 3);
		packet->payload.replicaChunk.offset = _read32(reader);
		packet->payload.replicaChunk.size = _read32(reader);
		reserved &= _readZeroes(reader, 4);
		packet->payload.replicaChunk.data = &reader->data[reader->offset];
		reader->offset += packet->payload.replicaChunk.size;
		if (reader->offset > reader->size) {
			reader->valid = false;
		}
		break;
	case GBA_LINK_V2_MESSAGE_REPLICA_INSTALLED:
		packet->payload.replicaInstalled.snapshotGeneration = _read64(reader);
		canonical &= _readBoolean(
		    reader, &packet->payload.replicaInstalled.installed);
		reserved &= _readZeroes(reader, 7);
		_readBytes(reader, packet->payload.replicaInstalled.playerDigests,
		    sizeof(packet->payload.replicaInstalled.playerDigests));
		break;
	case GBA_LINK_V2_MESSAGE_SESSION_READY:
	case GBA_LINK_V2_MESSAGE_SESSION_READY_ACK:
		packet->payload.sessionReady.snapshotGeneration = _read64(reader);
		packet->payload.sessionReady.firstFrame = _read64(reader);
		packet->payload.sessionReady.policy = _read32(reader);
		packet->payload.sessionReady.inputDelay = _read16(reader);
		reserved &= _readZeroes(reader, 2);
		_readBytes(reader, packet->payload.sessionReady.playerDigests,
		    sizeof(packet->payload.sessionReady.playerDigests));
		break;
	case GBA_LINK_V2_MESSAGE_INPUT_WINDOW:
		packet->payload.inputWindow.snapshotGeneration = _read64(reader);
		packet->payload.inputWindow.firstFrame = _read64(reader);
		packet->payload.inputWindow.frameCount = _read16(reader);
		packet->payload.inputWindow.inputDelay = _read16(reader);
		reserved &= _readZeroes(reader, 4);
		break;
	case GBA_LINK_V2_MESSAGE_INPUT_BATCH:
		packet->payload.inputBatch.snapshotGeneration = _read64(reader);
		packet->payload.inputBatch.player = _read8(reader);
		packet->payload.inputBatch.count = _read8(reader);
		reserved &= _readZeroes(reader, 6);
		if (packet->payload.inputBatch.count > GBA_LINK_V2_MAX_INPUT_RECORDS) {
			return GBA_LINK_DECODE_LENGTH;
		}
		for (unsigned i = 0; i < packet->payload.inputBatch.count; ++i) {
			packet->payload.inputBatch.records[i].frame = _read64(reader);
			packet->payload.inputBatch.records[i].keys = _read16(reader);
			reserved &= _readZeroes(reader, 6);
		}
		break;
	case GBA_LINK_V2_MESSAGE_STATE_CHECK:
		packet->payload.stateCheck.snapshotGeneration = _read64(reader);
		packet->payload.stateCheck.frame = _read64(reader);
		packet->payload.stateCheck.player = _read8(reader);
		reserved &= _readZeroes(reader, 7);
		_readBytes(reader, packet->payload.stateCheck.playerDigests,
		    sizeof(packet->payload.stateCheck.playerDigests));
		break;
	case GBA_LINK_V2_MESSAGE_DETACH:
	case GBA_LINK_V2_MESSAGE_DETACH_ACK:
	case GBA_LINK_V2_MESSAGE_REJECT:
		packet->payload.reason.snapshotGeneration = _read64(reader);
		packet->payload.reason.reason = _read16(reader);
		reserved &= _readZeroes(reader, 6);
		break;
	}
	if (!reader->valid) {
		return GBA_LINK_DECODE_TRUNCATED;
	}
	if (!canonical) {
		return GBA_LINK_DECODE_FIELD;
	}
	if (!reserved) {
		return GBA_LINK_DECODE_RESERVED;
	}
	return GBA_LINK_DECODE_OK;
}

enum GBALinkDecodeStatus GBALinkV2PacketDecode(
	const void* data, size_t size, enum GBALinkRole senderRole,
	struct GBALinkV2Packet* packet) {
	if (!data || !packet) {
		return GBA_LINK_DECODE_NULL;
	}
	if (size < GBA_LINK_V2_HEADER_SIZE) {
		return GBA_LINK_DECODE_TRUNCATED;
	}
	if (size > GBA_LINK_V2_MAX_PACKET_SIZE) {
		return GBA_LINK_DECODE_OVERSIZED;
	}
	struct GBALinkV2Reader reader = {
		.data = data,
		.size = size,
		.valid = true,
	};
	if (_read32(&reader) != GBA_LINK_V2_PROTOCOL_MAGIC) {
		return GBA_LINK_DECODE_MAGIC;
	}
	if (_read16(&reader) != GBA_LINK_V2_PROTOCOL_VERSION) {
		return GBA_LINK_DECODE_VERSION;
	}
	struct GBALinkV2Packet decoded;
	memset(&decoded, 0, sizeof(decoded));
	decoded.header.type = _read16(&reader);
	uint32_t payloadSize = _read32(&reader);
	if (_read32(&reader)) {
		return GBA_LINK_DECODE_RESERVED;
	}
	decoded.header.sessionId = _read64(&reader);
	decoded.header.packetSequence = _read64(&reader);
	if (!_validType(decoded.header.type)) {
		return GBA_LINK_DECODE_TYPE;
	}
	if (!GBALinkV2MessageAllowsRole(decoded.header.type, senderRole)) {
		return GBA_LINK_DECODE_ROLE;
	}
	if (payloadSize != size - GBA_LINK_V2_HEADER_SIZE) {
		return GBA_LINK_DECODE_LENGTH;
	}
	if (_preSessionType(decoded.header.type) != !decoded.header.sessionId) {
		return GBA_LINK_DECODE_SESSION;
	}
	if (!decoded.header.packetSequence) {
		return GBA_LINK_DECODE_SEQUENCE;
	}
	if (decoded.header.type != GBA_LINK_V2_MESSAGE_REPLICA_CHUNK &&
	    decoded.header.type != GBA_LINK_V2_MESSAGE_INPUT_BATCH &&
	    _payloadSize(&decoded) != payloadSize) {
		return GBA_LINK_DECODE_LENGTH;
	}
	enum GBALinkDecodeStatus status = _decodePayload(&reader, &decoded);
	if (status != GBA_LINK_DECODE_OK) {
		return status;
	}
	if (reader.offset != size || _payloadSize(&decoded) != payloadSize) {
		return GBA_LINK_DECODE_LENGTH;
	}
	if (!_validatePacket(&decoded, senderRole)) {
		return GBA_LINK_DECODE_FIELD;
	}
	*packet = decoded;
	return GBA_LINK_DECODE_OK;
}
