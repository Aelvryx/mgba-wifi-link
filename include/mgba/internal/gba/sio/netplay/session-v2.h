/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef GBA_SIO_NETPLAY_SESSION_V2_H
#define GBA_SIO_NETPLAY_SESSION_V2_H

#include <mgba-util/common.h>

#include <mgba/internal/gba/sio/netplay/identity.h>
#include <mgba/internal/gba/sio/netplay/input-sync.h>
#include <mgba/internal/gba/sio/netplay/protocol-v2.h>
#include <mgba/internal/gba/sio/netplay/transport.h>

CXX_GUARD_START

enum GBALinkV2SessionState {
	GBA_LINK_V2_SESSION_DISCONNECTED,
	GBA_LINK_V2_SESSION_WAIT_QUIESCENT,
	GBA_LINK_V2_SESSION_HELLO,
	GBA_LINK_V2_SESSION_CALIBRATION_BEGIN_WAIT,
	GBA_LINK_V2_SESSION_HOST_PROBES,
	GBA_LINK_V2_SESSION_CLIENT_PROBES,
	GBA_LINK_V2_SESSION_WAIT_ACCEPT,
	GBA_LINK_V2_SESSION_CALIBRATED,
	GBA_LINK_V2_SESSION_ACCEPTED,
	GBA_LINK_V2_SESSION_REPLICA_EXCHANGE,
	GBA_LINK_V2_SESSION_INSTALLING,
	GBA_LINK_V2_SESSION_READY_BARRIER,
	GBA_LINK_V2_SESSION_READY,
	GBA_LINK_V2_SESSION_FAILED,
};

enum GBALinkV2DeadlineOperation {
	GBA_LINK_V2_DEADLINE_NONE,
	GBA_LINK_V2_DEADLINE_QUIESCENT,
	GBA_LINK_V2_DEADLINE_HANDSHAKE,
	GBA_LINK_V2_DEADLINE_CALIBRATION,
	GBA_LINK_V2_DEADLINE_ACCEPT,
	GBA_LINK_V2_DEADLINE_MANIFEST,
	GBA_LINK_V2_DEADLINE_CHUNKS,
	GBA_LINK_V2_DEADLINE_INSTALL,
	GBA_LINK_V2_DEADLINE_READY,
	GBA_LINK_V2_DEADLINE_INPUT,
	GBA_LINK_V2_DEADLINE_VERIFY,
	GBA_LINK_V2_DEADLINE_DETACH,
	GBA_LINK_V2_DEADLINE_OPERATION_COUNT,
};

struct GBALinkV2DeadlinePolicy {
	uint32_t milliseconds[GBA_LINK_V2_DEADLINE_OPERATION_COUNT];
};

struct GBALinkV2SessionCallbacks {
	bool (*quiescentBoundary)(void* context);
	void (*setPaused)(void* context, bool paused);
	enum GBAReplicaResult (*captureReplica)(
		void* context, uint8_t player, uint64_t generation,
		enum GBAReplicaEncoding encoding, uint32_t chunkSize,
		struct GBAReplicaBundle* bundle);
	bool (*installPair)(
		void* context, const struct GBAReplicaManifest manifests[2],
		const struct GBAReplicaPayload payloads[2]);
	bool (*commitPair)(void* context);
	void (*discardPair)(void* context, bool committed);
	bool (*runtimePacket)(
		void* context, const struct GBALinkV2Packet* packet);
	void (*failed)(void* context, enum GBALinkV2Reason reason);
};

struct GBALinkV2SessionConfig {
	struct GBALinkContentIdentity identity;
	uint64_t capabilities;
	uint16_t supportedEncodings;
	uint32_t emulationCompatibilityVersion;
	uint32_t maxChunkSize;
	uint16_t minimumInputDelay;
	uint16_t maximumInputDelay;
	uint32_t estimatedJitterMs;
	bool experimentalRuntime;
	enum GBALinkV2ProductPolicy productPolicy;
	struct GBALinkV2DeterminismProfile determinismProfile;
	struct GBALinkV2DeterminismCapabilities deterministicCapabilities;
	uint64_t cartridgeRequiredInputMask;
	struct GBALinkV2DeadlinePolicy deadlines;
	const struct GBALinkV2SessionCallbacks* callbacks;
	void* callbackContext;
};

struct GBALinkV2Session {
	struct GBALinkTransport* transport;
	struct GBALinkV2SessionConfig config;
	enum GBALinkV2SessionState state;
	enum GBALinkRole localRole;
	uint64_t transportGeneration;
	uint64_t sessionId;
	uint64_t snapshotGeneration;
	uint64_t nextPacketSequence;
	uint64_t nextRemotePacketSequence;
	uint64_t deadlineAtMs;
	enum GBALinkV2DeadlineOperation deadlineOperation;
	struct GBALinkV2Hello localHello;
	struct GBALinkV2Hello remoteHello;
	struct GBALinkV2Packet lastRemoteCalibrationPacket;
	struct GBAReplicaBundle localBundle;
	struct GBAReplicaAssembler remoteAssembler;
	struct GBAReplicaManifest manifests[2];
	struct GBAReplicaPayload payloads[2];
	uint8_t playerDigests[2][MGBA_SHA256_DIGEST_SIZE];
	uint32_t selectedChunkSize;
	enum GBAReplicaEncoding selectedEncoding;
	uint16_t overlappingMinimumInputDelay;
	uint16_t overlappingMaximumInputDelay;
	uint16_t inputDelay;
	uint32_t handshakeRoundTripMs;
	uint64_t acceptSentAtMs;
	uint64_t calibrationDeadlineAtUs;
	uint64_t acceptDeadlineAtUs;
	uint64_t firstFrame;
	uint64_t missingRequiredInputMask;
	struct GBALinkInputCalibration calibration;
	struct GBALinkInputSelection selection;
	uint64_t probeStartedAtUs;
	uint8_t nextProbeOrdinal;
	uint8_t nextRemoteProbeOrdinal;
	uint16_t profileMismatchCategory;
	uint16_t remoteProfileSchemaVersion;
	uint32_t remoteRuntimeCompatibilityVersion;
	enum GBALinkV2CapabilityMismatch capabilityMismatch;
	enum GBALinkV2ProductPolicy productPolicy;
	uint16_t productionFloor;
	bool probeOutstanding;
	bool calibrationDeadlineActive;
	bool acceptDeadlineActive;
	bool hostReportReceived;
	bool clientReportReceived;
	bool remoteHelloReceived;
	bool lastRemoteCalibrationValid;
	bool configured;
	bool paused;
	bool localCaptured;
	bool localSent;
	bool remoteManifestReceived;
	bool remotePayloadReady;
	bool pairInstalled;
	bool pairCommitted;
	bool localInstalledSent;
	bool remoteInstalled;
	bool readySent;
	bool readyAcked;
};

void GBALinkV2DeadlinePolicyInit(struct GBALinkV2DeadlinePolicy* policy);
bool GBALinkV2DeadlinePolicyValidate(
	const struct GBALinkV2DeadlinePolicy* policy);
void GBALinkV2SessionInit(
	struct GBALinkV2Session* session, struct GBALinkTransport* transport);
void GBALinkV2SessionDeinit(struct GBALinkV2Session* session);
bool GBALinkV2SessionConfigure(
	struct GBALinkV2Session* session,
	const struct GBALinkV2SessionConfig* config);
bool GBALinkV2SessionStart(
	struct GBALinkV2Session* session, uint64_t transportGeneration,
	enum GBALinkRole localRole);
bool GBALinkV2SessionUpdate(
	struct GBALinkV2Session* session, bool pollReceive);
bool GBALinkV2SessionSendRuntime(
	struct GBALinkV2Session* session, struct GBALinkV2Packet* packet,
	enum GBALinkV2DeadlineOperation deadline);
void GBALinkV2SessionRuntimeDeadlineSatisfied(
	struct GBALinkV2Session* session,
	enum GBALinkV2DeadlineOperation operation);
void GBALinkV2SessionFail(
	struct GBALinkV2Session* session, enum GBALinkV2Reason reason,
	const char* diagnostic);
bool GBALinkV2SessionIsLive(const struct GBALinkV2Session* session);
const char* GBALinkV2SessionStateName(enum GBALinkV2SessionState state);
const char* GBALinkV2DeadlineOperationName(
	enum GBALinkV2DeadlineOperation operation);
size_t GBALinkV2SessionFormatFailureDetail(
	const struct GBALinkV2Session* session, enum GBALinkV2Reason reason,
	char* output, size_t capacity);

CXX_GUARD_END

#endif
