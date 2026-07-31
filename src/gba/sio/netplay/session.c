/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/gba/sio/netplay/session.h>

#include <mgba/internal/gba/sio/netplay/transport.h>

static void _setPaused(struct GBALinkSession* session, bool paused) {
	if (session->paused == paused) {
		return;
	}
	session->paused = paused;
	if (session->config.callbacks &&
	    session->config.callbacks->setPaused) {
		session->config.callbacks->setPaused(
		    session->config.callbackContext, paused);
	}
}

static void _setAttachment(
    struct GBALinkSession* session, bool installed, bool observable) {
	if (session->config.callbacks &&
	    session->config.callbacks->setAttachment) {
		session->config.callbacks->setAttachment(
		    session->config.callbackContext, installed, observable,
		    session->attachCycle);
	}
}

static void _setDeadline(
    struct GBALinkSession* session,
    enum GBALinkDeadlineOperation operation) {
	session->deadlineOperation = operation;
	if (operation == GBA_LINK_DEADLINE_NONE) {
		session->deadlineAtMs = 0;
		return;
	}
	uint64_t now = GBALinkTransportMonotonicTimeMs(session->transport);
	uint64_t duration = session->config.deadlines.milliseconds[operation];
	session->deadlineAtMs =
	    UINT64_MAX - now < duration ? UINT64_MAX : now + duration;
}

static enum GBALinkReason _deadlineReason(
    enum GBALinkDeadlineOperation operation) {
	switch (operation) {
	case GBA_LINK_DEADLINE_HANDSHAKE:
		return GBA_LINK_REASON_HANDSHAKE_TIMEOUT;
	case GBA_LINK_DEADLINE_ATTACHMENT:
		return GBA_LINK_REASON_ATTACHMENT_TIMEOUT;
	case GBA_LINK_DEADLINE_GRANT:
		return GBA_LINK_REASON_GRANT_TIMEOUT;
	case GBA_LINK_DEADLINE_MODE:
		return GBA_LINK_REASON_MODE_TIMEOUT;
	case GBA_LINK_DEADLINE_TRANSFER_READINESS:
		return GBA_LINK_REASON_TRANSFER_READY_TIMEOUT;
	case GBA_LINK_DEADLINE_TRANSFER_COMMIT:
		return GBA_LINK_REASON_TRANSFER_COMMIT_TIMEOUT;
	case GBA_LINK_DEADLINE_COMPLETION_CATCHUP:
		return GBA_LINK_REASON_COMPLETION_CATCHUP_TIMEOUT;
	case GBA_LINK_DEADLINE_COMPLETION_READINESS:
		return GBA_LINK_REASON_COMPLETION_READY_TIMEOUT;
	case GBA_LINK_DEADLINE_COMPLETION_DECISION:
		return GBA_LINK_REASON_COMPLETION_DECISION_TIMEOUT;
	case GBA_LINK_DEADLINE_DETACH:
		return GBA_LINK_REASON_DETACH_TIMEOUT;
	case GBA_LINK_DEADLINE_NONE:
	case GBA_LINK_DEADLINE_OPERATION_COUNT:
		break;
	}
	return GBA_LINK_REASON_INVALID_TRANSITION;
}

void GBALinkDeadlinePolicyInit(struct GBALinkDeadlinePolicy* policy) {
	memset(policy, 0, sizeof(*policy));
	policy->milliseconds[GBA_LINK_DEADLINE_HANDSHAKE] = 1500;
	policy->milliseconds[GBA_LINK_DEADLINE_ATTACHMENT] = 3000;
	policy->milliseconds[GBA_LINK_DEADLINE_GRANT] = 1500;
	policy->milliseconds[GBA_LINK_DEADLINE_MODE] = 1500;
	policy->milliseconds[GBA_LINK_DEADLINE_TRANSFER_READINESS] = 3000;
	policy->milliseconds[GBA_LINK_DEADLINE_TRANSFER_COMMIT] = 3000;
	policy->milliseconds[GBA_LINK_DEADLINE_COMPLETION_CATCHUP] = 3000;
	policy->milliseconds[GBA_LINK_DEADLINE_COMPLETION_READINESS] = 3000;
	policy->milliseconds[GBA_LINK_DEADLINE_COMPLETION_DECISION] = 3000;
	policy->milliseconds[GBA_LINK_DEADLINE_DETACH] = 1000;
}

bool GBALinkDeadlinePolicyValidate(
    const struct GBALinkDeadlinePolicy* policy) {
	if (!policy || policy->milliseconds[GBA_LINK_DEADLINE_NONE]) {
		return false;
	}
	for (unsigned i = GBA_LINK_DEADLINE_HANDSHAKE;
	     i < GBA_LINK_DEADLINE_OPERATION_COUNT; ++i) {
		if (!policy->milliseconds[i] ||
		    policy->milliseconds[i] > GBA_LINK_MAX_WAIT_MS) {
			return false;
		}
	}
	return true;
}

void GBALinkSequenceStateInit(struct GBALinkSequenceState* sequences) {
	memset(sequences, 0, sizeof(*sequences));
	for (unsigned i = 0; i < GBA_LINK_SEQUENCE_DOMAIN_COUNT; ++i) {
		sequences->next[i] = 1;
	}
}

bool GBALinkSequenceTake(
    struct GBALinkSequenceState* sequences,
    enum GBALinkSequenceDomain domain, uint64_t* value) {
	if (!sequences || !value || domain < 0 ||
	    domain >= GBA_LINK_SEQUENCE_DOMAIN_COUNT ||
	    GBALinkSequenceIsExhausted(sequences, domain)) {
		return false;
	}
	*value = sequences->next[domain];
	if (sequences->next[domain] == UINT64_MAX) {
		sequences->exhausted |= 1U << domain;
	} else {
		++sequences->next[domain];
	}
	return true;
}

bool GBALinkSequenceIsExhausted(
    const struct GBALinkSequenceState* sequences,
    enum GBALinkSequenceDomain domain) {
	return !sequences || domain < 0 ||
	       domain >= GBA_LINK_SEQUENCE_DOMAIN_COUNT ||
	       (sequences->exhausted & (1U << domain));
}

void GBALinkSessionInit(
    struct GBALinkSession* session, struct GBALinkTransport* transport) {
	memset(session, 0, sizeof(*session));
	session->transport = transport;
	session->state = GBA_LINK_SESSION_DISCONNECTED;
	GBALinkSequenceStateInit(&session->sequences);
}

void GBALinkSessionDeinit(struct GBALinkSession* session) {
	if (!session) {
		return;
	}
	if (session->transport && session->state != GBA_LINK_SESSION_DISCONNECTED) {
		GBALinkTransportRequestStop(
		    session->transport, GBA_LINK_REASON_TRANSPORT_STOP,
		    "session deinitialized while transport was active");
	}
	memset(session, 0, sizeof(*session));
}

static bool _configValid(const struct GBALinkSessionConfig* config) {
	if (!config || !config->identity.romSize ||
	    config->capabilities != GBA_LINK_MVP_CAPABILITIES ||
	    config->supportedPolicies !=
	        (1U << GBA_LINK_COMPATIBILITY_EXACT_ROM) ||
	    !config->emulationCompatibilityVersion ||
	    config->cheatsEnabled ||
	    !GBALinkDeadlinePolicyValidate(&config->deadlines) ||
	    !config->callbacks || !config->callbacks->quiescentSnapshot) {
		return false;
	}
	for (unsigned i = 0; i < GBA_LINK_MAX_DETERMINISM_DIGESTS; ++i) {
		if (config->digests[i].category !=
		    (enum GBALinkDeterminismCategory) (i + 1)) {
			return false;
		}
	}
	return true;
}

bool GBALinkSessionConfigure(
    struct GBALinkSession* session,
    const struct GBALinkSessionConfig* config) {
	if (!session || session->state != GBA_LINK_SESSION_DISCONNECTED ||
	    !_configValid(config)) {
		return false;
	}
	session->config = *config;
	session->configured = true;
	return true;
}

static bool _sendPacket(
    struct GBALinkSession* session, struct GBALinkPacket* packet) {
	uint64_t packetSequence;
	if (!GBALinkSequenceTake(
	        &session->sequences, GBA_LINK_SEQUENCE_PACKET,
	        &packetSequence)) {
		GBALinkSessionFail(
		    session, GBA_LINK_REASON_SEQUENCE_EXHAUSTED,
		    "local packet sequence exhausted");
		return false;
	}
	packet->header.packetSequence = packetSequence;
	uint8_t encoded[GBA_LINK_MAX_PACKET_SIZE];
	size_t encodedSize = 0;
	if (!GBALinkPacketEncode(
	        packet, encoded, sizeof(encoded), &encodedSize)) {
		GBALinkSessionFail(
		    session, GBA_LINK_REASON_INVALID_TRANSITION,
		    "session attempted to encode an invalid packet");
		return false;
	}
	if (!GBALinkTransportSend(
	        session->transport, encoded, encodedSize, true)) {
		GBALinkSessionFail(
		    session, GBA_LINK_REASON_SEND_FAILURE,
		    "session packet send failed");
		return false;
	}
	if (session->processingInbound) {
		if (session->replayPacketCount >=
		    sizeof(session->replayPackets) /
		        sizeof(session->replayPackets[0])) {
			GBALinkSessionFail(
			    session, GBA_LINK_REASON_INVALID_TRANSITION,
			    "too many responses for duplicate replay");
			return false;
		}
		size_t index = session->replayPacketCount++;
		memcpy(session->replayPackets[index], encoded, encodedSize);
		session->replayPacketSizes[index] = encodedSize;
	}
	return true;
}

static bool _sendHello(struct GBALinkSession* session) {
	struct GBALinkPacket packet;
	memset(&packet, 0, sizeof(packet));
	packet.header.type = GBA_LINK_MESSAGE_HELLO;
	packet.payload.hello = session->localHello;
	if (!_sendPacket(session, &packet)) {
		return false;
	}
	session->localHelloSent = true;
	_setDeadline(session, GBA_LINK_DEADLINE_HANDSHAKE);
	return true;
}

static bool _enterRendezvous(struct GBALinkSession* session) {
	enum GBALinkWireMode mode;
	uint64_t localCycle;
	if (!session->config.callbacks->quiescentSnapshot(
	        session->config.callbackContext, &mode, &localCycle)) {
		return true;
	}
	switch (mode) {
	case GBA_LINK_MODE_NORMAL_8:
	case GBA_LINK_MODE_NORMAL_32:
	case GBA_LINK_MODE_MULTI:
	case GBA_LINK_MODE_UART:
	case GBA_LINK_MODE_GPIO:
	case GBA_LINK_MODE_JOYBUS:
		break;
	default:
		GBALinkSessionFail(
		    session, GBA_LINK_REASON_INVALID_TRANSITION,
		    "quiescent SIO snapshot returned an invalid mode");
		return false;
	}

	memset(&session->localHello, 0, sizeof(session->localHello));
	session->localHello.capabilities = session->config.capabilities;
	session->localHello.romSize = session->config.identity.romSize;
	memcpy(
	    session->localHello.romSha1,
	    session->config.identity.romSha1,
	    sizeof(session->localHello.romSha1));
	session->localHello.supportedPolicies =
	    session->config.supportedPolicies;
	session->localHello.emulationCompatibilityVersion =
	    session->config.emulationCompatibilityVersion;
	session->localHello.initialMode = mode;
	session->localHello.digestCount =
	    GBA_LINK_MAX_DETERMINISM_DIGESTS;
	session->localHello.rendezvousCycle = localCycle;
	memcpy(
	    session->localHello.digests, session->config.digests,
	    sizeof(session->localHello.digests));
	_setPaused(session, true);
	return _sendHello(session);
}

bool GBALinkSessionStart(
    struct GBALinkSession* session, uint64_t transportGeneration,
    enum GBALinkRole localRole) {
	if (!session || !session->configured ||
	    session->state != GBA_LINK_SESSION_DISCONNECTED ||
	    !GBALinkTransportIsActive(
	        session->transport, transportGeneration) ||
	    (localRole != GBA_LINK_ROLE_HOST &&
	     localRole != GBA_LINK_ROLE_CLIENT)) {
		return false;
	}
	session->state = GBA_LINK_SESSION_TRANSPORT_STARTED;
	session->localRole = localRole;
	session->transportGeneration = transportGeneration;
	session->sessionId = 0;
	session->localHelloSent = false;
	session->remoteHelloReceived = false;
	session->remotePacketExhausted = false;
	session->nextRemotePacketSequence = 1;
	session->lastRemotePacketSequence = 0;
	session->lastRemotePacketSize = 0;
	session->replayPacketCount = 0;
	session->processingInbound = false;
	session->yieldInbound = false;
	GBALinkSequenceStateInit(&session->sequences);
	_setDeadline(session, GBA_LINK_DEADLINE_ATTACHMENT);
	return _enterRendezvous(session);
}

static enum GBALinkReason _compareHello(
    const struct GBALinkHello* local, const struct GBALinkHello* remote,
    enum GBALinkDeterminismCategory* mismatchCategory) {
	*mismatchCategory = 0;
	if (remote->capabilities != local->capabilities) {
		return GBA_LINK_REASON_CAPABILITY_MISMATCH;
	}
	if (!(remote->supportedPolicies &
	      (1U << GBA_LINK_COMPATIBILITY_EXACT_ROM))) {
		return GBA_LINK_REASON_POLICY_MISMATCH;
	}
	if (remote->romSize != local->romSize ||
	    memcmp(remote->romSha1, local->romSha1, sizeof(local->romSha1))) {
		return GBA_LINK_REASON_ROM_MISMATCH;
	}
	if (remote->emulationCompatibilityVersion !=
	    local->emulationCompatibilityVersion) {
		return GBA_LINK_REASON_COMPATIBILITY_MISMATCH;
	}
	for (unsigned i = 0; i < GBA_LINK_MAX_DETERMINISM_DIGESTS; ++i) {
		if (remote->digests[i].category != local->digests[i].category ||
		    memcmp(
		        remote->digests[i].digest, local->digests[i].digest,
		        GBA_LINK_DIGEST_SIZE)) {
			*mismatchCategory = local->digests[i].category;
			return GBA_LINK_REASON_DETERMINISM_MISMATCH;
		}
	}
	return 0;
}

static bool _sendReject(
    struct GBALinkSession* session, enum GBALinkReason reason,
    enum GBALinkDeterminismCategory category) {
	struct GBALinkPacket packet;
	memset(&packet, 0, sizeof(packet));
	packet.header.type = GBA_LINK_MESSAGE_REJECT;
	packet.payload.reason.reason = reason;
	packet.payload.reason.category = category;
	return _sendPacket(session, &packet);
}

static bool _hostSendAccept(struct GBALinkSession* session) {
	uint64_t sessionId;
	uint64_t modeGeneration;
	if (!GBALinkSequenceTake(
	        &session->sequences, GBA_LINK_SEQUENCE_SESSION, &sessionId) ||
	    !GBALinkSequenceTake(
	        &session->sequences, GBA_LINK_SEQUENCE_MODE,
	        &modeGeneration)) {
		GBALinkSessionFail(
		    session, GBA_LINK_REASON_SEQUENCE_EXHAUSTED,
		    "attachment sequence exhausted");
		return false;
	}
	session->sessionId = sessionId;
	session->attachCycle = session->localHello.rendezvousCycle;
	session->initialModeGeneration = modeGeneration;

	struct GBALinkPacket packet;
	memset(&packet, 0, sizeof(packet));
	packet.header.type = GBA_LINK_MESSAGE_ACCEPT;
	packet.payload.accept.proposedSessionId = sessionId;
	packet.payload.accept.hostTransportId = 0;
	packet.payload.accept.clientTransportId = 1;
	packet.payload.accept.policy = GBA_LINK_COMPATIBILITY_EXACT_ROM;
	packet.payload.accept.compatibilityGroup = 0;
	packet.payload.accept.attachCycle = session->attachCycle;
	packet.payload.accept.initialModeGeneration = modeGeneration;
	if (!_sendPacket(session, &packet)) {
		return false;
	}
	session->state = GBA_LINK_SESSION_ACCEPTED;
	_setDeadline(session, GBA_LINK_DEADLINE_ATTACHMENT);
	return true;
}

static bool _handleHello(
    struct GBALinkSession* session, const struct GBALinkPacket* packet) {
	if (session->remoteHelloReceived ||
	    (session->state != GBA_LINK_SESSION_TRANSPORT_STARTED &&
	     session->state != GBA_LINK_SESSION_HELLO_EXCHANGED)) {
		return false;
	}
	session->remoteHello = packet->payload.hello;
	session->remoteHelloReceived = true;
	enum GBALinkDeterminismCategory category;
	enum GBALinkReason reason = _compareHello(
	    &session->localHello, &session->remoteHello, &category);
	if (reason) {
		_sendReject(session, reason, category);
		GBALinkSessionFail(session, reason, "peer compatibility rejected");
		return true;
	}
	session->state = GBA_LINK_SESSION_HELLO_EXCHANGED;
	if (session->localRole == GBA_LINK_ROLE_HOST) {
		return _hostSendAccept(session);
	}
	_setDeadline(session, GBA_LINK_DEADLINE_HANDSHAKE);
	return true;
}

static bool _handleAccept(
    struct GBALinkSession* session, const struct GBALinkPacket* packet) {
	if (session->localRole != GBA_LINK_ROLE_CLIENT ||
	    session->state != GBA_LINK_SESSION_HELLO_EXCHANGED) {
		return false;
	}
	const struct GBALinkAccept* accept = &packet->payload.accept;
	if (accept->policy != GBA_LINK_COMPATIBILITY_EXACT_ROM ||
	    accept->compatibilityGroup ||
	    !accept->proposedSessionId ||
	    accept->hostTransportId != 0 ||
	    accept->clientTransportId != 1) {
		return false;
	}
	session->sessionId = accept->proposedSessionId;
	session->attachCycle = accept->attachCycle;
	session->initialModeGeneration = accept->initialModeGeneration;

	struct GBALinkPacket response;
	memset(&response, 0, sizeof(response));
	response.header.type = GBA_LINK_MESSAGE_ACCEPT_ACK;
	response.header.sessionId = session->sessionId;
	response.payload.sessionId.acceptedSessionId = session->sessionId;
	if (!_sendPacket(session, &response)) {
		return false;
	}
	session->state = GBA_LINK_SESSION_ACCEPTED;
	_setDeadline(session, GBA_LINK_DEADLINE_ATTACHMENT);
	return true;
}

static bool _handleAcceptAck(
    struct GBALinkSession* session, const struct GBALinkPacket* packet) {
	if (session->localRole != GBA_LINK_ROLE_HOST ||
	    session->state != GBA_LINK_SESSION_ACCEPTED ||
	    packet->payload.sessionId.acceptedSessionId !=
	        session->sessionId) {
		return false;
	}
	struct GBALinkPacket response;
	memset(&response, 0, sizeof(response));
	response.header.type = GBA_LINK_MESSAGE_SESSION_READY;
	response.header.sessionId = session->sessionId;
	response.payload.sessionReady.attachCycle = session->attachCycle;
	response.payload.sessionReady.initialModeGeneration =
	    session->initialModeGeneration;
	if (!_sendPacket(session, &response)) {
		return false;
	}
	session->state = GBA_LINK_SESSION_ATTACH_BARRIER;
	_setDeadline(session, GBA_LINK_DEADLINE_ATTACHMENT);
	return true;
}

static bool _handleSessionReady(
    struct GBALinkSession* session, const struct GBALinkPacket* packet) {
	if (session->localRole != GBA_LINK_ROLE_CLIENT ||
	    session->state != GBA_LINK_SESSION_ACCEPTED ||
	    packet->payload.sessionReady.attachCycle !=
	        session->attachCycle ||
	    packet->payload.sessionReady.initialModeGeneration !=
	        session->initialModeGeneration) {
		return false;
	}
	struct GBALinkPacket response;
	memset(&response, 0, sizeof(response));
	response.header.type = GBA_LINK_MESSAGE_SESSION_READY_ACK;
	response.header.sessionId = session->sessionId;
	response.payload.sessionReady.attachCycle = session->attachCycle;
	response.payload.sessionReady.initialModeGeneration =
	    session->initialModeGeneration;
	if (!_sendPacket(session, &response)) {
		return false;
	}
	session->state = GBA_LINK_SESSION_ATTACH_BARRIER;
	_setAttachment(session, true, false);
	_setDeadline(session, GBA_LINK_DEADLINE_MODE);
	return true;
}

static bool _handleSessionReadyAck(
    struct GBALinkSession* session, const struct GBALinkPacket* packet) {
	if (session->localRole != GBA_LINK_ROLE_HOST ||
	    session->state != GBA_LINK_SESSION_ATTACH_BARRIER ||
	    packet->payload.sessionReady.attachCycle !=
	        session->attachCycle ||
	    packet->payload.sessionReady.initialModeGeneration !=
	        session->initialModeGeneration) {
		return false;
	}
	session->state = GBA_LINK_SESSION_READY;
	_setAttachment(session, true, true);
	_setDeadline(session, GBA_LINK_DEADLINE_NONE);
	return true;
}

static bool _handleRuntime(
    struct GBALinkSession* session, const struct GBALinkPacket* packet) {
	if (session->state == GBA_LINK_SESSION_ATTACH_BARRIER &&
	    session->localRole == GBA_LINK_ROLE_CLIENT &&
	    (packet->header.type == GBA_LINK_MESSAGE_MODE_COMMIT ||
	     packet->header.type == GBA_LINK_MESSAGE_EXECUTION_GRANT)) {
		session->state = GBA_LINK_SESSION_READY;
		_setAttachment(session, true, true);
		_setDeadline(session, GBA_LINK_DEADLINE_NONE);
	}
	return session->config.callbacks->runtimePacket &&
	       session->config.callbacks->runtimePacket(
	           session->config.callbackContext, packet);
}

static bool _handlePacket(
    struct GBALinkSession* session, const struct GBALinkPacket* packet) {
	if (packet->header.type != GBA_LINK_MESSAGE_HELLO &&
	    packet->header.type != GBA_LINK_MESSAGE_ACCEPT &&
	    packet->header.type != GBA_LINK_MESSAGE_REJECT &&
	    packet->header.sessionId != session->sessionId) {
		return false;
	}
	switch (packet->header.type) {
	case GBA_LINK_MESSAGE_HELLO:
		return _handleHello(session, packet);
	case GBA_LINK_MESSAGE_ACCEPT:
		return _handleAccept(session, packet);
	case GBA_LINK_MESSAGE_ACCEPT_ACK:
		return _handleAcceptAck(session, packet);
	case GBA_LINK_MESSAGE_SESSION_READY:
		return _handleSessionReady(session, packet);
	case GBA_LINK_MESSAGE_SESSION_READY_ACK:
		return _handleSessionReadyAck(session, packet);
	case GBA_LINK_MESSAGE_REJECT:
		GBALinkSessionFail(
		    session, packet->payload.reason.reason,
		    "peer rejected link session");
		return true;
	case GBA_LINK_MESSAGE_DETACH: {
		struct GBALinkPacket response;
		memset(&response, 0, sizeof(response));
		response.header.type = GBA_LINK_MESSAGE_DETACH_ACK;
		response.header.sessionId = session->sessionId;
		response.payload.reason = packet->payload.reason;
		_sendPacket(session, &response);
		GBALinkSessionFail(
		    session, packet->payload.reason.reason,
		    "peer detached link session");
		return true;
	}
	case GBA_LINK_MESSAGE_DETACH_ACK:
		GBALinkSessionFail(
		    session, packet->payload.reason.reason,
		    "link detach acknowledged");
		return true;
	default:
		return _handleRuntime(session, packet);
	}
}

static bool _replayDuplicate(
    struct GBALinkSession* session,
    const struct GBALinkCopiedPacket* copied) {
	if (copied->size != session->lastRemotePacketSize ||
	    memcmp(
	        copied->data, session->lastRemotePacket,
	        copied->size)) {
		GBALinkSessionFail(
		    session, GBA_LINK_REASON_MALFORMED_PACKET,
		    "conflicting duplicate packet");
		return false;
	}
	for (size_t i = 0; i < session->replayPacketCount; ++i) {
		if (!GBALinkTransportSend(
		        session->transport, session->replayPackets[i],
		        session->replayPacketSizes[i], true)) {
			GBALinkSessionFail(
			    session, GBA_LINK_REASON_SEND_FAILURE,
			    "duplicate response replay failed");
			return false;
		}
	}
	return true;
}

static bool _processCopiedPacket(
    struct GBALinkSession* session,
    const struct GBALinkCopiedPacket* copied) {
	if (copied->generation != session->transportGeneration) {
		return true;
	}
	struct GBALinkPacket packet;
	enum GBALinkRole remoteRole =
	    session->localRole == GBA_LINK_ROLE_HOST
	        ? GBA_LINK_ROLE_CLIENT
	        : GBA_LINK_ROLE_HOST;
	enum GBALinkDecodeStatus status = GBALinkPacketDecode(
	    copied->data, copied->size, remoteRole, &packet);
	if (status != GBA_LINK_DECODE_OK) {
		GBALinkSessionFail(
		    session, GBA_LINK_REASON_MALFORMED_PACKET,
		    GBALinkDecodeStatusName(status));
		return false;
	}
	if (packet.header.packetSequence ==
	    session->lastRemotePacketSequence) {
		return _replayDuplicate(session, copied);
	}
	if (session->remotePacketExhausted ||
	    packet.header.packetSequence !=
	        session->nextRemotePacketSequence) {
		GBALinkSessionFail(
		    session, GBA_LINK_REASON_MALFORMED_PACKET,
		    "stale, skipped, or future packet sequence");
		return false;
	}

	session->lastRemotePacketSequence =
	    packet.header.packetSequence;
	memcpy(
	    session->lastRemotePacket, copied->data, copied->size);
	session->lastRemotePacketSize = copied->size;
	session->replayPacketCount = 0;
	if (session->nextRemotePacketSequence == UINT64_MAX) {
		session->remotePacketExhausted = true;
	} else {
		++session->nextRemotePacketSequence;
	}
	session->processingInbound = true;
	bool handled = _handlePacket(session, &packet);
	session->processingInbound = false;
	if (!handled && session->state != GBA_LINK_SESSION_FAILED) {
		GBALinkSessionFail(
		    session, GBA_LINK_REASON_INVALID_TRANSITION,
		    "packet is invalid in current session state");
		return false;
	}
	return session->state != GBA_LINK_SESSION_FAILED;
}

bool GBALinkSessionUpdate(
    struct GBALinkSession* session, bool pollReceive) {
	if (!session || !GBALinkSessionIsLive(session)) {
		return false;
	}
	if (!GBALinkTransportIsActive(
	        session->transport, session->transportGeneration)) {
		GBALinkSessionFail(
		    session, GBA_LINK_REASON_TRANSPORT_STOP,
		    "transport generation stopped");
		return false;
	}
	if (!session->localHelloSent &&
	    !_enterRendezvous(session)) {
		return false;
	}
	if (!session->localHelloSent) {
		if (GBALinkTransportMonotonicTimeMs(session->transport) >=
		    session->deadlineAtMs) {
			GBALinkSessionFail(
			    session, GBA_LINK_REASON_ATTACHMENT_TIMEOUT,
			    "quiescent SIO rendezvous");
			return false;
		}
		return true;
	}
	if (pollReceive && !GBALinkTransportPoll(session->transport)) {
		GBALinkSessionFail(
		    session, GBA_LINK_REASON_TRANSPORT_STOP,
		    "receive polling stopped transport");
		return false;
	}
	struct GBALinkCopiedPacket copied;
	while (GBALinkTransportPopInbound(
	           session->transport, &copied)) {
		if (!_processCopiedPacket(session, &copied)) {
			return false;
		}
		if (session->yieldInbound) {
			session->yieldInbound = false;
			break;
		}
	}
	if (session->deadlineOperation != GBA_LINK_DEADLINE_NONE &&
	    GBALinkTransportMonotonicTimeMs(session->transport) >=
	        session->deadlineAtMs) {
		enum GBALinkReason reason =
		    _deadlineReason(session->deadlineOperation);
		GBALinkSessionFail(
		    session, reason,
		    GBALinkDeadlineOperationName(
		        session->deadlineOperation));
		return false;
	}
	return true;
}

void GBALinkSessionYieldInbound(
    struct GBALinkSession* session) {
	if (session) {
		session->yieldInbound = true;
	}
}

bool GBALinkSessionSendRuntime(
    struct GBALinkSession* session, struct GBALinkPacket* packet,
    enum GBALinkDeadlineOperation deadline) {
	if (!session || !packet || !GBALinkSessionIsLive(session) ||
	    !session->sessionId ||
	    packet->header.type < GBA_LINK_MESSAGE_EXECUTION_GRANT ||
	    packet->header.type > GBA_LINK_MESSAGE_COMPLETION_DECISION_ACK ||
	    deadline < GBA_LINK_DEADLINE_NONE ||
	    deadline >= GBA_LINK_DEADLINE_OPERATION_COUNT) {
		return false;
	}
	packet->header.sessionId = session->sessionId;
	if (!_sendPacket(session, packet)) {
		return false;
	}
	_setDeadline(session, deadline);
	return true;
}

void GBALinkSessionSetDeadline(
    struct GBALinkSession* session,
    enum GBALinkDeadlineOperation deadline) {
	if (!session || deadline < GBA_LINK_DEADLINE_NONE ||
	    deadline >= GBA_LINK_DEADLINE_OPERATION_COUNT) {
		return;
	}
	_setDeadline(session, deadline);
}

void GBALinkSessionFail(
    struct GBALinkSession* session, enum GBALinkReason reason,
    const char* diagnostic) {
	if (!session || session->state == GBA_LINK_SESSION_FAILED ||
	    session->state == GBA_LINK_SESSION_DISCONNECTED) {
		return;
	}
	session->state = GBA_LINK_SESSION_FAILED;
	_setDeadline(session, GBA_LINK_DEADLINE_NONE);
	_setAttachment(session, false, false);
	_setPaused(session, false);
	if (session->config.callbacks &&
	    session->config.callbacks->failed) {
		session->config.callbacks->failed(
		    session->config.callbackContext, reason);
	}
	if (session->transport && session->transport->active) {
		GBALinkTransportRequestStop(
		    session->transport, reason, diagnostic);
	}
}

bool GBALinkSessionIsLive(const struct GBALinkSession* session) {
	return session &&
	       session->state != GBA_LINK_SESSION_DISCONNECTED &&
	       session->state != GBA_LINK_SESSION_FAILED;
}

const char* GBALinkSessionStateName(enum GBALinkSessionState state) {
	switch (state) {
	case GBA_LINK_SESSION_DISCONNECTED: return "DISCONNECTED";
	case GBA_LINK_SESSION_TRANSPORT_STARTED: return "TRANSPORT_STARTED";
	case GBA_LINK_SESSION_HELLO_EXCHANGED: return "HELLO_EXCHANGED";
	case GBA_LINK_SESSION_ACCEPTED: return "ACCEPTED";
	case GBA_LINK_SESSION_ATTACH_BARRIER: return "ATTACH_BARRIER";
	case GBA_LINK_SESSION_READY: return "READY";
	case GBA_LINK_SESSION_TRANSFERRING: return "TRANSFERRING";
	case GBA_LINK_SESSION_FAILED: return "FAILED";
	}
	return "(unknown)";
}

const char* GBALinkDeadlineOperationName(
    enum GBALinkDeadlineOperation operation) {
	switch (operation) {
	case GBA_LINK_DEADLINE_NONE: return "none";
	case GBA_LINK_DEADLINE_HANDSHAKE: return "initial handshake";
	case GBA_LINK_DEADLINE_ATTACHMENT: return "attachment barrier";
	case GBA_LINK_DEADLINE_GRANT: return "execution grant";
	case GBA_LINK_DEADLINE_MODE: return "mode barrier";
	case GBA_LINK_DEADLINE_TRANSFER_READINESS: return "transfer readiness";
	case GBA_LINK_DEADLINE_TRANSFER_COMMIT: return "transfer commit";
	case GBA_LINK_DEADLINE_COMPLETION_CATCHUP: return "completion catch-up";
	case GBA_LINK_DEADLINE_COMPLETION_READINESS: return "completion readiness";
	case GBA_LINK_DEADLINE_COMPLETION_DECISION: return "completion decision";
	case GBA_LINK_DEADLINE_DETACH: return "graceful detach";
	case GBA_LINK_DEADLINE_OPERATION_COUNT: break;
	}
	return "(unknown)";
}
