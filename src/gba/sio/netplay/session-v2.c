/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/gba/sio/netplay/session-v2.h>

#include <mgba/internal/gba/sio/netplay/input-sync.h>

static enum GBALinkReason _transportReason(enum GBALinkV2Reason reason) {
	switch (reason) {
	case GBA_LINK_V2_REASON_QUEUE_EXHAUSTED:
		return GBA_LINK_REASON_QUEUE_EXHAUSTED;
	case GBA_LINK_V2_REASON_ATTACHMENT_TIMEOUT:
		return GBA_LINK_REASON_ATTACHMENT_TIMEOUT;
	case GBA_LINK_V2_REASON_MALFORMED_PACKET:
	case GBA_LINK_V2_REASON_SEQUENCE:
		return GBA_LINK_REASON_MALFORMED_PACKET;
	case GBA_LINK_V2_REASON_RESET:
		return GBA_LINK_REASON_RESET;
	case GBA_LINK_V2_REASON_UNLOAD:
		return GBA_LINK_REASON_UNLOAD;
	case GBA_LINK_V2_REASON_USER_DISCONNECT:
		return GBA_LINK_REASON_USER_DISCONNECT;
	case GBA_LINK_V2_REASON_PEER_DETACH:
		return GBA_LINK_REASON_PEER_DETACH;
	default:
		return GBA_LINK_REASON_TRANSPORT_STOP;
	}
}

void GBALinkV2DeadlinePolicyInit(struct GBALinkV2DeadlinePolicy* policy) {
	if (!policy) {
		return;
	}
	memset(policy, 0, sizeof(*policy));
	for (unsigned i = GBA_LINK_V2_DEADLINE_QUIESCENT;
	     i < GBA_LINK_V2_DEADLINE_OPERATION_COUNT; ++i) {
		policy->milliseconds[i] = i == GBA_LINK_V2_DEADLINE_CHUNKS
		    ? 10000
		    : 3000;
	}
}

bool GBALinkV2DeadlinePolicyValidate(
	const struct GBALinkV2DeadlinePolicy* policy) {
	if (!policy || policy->milliseconds[GBA_LINK_V2_DEADLINE_NONE]) {
		return false;
	}
	for (unsigned i = GBA_LINK_V2_DEADLINE_QUIESCENT;
	     i < GBA_LINK_V2_DEADLINE_OPERATION_COUNT; ++i) {
		if (!policy->milliseconds[i] ||
		    policy->milliseconds[i] > 30000) {
			return false;
		}
	}
	return true;
}

const char* GBALinkV2DeadlineOperationName(
	enum GBALinkV2DeadlineOperation operation) {
	switch (operation) {
	case GBA_LINK_V2_DEADLINE_NONE: return "none";
	case GBA_LINK_V2_DEADLINE_QUIESCENT: return "quiescent rendezvous";
	case GBA_LINK_V2_DEADLINE_HANDSHAKE: return "protocol-v2 handshake";
	case GBA_LINK_V2_DEADLINE_MANIFEST: return "replica manifest";
	case GBA_LINK_V2_DEADLINE_CHUNKS: return "replica chunks";
	case GBA_LINK_V2_DEADLINE_INSTALL: return "replica pair installation";
	case GBA_LINK_V2_DEADLINE_READY: return "session readiness";
	case GBA_LINK_V2_DEADLINE_INPUT: return "input frame";
	case GBA_LINK_V2_DEADLINE_VERIFY: return "state verification";
	case GBA_LINK_V2_DEADLINE_DETACH: return "graceful detach";
	case GBA_LINK_V2_DEADLINE_OPERATION_COUNT: break;
	}
	return "invalid deadline";
}

const char* GBALinkV2SessionStateName(enum GBALinkV2SessionState state) {
	switch (state) {
	case GBA_LINK_V2_SESSION_DISCONNECTED: return "disconnected";
	case GBA_LINK_V2_SESSION_WAIT_QUIESCENT: return "wait-quiescent";
	case GBA_LINK_V2_SESSION_HELLO: return "hello";
	case GBA_LINK_V2_SESSION_ACCEPTED: return "accepted";
	case GBA_LINK_V2_SESSION_REPLICA_EXCHANGE: return "replica-exchange";
	case GBA_LINK_V2_SESSION_INSTALLING: return "installing";
	case GBA_LINK_V2_SESSION_READY_BARRIER: return "ready-barrier";
	case GBA_LINK_V2_SESSION_READY: return "ready";
	case GBA_LINK_V2_SESSION_FAILED: return "failed";
	}
	return "invalid";
}

static void _setDeadline(
	struct GBALinkV2Session* session,
	enum GBALinkV2DeadlineOperation operation) {
	session->deadlineOperation = operation;
	if (operation == GBA_LINK_V2_DEADLINE_NONE) {
		session->deadlineAtMs = 0;
		return;
	}
	session->deadlineAtMs =
	    GBALinkTransportMonotonicTimeMs(session->transport) +
	    session->config.deadlines.milliseconds[operation];
}

static void _releaseReplicaState(struct GBALinkV2Session* session) {
	GBAReplicaAssemblerDeinit(&session->remoteAssembler);
	GBAReplicaBundleDeinit(&session->localBundle);
	for (unsigned i = 0; i < 2; ++i) {
		GBAReplicaPayloadDeinit(&session->payloads[i]);
	}
	memset(session->manifests, 0, sizeof(session->manifests));
	memset(session->playerDigests, 0, sizeof(session->playerDigests));
}

void GBALinkV2SessionInit(
	struct GBALinkV2Session* session, struct GBALinkTransport* transport) {
	if (!session) {
		return;
	}
	memset(session, 0, sizeof(*session));
	session->transport = transport;
	session->state = GBA_LINK_V2_SESSION_DISCONNECTED;
}

void GBALinkV2SessionDeinit(struct GBALinkV2Session* session) {
	if (!session) {
		return;
	}
	if (session->pairInstalled && session->config.callbacks &&
	    session->config.callbacks->discardPair) {
		session->config.callbacks->discardPair(
		    session->config.callbackContext, session->pairCommitted);
	}
	if (session->paused && session->config.callbacks &&
	    session->config.callbacks->setPaused) {
		session->config.callbacks->setPaused(
		    session->config.callbackContext, false);
	}
	_releaseReplicaState(session);
	if (session->transport && session->transport->active) {
		GBALinkTransportRequestStop(
		    session->transport, GBA_LINK_REASON_TRANSPORT_STOP,
		    "protocol-v2 session deinitialized while live");
	}
	struct GBALinkTransport* transport = session->transport;
	memset(session, 0, sizeof(*session));
	session->transport = transport;
	session->state = GBA_LINK_V2_SESSION_DISCONNECTED;
}

bool GBALinkV2SessionConfigure(
	struct GBALinkV2Session* session,
	const struct GBALinkV2SessionConfig* config) {
	if (!session || !config || session->state != GBA_LINK_V2_SESSION_DISCONNECTED ||
	    !config->identity.romSize ||
	    config->capabilities != GBA_LINK_V2_REQUIRED_CAPABILITIES ||
	    !config->supportedEncodings ||
	    (config->supportedEncodings &
	     ~(GBA_LINK_V2_ENCODING_NONE | GBA_LINK_V2_ENCODING_DEFLATE)) ||
	    config->emulationCompatibilityVersion !=
	        GBA_REPLICA_EMULATION_COMPATIBILITY_VERSION ||
	    !config->maxChunkSize ||
	    config->maxChunkSize > GBA_REPLICA_MAX_CHUNK_SIZE ||
	    config->minimumInputDelay > config->maximumInputDelay ||
	    config->maximumInputDelay > GBA_LINK_V2_MAX_INPUT_DELAY ||
	    config->estimatedJitterMs > 30000 ||
	    !GBALinkV2DeadlinePolicyValidate(&config->deadlines) ||
	    !config->callbacks || !config->callbacks->quiescentBoundary ||
	    !config->callbacks->setPaused ||
	    !config->callbacks->captureReplica ||
	    !config->callbacks->installPair ||
	    !config->callbacks->commitPair ||
	    !config->callbacks->discardPair || !config->callbacks->failed) {
		return false;
	}
	session->config = *config;
	session->configured = true;
	return true;
}

bool GBALinkV2SessionIsLive(const struct GBALinkV2Session* session) {
	return session &&
	       session->state != GBA_LINK_V2_SESSION_DISCONNECTED &&
	       session->state != GBA_LINK_V2_SESSION_FAILED;
}

void GBALinkV2SessionFail(
	struct GBALinkV2Session* session, enum GBALinkV2Reason reason,
	const char* diagnostic) {
	if (!session || session->state == GBA_LINK_V2_SESSION_FAILED ||
	    session->state == GBA_LINK_V2_SESSION_DISCONNECTED) {
		return;
	}
	session->state = GBA_LINK_V2_SESSION_FAILED;
	_setDeadline(session, GBA_LINK_V2_DEADLINE_NONE);
	if (session->pairInstalled && session->config.callbacks->discardPair) {
		session->config.callbacks->discardPair(
		    session->config.callbackContext, session->pairCommitted);
		session->pairInstalled = false;
	}
	if (session->paused) {
		session->config.callbacks->setPaused(
		    session->config.callbackContext, false);
		session->paused = false;
	}
	_releaseReplicaState(session);
	if (session->config.callbacks->failed) {
		session->config.callbacks->failed(
		    session->config.callbackContext, reason);
	}
	if (session->transport && session->transport->active) {
		GBALinkTransportRequestStop(
		    session->transport, _transportReason(reason), diagnostic);
	}
}

static bool _send(
	struct GBALinkV2Session* session, struct GBALinkV2Packet* packet,
	bool flush) {
	if (!session || !packet || !GBALinkV2SessionIsLive(session) ||
	    session->nextPacketSequence == UINT64_MAX) {
		GBALinkV2SessionFail(
		    session, GBA_LINK_V2_REASON_SEQUENCE,
		    "protocol-v2 packet sequence exhausted");
		return false;
	}
	packet->header.packetSequence = session->nextPacketSequence++;
	uint8_t* encoded = malloc(GBA_LINK_V2_MAX_PACKET_SIZE);
	if (!encoded) {
		GBALinkV2SessionFail(
		    session, GBA_LINK_V2_REASON_QUEUE_EXHAUSTED,
		    "protocol-v2 packet allocation failed");
		return false;
	}
	size_t size = 0;
	bool valid = GBALinkV2PacketEncode(
	    packet, encoded, GBA_LINK_V2_MAX_PACKET_SIZE, &size);
	bool sent = valid && GBALinkTransportSend(
	    session->transport, encoded, size, flush);
	free(encoded);
	if (!sent) {
		GBALinkV2SessionFail(
		    session,
		    session->transport->failureReason == GBA_LINK_REASON_QUEUE_EXHAUSTED
		        ? GBA_LINK_V2_REASON_QUEUE_EXHAUSTED
		        : GBA_LINK_V2_REASON_TRANSPORT_STOP,
		    valid ? "protocol-v2 reliable send failed"
		          : "protocol-v2 packet encode failed");
		return false;
	}
	return true;
}

static bool _sendHello(struct GBALinkV2Session* session) {
	struct GBALinkV2Packet packet;
	memset(&packet, 0, sizeof(packet));
	packet.header.type = GBA_LINK_V2_MESSAGE_HELLO;
	packet.payload.hello = session->localHello;
	return _send(session, &packet, true);
}

static bool _enterQuiescent(struct GBALinkV2Session* session) {
	if (session->paused) {
		return true;
	}
	if (!session->config.callbacks->quiescentBoundary(
	        session->config.callbackContext)) {
		return true;
	}
	session->config.callbacks->setPaused(
	    session->config.callbackContext, true);
	session->paused = true;
	session->state = GBA_LINK_V2_SESSION_HELLO;
	_setDeadline(session, GBA_LINK_V2_DEADLINE_HANDSHAKE);
	return _sendHello(session);
}

bool GBALinkV2SessionStart(
	struct GBALinkV2Session* session, uint64_t transportGeneration,
	enum GBALinkRole localRole) {
	if (!session || !session->configured ||
	    session->state != GBA_LINK_V2_SESSION_DISCONNECTED ||
	    !GBALinkTransportIsActive(session->transport, transportGeneration) ||
	    (localRole != GBA_LINK_ROLE_HOST && localRole != GBA_LINK_ROLE_CLIENT)) {
		return false;
	}
	session->localRole = localRole;
	session->transportGeneration = transportGeneration;
	session->nextPacketSequence = 1;
	session->nextRemotePacketSequence = 1;
	session->state = GBA_LINK_V2_SESSION_WAIT_QUIESCENT;
	memset(&session->localHello, 0, sizeof(session->localHello));
	session->localHello.capabilities = session->config.capabilities;
	session->localHello.requiredCapabilities =
	    GBA_LINK_V2_REQUIRED_CAPABILITIES;
	session->localHello.romSize = session->config.identity.romSize;
	memcpy(session->localHello.romSha1,
	    session->config.identity.romSha1, GBA_LINK_ROM_SHA1_SIZE);
	session->localHello.emulationCompatibilityVersion =
	    session->config.emulationCompatibilityVersion;
	session->localHello.runtimeCompatibilityVersion =
	    GBA_LINK_V2_RUNTIME_COMPATIBILITY_VERSION;
	session->localHello.maxChunkSize = session->config.maxChunkSize;
	session->localHello.supportedEncodings =
	    session->config.supportedEncodings;
	session->localHello.minimumInputDelay =
	    session->config.minimumInputDelay;
	session->localHello.maximumInputDelay =
	    session->config.maximumInputDelay;
	session->localHello.experimentalRuntime =
	    session->config.experimentalRuntime;
	_setDeadline(session, GBA_LINK_V2_DEADLINE_QUIESCENT);
	return _enterQuiescent(session);
}

static bool _helloCompatible(
	struct GBALinkV2Session* session, const struct GBALinkV2Hello* hello) {
	if (hello->runtimeCompatibilityVersion !=
	        GBA_LINK_V2_RUNTIME_COMPATIBILITY_VERSION ||
	    hello->emulationCompatibilityVersion !=
	        session->config.emulationCompatibilityVersion) {
		GBALinkV2SessionFail(
		    session, GBA_LINK_V2_REASON_RUNTIME_MISMATCH,
		    "protocol-v2 runtime compatibility mismatch");
		return false;
	}
	if ((hello->capabilities & GBA_LINK_V2_REQUIRED_CAPABILITIES) !=
	        GBA_LINK_V2_REQUIRED_CAPABILITIES ||
	    hello->requiredCapabilities != GBA_LINK_V2_REQUIRED_CAPABILITIES) {
		GBALinkV2SessionFail(
		    session, GBA_LINK_V2_REASON_CAPABILITY_MISMATCH,
		    "protocol-v2 required capability mismatch");
		return false;
	}
	if (hello->romSize != session->config.identity.romSize ||
	    memcmp(hello->romSha1, session->config.identity.romSha1,
	        GBA_LINK_ROM_SHA1_SIZE)) {
		GBALinkV2SessionFail(
		    session, GBA_LINK_V2_REASON_ROM_MISMATCH,
		    "protocol-v2 exact ROM mismatch");
		return false;
	}
	uint16_t minimum = session->config.minimumInputDelay;
	if (hello->minimumInputDelay > minimum) {
		minimum = hello->minimumInputDelay;
	}
	uint16_t maximum = session->config.maximumInputDelay;
	if (hello->maximumInputDelay < maximum) {
		maximum = hello->maximumInputDelay;
	}
	if (minimum > maximum ||
	    !(hello->supportedEncodings & session->config.supportedEncodings)) {
		GBALinkV2SessionFail(
		    session, GBA_LINK_V2_REASON_CAPABILITY_MISMATCH,
		    "protocol-v2 runtime ranges do not overlap");
		return false;
	}
	session->overlappingMinimumInputDelay = minimum;
	session->overlappingMaximumInputDelay = maximum;
	/* ACCEPT carries a valid provisional value; SESSION_READY freezes D. */
	session->inputDelay = minimum;
	session->selectedChunkSize = session->config.maxChunkSize;
	if (hello->maxChunkSize < session->selectedChunkSize) {
		session->selectedChunkSize = hello->maxChunkSize;
	}
	uint16_t encodings = hello->supportedEncodings &
	                     session->config.supportedEncodings;
	session->selectedEncoding =
	    encodings & GBA_LINK_V2_ENCODING_DEFLATE
	        ? GBA_REPLICA_ENCODING_DEFLATE
	        : GBA_REPLICA_ENCODING_NONE;
	return true;
}

static bool _captureLocal(struct GBALinkV2Session* session) {
	if (session->localCaptured) {
		return true;
	}
	uint8_t player = session->localRole == GBA_LINK_ROLE_HOST ? 0 : 1;
	enum GBAReplicaResult result =
	    session->config.callbacks->captureReplica(
	        session->config.callbackContext, player,
	        session->snapshotGeneration, session->selectedEncoding,
	        session->selectedChunkSize, &session->localBundle);
	if (result != GBA_REPLICA_OK) {
		GBALinkV2SessionFail(
		    session, GBA_LINK_V2_REASON_REPLICA_INVALID,
		    GBAReplicaResultName(result));
		return false;
	}
	session->manifests[player] = session->localBundle.manifest;
	memcpy(session->playerDigests[player],
	    session->localBundle.manifest.uncompressedDigest,
	    MGBA_SHA256_DIGEST_SIZE);
	struct GBAReplicaAssembler assembler;
	result = GBAReplicaAssemblerInit(
	    &assembler, &session->localBundle.manifest, player,
	    session->snapshotGeneration, NULL);
	if (result == GBA_REPLICA_OK) {
		for (size_t offset = 0; offset < session->localBundle.encodedSize;
		     offset += session->localBundle.manifest.chunkSize) {
			size_t size = session->localBundle.encodedSize - offset;
			if (size > session->localBundle.manifest.chunkSize) {
				size = session->localBundle.manifest.chunkSize;
			}
			result = GBAReplicaAssemblerAdd(
			    &assembler, player, session->snapshotGeneration,
			    offset, &session->localBundle.encodedData[offset], size);
			if (result != GBA_REPLICA_OK) {
				break;
			}
		}
	}
	if (result == GBA_REPLICA_OK) {
		result = GBAReplicaAssemblerFinalize(
		    &assembler, &session->payloads[player]);
	}
	GBAReplicaAssemblerDeinit(&assembler);
	if (result != GBA_REPLICA_OK) {
		GBALinkV2SessionFail(
		    session, GBA_LINK_V2_REASON_REPLICA_INVALID,
		    "local replica failed self-verification");
		return false;
	}
	session->localCaptured = true;
	return true;
}

static bool _sendLocalReplica(struct GBALinkV2Session* session) {
	if (session->localSent || !_captureLocal(session)) {
		return session->localSent;
	}
	uint8_t player = session->localRole == GBA_LINK_ROLE_HOST ? 0 : 1;
	struct GBALinkV2Packet packet;
	memset(&packet, 0, sizeof(packet));
	packet.header.type = GBA_LINK_V2_MESSAGE_REPLICA_MANIFEST;
	packet.header.sessionId = session->sessionId;
	packet.payload.replicaManifest.snapshotGeneration =
	    session->snapshotGeneration;
	packet.payload.replicaManifest.player = player;
	if (GBAReplicaManifestEncode(
	        &session->localBundle.manifest,
	        packet.payload.replicaManifest.encoded) != GBA_REPLICA_OK ||
	    !_send(session, &packet, true)) {
		return false;
	}
	for (size_t offset = 0; offset < session->localBundle.encodedSize;
	     offset += session->localBundle.manifest.chunkSize) {
		size_t size = session->localBundle.encodedSize - offset;
		if (size > session->localBundle.manifest.chunkSize) {
			size = session->localBundle.manifest.chunkSize;
		}
		memset(&packet, 0, sizeof(packet));
		packet.header.type = GBA_LINK_V2_MESSAGE_REPLICA_CHUNK;
		packet.header.sessionId = session->sessionId;
		packet.payload.replicaChunk.snapshotGeneration =
		    session->snapshotGeneration;
		packet.payload.replicaChunk.player = player;
		packet.payload.replicaChunk.offset = offset;
		packet.payload.replicaChunk.size = size;
		packet.payload.replicaChunk.data =
		    &session->localBundle.encodedData[offset];
		if (!_send(session, &packet, false)) {
			return false;
		}
	}
	session->localSent = true;
	_setDeadline(session, GBA_LINK_V2_DEADLINE_MANIFEST);
	return true;
}

static bool _tryInstall(struct GBALinkV2Session* session) {
	if (session->pairInstalled || !session->localCaptured ||
	    !session->remotePayloadReady) {
		return true;
	}
	session->state = GBA_LINK_V2_SESSION_INSTALLING;
	_setDeadline(session, GBA_LINK_V2_DEADLINE_INSTALL);
	if (!session->config.callbacks->installPair(
	        session->config.callbackContext, session->manifests,
	        session->payloads)) {
		GBALinkV2SessionFail(
		    session, GBA_LINK_V2_REASON_INSTALL_FAILED,
		    "provisional replicated pair installation failed");
		return false;
	}
	session->pairInstalled = true;
	struct GBALinkV2Packet packet;
	memset(&packet, 0, sizeof(packet));
	packet.header.type = GBA_LINK_V2_MESSAGE_REPLICA_INSTALLED;
	packet.header.sessionId = session->sessionId;
	packet.payload.replicaInstalled.snapshotGeneration =
	    session->snapshotGeneration;
	packet.payload.replicaInstalled.installed = true;
	memcpy(packet.payload.replicaInstalled.playerDigests,
	    session->playerDigests, sizeof(session->playerDigests));
	if (!_send(session, &packet, true)) {
		return false;
	}
	session->localInstalledSent = true;
	session->state = GBA_LINK_V2_SESSION_READY_BARRIER;
	_setDeadline(session, GBA_LINK_V2_DEADLINE_READY);
	return true;
}

static void _fillReady(
	struct GBALinkV2Session* session, struct GBALinkV2SessionReady* ready) {
	memset(ready, 0, sizeof(*ready));
	ready->snapshotGeneration = session->snapshotGeneration;
	ready->firstFrame = session->firstFrame;
	ready->policy = GBA_LINK_V2_READY_EXACT_ROM |
	                GBA_LINK_V2_READY_FIXED_DELAY |
	                GBA_LINK_V2_READY_BILATERAL_INSTALL;
	ready->inputDelay = session->inputDelay;
	memcpy(ready->playerDigests, session->playerDigests,
	    sizeof(ready->playerDigests));
}

static bool _readyEqual(
	struct GBALinkV2Session* session,
	const struct GBALinkV2SessionReady* ready) {
	struct GBALinkV2SessionReady expected;
	_fillReady(session, &expected);
	return !memcmp(&expected, ready, sizeof(expected));
}

static bool _readyCompatibleClient(
	struct GBALinkV2Session* session,
	const struct GBALinkV2SessionReady* ready) {
	if (ready->inputDelay < session->overlappingMinimumInputDelay ||
	    ready->inputDelay > session->overlappingMaximumInputDelay) {
		return false;
	}
	struct GBALinkV2SessionReady expected;
	_fillReady(session, &expected);
	expected.inputDelay = ready->inputDelay;
	return !memcmp(&expected, ready, sizeof(expected));
}

static bool _trySendReady(struct GBALinkV2Session* session) {
	if (session->localRole != GBA_LINK_ROLE_HOST || session->readySent ||
	    !session->pairInstalled || !session->remoteInstalled) {
		return true;
	}
	struct GBALinkV2Packet packet;
	memset(&packet, 0, sizeof(packet));
	packet.header.type = GBA_LINK_V2_MESSAGE_SESSION_READY;
	packet.header.sessionId = session->sessionId;
	_fillReady(session, &packet.payload.sessionReady);
	if (!_send(session, &packet, true)) {
		return false;
	}
	session->readySent = true;
	_setDeadline(session, GBA_LINK_V2_DEADLINE_READY);
	return true;
}

static bool _handleHello(
	struct GBALinkV2Session* session, const struct GBALinkV2Packet* packet) {
	if (session->state != GBA_LINK_V2_SESSION_HELLO ||
	    !_helloCompatible(session, &packet->payload.hello)) {
		return false;
	}
	session->remoteHello = packet->payload.hello;
	if (session->localRole == GBA_LINK_ROLE_CLIENT) {
		return true;
	}
	session->sessionId =
	    (session->transportGeneration << 32) ^
	    GBALinkTransportMonotonicTimeMs(session->transport) ^ UINT64_C(0x52504C32);
	if (!session->sessionId) {
		session->sessionId = 1;
	}
	session->snapshotGeneration = session->sessionId;
	session->firstFrame = 0;
	struct GBALinkV2Packet accept;
	memset(&accept, 0, sizeof(accept));
	accept.header.type = GBA_LINK_V2_MESSAGE_ACCEPT;
	accept.payload.accept.proposedSessionId = session->sessionId;
	accept.payload.accept.snapshotGeneration = session->snapshotGeneration;
	accept.payload.accept.hostTransportId = 0;
	accept.payload.accept.clientTransportId = 1;
	accept.payload.accept.runtimeCompatibilityVersion =
	    GBA_LINK_V2_RUNTIME_COMPATIBILITY_VERSION;
	accept.payload.accept.selectedChunkSize = session->selectedChunkSize;
	accept.payload.accept.selectedEncoding = session->selectedEncoding;
	accept.payload.accept.inputDelay = session->inputDelay;
	session->acceptSentAtMs =
	    GBALinkTransportMonotonicTimeMs(session->transport);
	if (!_send(session, &accept, true)) {
		return false;
	}
	session->state = GBA_LINK_V2_SESSION_ACCEPTED;
	return true;
}

static bool _handleAccept(
	struct GBALinkV2Session* session, const struct GBALinkV2Packet* packet) {
	const struct GBALinkV2Accept* accept = &packet->payload.accept;
	if (session->localRole != GBA_LINK_ROLE_CLIENT ||
	    session->state != GBA_LINK_V2_SESSION_HELLO ||
	    !session->remoteHello.capabilities ||
	    accept->selectedChunkSize != session->selectedChunkSize ||
	    accept->selectedEncoding != session->selectedEncoding ||
	    accept->inputDelay < session->overlappingMinimumInputDelay ||
	    accept->inputDelay > session->overlappingMaximumInputDelay) {
		return false;
	}
	session->inputDelay = accept->inputDelay;
	session->sessionId = accept->proposedSessionId;
	session->snapshotGeneration = accept->snapshotGeneration;
	session->firstFrame = 0;
	if (!_captureLocal(session)) {
		return false;
	}
	struct GBALinkV2Packet ack;
	memset(&ack, 0, sizeof(ack));
	ack.header.type = GBA_LINK_V2_MESSAGE_ACCEPT_ACK;
	ack.header.sessionId = session->sessionId;
	ack.payload.acceptAck.acceptedSessionId = session->sessionId;
	ack.payload.acceptAck.snapshotGeneration = session->snapshotGeneration;
	if (!_send(session, &ack, true)) {
		return false;
	}
	session->state = GBA_LINK_V2_SESSION_REPLICA_EXCHANGE;
	_setDeadline(session, GBA_LINK_V2_DEADLINE_MANIFEST);
	return _sendLocalReplica(session);
}

static bool _handleAcceptAck(
	struct GBALinkV2Session* session, const struct GBALinkV2Packet* packet) {
	if (session->localRole != GBA_LINK_ROLE_HOST ||
	    session->state != GBA_LINK_V2_SESSION_ACCEPTED ||
	    packet->payload.acceptAck.acceptedSessionId != session->sessionId ||
	    packet->payload.acceptAck.snapshotGeneration !=
	        session->snapshotGeneration || !_captureLocal(session)) {
		return false;
	}
	uint64_t now = GBALinkTransportMonotonicTimeMs(session->transport);
	uint64_t elapsed = now >= session->acceptSentAtMs
	    ? now - session->acceptSentAtMs
	    : 0;
	session->handshakeRoundTripMs =
	    elapsed > UINT32_MAX ? UINT32_MAX : elapsed;
	session->inputDelay = GBALinkInputSelectDelay(
	    session->overlappingMinimumInputDelay,
	    session->overlappingMaximumInputDelay,
	    session->handshakeRoundTripMs, session->config.estimatedJitterMs);
	if (session->inputDelay == UINT16_MAX) {
		return false;
	}
	session->state = GBA_LINK_V2_SESSION_REPLICA_EXCHANGE;
	_setDeadline(session, GBA_LINK_V2_DEADLINE_MANIFEST);
	return _sendLocalReplica(session);
}

static bool _handleManifest(
	struct GBALinkV2Session* session, const struct GBALinkV2Packet* packet) {
	uint8_t remotePlayer = session->localRole == GBA_LINK_ROLE_HOST ? 1 : 0;
	if ((session->state != GBA_LINK_V2_SESSION_REPLICA_EXCHANGE &&
	     session->state != GBA_LINK_V2_SESSION_INSTALLING) ||
	    session->remoteManifestReceived ||
	    packet->payload.replicaManifest.snapshotGeneration !=
	        session->snapshotGeneration ||
	    packet->payload.replicaManifest.player != remotePlayer) {
		return false;
	}
	struct GBAReplicaManifest manifest;
	if (GBAReplicaManifestDecode(
	        packet->payload.replicaManifest.encoded,
	        GBA_REPLICA_MANIFEST_SIZE, NULL, &manifest) != GBA_REPLICA_OK ||
	    manifest.generation != session->snapshotGeneration ||
	    manifest.player != remotePlayer ||
	    manifest.chunkSize != session->selectedChunkSize ||
	    manifest.encoding != session->selectedEncoding ||
	    GBAReplicaAssemblerInit(
	        &session->remoteAssembler, &manifest, remotePlayer,
	        session->snapshotGeneration, NULL) != GBA_REPLICA_OK) {
		GBALinkV2SessionFail(
		    session, GBA_LINK_V2_REASON_REPLICA_INVALID,
		    "remote replica manifest failed validation");
		return false;
	}
	session->manifests[remotePlayer] = manifest;
	memcpy(session->playerDigests[remotePlayer],
	    manifest.uncompressedDigest, MGBA_SHA256_DIGEST_SIZE);
	session->remoteManifestReceived = true;
	_setDeadline(session, GBA_LINK_V2_DEADLINE_CHUNKS);
	return true;
}

static bool _handleChunk(
	struct GBALinkV2Session* session, const struct GBALinkV2Packet* packet) {
	const struct GBALinkV2ReplicaChunk* chunk = &packet->payload.replicaChunk;
	uint8_t remotePlayer = session->localRole == GBA_LINK_ROLE_HOST ? 1 : 0;
	if (!session->remoteManifestReceived || session->remotePayloadReady ||
	    chunk->snapshotGeneration != session->snapshotGeneration ||
	    chunk->player != remotePlayer) {
		return false;
	}
	enum GBAReplicaResult result = GBAReplicaAssemblerAdd(
	    &session->remoteAssembler, chunk->player,
	    chunk->snapshotGeneration, chunk->offset, chunk->data, chunk->size);
	if (result != GBA_REPLICA_OK && result != GBA_REPLICA_DUPLICATE) {
		GBALinkV2SessionFail(
		    session, GBA_LINK_V2_REASON_REPLICA_INVALID,
		    GBAReplicaResultName(result));
		return false;
	}
	if (session->remoteAssembler.receivedBytes ==
	    session->remoteAssembler.manifest.encodedSize) {
		result = GBAReplicaAssemblerFinalize(
		    &session->remoteAssembler, &session->payloads[remotePlayer]);
		if (result != GBA_REPLICA_OK) {
			GBALinkV2SessionFail(
			    session, GBA_LINK_V2_REASON_REPLICA_INVALID,
			    GBAReplicaResultName(result));
			return false;
		}
		session->remotePayloadReady = true;
		return _tryInstall(session);
	}
	return true;
}

static bool _handleInstalled(
	struct GBALinkV2Session* session, const struct GBALinkV2Packet* packet) {
	if (!session->pairInstalled || session->remoteInstalled ||
	    packet->payload.replicaInstalled.snapshotGeneration !=
	        session->snapshotGeneration ||
	    !packet->payload.replicaInstalled.installed ||
	    memcmp(packet->payload.replicaInstalled.playerDigests,
	        session->playerDigests, sizeof(session->playerDigests))) {
		return false;
	}
	session->remoteInstalled = true;
	return _trySendReady(session);
}

static bool _handleReady(
	struct GBALinkV2Session* session, const struct GBALinkV2Packet* packet) {
	if (session->localRole != GBA_LINK_ROLE_CLIENT ||
	    session->state != GBA_LINK_V2_SESSION_READY_BARRIER ||
	    !session->pairInstalled || !session->remoteInstalled ||
	    !_readyCompatibleClient(session, &packet->payload.sessionReady)) {
		return false;
	}
	session->inputDelay = packet->payload.sessionReady.inputDelay;
	struct GBALinkV2Packet ack;
	memset(&ack, 0, sizeof(ack));
	ack.header.type = GBA_LINK_V2_MESSAGE_SESSION_READY_ACK;
	ack.header.sessionId = session->sessionId;
	ack.payload.sessionReady = packet->payload.sessionReady;
	return _send(session, &ack, true);
}

static bool _handleReadyAck(
	struct GBALinkV2Session* session, const struct GBALinkV2Packet* packet) {
	if (session->localRole != GBA_LINK_ROLE_HOST || !session->readySent ||
	    session->readyAcked || !_readyEqual(session, &packet->payload.sessionReady)) {
		return false;
	}
	session->readyAcked = true;
	struct GBALinkV2Packet release;
	memset(&release, 0, sizeof(release));
	release.header.type = GBA_LINK_V2_MESSAGE_INPUT_WINDOW;
	release.header.sessionId = session->sessionId;
	release.payload.inputWindow.snapshotGeneration = session->snapshotGeneration;
	release.payload.inputWindow.firstFrame = session->firstFrame;
	release.payload.inputWindow.frameCount = session->inputDelay + 1;
	release.payload.inputWindow.inputDelay = session->inputDelay;
	if (!_send(session, &release, true)) {
		return false;
	}
	session->config.callbacks->commitPair(session->config.callbackContext);
	session->pairCommitted = true;
	session->config.callbacks->setPaused(
	    session->config.callbackContext, false);
	session->paused = false;
	session->state = GBA_LINK_V2_SESSION_READY;
	_setDeadline(session, GBA_LINK_V2_DEADLINE_NONE);
	return true;
}

static bool _handleInputWindow(
	struct GBALinkV2Session* session, const struct GBALinkV2Packet* packet) {
	const struct GBALinkV2InputWindow* window = &packet->payload.inputWindow;
	if (session->localRole != GBA_LINK_ROLE_CLIENT ||
	    session->state != GBA_LINK_V2_SESSION_READY_BARRIER ||
	    window->snapshotGeneration != session->snapshotGeneration ||
	    window->firstFrame != session->firstFrame ||
	    window->inputDelay != session->inputDelay ||
	    window->frameCount != session->inputDelay + 1) {
		return false;
	}
	session->config.callbacks->commitPair(session->config.callbackContext);
	session->pairCommitted = true;
	session->config.callbacks->setPaused(
	    session->config.callbackContext, false);
	session->paused = false;
	session->state = GBA_LINK_V2_SESSION_READY;
	_setDeadline(session, GBA_LINK_V2_DEADLINE_NONE);
	return true;
}

static bool _handlePacket(
	struct GBALinkV2Session* session, const struct GBALinkV2Packet* packet) {
	switch (packet->header.type) {
	case GBA_LINK_V2_MESSAGE_HELLO:
		return _handleHello(session, packet);
	case GBA_LINK_V2_MESSAGE_ACCEPT:
		return _handleAccept(session, packet);
	case GBA_LINK_V2_MESSAGE_ACCEPT_ACK:
		return _handleAcceptAck(session, packet);
	case GBA_LINK_V2_MESSAGE_REPLICA_MANIFEST:
		return _handleManifest(session, packet);
	case GBA_LINK_V2_MESSAGE_REPLICA_CHUNK:
		return _handleChunk(session, packet);
	case GBA_LINK_V2_MESSAGE_REPLICA_INSTALLED:
		return _handleInstalled(session, packet);
	case GBA_LINK_V2_MESSAGE_SESSION_READY:
		return _handleReady(session, packet);
	case GBA_LINK_V2_MESSAGE_SESSION_READY_ACK:
		return _handleReadyAck(session, packet);
	case GBA_LINK_V2_MESSAGE_INPUT_WINDOW:
		return _handleInputWindow(session, packet);
	case GBA_LINK_V2_MESSAGE_DETACH: {
		if (packet->payload.reason.snapshotGeneration !=
		    session->snapshotGeneration) {
			return false;
		}
		struct GBALinkV2Packet ack;
		memset(&ack, 0, sizeof(ack));
		ack.header.type = GBA_LINK_V2_MESSAGE_DETACH_ACK;
		ack.header.sessionId = session->sessionId;
		ack.payload.reason = packet->payload.reason;
		_send(session, &ack, true);
		GBALinkV2SessionFail(
		    session, GBA_LINK_V2_REASON_PEER_DETACH,
		    "protocol-v2 peer detached");
		return true;
	}
	case GBA_LINK_V2_MESSAGE_DETACH_ACK:
		if (packet->payload.reason.snapshotGeneration !=
		    session->snapshotGeneration) {
			return false;
		}
		GBALinkV2SessionFail(
		    session, packet->payload.reason.reason,
		    "protocol-v2 detach acknowledged");
		return true;
	case GBA_LINK_V2_MESSAGE_REJECT:
		GBALinkV2SessionFail(
		    session, packet->payload.reason.reason,
		    "protocol-v2 peer rejected or acknowledged detach");
		return true;
	case GBA_LINK_V2_MESSAGE_INPUT_BATCH:
		if (packet->payload.inputBatch.snapshotGeneration !=
		        session->snapshotGeneration ||
		    packet->payload.inputBatch.records[0].frame <
		        session->firstFrame) {
			return false;
		}
		return session->state == GBA_LINK_V2_SESSION_READY &&
		       (!session->config.callbacks->runtimePacket ||
		        session->config.callbacks->runtimePacket(
		            session->config.callbackContext, packet));
	case GBA_LINK_V2_MESSAGE_STATE_CHECK:
		if (packet->payload.stateCheck.snapshotGeneration !=
		        session->snapshotGeneration ||
		    packet->payload.stateCheck.frame < session->firstFrame) {
			return false;
		}
		return session->state == GBA_LINK_V2_SESSION_READY &&
		       (!session->config.callbacks->runtimePacket ||
		        session->config.callbacks->runtimePacket(
		            session->config.callbackContext, packet));
	}
	return false;
}

static bool _processCopied(
	struct GBALinkV2Session* session,
	const struct GBALinkCopiedPacket* copied) {
	if (copied->generation != session->transportGeneration) {
		return true;
	}
	enum GBALinkRole remoteRole = session->localRole == GBA_LINK_ROLE_HOST
	    ? GBA_LINK_ROLE_CLIENT
	    : GBA_LINK_ROLE_HOST;
	struct GBALinkV2Packet packet;
	enum GBALinkDecodeStatus status = GBALinkV2PacketDecode(
	    copied->data, copied->size, remoteRole, &packet);
	if (status != GBA_LINK_DECODE_OK) {
		GBALinkV2SessionFail(
		    session, GBA_LINK_V2_REASON_MALFORMED_PACKET,
		    GBALinkDecodeStatusName(status));
		return false;
	}
	if (packet.header.packetSequence != session->nextRemotePacketSequence) {
		GBALinkV2SessionFail(
		    session, GBA_LINK_V2_REASON_SEQUENCE,
		    "protocol-v2 stale, skipped, or future packet sequence");
		return false;
	}
	if (session->nextRemotePacketSequence == UINT64_MAX) {
		GBALinkV2SessionFail(
		    session, GBA_LINK_V2_REASON_SEQUENCE,
		    "protocol-v2 remote packet sequence exhausted");
		return false;
	}
	++session->nextRemotePacketSequence;
	if (packet.header.sessionId &&
	    packet.header.sessionId != session->sessionId) {
		GBALinkV2SessionFail(
		    session, GBA_LINK_V2_REASON_SEQUENCE,
		    "protocol-v2 session ID mismatch");
		return false;
	}
	if (!_handlePacket(session, &packet) &&
	    session->state != GBA_LINK_V2_SESSION_FAILED) {
		GBALinkV2SessionFail(
		    session, GBA_LINK_V2_REASON_INVALID_TRANSITION,
		    "protocol-v2 message invalid in current state");
		return false;
	}
	return session->state != GBA_LINK_V2_SESSION_FAILED;
}

static enum GBALinkV2Reason _deadlineReason(
	enum GBALinkV2DeadlineOperation operation) {
	switch (operation) {
	case GBA_LINK_V2_DEADLINE_QUIESCENT:
		return GBA_LINK_V2_REASON_ATTACHMENT_TIMEOUT;
	case GBA_LINK_V2_DEADLINE_MANIFEST:
	case GBA_LINK_V2_DEADLINE_CHUNKS:
		return GBA_LINK_V2_REASON_REPLICA_TIMEOUT;
	case GBA_LINK_V2_DEADLINE_INSTALL:
		return GBA_LINK_V2_REASON_INSTALL_FAILED;
	case GBA_LINK_V2_DEADLINE_READY:
		return GBA_LINK_V2_REASON_READY_TIMEOUT;
	default:
		return GBA_LINK_V2_REASON_TRANSPORT_STOP;
	}
}

bool GBALinkV2SessionUpdate(
	struct GBALinkV2Session* session, bool pollReceive) {
	if (!GBALinkV2SessionIsLive(session)) {
		return false;
	}
	if (!GBALinkTransportIsActive(
	        session->transport, session->transportGeneration)) {
		GBALinkV2SessionFail(
		    session, GBA_LINK_V2_REASON_TRANSPORT_STOP,
		    "protocol-v2 transport generation stopped");
		return false;
	}
	if (session->state == GBA_LINK_V2_SESSION_WAIT_QUIESCENT &&
	    !_enterQuiescent(session)) {
		return false;
	}
	if (session->state == GBA_LINK_V2_SESSION_WAIT_QUIESCENT) {
		if (GBALinkTransportMonotonicTimeMs(session->transport) >=
		    session->deadlineAtMs) {
			GBALinkV2SessionFail(
			    session, GBA_LINK_V2_REASON_ATTACHMENT_TIMEOUT,
			    GBALinkV2DeadlineOperationName(
			        GBA_LINK_V2_DEADLINE_QUIESCENT));
			return false;
		}
		/* Do not expose peer traffic before accepting the local boundary. */
		return true;
	}
	if (pollReceive && !GBALinkTransportPoll(session->transport)) {
		GBALinkV2SessionFail(
		    session, GBA_LINK_V2_REASON_TRANSPORT_STOP,
		    "protocol-v2 receive poll stopped transport");
		return false;
	}
	struct GBALinkCopiedPacket copied;
	memset(&copied, 0, sizeof(copied));
	while (GBALinkTransportPopInbound(session->transport, &copied)) {
		bool processed = _processCopied(session, &copied);
		GBALinkCopiedPacketDeinit(&copied);
		if (!processed) {
			return false;
		}
	}
	if (session->deadlineOperation != GBA_LINK_V2_DEADLINE_NONE &&
	    GBALinkTransportMonotonicTimeMs(session->transport) >=
	        session->deadlineAtMs) {
		enum GBALinkV2DeadlineOperation operation =
		    session->deadlineOperation;
		GBALinkV2SessionFail(
		    session, _deadlineReason(operation),
		    GBALinkV2DeadlineOperationName(operation));
		return false;
	}
	return true;
}

bool GBALinkV2SessionSendRuntime(
	struct GBALinkV2Session* session, struct GBALinkV2Packet* packet,
	enum GBALinkV2DeadlineOperation deadline) {
	if (!session || session->state != GBA_LINK_V2_SESSION_READY || !packet ||
	    (packet->header.type != GBA_LINK_V2_MESSAGE_INPUT_BATCH &&
	     packet->header.type != GBA_LINK_V2_MESSAGE_STATE_CHECK &&
	     packet->header.type != GBA_LINK_V2_MESSAGE_DETACH) ||
	    deadline < GBA_LINK_V2_DEADLINE_NONE ||
	    deadline >= GBA_LINK_V2_DEADLINE_OPERATION_COUNT) {
		return false;
	}
	packet->header.sessionId = session->sessionId;
	if (!_send(session, packet, true)) {
		return false;
	}
	_setDeadline(session, deadline);
	return true;
}
