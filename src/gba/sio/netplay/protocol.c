/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/gba/sio/netplay/protocol.h>

struct GBALinkWriter {
	uint8_t* data;
	size_t capacity;
	size_t offset;
	bool valid;
};

struct GBALinkReader {
	const uint8_t* data;
	size_t size;
	size_t offset;
	bool valid;
};

static void _write8(struct GBALinkWriter* writer, uint8_t value) {
	if (!writer->valid || writer->offset >= writer->capacity) {
		writer->valid = false;
		return;
	}
	writer->data[writer->offset++] = value;
}

static void _write16(struct GBALinkWriter* writer, uint16_t value) {
	_write8(writer, value);
	_write8(writer, value >> 8);
}

static void _write32(struct GBALinkWriter* writer, uint32_t value) {
	_write16(writer, value);
	_write16(writer, value >> 16);
}

static void _write64(struct GBALinkWriter* writer, uint64_t value) {
	_write32(writer, value);
	_write32(writer, value >> 32);
}

static void _writeBytes(struct GBALinkWriter* writer, const void* data, size_t size) {
	const uint8_t* bytes = data;
	for (size_t i = 0; i < size; ++i) {
		_write8(writer, bytes[i]);
	}
}

static void _writeZeroes(struct GBALinkWriter* writer, size_t size) {
	for (size_t i = 0; i < size; ++i) {
		_write8(writer, 0);
	}
}

static uint8_t _read8(struct GBALinkReader* reader) {
	if (!reader->valid || reader->offset >= reader->size) {
		reader->valid = false;
		return 0;
	}
	return reader->data[reader->offset++];
}

static uint16_t _read16(struct GBALinkReader* reader) {
	uint16_t value = _read8(reader);
	value |= (uint16_t) _read8(reader) << 8;
	return value;
}

static uint32_t _read32(struct GBALinkReader* reader) {
	uint32_t value = _read16(reader);
	value |= (uint32_t) _read16(reader) << 16;
	return value;
}

static uint64_t _read64(struct GBALinkReader* reader) {
	uint64_t value = _read32(reader);
	value |= (uint64_t) _read32(reader) << 32;
	return value;
}

static void _readBytes(struct GBALinkReader* reader, void* data, size_t size) {
	uint8_t* bytes = data;
	for (size_t i = 0; i < size; ++i) {
		bytes[i] = _read8(reader);
	}
}

static bool _readZeroes(struct GBALinkReader* reader, size_t size) {
	bool zero = true;
	for (size_t i = 0; i < size; ++i) {
		zero &= !_read8(reader);
	}
	return zero;
}

static bool _readBoolean(
    struct GBALinkReader* reader, bool* value) {
	uint8_t encoded = _read8(reader);
	if (encoded > 1) {
		return false;
	}
	*value = encoded != 0;
	return true;
}

static bool _validMode(enum GBALinkWireMode mode) {
	switch (mode) {
	case GBA_LINK_MODE_NORMAL_8:
	case GBA_LINK_MODE_NORMAL_32:
	case GBA_LINK_MODE_MULTI:
	case GBA_LINK_MODE_UART:
	case GBA_LINK_MODE_GPIO:
	case GBA_LINK_MODE_JOYBUS:
		return true;
	}
	return false;
}

static bool _validReason(enum GBALinkReason reason, bool allowNone) {
	return (allowNone && !reason) || (reason >= GBA_LINK_REASON_PROTOCOL_MISMATCH &&
	                                 reason <= GBA_LINK_REASON_GRANT_TIMEOUT);
}

static bool _validOutcome(enum GBALinkTransferOutcome outcome) {
	return outcome == GBA_LINK_OUTCOME_SUCCESS || outcome == GBA_LINK_OUTCOME_ERROR;
}

static bool _validCategory(enum GBALinkDeterminismCategory category) {
	return category >= GBA_LINK_DETERMINISM_BIOS && category <= GBA_LINK_DETERMINISM_CHEATS;
}

static bool _validType(enum GBALinkMessageType type) {
	return type >= GBA_LINK_MESSAGE_HELLO &&
	       type <= GBA_LINK_MESSAGE_COMPLETION_DECISION_ACK;
}

static bool _preSessionType(enum GBALinkMessageType type) {
	return type == GBA_LINK_MESSAGE_HELLO ||
	       type == GBA_LINK_MESSAGE_ACCEPT ||
	       type == GBA_LINK_MESSAGE_REJECT;
}

static size_t _payloadSize(const struct GBALinkPacket* packet) {
	switch (packet->header.type) {
	case GBA_LINK_MESSAGE_HELLO:
		if (packet->payload.hello.digestCount > GBA_LINK_MAX_DETERMINISM_DIGESTS) {
			return 0;
		}
		return 56 + packet->payload.hello.digestCount * 24;
	case GBA_LINK_MESSAGE_ACCEPT:
		return 40;
	case GBA_LINK_MESSAGE_ACCEPT_ACK:
		return 8;
	case GBA_LINK_MESSAGE_SESSION_READY:
	case GBA_LINK_MESSAGE_SESSION_READY_ACK:
		return 16;
	case GBA_LINK_MESSAGE_REJECT:
	case GBA_LINK_MESSAGE_DETACH:
	case GBA_LINK_MESSAGE_DETACH_ACK:
		return 4;
	case GBA_LINK_MESSAGE_EXECUTION_GRANT:
	case GBA_LINK_MESSAGE_GRANT_ACK:
	case GBA_LINK_MESSAGE_MODE_ACK:
		return 16;
	case GBA_LINK_MESSAGE_MODE_INTENT:
	case GBA_LINK_MESSAGE_MODE_COMMIT:
	case GBA_LINK_MESSAGE_TRANSFER_ABORT:
		return 24;
	case GBA_LINK_MESSAGE_TRANSFER_START:
	case GBA_LINK_MESSAGE_TRANSFER_READY:
		return 28;
	case GBA_LINK_MESSAGE_TRANSFER_COMMIT:
	case GBA_LINK_MESSAGE_COMPLETION_CATCHUP:
	case GBA_LINK_MESSAGE_COMPLETION_READY:
	case GBA_LINK_MESSAGE_COMPLETION_DECISION_ACK:
		return 32;
	case GBA_LINK_MESSAGE_COMPLETION_DECISION:
		return 36;
	}
	return 0;
}

static bool _validatePacket(const struct GBALinkPacket* packet) {
	if (!packet || !_validType(packet->header.type) || !packet->header.packetSequence) {
		return false;
	}
	if (_preSessionType(packet->header.type) != !packet->header.sessionId) {
		return false;
	}

	switch (packet->header.type) {
	case GBA_LINK_MESSAGE_HELLO: {
		const struct GBALinkHello* hello = &packet->payload.hello;
		uint64_t knownCapabilities = GBA_LINK_MVP_CAPABILITIES;
		uint32_t knownPolicies =
		    (1U << GBA_LINK_COMPATIBILITY_EXACT_ROM) |
		    (1U << GBA_LINK_COMPATIBILITY_GROUP);
		if (!hello->romSize || !hello->capabilities ||
		    (hello->capabilities & ~knownCapabilities) ||
		    !hello->supportedPolicies ||
		    (hello->supportedPolicies & ~knownPolicies) ||
		    !hello->emulationCompatibilityVersion || !_validMode(hello->initialMode) ||
		    hello->digestCount != GBA_LINK_MAX_DETERMINISM_DIGESTS) {
			return false;
		}
		for (unsigned i = 0; i < hello->digestCount; ++i) {
			if (hello->digests[i].category != (enum GBALinkDeterminismCategory) (i + 1)) {
				return false;
			}
		}
		return true;
	}
	case GBA_LINK_MESSAGE_ACCEPT: {
		const struct GBALinkAccept* accept = &packet->payload.accept;
		return accept->proposedSessionId && accept->hostTransportId == 0 &&
		       accept->clientTransportId == 1 &&
		       (accept->policy == GBA_LINK_COMPATIBILITY_EXACT_ROM ||
		        accept->policy == GBA_LINK_COMPATIBILITY_GROUP) &&
		       (accept->policy != GBA_LINK_COMPATIBILITY_EXACT_ROM || !accept->compatibilityGroup) &&
		       accept->initialModeGeneration;
	}
	case GBA_LINK_MESSAGE_ACCEPT_ACK:
		return packet->payload.sessionId.acceptedSessionId == packet->header.sessionId;
	case GBA_LINK_MESSAGE_SESSION_READY:
	case GBA_LINK_MESSAGE_SESSION_READY_ACK:
		return packet->payload.sessionReady.initialModeGeneration != 0;
	case GBA_LINK_MESSAGE_REJECT:
	case GBA_LINK_MESSAGE_DETACH:
	case GBA_LINK_MESSAGE_DETACH_ACK:
		return _validReason(packet->payload.reason.reason, false) &&
		       (!packet->payload.reason.category || _validCategory(packet->payload.reason.category));
	case GBA_LINK_MESSAGE_EXECUTION_GRANT:
	case GBA_LINK_MESSAGE_GRANT_ACK:
		return packet->payload.grant.grantSequence != 0;
	case GBA_LINK_MESSAGE_MODE_INTENT:
		return packet->payload.modeIntent.modeGeneration != 0 &&
		       _validMode(packet->payload.modeIntent.localMode);
	case GBA_LINK_MESSAGE_MODE_COMMIT:
		return packet->payload.modeCommit.modeGeneration != 0 &&
		       _validMode(packet->payload.modeCommit.hostMode) &&
		       _validMode(packet->payload.modeCommit.clientMode) &&
		       packet->payload.modeCommit.jointlyReady ==
		           (packet->payload.modeCommit.hostMode == GBA_LINK_MODE_MULTI &&
		            packet->payload.modeCommit.clientMode == GBA_LINK_MODE_MULTI);
	case GBA_LINK_MESSAGE_MODE_ACK:
		return packet->payload.modeAck.modeGeneration != 0;
	case GBA_LINK_MESSAGE_TRANSFER_START:
	case GBA_LINK_MESSAGE_TRANSFER_READY:
		return packet->payload.transferStart.transferSequence != 0 &&
		       packet->payload.transferStart.completionCycle > packet->payload.transferStart.startCycle &&
		       (packet->payload.transferStart.siocnt & 0x3000) == 0x2000;
	case GBA_LINK_MESSAGE_TRANSFER_COMMIT:
		return packet->payload.transferCommit.transferSequence != 0 &&
		       packet->payload.transferCommit.completionCycle > packet->payload.transferCommit.startCycle &&
		       packet->payload.transferCommit.words[2] == 0xFFFF &&
		       packet->payload.transferCommit.words[3] == 0xFFFF;
	case GBA_LINK_MESSAGE_TRANSFER_ABORT:
		return packet->payload.transferAbort.transferSequence != 0 &&
		       _validReason(packet->payload.transferAbort.reason, false);
	case GBA_LINK_MESSAGE_COMPLETION_CATCHUP:
		return packet->payload.completionCatchup.transferSequence != 0 &&
		       packet->payload.completionCatchup.completionSequence != 0 &&
		       _validOutcome(packet->payload.completionCatchup.pendingOutcome);
	case GBA_LINK_MESSAGE_COMPLETION_READY:
		return packet->payload.completionReady.transferSequence != 0 &&
		       packet->payload.completionReady.completionSequence != 0 &&
		       _validReason(packet->payload.completionReady.abortReason, true);
	case GBA_LINK_MESSAGE_COMPLETION_DECISION:
		return packet->payload.completionDecision.transferSequence != 0 &&
		       packet->payload.completionDecision.completionSequence != 0 &&
		       _validOutcome(packet->payload.completionDecision.outcome) &&
		       _validReason(packet->payload.completionDecision.reason,
		                    packet->payload.completionDecision.outcome == GBA_LINK_OUTCOME_SUCCESS) &&
		       (packet->payload.completionDecision.outcome != GBA_LINK_OUTCOME_SUCCESS ||
		        !packet->payload.completionDecision.reason) &&
		       packet->payload.completionDecision.words[2] == 0xFFFF &&
		       packet->payload.completionDecision.words[3] == 0xFFFF;
	case GBA_LINK_MESSAGE_COMPLETION_DECISION_ACK:
		return packet->payload.completionDecisionAck.transferSequence != 0 &&
		       packet->payload.completionDecisionAck.completionSequence != 0 &&
		       _validOutcome(packet->payload.completionDecisionAck.outcome);
	}
	return false;
}

bool GBALinkMessageAllowsRole(enum GBALinkMessageType type, enum GBALinkRole role) {
	if (role != GBA_LINK_ROLE_HOST && role != GBA_LINK_ROLE_CLIENT) {
		return false;
	}
	switch (type) {
	case GBA_LINK_MESSAGE_HELLO:
	case GBA_LINK_MESSAGE_REJECT:
	case GBA_LINK_MESSAGE_DETACH:
	case GBA_LINK_MESSAGE_DETACH_ACK:
	case GBA_LINK_MESSAGE_MODE_INTENT:
	case GBA_LINK_MESSAGE_MODE_ACK:
	case GBA_LINK_MESSAGE_TRANSFER_ABORT:
		return true;
	case GBA_LINK_MESSAGE_ACCEPT:
	case GBA_LINK_MESSAGE_SESSION_READY:
	case GBA_LINK_MESSAGE_EXECUTION_GRANT:
	case GBA_LINK_MESSAGE_MODE_COMMIT:
	case GBA_LINK_MESSAGE_TRANSFER_START:
	case GBA_LINK_MESSAGE_TRANSFER_COMMIT:
	case GBA_LINK_MESSAGE_COMPLETION_CATCHUP:
	case GBA_LINK_MESSAGE_COMPLETION_DECISION:
		return role == GBA_LINK_ROLE_HOST;
	case GBA_LINK_MESSAGE_ACCEPT_ACK:
	case GBA_LINK_MESSAGE_SESSION_READY_ACK:
	case GBA_LINK_MESSAGE_GRANT_ACK:
	case GBA_LINK_MESSAGE_TRANSFER_READY:
	case GBA_LINK_MESSAGE_COMPLETION_READY:
	case GBA_LINK_MESSAGE_COMPLETION_DECISION_ACK:
		return role == GBA_LINK_ROLE_CLIENT;
	}
	return false;
}

const char* GBALinkMessageTypeName(enum GBALinkMessageType type) {
	switch (type) {
	case GBA_LINK_MESSAGE_HELLO: return "HELLO";
	case GBA_LINK_MESSAGE_ACCEPT: return "ACCEPT";
	case GBA_LINK_MESSAGE_ACCEPT_ACK: return "ACCEPT_ACK";
	case GBA_LINK_MESSAGE_SESSION_READY: return "SESSION_READY";
	case GBA_LINK_MESSAGE_SESSION_READY_ACK: return "SESSION_READY_ACK";
	case GBA_LINK_MESSAGE_REJECT: return "REJECT";
	case GBA_LINK_MESSAGE_DETACH: return "DETACH";
	case GBA_LINK_MESSAGE_DETACH_ACK: return "DETACH_ACK";
	case GBA_LINK_MESSAGE_EXECUTION_GRANT: return "EXECUTION_GRANT";
	case GBA_LINK_MESSAGE_GRANT_ACK: return "GRANT_ACK";
	case GBA_LINK_MESSAGE_MODE_INTENT: return "MODE_INTENT";
	case GBA_LINK_MESSAGE_MODE_COMMIT: return "MODE_COMMIT";
	case GBA_LINK_MESSAGE_MODE_ACK: return "MODE_ACK";
	case GBA_LINK_MESSAGE_TRANSFER_START: return "TRANSFER_START";
	case GBA_LINK_MESSAGE_TRANSFER_READY: return "TRANSFER_READY";
	case GBA_LINK_MESSAGE_TRANSFER_COMMIT: return "TRANSFER_COMMIT";
	case GBA_LINK_MESSAGE_TRANSFER_ABORT: return "TRANSFER_ABORT";
	case GBA_LINK_MESSAGE_COMPLETION_CATCHUP: return "COMPLETION_CATCHUP";
	case GBA_LINK_MESSAGE_COMPLETION_READY: return "COMPLETION_READY";
	case GBA_LINK_MESSAGE_COMPLETION_DECISION: return "COMPLETION_DECISION";
	case GBA_LINK_MESSAGE_COMPLETION_DECISION_ACK: return "COMPLETION_DECISION_ACK";
	}
	return "(unknown)";
}

size_t GBALinkPacketEncodedSize(const struct GBALinkPacket* packet) {
	if (!_validatePacket(packet)) {
		return 0;
	}
	size_t payloadSize = _payloadSize(packet);
	if (!payloadSize || payloadSize > GBA_LINK_MAX_PAYLOAD_SIZE) {
		return 0;
	}
	return GBA_LINK_HEADER_SIZE + payloadSize;
}

static void _encodePayload(struct GBALinkWriter* writer, const struct GBALinkPacket* packet) {
	switch (packet->header.type) {
	case GBA_LINK_MESSAGE_HELLO: {
		const struct GBALinkHello* hello = &packet->payload.hello;
		_write64(writer, hello->capabilities);
		_write64(writer, hello->romSize);
		_writeBytes(writer, hello->romSha1, sizeof(hello->romSha1));
		_write32(writer, hello->supportedPolicies);
		_write32(writer, hello->emulationCompatibilityVersion);
		_write8(writer, hello->initialMode);
		_write8(writer, hello->digestCount);
		_writeZeroes(writer, 2);
		_write64(writer, hello->rendezvousCycle);
		for (unsigned i = 0; i < hello->digestCount; ++i) {
			_write8(writer, hello->digests[i].category);
			_writeZeroes(writer, 3);
			_writeBytes(writer, hello->digests[i].digest, GBA_LINK_DIGEST_SIZE);
		}
		break;
	}
	case GBA_LINK_MESSAGE_ACCEPT: {
		const struct GBALinkAccept* accept = &packet->payload.accept;
		_write64(writer, accept->proposedSessionId);
		_write16(writer, accept->hostTransportId);
		_write16(writer, accept->clientTransportId);
		_write8(writer, accept->policy);
		_writeZeroes(writer, 3);
		_write64(writer, accept->compatibilityGroup);
		_write64(writer, accept->attachCycle);
		_write64(writer, accept->initialModeGeneration);
		break;
	}
	case GBA_LINK_MESSAGE_ACCEPT_ACK:
		_write64(writer, packet->payload.sessionId.acceptedSessionId);
		break;
	case GBA_LINK_MESSAGE_SESSION_READY:
	case GBA_LINK_MESSAGE_SESSION_READY_ACK:
		_write64(writer, packet->payload.sessionReady.attachCycle);
		_write64(writer, packet->payload.sessionReady.initialModeGeneration);
		break;
	case GBA_LINK_MESSAGE_REJECT:
	case GBA_LINK_MESSAGE_DETACH:
	case GBA_LINK_MESSAGE_DETACH_ACK:
		_write16(writer, packet->payload.reason.reason);
		_write8(writer, packet->payload.reason.category);
		_write8(writer, 0);
		break;
	case GBA_LINK_MESSAGE_EXECUTION_GRANT:
	case GBA_LINK_MESSAGE_GRANT_ACK:
		_write64(writer, packet->payload.grant.grantSequence);
		_write64(writer, packet->payload.grant.horizon);
		break;
	case GBA_LINK_MESSAGE_MODE_INTENT:
		_write64(writer, packet->payload.modeIntent.modeGeneration);
		_write64(writer, packet->payload.modeIntent.localCycle);
		_write8(writer, packet->payload.modeIntent.localMode);
		_write8(writer, packet->payload.modeIntent.deferred);
		_writeZeroes(writer, 6);
		break;
	case GBA_LINK_MESSAGE_MODE_COMMIT:
		_write64(writer, packet->payload.modeCommit.modeGeneration);
		_write64(writer, packet->payload.modeCommit.commitCycle);
		_write8(writer, packet->payload.modeCommit.hostMode);
		_write8(writer, packet->payload.modeCommit.clientMode);
		_write8(writer, packet->payload.modeCommit.jointlyReady);
		_writeZeroes(writer, 5);
		break;
	case GBA_LINK_MESSAGE_MODE_ACK:
		_write64(writer, packet->payload.modeAck.modeGeneration);
		_write64(writer, packet->payload.modeAck.commitCycle);
		break;
	case GBA_LINK_MESSAGE_TRANSFER_START:
	case GBA_LINK_MESSAGE_TRANSFER_READY:
		_write64(writer, packet->payload.transferStart.transferSequence);
		_write64(writer, packet->payload.transferStart.startCycle);
		_write64(writer, packet->payload.transferStart.completionCycle);
		_write16(writer, packet->payload.transferStart.outgoingWord);
		_write16(writer, packet->payload.transferStart.siocnt);
		break;
	case GBA_LINK_MESSAGE_TRANSFER_COMMIT:
		_write64(writer, packet->payload.transferCommit.transferSequence);
		_write64(writer, packet->payload.transferCommit.startCycle);
		_write64(writer, packet->payload.transferCommit.completionCycle);
		for (unsigned i = 0; i < 4; ++i) {
			_write16(writer, packet->payload.transferCommit.words[i]);
		}
		break;
	case GBA_LINK_MESSAGE_TRANSFER_ABORT:
		_write64(writer, packet->payload.transferAbort.transferSequence);
		_write64(writer, packet->payload.transferAbort.completionCycle);
		_write16(writer, packet->payload.transferAbort.reason);
		_writeZeroes(writer, 6);
		break;
	case GBA_LINK_MESSAGE_COMPLETION_CATCHUP:
		_write64(writer, packet->payload.completionCatchup.transferSequence);
		_write64(writer, packet->payload.completionCatchup.completionSequence);
		_write64(writer, packet->payload.completionCatchup.completionCycle);
		_write8(writer, packet->payload.completionCatchup.pendingOutcome);
		_writeZeroes(writer, 7);
		break;
	case GBA_LINK_MESSAGE_COMPLETION_READY:
		_write64(writer, packet->payload.completionReady.transferSequence);
		_write64(writer, packet->payload.completionReady.completionSequence);
		_write64(writer, packet->payload.completionReady.completionCycle);
		_write16(writer, packet->payload.completionReady.abortReason);
		_write8(writer, packet->payload.completionReady.hasDeferredMode);
		_writeZeroes(writer, 5);
		break;
	case GBA_LINK_MESSAGE_COMPLETION_DECISION:
		_write64(writer, packet->payload.completionDecision.transferSequence);
		_write64(writer, packet->payload.completionDecision.completionSequence);
		_write64(writer, packet->payload.completionDecision.completionCycle);
		_write8(writer, packet->payload.completionDecision.outcome);
		_write8(writer, 0);
		_write16(writer, packet->payload.completionDecision.reason);
		for (unsigned i = 0; i < 4; ++i) {
			_write16(writer, packet->payload.completionDecision.words[i]);
		}
		break;
	case GBA_LINK_MESSAGE_COMPLETION_DECISION_ACK:
		_write64(writer, packet->payload.completionDecisionAck.transferSequence);
		_write64(writer, packet->payload.completionDecisionAck.completionSequence);
		_write64(writer, packet->payload.completionDecisionAck.completionCycle);
		_write8(writer, packet->payload.completionDecisionAck.outcome);
		_writeZeroes(writer, 7);
		break;
	}
}

bool GBALinkPacketEncode(const struct GBALinkPacket* packet, void* data, size_t capacity, size_t* encodedSize) {
	if (encodedSize) {
		*encodedSize = 0;
	}
	size_t size = GBALinkPacketEncodedSize(packet);
	if (!data || !size || capacity < size) {
		return false;
	}
	size_t payloadSize = size - GBA_LINK_HEADER_SIZE;
	struct GBALinkWriter writer = {
		.data = data,
		.capacity = capacity,
		.valid = true,
	};
	_write32(&writer, GBA_LINK_PROTOCOL_MAGIC);
	_write16(&writer, GBA_LINK_PROTOCOL_VERSION);
	_write16(&writer, packet->header.type);
	_write32(&writer, payloadSize);
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
    struct GBALinkReader* reader, struct GBALinkPacket* packet) {
	bool reserved = true;
	bool canonical = true;
	switch (packet->header.type) {
	case GBA_LINK_MESSAGE_HELLO: {
		struct GBALinkHello* hello = &packet->payload.hello;
		hello->capabilities = _read64(reader);
		hello->romSize = _read64(reader);
		_readBytes(reader, hello->romSha1, sizeof(hello->romSha1));
		hello->supportedPolicies = _read32(reader);
		hello->emulationCompatibilityVersion = _read32(reader);
		hello->initialMode = _read8(reader);
		hello->digestCount = _read8(reader);
		reserved &= _readZeroes(reader, 2);
		hello->rendezvousCycle = _read64(reader);
		if (hello->digestCount > GBA_LINK_MAX_DETERMINISM_DIGESTS ||
		    reader->size - reader->offset != (size_t) hello->digestCount * 24) {
			return GBA_LINK_DECODE_LENGTH;
		}
		for (unsigned i = 0; i < hello->digestCount; ++i) {
			hello->digests[i].category = _read8(reader);
			reserved &= _readZeroes(reader, 3);
			_readBytes(reader, hello->digests[i].digest, GBA_LINK_DIGEST_SIZE);
		}
		break;
	}
	case GBA_LINK_MESSAGE_ACCEPT: {
		struct GBALinkAccept* accept = &packet->payload.accept;
		accept->proposedSessionId = _read64(reader);
		accept->hostTransportId = _read16(reader);
		accept->clientTransportId = _read16(reader);
		accept->policy = _read8(reader);
		reserved &= _readZeroes(reader, 3);
		accept->compatibilityGroup = _read64(reader);
		accept->attachCycle = _read64(reader);
		accept->initialModeGeneration = _read64(reader);
		break;
	}
	case GBA_LINK_MESSAGE_ACCEPT_ACK:
		packet->payload.sessionId.acceptedSessionId = _read64(reader);
		break;
	case GBA_LINK_MESSAGE_SESSION_READY:
	case GBA_LINK_MESSAGE_SESSION_READY_ACK:
		packet->payload.sessionReady.attachCycle = _read64(reader);
		packet->payload.sessionReady.initialModeGeneration = _read64(reader);
		break;
	case GBA_LINK_MESSAGE_REJECT:
	case GBA_LINK_MESSAGE_DETACH:
	case GBA_LINK_MESSAGE_DETACH_ACK:
		packet->payload.reason.reason = _read16(reader);
		packet->payload.reason.category = _read8(reader);
		reserved &= !_read8(reader);
		break;
	case GBA_LINK_MESSAGE_EXECUTION_GRANT:
	case GBA_LINK_MESSAGE_GRANT_ACK:
		packet->payload.grant.grantSequence = _read64(reader);
		packet->payload.grant.horizon = _read64(reader);
		break;
	case GBA_LINK_MESSAGE_MODE_INTENT:
		packet->payload.modeIntent.modeGeneration = _read64(reader);
		packet->payload.modeIntent.localCycle = _read64(reader);
		packet->payload.modeIntent.localMode = _read8(reader);
		canonical &= _readBoolean(
		    reader, &packet->payload.modeIntent.deferred);
		reserved &= _readZeroes(reader, 6);
		break;
	case GBA_LINK_MESSAGE_MODE_COMMIT:
		packet->payload.modeCommit.modeGeneration = _read64(reader);
		packet->payload.modeCommit.commitCycle = _read64(reader);
		packet->payload.modeCommit.hostMode = _read8(reader);
		packet->payload.modeCommit.clientMode = _read8(reader);
		canonical &= _readBoolean(
		    reader, &packet->payload.modeCommit.jointlyReady);
		reserved &= _readZeroes(reader, 5);
		break;
	case GBA_LINK_MESSAGE_MODE_ACK:
		packet->payload.modeAck.modeGeneration = _read64(reader);
		packet->payload.modeAck.commitCycle = _read64(reader);
		break;
	case GBA_LINK_MESSAGE_TRANSFER_START:
	case GBA_LINK_MESSAGE_TRANSFER_READY:
		packet->payload.transferStart.transferSequence = _read64(reader);
		packet->payload.transferStart.startCycle = _read64(reader);
		packet->payload.transferStart.completionCycle = _read64(reader);
		packet->payload.transferStart.outgoingWord = _read16(reader);
		packet->payload.transferStart.siocnt = _read16(reader);
		break;
	case GBA_LINK_MESSAGE_TRANSFER_COMMIT:
		packet->payload.transferCommit.transferSequence = _read64(reader);
		packet->payload.transferCommit.startCycle = _read64(reader);
		packet->payload.transferCommit.completionCycle = _read64(reader);
		for (unsigned i = 0; i < 4; ++i) {
			packet->payload.transferCommit.words[i] = _read16(reader);
		}
		break;
	case GBA_LINK_MESSAGE_TRANSFER_ABORT:
		packet->payload.transferAbort.transferSequence = _read64(reader);
		packet->payload.transferAbort.completionCycle = _read64(reader);
		packet->payload.transferAbort.reason = _read16(reader);
		reserved &= _readZeroes(reader, 6);
		break;
	case GBA_LINK_MESSAGE_COMPLETION_CATCHUP:
		packet->payload.completionCatchup.transferSequence = _read64(reader);
		packet->payload.completionCatchup.completionSequence = _read64(reader);
		packet->payload.completionCatchup.completionCycle = _read64(reader);
		packet->payload.completionCatchup.pendingOutcome = _read8(reader);
		reserved &= _readZeroes(reader, 7);
		break;
	case GBA_LINK_MESSAGE_COMPLETION_READY:
		packet->payload.completionReady.transferSequence = _read64(reader);
		packet->payload.completionReady.completionSequence = _read64(reader);
		packet->payload.completionReady.completionCycle = _read64(reader);
		packet->payload.completionReady.abortReason = _read16(reader);
		canonical &= _readBoolean(
		    reader,
		    &packet->payload.completionReady.hasDeferredMode);
		reserved &= _readZeroes(reader, 5);
		break;
	case GBA_LINK_MESSAGE_COMPLETION_DECISION:
		packet->payload.completionDecision.transferSequence = _read64(reader);
		packet->payload.completionDecision.completionSequence = _read64(reader);
		packet->payload.completionDecision.completionCycle = _read64(reader);
		packet->payload.completionDecision.outcome = _read8(reader);
		reserved &= !_read8(reader);
		packet->payload.completionDecision.reason = _read16(reader);
		for (unsigned i = 0; i < 4; ++i) {
			packet->payload.completionDecision.words[i] = _read16(reader);
		}
		break;
	case GBA_LINK_MESSAGE_COMPLETION_DECISION_ACK:
		packet->payload.completionDecisionAck.transferSequence = _read64(reader);
		packet->payload.completionDecisionAck.completionSequence = _read64(reader);
		packet->payload.completionDecisionAck.completionCycle = _read64(reader);
		packet->payload.completionDecisionAck.outcome = _read8(reader);
		reserved &= _readZeroes(reader, 7);
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

enum GBALinkDecodeStatus GBALinkPacketDecode(
    const void* data, size_t size, enum GBALinkRole senderRole, struct GBALinkPacket* packet) {
	if (!data || !packet) {
		return GBA_LINK_DECODE_NULL;
	}
	if (size < GBA_LINK_HEADER_SIZE) {
		return GBA_LINK_DECODE_TRUNCATED;
	}
	if (size > GBA_LINK_MAX_PACKET_SIZE) {
		return GBA_LINK_DECODE_OVERSIZED;
	}

	struct GBALinkReader reader = {
		.data = data,
		.size = size,
		.valid = true,
	};
	if (_read32(&reader) != GBA_LINK_PROTOCOL_MAGIC) {
		return GBA_LINK_DECODE_MAGIC;
	}
	if (_read16(&reader) != GBA_LINK_PROTOCOL_VERSION) {
		return GBA_LINK_DECODE_VERSION;
	}

	struct GBALinkPacket decoded;
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
	if (!GBALinkMessageAllowsRole(decoded.header.type, senderRole)) {
		return GBA_LINK_DECODE_ROLE;
	}
	if (payloadSize > GBA_LINK_MAX_PAYLOAD_SIZE ||
	    payloadSize != size - GBA_LINK_HEADER_SIZE) {
		return GBA_LINK_DECODE_LENGTH;
	}
	if (_preSessionType(decoded.header.type) != !decoded.header.sessionId) {
		return GBA_LINK_DECODE_SESSION;
	}
	if (!decoded.header.packetSequence) {
		return GBA_LINK_DECODE_SEQUENCE;
	}

	size_t fixedPayloadSize = _payloadSize(&decoded);
	if (decoded.header.type != GBA_LINK_MESSAGE_HELLO &&
	    fixedPayloadSize != payloadSize) {
		return GBA_LINK_DECODE_LENGTH;
	}
	reader.size = size;
	enum GBALinkDecodeStatus status = _decodePayload(&reader, &decoded);
	if (status != GBA_LINK_DECODE_OK) {
		return status;
	}
	if (reader.offset != size) {
		return GBA_LINK_DECODE_LENGTH;
	}
	if (!_validatePacket(&decoded)) {
		return GBA_LINK_DECODE_FIELD;
	}
	*packet = decoded;
	return GBA_LINK_DECODE_OK;
}
