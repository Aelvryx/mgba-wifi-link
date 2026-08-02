/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef GBA_SIO_NETPLAY_SESSION_H
#define GBA_SIO_NETPLAY_SESSION_H

#include <mgba-util/common.h>

#include <mgba/internal/gba/sio/netplay/identity-v1.h>
#include <mgba/internal/gba/sio/netplay/protocol.h>

CXX_GUARD_START

struct GBALinkTransport;

enum GBALinkSessionState {
	GBA_LINK_SESSION_DISCONNECTED,
	GBA_LINK_SESSION_TRANSPORT_STARTED,
	GBA_LINK_SESSION_HELLO_EXCHANGED,
	GBA_LINK_SESSION_ACCEPTED,
	GBA_LINK_SESSION_ATTACH_BARRIER,
	GBA_LINK_SESSION_READY,
	GBA_LINK_SESSION_TRANSFERRING,
	GBA_LINK_SESSION_FAILED,
};

enum GBALinkSequenceDomain {
	GBA_LINK_SEQUENCE_PACKET,
	GBA_LINK_SEQUENCE_SESSION,
	GBA_LINK_SEQUENCE_GRANT,
	GBA_LINK_SEQUENCE_MODE,
	GBA_LINK_SEQUENCE_TRANSFER,
	GBA_LINK_SEQUENCE_COMPLETION,
	GBA_LINK_SEQUENCE_HEALTH,
	GBA_LINK_SEQUENCE_DOMAIN_COUNT,
};

enum GBALinkDeadlineOperation {
	GBA_LINK_DEADLINE_NONE,
	GBA_LINK_DEADLINE_HANDSHAKE,
	GBA_LINK_DEADLINE_ATTACHMENT,
	GBA_LINK_DEADLINE_GRANT,
	GBA_LINK_DEADLINE_MODE,
	GBA_LINK_DEADLINE_TRANSFER_READINESS,
	GBA_LINK_DEADLINE_TRANSFER_COMMIT,
	GBA_LINK_DEADLINE_COMPLETION_CATCHUP,
	GBA_LINK_DEADLINE_COMPLETION_READINESS,
	GBA_LINK_DEADLINE_COMPLETION_DECISION,
	GBA_LINK_DEADLINE_DETACH,
	GBA_LINK_DEADLINE_OPERATION_COUNT,
};

struct GBALinkSequenceState {
	uint64_t next[GBA_LINK_SEQUENCE_DOMAIN_COUNT];
	uint32_t exhausted;
};

struct GBALinkDeadlinePolicy {
	uint32_t milliseconds[GBA_LINK_DEADLINE_OPERATION_COUNT];
};

struct GBALinkSessionCallbacks {
	bool (*quiescentSnapshot)(
	    void* context, enum GBALinkWireMode* mode, uint64_t* localCycle);
	void (*setPaused)(void* context, bool paused);
	void (*setAttachment)(
	    void* context, bool installed, bool observable, uint64_t attachCycle);
	bool (*runtimePacket)(void* context, const struct GBALinkPacket* packet);
	void (*failed)(void* context, enum GBALinkReason reason);
};

struct GBALinkSessionConfig {
	struct GBALinkContentIdentity identity;
	struct GBALinkDeterminismDigest
	    digests[GBA_LINK_MAX_DETERMINISM_DIGESTS];
	uint64_t capabilities;
	uint32_t supportedPolicies;
	uint32_t emulationCompatibilityVersion;
	bool cheatsEnabled;
	struct GBALinkDeadlinePolicy deadlines;
	const struct GBALinkSessionCallbacks* callbacks;
	void* callbackContext;
};

struct GBALinkSession {
	struct GBALinkTransport* transport;
	enum GBALinkSessionState state;
	enum GBALinkRole localRole;
	uint64_t transportGeneration;
	uint64_t sessionId;
	struct GBALinkSequenceState sequences;
	struct GBALinkSessionConfig config;
	bool configured;
	bool paused;
	bool localHelloSent;
	bool remoteHelloReceived;
	bool remotePacketExhausted;
	struct GBALinkHello localHello;
	struct GBALinkHello remoteHello;
	uint64_t attachCycle;
	uint64_t initialModeGeneration;
	enum GBALinkDeadlineOperation deadlineOperation;
	uint64_t deadlineAtMs;
	uint64_t nextRemotePacketSequence;
	uint64_t lastRemotePacketSequence;
	uint8_t lastRemotePacket[GBA_LINK_MAX_PACKET_SIZE];
	size_t lastRemotePacketSize;
	uint8_t replayPackets[2][GBA_LINK_MAX_PACKET_SIZE];
	size_t replayPacketSizes[2];
	size_t replayPacketCount;
	bool processingInbound;
	bool yieldInbound;
};

void GBALinkDeadlinePolicyInit(struct GBALinkDeadlinePolicy* policy);
bool GBALinkDeadlinePolicyValidate(
    const struct GBALinkDeadlinePolicy* policy);
void GBALinkSequenceStateInit(struct GBALinkSequenceState* sequences);
bool GBALinkSequenceTake(
    struct GBALinkSequenceState* sequences,
    enum GBALinkSequenceDomain domain, uint64_t* value);
bool GBALinkSequenceIsExhausted(
    const struct GBALinkSequenceState* sequences,
    enum GBALinkSequenceDomain domain);

void GBALinkSessionInit(
    struct GBALinkSession* session, struct GBALinkTransport* transport);
void GBALinkSessionDeinit(struct GBALinkSession* session);
bool GBALinkSessionConfigure(
    struct GBALinkSession* session,
    const struct GBALinkSessionConfig* config);
bool GBALinkSessionStart(
    struct GBALinkSession* session, uint64_t transportGeneration,
    enum GBALinkRole localRole);
bool GBALinkSessionUpdate(
    struct GBALinkSession* session, bool pollReceive);
void GBALinkSessionYieldInbound(
    struct GBALinkSession* session);
bool GBALinkSessionSendRuntime(
    struct GBALinkSession* session, struct GBALinkPacket* packet,
    enum GBALinkDeadlineOperation deadline);
void GBALinkSessionSetDeadline(
    struct GBALinkSession* session,
    enum GBALinkDeadlineOperation deadline);
void GBALinkSessionFail(
    struct GBALinkSession* session, enum GBALinkReason reason,
    const char* diagnostic);
bool GBALinkSessionIsLive(const struct GBALinkSession* session);
const char* GBALinkSessionStateName(enum GBALinkSessionState state);
const char* GBALinkDeadlineOperationName(
    enum GBALinkDeadlineOperation operation);

CXX_GUARD_END

#endif
