/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifdef M_LIBRETRO_NETPACKET_V2_INSTANCE_PREFIX
#define M_NETPACKET_V2_JOIN_(A, B) A ## B
#define M_NETPACKET_V2_JOIN(A, B) M_NETPACKET_V2_JOIN_(A, B)
#define M_NETPACKET_V2_SYMBOL(NAME) \
	M_NETPACKET_V2_JOIN(M_LIBRETRO_NETPACKET_V2_INSTANCE_PREFIX, NAME)
#define mLibretroNetpacketV2Register M_NETPACKET_V2_SYMBOL(Register)
#define mLibretroNetpacketV2RunBegin M_NETPACKET_V2_SYMBOL(RunBegin)
#define mLibretroNetpacketV2RunFrame M_NETPACKET_V2_SYMBOL(RunFrame)
#define mLibretroNetpacketV2ExecutionBlocked \
	M_NETPACKET_V2_SYMBOL(ExecutionBlocked)
#define mLibretroNetpacketV2OwnsExecution \
	M_NETPACKET_V2_SYMBOL(OwnsExecution)
#define mLibretroNetpacketV2PresentedCore \
	M_NETPACKET_V2_SYMBOL(PresentedCore)
#define mLibretroNetpacketV2PresentedVideo \
	M_NETPACKET_V2_SYMBOL(PresentedVideo)
#define mLibretroNetpacketV2ReportAudio \
	M_NETPACKET_V2_SYMBOL(ReportAudio)
#define mLibretroNetpacketV2Reset M_NETPACKET_V2_SYMBOL(Reset)
#define mLibretroNetpacketV2Unload M_NETPACKET_V2_SYMBOL(Unload)
#define mLibretroNetpacketV2SessionActive \
	M_NETPACKET_V2_SYMBOL(SessionActive)
#define mLibretroNetpacketV2RejectOperation \
	M_NETPACKET_V2_SYMBOL(RejectOperation)
#define mLibretroNetpacketV2TestPollReceive \
	M_NETPACKET_V2_SYMBOL(TestPollReceive)
#define mLibretroNetpacketV2TestSetTimeMs \
	M_NETPACKET_V2_SYMBOL(TestSetTimeMs)
#define mLibretroNetpacketV2TestCallbackGeneration \
	M_NETPACKET_V2_SYMBOL(TestCallbackGeneration)
#define mLibretroNetpacketV2TestPendingPacketCount \
	M_NETPACKET_V2_SYMBOL(TestPendingPacketCount)
#define mLibretroNetpacketV2TestPlayerForRole \
	M_NETPACKET_V2_SYMBOL(TestPlayerForRole)
#define mLibretroNetpacketV2TestInstallPair \
	M_NETPACKET_V2_SYMBOL(TestInstallPair)
#define mLibretroNetpacketV2TestPairCore \
	M_NETPACKET_V2_SYMBOL(TestPairCore)
#define mLibretroNetpacketV2TestFail M_NETPACKET_V2_SYMBOL(TestFail)
#endif

#include "netpacket-v2.h"

#include <inttypes.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sched.h>
#endif

#include <mgba/core/cheats.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/replicated-pair.h>
#include <mgba/internal/gba/replicated-runtime.h>
#include <mgba/internal/gba/serialize.h>
#include <mgba/internal/gba/sio/netplay/identity.h>
#include <mgba/internal/gba/sio/netplay/session-v2.h>
#include <mgba/internal/gba/sio/netplay/transport.h>
#include <mgba-util/audio-buffer.h>

#define NETPACKET_V2_NO_CLIENT UINT16_MAX

enum {
	NETPACKET_V2_VERIFICATION_INTERVAL = 60,
	NETPACKET_V2_RENDEZVOUS_HISTOGRAM_MAX_MS = 3000,
	NETPACKET_V2_LINK_TEST_MAGIC = 0x31544B4C,
};

struct mLibretroNetpacketV2Metrics {
	uint64_t startedAtMs;
	uint64_t readyAtMs;
	uint64_t sentPackets;
	uint64_t sentBytes;
	uint64_t receivedPackets;
	uint64_t receivedBytes;
	uint64_t receivePolls;
	uint64_t rendezvousCount;
	uint64_t rendezvousTotalMs;
	uint64_t rendezvousMaxMs;
	uint32_t rendezvousHistogram[
	    NETPACKET_V2_RENDEZVOUS_HISTOGRAM_MAX_MS + 1];
	uint64_t verificationMatches;
	uint64_t audioFrames;
	uint64_t audioSamples;
	uint64_t emptyAudioFrames;
	size_t queueHighWater;
};

struct mLibretroNetpacketV2Adapter {
	retro_environment_t environment;
	struct mCore* core;
	struct GBA* gba;
	struct GBALinkTransport transport;
	struct GBALinkV2Session session;
	struct GBALinkV2SessionConfig sessionConfig;
	struct GBAReplicatedPair pair;
	struct GBAReplicatedRuntime runtime;
	void* saveData;
	size_t saveCapacity;
	struct GBALinkCopiedQueue preAdmission;
	retro_netpacket_send_t send;
	retro_netpacket_poll_receive_t pollReceive;
	uint64_t callbackGeneration;
	uint16_t localId;
	uint16_t remoteId;
	bool registered;
	bool frontendStarted;
	bool sessionPrepared;
	bool protocolPending;
	bool paused;
	bool pairInitialized;
	bool runtimeInitialized;
	bool reportedReady;
	uint64_t lastReportedFrame;
	uint64_t verificationFrame;
	uint64_t remoteVerificationFrame;
	uint8_t verificationDigests[2][MGBA_SHA256_DIGEST_SIZE];
	uint8_t remoteVerificationDigests[2][MGBA_SHA256_DIGEST_SIZE];
	bool verificationPending;
	bool remoteVerificationReceived;
	struct GBASerializedState* verifiedLocalState;
	uint64_t lastVerifiedFrame;
	uint64_t localSaveGeneration;
	uint64_t verifiedSaveGeneration;
	enum mRTCGenericType verifiedRtcType;
	int64_t verifiedRtcValue;
	time_t verifiedCartridgeRtcLastLatch;
	time_t verifiedCartridgeRtcOffset;
	struct mLibretroNetpacketV2Metrics metrics;
#ifdef M_LIBRETRO_NETPACKET_V2_TEST
	bool testClockEnabled;
	uint64_t testNowMs;
#endif
};

static struct mLibretroNetpacketV2Adapter _adapter = {
	.localId = NETPACKET_V2_NO_CLIENT,
	.remoteId = NETPACKET_V2_NO_CLIENT,
};

static uint64_t _monotonicTimeMs(void* context);
static void _digestText(
	const uint8_t digest[MGBA_SHA256_DIGEST_SIZE],
	char text[MGBA_SHA256_DIGEST_SIZE * 2 + 1]);

static void _recordRendezvous(
		struct mLibretroNetpacketV2Adapter* adapter,
		uint64_t duration) {
	++adapter->metrics.rendezvousCount;
	adapter->metrics.rendezvousTotalMs += duration;
	if (duration > adapter->metrics.rendezvousMaxMs) {
		adapter->metrics.rendezvousMaxMs = duration;
	}
	size_t bucket = duration > NETPACKET_V2_RENDEZVOUS_HISTOGRAM_MAX_MS
	    ? NETPACKET_V2_RENDEZVOUS_HISTOGRAM_MAX_MS : (size_t) duration;
	++adapter->metrics.rendezvousHistogram[bucket];
}

static uint64_t _rendezvousPercentile(
		const struct mLibretroNetpacketV2Adapter* adapter,
		unsigned percentile) {
	if (!adapter->metrics.rendezvousCount) {
		return 0;
	}
	uint64_t target =
	    (adapter->metrics.rendezvousCount * percentile + 99) / 100;
	uint64_t seen = 0;
	for (size_t i = 0;
	     i <= NETPACKET_V2_RENDEZVOUS_HISTOGRAM_MAX_MS; ++i) {
		seen += adapter->metrics.rendezvousHistogram[i];
		if (seen >= target) {
			return i;
		}
	}
	return NETPACKET_V2_RENDEZVOUS_HISTOGRAM_MAX_MS;
}

static uint8_t _playerForRole(enum GBALinkRole role) {
	return role == GBA_LINK_ROLE_HOST ? 0 : 1;
}

static void _log(enum retro_log_level level, const char* message) {
	enum mLogLevel mapped = mLOG_INFO;
	if (level == RETRO_LOG_ERROR) {
		mapped = mLOG_ERROR;
	} else if (level == RETRO_LOG_WARN) {
		mapped = mLOG_WARN;
	}
	mLog(_mLOG_CAT_STATUS, mapped, "GBA replicated link: %s", message);
}

static void _frontendMessage(
	enum retro_log_level level, const char* message) {
	if (!_adapter.environment || !message) {
		return;
	}
	struct retro_message_ext extended = {
		.msg = message,
		.duration = level == RETRO_LOG_INFO ? 2500 : 5000,
		.priority = level == RETRO_LOG_ERROR ? 3 : 2,
		.level = level,
		.target = RETRO_MESSAGE_TARGET_ALL,
		.type = RETRO_MESSAGE_TYPE_NOTIFICATION,
		.progress = -1,
	};
	if (_adapter.environment(
	        RETRO_ENVIRONMENT_SET_MESSAGE_EXT, &extended)) {
		return;
	}
	struct retro_message legacy = {
		.msg = message,
		.frames = level == RETRO_LOG_INFO ? 150 : 300,
	};
	_adapter.environment(RETRO_ENVIRONMENT_SET_MESSAGE, &legacy);
}

static uint64_t _monotonicTimeMs(void* context) {
	UNUSED(context);
#ifdef M_LIBRETRO_NETPACKET_V2_TEST
	if (_adapter.testClockEnabled) {
		return _adapter.testNowMs;
	}
#endif
#ifdef _WIN32
	return GetTickCount64();
#else
	struct timespec now;
	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
		return 0;
	}
	return (uint64_t) now.tv_sec * 1000 +
	       (uint64_t) now.tv_nsec / 1000000;
#endif
}

static bool _sendReliable(
	void* context, const void* data, size_t size, bool flush) {
	struct mLibretroNetpacketV2Adapter* adapter = context;
	if (!adapter || !adapter->frontendStarted || !adapter->send ||
	    adapter->remoteId == NETPACKET_V2_NO_CLIENT ||
	    !data || !size) {
		return false;
	}
	uint64_t generation = adapter->callbackGeneration;
	retro_netpacket_send_t send = adapter->send;
	int flags = RETRO_NETPACKET_RELIABLE;
	if (flush) {
		flags |= RETRO_NETPACKET_FLUSH_HINT;
	}
	send(flags, data, size, adapter->remoteId);
	bool valid = adapter->frontendStarted &&
	       adapter->callbackGeneration == generation &&
	       adapter->send == send;
	if (valid) {
		++adapter->metrics.sentPackets;
		adapter->metrics.sentBytes += size;
	}
	return valid;
}

static bool _pollReceive(void* context) {
	struct mLibretroNetpacketV2Adapter* adapter = context;
	if (!adapter || !adapter->frontendStarted || !adapter->pollReceive) {
		return false;
	}
	uint64_t generation = adapter->callbackGeneration;
	retro_netpacket_poll_receive_t poll = adapter->pollReceive;
	poll();
	++adapter->metrics.receivePolls;
	return adapter->frontendStarted &&
	       adapter->callbackGeneration == generation &&
	       adapter->pollReceive == poll;
}

static void _yield(void* context) {
	UNUSED(context);
#ifdef _WIN32
	Sleep(0);
#else
	sched_yield();
#endif
}

static const char* _reasonName(enum GBALinkReason reason) {
	switch (reason) {
	case GBA_LINK_REASON_PROTOCOL_MISMATCH: return "protocol mismatch";
	case GBA_LINK_REASON_CAPABILITY_MISMATCH: return "capability mismatch";
	case GBA_LINK_REASON_ROM_MISMATCH: return "ROM mismatch";
	case GBA_LINK_REASON_ATTACHMENT_TIMEOUT: return "attachment timed out";
	case GBA_LINK_REASON_QUEUE_EXHAUSTED: return "packet queue exhausted";
	case GBA_LINK_REASON_OVERSIZED_PACKET: return "packet too large";
	case GBA_LINK_REASON_MALFORMED_PACKET: return "malformed packet";
	case GBA_LINK_REASON_PEER_DETACH: return "peer detached";
	case GBA_LINK_REASON_RESET: return "core reset";
	case GBA_LINK_REASON_UNLOAD: return "content unloaded";
	case GBA_LINK_REASON_USER_DISCONNECT: return "user disconnected";
	default: return "transport stopped";
	}
}

static void _diagnostic(
	void* context, enum GBALinkDiagnosticLevel level,
	enum GBALinkReason reason, const char* detail) {
	UNUSED(context);
	char message[384];
	snprintf(message, sizeof(message), "Link failed: %s%s%s",
	    _reasonName(reason), detail && detail[0] ? " (" : "",
	    detail && detail[0] ? detail : "");
	if (detail && detail[0]) {
		size_t length = strlen(message);
		if (length + 1 < sizeof(message)) {
			message[length] = ')';
			message[length + 1] = '\0';
		}
	}
	enum retro_log_level mapped =
	    level == GBA_LINK_DIAGNOSTIC_INFO
	        ? RETRO_LOG_INFO
	        : level == GBA_LINK_DIAGNOSTIC_WARN
	              ? RETRO_LOG_WARN
	              : RETRO_LOG_ERROR;
	_log(mapped, message);
	_frontendMessage(mapped, message);
}

static void _clearPreAdmission(
	struct mLibretroNetpacketV2Adapter* adapter) {
	GBALinkCopiedQueueDeinit(&adapter->preAdmission);
}

static void _invalidateFrontend(
	struct mLibretroNetpacketV2Adapter* adapter) {
	adapter->send = NULL;
	adapter->pollReceive = NULL;
	adapter->frontendStarted = false;
	adapter->localId = NETPACKET_V2_NO_CLIENT;
	adapter->remoteId = NETPACKET_V2_NO_CLIENT;
	adapter->protocolPending = false;
	_clearPreAdmission(adapter);
	++adapter->callbackGeneration;
	if (!adapter->callbackGeneration) {
		++adapter->callbackGeneration;
	}
}

static void _transportStop(void* context) {
	struct mLibretroNetpacketV2Adapter* adapter = context;
	if (adapter) {
		_invalidateFrontend(adapter);
	}
}

static const struct GBALinkTransportVTable _transportVTable = {
	.sendReliable = _sendReliable,
	.pollReceive = _pollReceive,
	.yield = _yield,
	.monotonicTimeMs = _monotonicTimeMs,
	.diagnostic = _diagnostic,
	.stop = _transportStop,
};

static bool _quiescentBoundary(void* context) {
	struct mLibretroNetpacketV2Adapter* adapter = context;
	if (!adapter || !adapter->gba || adapter->gba->sio.driver) {
		return false;
	}
	return !GBASIOMultiplayerIsBusy(adapter->gba->sio.siocnt) &&
	       !mTimingIsScheduled(
	           &adapter->gba->timing,
	           &adapter->gba->sio.completeEvent);
}

static void _setPaused(void* context, bool paused) {
	((struct mLibretroNetpacketV2Adapter*) context)->paused = paused;
}

static enum GBAReplicaResult _captureReplica(
	void* context, uint8_t player, uint64_t generation,
	enum GBAReplicaEncoding encoding, uint32_t chunkSize,
	struct GBAReplicaBundle* bundle) {
	struct mLibretroNetpacketV2Adapter* adapter = context;
	return GBAReplicaCapture(
	    adapter->core, player, generation, encoding, chunkSize, bundle);
}

static bool _installPair(
	void* context, const struct GBAReplicaManifest manifests[2],
	const struct GBAReplicaPayload payloads[2]) {
	struct mLibretroNetpacketV2Adapter* adapter = context;
	if (adapter->pairInitialized) {
		return false;
	}
	free(adapter->verifiedLocalState);
	adapter->verifiedLocalState = NULL;
	adapter->lastVerifiedFrame = 0;
	adapter->verifiedSaveGeneration = 0;
	GBAReplicatedPairInit(&adapter->pair);
	adapter->pairInitialized = true;
	if (GBAReplicatedPairInstall(
	        &adapter->pair, adapter->core, manifests, payloads,
	        adapter->session.snapshotGeneration) ==
	    GBA_REPLICATED_PAIR_OK) {
		uint8_t localPlayer = _playerForRole(
		    adapter->session.localRole);
		if (GBAReplicatedPairAssignFrontend(
		        &adapter->pair, localPlayer, adapter->core) &&
		    GBAReplicatedPairAssignSaveBacking(
		        &adapter->pair, localPlayer,
		        adapter->saveData, adapter->saveCapacity)) {
			return true;
		}
	}
	GBAReplicatedPairStop(&adapter->pair);
	memset(&adapter->pair, 0, sizeof(adapter->pair));
	adapter->pairInitialized = false;
	return false;
}

static bool _commitPair(void* context) {
	struct mLibretroNetpacketV2Adapter* adapter = context;
	if (!adapter->pairInitialized || !adapter->pair.installed ||
	    adapter->runtimeInitialized) {
		return false;
	}
	if (!GBAReplicatedRuntimeInit(
	        &adapter->runtime, &adapter->pair,
	        adapter->session.snapshotGeneration,
	        adapter->session.localRole, adapter->session.inputDelay,
	        adapter->session.firstFrame)) {
		return false;
	}
	adapter->runtimeInitialized = true;
	return true;
}

static void _restoreDisconnectedLines(struct GBA* gba) {
	if (!gba || gba->sio.driver || gba->sio.mode != GBA_SIO_MULTI) {
		return;
	}
	mTimingDeschedule(&gba->timing, &gba->sio.completeEvent);
	gba->sio.transferMode = -1;
	gba->sio.siocnt = GBASIOMultiplayerClearBusy(gba->sio.siocnt);
	gba->sio.siocnt = GBASIOMultiplayerFillReady(gba->sio.siocnt);
	gba->sio.siocnt = GBASIOMultiplayerFillSlave(gba->sio.siocnt);
	gba->sio.siocnt = GBASIOMultiplayerSetId(gba->sio.siocnt, 0);
	gba->sio.rcnt = GBASIORegisterRCNTFillSc(gba->sio.rcnt);
}

static void _restoreVerifiedLocalState(
		struct mLibretroNetpacketV2Adapter* adapter) {
	if (!adapter || !adapter->verifiedLocalState || !adapter->gba ||
	    adapter->gba->sio.driver) {
		return;
	}
	if (!GBADeserialize(adapter->gba, adapter->verifiedLocalState)) {
		_log(RETRO_LOG_WARN,
		    "verified local state restore failed; preserving frozen core");
		return;
	}
	adapter->core->rtc.override = adapter->verifiedRtcType;
	adapter->core->rtc.value = adapter->verifiedRtcValue;
	adapter->gba->memory.hw.rtc.lastLatch =
	    adapter->verifiedCartridgeRtcLastLatch;
	adapter->gba->memory.hw.rtc.offset =
	    adapter->verifiedCartridgeRtcOffset;
	_restoreDisconnectedLines(adapter->gba);
	char message[160];
	snprintf(message, sizeof(message),
	    "restored verified local state frame=%" PRIu64
	    " save_generation=%" PRIu64,
	    adapter->lastVerifiedFrame,
	    adapter->verifiedSaveGeneration);
	_log(RETRO_LOG_INFO, message);
}

static void _logLinkTestStatus(
		struct mLibretroNetpacketV2Adapter* adapter,
		const char* event) {
	uint32_t status[2];
	uint32_t transfers[2];
	uint32_t errors[2];
	uint32_t timeouts[2];
	uint32_t lines[2];
	for (unsigned player = 0; player < 2; ++player) {
		struct mCore* core = GBAReplicatedPairCore(
		    &adapter->pair, player);
		if (!core || core->rawRead32(core, 0x02000000, -1) !=
		        NETPACKET_V2_LINK_TEST_MAGIC) {
			return;
		}
		status[player] = core->rawRead32(core, 0x02000008, -1);
		transfers[player] = core->rawRead32(core, 0x02000018, -1);
		errors[player] = core->rawRead32(core, 0x02000020, -1);
		timeouts[player] = core->rawRead32(core, 0x02000030, -1);
		lines[player] = core->rawRead32(core, 0x02000034, -1);
	}
	char message[256];
	snprintf(message, sizeof(message),
	    "%s fixture P%u status=%08" PRIx32 "/%08" PRIx32
	    " transfers=%" PRIu32 "/%" PRIu32
	    " errors=%" PRIu32 "/%" PRIu32
	    " timeouts=%" PRIu32 "/%" PRIu32
	    " lines=%08" PRIx32 "/%08" PRIx32,
	    event ? event : "runtime", adapter->session.localRole,
	    status[0], status[1], transfers[0], transfers[1],
	    errors[0], errors[1], timeouts[0], timeouts[1],
	    lines[0], lines[1]);
	_log(RETRO_LOG_INFO, message);
}

static void _logRuntimeSummary(
	struct mLibretroNetpacketV2Adapter* adapter,
	const char* event) {
	if (!adapter || !adapter->runtimeInitialized) {
		return;
	}
	struct GBAReplicatedRuntimeMetrics runtime;
	struct GBAReplicatedPairMetrics pair;
	if (!GBAReplicatedRuntimeGetMetrics(
	        &adapter->runtime, &runtime) ||
	    !GBAReplicatedPairGetMetrics(&adapter->pair, &pair)) {
		return;
	}
	char message[192];
	snprintf(message, sizeof(message),
	    "%s P%u f=%" PRIu64 " pkt=%" PRIu64 "/%" PRIu64
	    " B=%" PRIu64 "/%" PRIu64 " chk=%" PRIu64
	    " sio=%" PRIu64 "/%" PRIu64,
	    event ? event : "runtime", adapter->session.localRole,
	    runtime.framesReleased, adapter->metrics.sentPackets,
	    adapter->metrics.receivedPackets, adapter->metrics.sentBytes,
	    adapter->metrics.receivedBytes,
	    adapter->metrics.verificationMatches,
	    pair.transferCompletions, pair.transferredWords);
	_log(RETRO_LOG_INFO, message);
	unsigned inputDepth[2] = { 0, 0 };
	for (unsigned player = 0; player < 2; ++player) {
		for (unsigned i = 0; i < GBA_LINK_INPUT_RING_CAPACITY; ++i) {
			if (adapter->runtime.input.rings[player][i].present &&
			    adapter->runtime.input.rings[player][i].frame >=
			        adapter->runtime.input.nextFrame) {
				++inputDepth[player];
			}
		}
	}
	snprintf(message, sizeof(message),
	    "%s P%u rv=%" PRIu64 "/%" PRIu64 "ms max=%" PRIu64
	    " q=%zu in=%u/%u lead=%" PRIu64 "/%" PRIu64
	    " audio=%" PRIu64 "/%" PRIu64
	    "/%" PRIu64 " wait=%" PRIu64 " run=%" PRIu64 "/%" PRIu64,
	    event ? event : "runtime", adapter->session.localRole,
	    adapter->metrics.rendezvousCount,
	    adapter->metrics.rendezvousTotalMs,
	    adapter->metrics.rendezvousMaxMs,
	    adapter->metrics.queueHighWater, inputDepth[0], inputDepth[1],
	    pair.recoveredFrameLeads[0], pair.recoveredFrameLeads[1],
	    adapter->metrics.audioSamples,
	    adapter->metrics.audioFrames,
	    adapter->metrics.emptyAudioFrames,
	    pair.waitEvents,
	    pair.runLoops[0], pair.runLoops[1]);
	_log(RETRO_LOG_INFO, message);
	struct GBA* gbas[2] = {
		adapter->pair.players[0].core
		    ? adapter->pair.players[0].core->board : NULL,
		adapter->pair.players[1].core
		    ? adapter->pair.players[1].core->board : NULL,
	};
	snprintf(message, sizeof(message),
	    "%s lines P%u mode=%d/%d cnt=%04x/%04x rcnt=%04x/%04x"
	    " id=%d/%d attached=%d coord=%d active=%u waiting=%08x",
	    event ? event : "runtime", adapter->session.localRole,
	    gbas[0] ? (int) gbas[0]->sio.mode : -1,
	    gbas[1] ? (int) gbas[1]->sio.mode : -1,
	    gbas[0] ? gbas[0]->sio.siocnt : 0,
	    gbas[1] ? gbas[1]->sio.siocnt : 0,
	    gbas[0] ? gbas[0]->sio.rcnt : 0,
	    gbas[1] ? gbas[1]->sio.rcnt : 0,
	    adapter->pair.players[0].driver.d.deviceId(
	        &adapter->pair.players[0].driver.d),
	    adapter->pair.players[1].driver.d.deviceId(
	        &adapter->pair.players[1].driver.d),
	    adapter->pair.coordinator.nAttached,
	    adapter->pair.coordinator.transferMode,
	    adapter->pair.coordinator.transferActive,
	    adapter->pair.coordinator.waiting);
	_log(RETRO_LOG_INFO, message);
	char traces[2][MGBA_SHA256_DIGEST_SIZE * 2 + 1];
	_digestText(pair.stateTrace[0], traces[0]);
	_digestText(pair.stateTrace[1], traces[1]);
	for (unsigned player = 0; player < 2; ++player) {
		snprintf(message, sizeof(message),
		    "%s P%u trace%u=%s",
		    event ? event : "runtime",
		    adapter->session.localRole, player, traces[player]);
		_log(RETRO_LOG_INFO, message);
	}
	uint64_t now = _monotonicTimeMs(adapter);
	uint64_t elapsed = adapter->metrics.readyAtMs &&
	                           now >= adapter->metrics.readyAtMs
	    ? now - adapter->metrics.readyAtMs : 0;
	uint64_t fpsMilli = elapsed
	    ? runtime.framesReleased * UINT64_C(1000000) / elapsed : 0;
	snprintf(message, sizeof(message),
	    "%s timing P%u elapsed=%" PRIu64 "ms fps-milli=%" PRIu64
	    " rv-p50=%" PRIu64 "ms rv-p95=%" PRIu64
	    "ms rv-max=%" PRIu64 "ms",
	    event ? event : "runtime", adapter->session.localRole,
	    elapsed, fpsMilli, _rendezvousPercentile(adapter, 50),
	    _rendezvousPercentile(adapter, 95),
	    adapter->metrics.rendezvousMaxMs);
	_log(RETRO_LOG_INFO, message);
	_logLinkTestStatus(adapter, event);
}

static void _discardPair(void* context, bool committed) {
	UNUSED(committed);
	struct mLibretroNetpacketV2Adapter* adapter = context;
	if (adapter->runtimeInitialized) {
		_logRuntimeSummary(adapter, "teardown");
		_restoreVerifiedLocalState(adapter);
		GBAReplicatedRuntimeDeinit(&adapter->runtime);
		adapter->runtimeInitialized = false;
	}
	if (adapter->pairInitialized) {
		GBAReplicatedPairStop(&adapter->pair);
		memset(&adapter->pair, 0, sizeof(adapter->pair));
		adapter->pairInitialized = false;
	}
	adapter->verificationPending = false;
	adapter->remoteVerificationReceived = false;
	free(adapter->verifiedLocalState);
	adapter->verifiedLocalState = NULL;
}

static void _digestText(
		const uint8_t digest[MGBA_SHA256_DIGEST_SIZE],
		char text[MGBA_SHA256_DIGEST_SIZE * 2 + 1]) {
	static const char hex[] = "0123456789abcdef";
	for (unsigned i = 0; i < MGBA_SHA256_DIGEST_SIZE; ++i) {
		text[i * 2] = hex[digest[i] >> 4];
		text[i * 2 + 1] = hex[digest[i] & 0xF];
	}
	text[MGBA_SHA256_DIGEST_SIZE * 2] = '\0';
}

static void _logDivergence(
		struct mLibretroNetpacketV2Adapter* adapter,
		uint8_t player) {
	char local[MGBA_SHA256_DIGEST_SIZE * 2 + 1];
	char remote[MGBA_SHA256_DIGEST_SIZE * 2 + 1];
	_digestText(adapter->verificationDigests[player], local);
	_digestText(adapter->remoteVerificationDigests[player], remote);
	uint64_t next = adapter->runtime.input.nextFrame;
	char inputs[160];
	size_t offset = 0;
	for (unsigned i = 0; i < 4 && offset < sizeof(inputs); ++i) {
		uint64_t frame = next + i;
		struct GBALinkInputSlot* p0 =
		    &adapter->runtime.input.rings[0][
		        frame % GBA_LINK_INPUT_RING_CAPACITY];
		struct GBALinkInputSlot* p1 =
		    &adapter->runtime.input.rings[1][
		        frame % GBA_LINK_INPUT_RING_CAPACITY];
		offset += snprintf(inputs + offset, sizeof(inputs) - offset,
		    "%s%" PRIu64 ":%s%03x/%s%03x",
		    i ? "," : "", frame,
		    p0->present && p0->frame == frame ? "" : "-",
		    p0->present && p0->frame == frame ? p0->keys : 0,
		    p1->present && p1->frame == frame ? "" : "-",
		    p1->present && p1->frame == frame ? p1->keys : 0);
	}
	char message[640];
	snprintf(message, sizeof(message),
	    "divergence frame=%" PRIu64 " player=P%u local=%s remote=%s "
	    "inputs=[%s] session=%" PRIu64 " protocol=%u delay=%u verify=%u",
	    adapter->verificationFrame, player, local, remote, inputs,
	    adapter->session.sessionId, GBA_LINK_V2_PROTOCOL_VERSION,
	    adapter->session.inputDelay, NETPACKET_V2_VERIFICATION_INTERVAL);
	_log(RETRO_LOG_ERROR, message);
}

static bool _completeVerification(
		struct mLibretroNetpacketV2Adapter* adapter) {
	if (!adapter->verificationPending ||
	    !adapter->remoteVerificationReceived) {
		return true;
	}
	if (adapter->verificationFrame !=
	    adapter->remoteVerificationFrame) {
		GBALinkV2SessionFail(
		    &adapter->session,
		    GBA_LINK_V2_REASON_INVALID_TRANSITION,
		    "state-check frame mismatch");
		return false;
	}
	for (unsigned player = 0; player < 2; ++player) {
		if (memcmp(adapter->verificationDigests[player],
		        adapter->remoteVerificationDigests[player],
		        MGBA_SHA256_DIGEST_SIZE) != 0) {
			_logDivergence(adapter, player);
			GBALinkV2SessionFail(
			    &adapter->session,
			    GBA_LINK_V2_REASON_DIVERGENCE,
			    "canonical state digest mismatch");
			return false;
		}
	}
	uint8_t localPlayer = _playerForRole(
	    adapter->session.localRole);
	struct GBA* localGBA = GBAReplicatedPairCore(
	    &adapter->pair, localPlayer)->board;
	if (!GBASIOMultiplayerIsBusy(localGBA->sio.siocnt) &&
	    !mTimingIsScheduled(
	        &localGBA->timing, &localGBA->sio.completeEvent)) {
		if (!adapter->verifiedLocalState) {
			adapter->verifiedLocalState = calloc(
			    1, sizeof(*adapter->verifiedLocalState));
		}
		if (adapter->verifiedLocalState) {
			memset(adapter->verifiedLocalState, 0,
			    sizeof(*adapter->verifiedLocalState));
			GBASerialize(
			    localGBA, adapter->verifiedLocalState);
			adapter->verifiedRtcType =
			    GBAReplicatedPairCore(
			        &adapter->pair, localPlayer)->rtc.override;
			adapter->verifiedRtcValue =
			    GBAReplicatedPairCore(
			        &adapter->pair, localPlayer)->rtc.value;
			adapter->verifiedCartridgeRtcLastLatch =
			    localGBA->memory.hw.rtc.lastLatch;
			adapter->verifiedCartridgeRtcOffset =
			    localGBA->memory.hw.rtc.offset;
			adapter->lastVerifiedFrame =
			    adapter->verificationFrame;
			struct GBAReplicatedPairMetrics metrics;
			if (GBAReplicatedPairGetMetrics(
			        &adapter->pair, &metrics)) {
				adapter->localSaveGeneration =
				    metrics.saveGenerations[localPlayer];
				adapter->verifiedSaveGeneration =
				    adapter->localSaveGeneration;
			}
		}
	}
	++adapter->metrics.verificationMatches;
	adapter->verificationPending = false;
	adapter->remoteVerificationReceived = false;
	GBALinkV2SessionRuntimeDeadlineSatisfied(
	    &adapter->session, GBA_LINK_V2_DEADLINE_VERIFY);
	return true;
}

static bool _runtimePacket(
	void* context, const struct GBALinkV2Packet* packet) {
	struct mLibretroNetpacketV2Adapter* adapter = context;
	if (!adapter->runtimeInitialized) {
		return false;
	}
	if (packet->header.type == GBA_LINK_V2_MESSAGE_STATE_CHECK) {
		const struct GBALinkV2StateCheck* check =
		    &packet->payload.stateCheck;
		uint64_t current = adapter->runtime.metrics.framesReleased;
		if (!check->frame ||
		    check->frame % NETPACKET_V2_VERIFICATION_INTERVAL ||
		    check->frame < current ||
		    (check->frame > current &&
		     check->frame - current >
		         (uint64_t) adapter->session.inputDelay + 1) ||
		    (adapter->remoteVerificationReceived &&
		     (adapter->remoteVerificationFrame != check->frame ||
		      memcmp(adapter->remoteVerificationDigests,
		          check->playerDigests,
		          sizeof(check->playerDigests)) != 0))) {
			GBALinkV2SessionFail(
			    &adapter->session,
			    GBA_LINK_V2_REASON_INVALID_TRANSITION,
			    "invalid or conflicting state check");
			return false;
		}
		adapter->remoteVerificationFrame = check->frame;
		memcpy(adapter->remoteVerificationDigests,
		    check->playerDigests,
		    sizeof(adapter->remoteVerificationDigests));
		adapter->remoteVerificationReceived = true;
		return _completeVerification(adapter);
	}
	if (packet->header.type != GBA_LINK_V2_MESSAGE_INPUT_BATCH) {
		return false;
	}
	enum GBALinkRole remoteRole =
	    adapter->session.localRole == GBA_LINK_ROLE_HOST
	        ? GBA_LINK_ROLE_CLIENT
	        : GBA_LINK_ROLE_HOST;
	enum GBAReplicatedRuntimeResult result =
	    GBAReplicatedRuntimeHandleInput(
	        &adapter->runtime, remoteRole,
	        &packet->payload.inputBatch);
	if (result != GBA_REPLICATED_RUNTIME_OK) {
		GBALinkV2SessionFail(
		    &adapter->session, GBA_LINK_V2_REASON_INPUT_CONFLICT,
		    GBAReplicatedRuntimeResultName(result));
		return false;
	}
	return true;
}

static bool _beginVerification(
		struct mLibretroNetpacketV2Adapter* adapter,
		uint64_t frame) {
	if (!adapter || !adapter->runtimeInitialized ||
	    !frame || frame % NETPACKET_V2_VERIFICATION_INTERVAL ||
	    adapter->verificationPending) {
		return false;
	}
	for (unsigned player = 0; player < 2; ++player) {
		if (!GBAReplicatedPairStateDigest(
		        &adapter->pair, player,
		        adapter->verificationDigests[player])) {
			GBALinkV2SessionFail(
			    &adapter->session,
			    GBA_LINK_V2_REASON_INVALID_TRANSITION,
			    "canonical state digest failed");
			return false;
		}
	}
	adapter->verificationFrame = frame;
	adapter->verificationPending = true;
	struct GBALinkV2Packet packet;
	memset(&packet, 0, sizeof(packet));
	packet.header.type = GBA_LINK_V2_MESSAGE_STATE_CHECK;
	packet.payload.stateCheck.snapshotGeneration =
	    adapter->session.snapshotGeneration;
	packet.payload.stateCheck.frame = frame;
	packet.payload.stateCheck.player = _playerForRole(
	    adapter->session.localRole);
	memcpy(packet.payload.stateCheck.playerDigests,
	    adapter->verificationDigests,
	    sizeof(packet.payload.stateCheck.playerDigests));
	if (!GBALinkV2SessionSendRuntime(
	        &adapter->session, &packet,
	        GBA_LINK_V2_DEADLINE_VERIFY)) {
		return false;
	}
	return _completeVerification(adapter);
}

static void _failed(void* context, enum GBALinkV2Reason reason) {
	struct mLibretroNetpacketV2Adapter* adapter = context;
	char message[224];
	snprintf(message, sizeof(message),
	    "protocol-v2 session failed: reason=%u state=%s frame=%" PRIu64,
	    reason, GBALinkV2SessionStateName(adapter->session.state),
	    adapter->runtimeInitialized ? adapter->runtime.input.nextFrame : 0);
	_log(RETRO_LOG_ERROR, message);
}

static const struct GBALinkV2SessionCallbacks _sessionCallbacks = {
	.quiescentBoundary = _quiescentBoundary,
	.setPaused = _setPaused,
	.captureReplica = _captureReplica,
	.installPair = _installPair,
	.commitPair = _commitPair,
	.discardPair = _discardPair,
	.runtimePacket = _runtimePacket,
	.failed = _failed,
};

static bool _cheatsEnabled(struct mCore* core) {
	if (!core || !core->cheatDevice) {
		return false;
	}
	struct mCheatDevice* device = core->cheatDevice(core);
	if (!device) {
		return false;
	}
	for (size_t i = 0; i < mCheatSetsSize(&device->cheats); ++i) {
		const struct mCheatSet* set =
		    *mCheatSetsGetConstPointer(&device->cheats, i);
		if (set && set->enabled) {
			return true;
		}
	}
	return false;
}

static bool _buildConfig(
	struct mLibretroNetpacketV2Adapter* adapter) {
	memset(&adapter->sessionConfig, 0, sizeof(adapter->sessionConfig));
	if (_cheatsEnabled(adapter->core) ||
	    !GBALinkContentIdentityFromCore(
	        adapter->core, &adapter->sessionConfig.identity)) {
		return false;
	}
	adapter->sessionConfig.capabilities =
	    GBA_LINK_V2_REQUIRED_CAPABILITIES;
	adapter->sessionConfig.supportedEncodings =
	    GBA_LINK_V2_ENCODING_NONE;
	adapter->sessionConfig.emulationCompatibilityVersion =
	    GBA_REPLICA_EMULATION_COMPATIBILITY_VERSION;
	adapter->sessionConfig.maxChunkSize =
	    GBA_REPLICA_DEFAULT_CHUNK_SIZE;
	adapter->sessionConfig.minimumInputDelay = 2;
	adapter->sessionConfig.maximumInputDelay = 8;
	adapter->sessionConfig.estimatedJitterMs = 5;
	adapter->sessionConfig.experimentalRuntime = false;
	GBALinkV2DeadlinePolicyInit(&adapter->sessionConfig.deadlines);
	adapter->sessionConfig.callbacks = &_sessionCallbacks;
	adapter->sessionConfig.callbackContext = adapter;
	return true;
}

static void _deinitSession(
	struct mLibretroNetpacketV2Adapter* adapter) {
	if (!adapter || !adapter->sessionPrepared) {
		return;
	}
	GBALinkV2SessionDeinit(&adapter->session);
	GBALinkTransportDeinit(&adapter->transport);
	memset(&adapter->session, 0, sizeof(adapter->session));
	memset(&adapter->transport, 0, sizeof(adapter->transport));
	adapter->sessionPrepared = false;
	adapter->paused = false;
	adapter->reportedReady = false;
}

static bool _prepareSession(
	struct mLibretroNetpacketV2Adapter* adapter) {
	_deinitSession(adapter);
	GBALinkTransportInit(
	    &adapter->transport, &_transportVTable, adapter);
	GBALinkV2SessionInit(&adapter->session, &adapter->transport);
	adapter->sessionPrepared = true;
	if (!_buildConfig(adapter) ||
	    !GBALinkV2SessionConfigure(
	        &adapter->session, &adapter->sessionConfig)) {
		_deinitSession(adapter);
		return false;
	}
	return true;
}

static bool _drainPreAdmission(
	struct mLibretroNetpacketV2Adapter* adapter) {
	struct GBALinkCopiedPacket packet;
	memset(&packet, 0, sizeof(packet));
	while (GBALinkCopiedQueuePop(&adapter->preAdmission, &packet)) {
		bool queued = GBALinkTransportQueueInbound(
		    &adapter->transport, adapter->callbackGeneration,
		    packet.data, packet.size);
		GBALinkCopiedPacketDeinit(&packet);
		if (!queued) {
			_clearPreAdmission(adapter);
			return false;
		}
	}
	_clearPreAdmission(adapter);
	return true;
}

static bool _beginProtocol(
	struct mLibretroNetpacketV2Adapter* adapter) {
	enum GBALinkRole role = adapter->localId == 0
	    ? GBA_LINK_ROLE_HOST
	    : GBA_LINK_ROLE_CLIENT;
	if (!_prepareSession(adapter) ||
	    !GBALinkTransportStart(
	        &adapter->transport, adapter->callbackGeneration, role) ||
	    !GBALinkV2SessionStart(
	        &adapter->session, adapter->callbackGeneration, role) ||
	    !_drainPreAdmission(adapter)) {
		return false;
	}
	adapter->protocolPending = false;
	return true;
}

static bool _enterRendezvous(
	struct mLibretroNetpacketV2Adapter* adapter) {
	if (!adapter || !adapter->frontendStarted) {
		return false;
	}
	if (adapter->localId == 0 &&
	    adapter->remoteId == NETPACKET_V2_NO_CLIENT &&
	    !adapter->protocolPending) {
		return true;
	}
	if (adapter->protocolPending && !adapter->sessionPrepared) {
		if (!_beginProtocol(adapter)) {
			_invalidateFrontend(adapter);
			return false;
		}
	}
	return true;
}

static void RETRO_CALLCONV _start(
	uint16_t clientId, retro_netpacket_send_t send,
	retro_netpacket_poll_receive_t pollReceive) {
	_deinitSession(&_adapter);
	_invalidateFrontend(&_adapter);
	if (!_adapter.registered || !_adapter.core || !_adapter.gba ||
	    !send || !pollReceive || (clientId != 0 && clientId != 1)) {
		_frontendMessage(
		    RETRO_LOG_ERROR,
		    "GBA replicated link requires two-player Netpacket receive polling");
		return;
	}
	_adapter.frontendStarted = true;
	_adapter.localId = clientId;
	_adapter.remoteId =
	    clientId == 0 ? NETPACKET_V2_NO_CLIENT : 0;
	_adapter.send = send;
	_adapter.pollReceive = pollReceive;
	memset(&_adapter.metrics, 0, sizeof(_adapter.metrics));
	_adapter.metrics.startedAtMs = _monotonicTimeMs(&_adapter);
	_adapter.lastReportedFrame = 0;
	++_adapter.callbackGeneration;
	if (!_adapter.callbackGeneration) {
		++_adapter.callbackGeneration;
	}
	if (clientId == 1) {
		_adapter.protocolPending = true;
		_enterRendezvous(&_adapter);
	}
}

static void RETRO_CALLCONV _receive(
	const void* data, size_t size, uint16_t clientId) {
	if (!_adapter.frontendStarted) {
		return;
	}
	++_adapter.metrics.receivedPackets;
	_adapter.metrics.receivedBytes += size;
	if (!_adapter.sessionPrepared &&
	    ((_adapter.localId == 0 &&
	      _adapter.remoteId == NETPACKET_V2_NO_CLIENT &&
	      clientId == 1) ||
	     (_adapter.protocolPending && clientId == _adapter.remoteId))) {
		if (!GBALinkCopiedQueuePush(
		        &_adapter.preAdmission,
		        _adapter.callbackGeneration, data, size)) {
			_diagnostic(
			    &_adapter, GBA_LINK_DIAGNOSTIC_ERROR,
			    size > GBA_LINK_TRANSPORT_MAX_PACKET_SIZE
			        ? GBA_LINK_REASON_OVERSIZED_PACKET
			        : GBA_LINK_REASON_QUEUE_EXHAUSTED,
			    "pre-admission packet queue rejected data");
			_invalidateFrontend(&_adapter);
		}
		if (_adapter.preAdmission.size >
		    _adapter.metrics.queueHighWater) {
			_adapter.metrics.queueHighWater =
			    _adapter.preAdmission.size;
		}
		return;
	}
	if (!_adapter.sessionPrepared ||
	    clientId != _adapter.remoteId ||
	    !GBALinkTransportQueueInbound(
	        &_adapter.transport, _adapter.callbackGeneration,
	        data, size)) {
		if (_adapter.sessionPrepared &&
		    GBALinkV2SessionIsLive(&_adapter.session)) {
			GBALinkV2SessionFail(
			    &_adapter.session,
			    GBA_LINK_V2_REASON_MALFORMED_PACKET,
			    "receive callback rejected packet");
		}
		_invalidateFrontend(&_adapter);
	} else if (_adapter.transport.inbound.size >
	           _adapter.metrics.queueHighWater) {
		_adapter.metrics.queueHighWater =
		    _adapter.transport.inbound.size;
	}
}

static void RETRO_CALLCONV _stop(void) {
	bool live = _adapter.sessionPrepared &&
	            GBALinkV2SessionIsLive(&_adapter.session);
	_invalidateFrontend(&_adapter);
	if (_adapter.sessionPrepared) {
		GBALinkTransportInvalidate(
		    &_adapter.transport, GBA_LINK_REASON_TRANSPORT_STOP,
		    "frontend stopped protocol-v2 transport");
		if (live) {
			GBALinkV2SessionFail(
			    &_adapter.session,
			    GBA_LINK_V2_REASON_TRANSPORT_STOP,
			    "frontend stopped protocol-v2 transport");
		}
	}
}

static void RETRO_CALLCONV _poll(void) {
	if (_adapter.sessionPrepared &&
	    GBALinkV2SessionIsLive(&_adapter.session)) {
		GBALinkV2SessionUpdate(&_adapter.session, false);
	}
}

static bool RETRO_CALLCONV _connected(uint16_t clientId) {
	if (!_adapter.frontendStarted || _adapter.localId != 0 ||
	    _adapter.remoteId != NETPACKET_V2_NO_CLIENT ||
	    clientId != 1) {
		return false;
	}
	_adapter.remoteId = clientId;
	_adapter.protocolPending = true;
	if (!_enterRendezvous(&_adapter)) {
		_adapter.remoteId = NETPACKET_V2_NO_CLIENT;
		return false;
	}
	return true;
}

static void RETRO_CALLCONV _disconnected(uint16_t clientId) {
	if (!_adapter.frontendStarted || clientId != _adapter.remoteId) {
		return;
	}
	_adapter.remoteId = NETPACKET_V2_NO_CLIENT;
	if (_adapter.sessionPrepared &&
	    GBALinkV2SessionIsLive(&_adapter.session)) {
		GBALinkV2SessionFail(
		    &_adapter.session, GBA_LINK_V2_REASON_PEER_DETACH,
		    "Netpacket peer disconnected");
	}
}

static const struct retro_netpacket_callback _callbacks = {
	.start = _start,
	.receive = _receive,
	.stop = _stop,
	.poll = _poll,
	.connected = _connected,
	.disconnected = _disconnected,
	.protocol_version = GBA_LINK_V2_PROTOCOL_NAME,
};

bool mLibretroNetpacketV2Register(
	retro_environment_t environment, struct mCore* core,
	void* saveData, size_t saveCapacity) {
	mLibretroNetpacketV2Unload();
	if (!environment || !core || !core->platform ||
	    core->platform(core) != mPLATFORM_GBA || !core->board ||
	    !saveData || saveCapacity < GBA_SIZE_FLASH1M) {
		return false;
	}
	_adapter.environment = environment;
	_adapter.core = core;
	_adapter.gba = core->board;
	_adapter.saveData = saveData;
	_adapter.saveCapacity = saveCapacity;
	_adapter.registered = true;
	if (!environment(
	        RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE,
	        (void*) &_callbacks)) {
		mLibretroNetpacketV2Unload();
		return false;
	}
	_log(RETRO_LOG_INFO,
	    "registered replicated-pair Netpacket protocol v2");
	return true;
}

static void _finishFailed(void) {
	if (_adapter.sessionPrepared &&
	    _adapter.session.state == GBA_LINK_V2_SESSION_FAILED) {
		_deinitSession(&_adapter);
	}
}

void mLibretroNetpacketV2RunBegin(void) {
	_enterRendezvous(&_adapter);
	if (_adapter.sessionPrepared &&
	    GBALinkV2SessionIsLive(&_adapter.session)) {
		GBALinkV2SessionUpdate(&_adapter.session, false);
	}
	if (_adapter.sessionPrepared && !_adapter.reportedReady &&
	    _adapter.session.state == GBA_LINK_V2_SESSION_READY) {
		_adapter.reportedReady = true;
		_adapter.metrics.readyAtMs = _monotonicTimeMs(&_adapter);
		char message[160];
		snprintf(message, sizeof(message),
		    "GBA replicated link ready: player %u, input delay %u frames",
		    _adapter.localId + 1, _adapter.session.inputDelay);
		_log(RETRO_LOG_INFO, message);
		_frontendMessage(RETRO_LOG_INFO, message);
		snprintf(message, sizeof(message),
		    "attach P%u rtt=%ums jitter=%ums delay=%u rendezvous=%" PRIu64 "ms",
		    _adapter.localId, _adapter.session.handshakeRoundTripMs,
		    _adapter.session.config.estimatedJitterMs,
		    _adapter.session.inputDelay,
		    _adapter.metrics.readyAtMs -
		        _adapter.metrics.startedAtMs);
		_log(RETRO_LOG_INFO, message);
	}
	_finishFailed();
}

bool mLibretroNetpacketV2OwnsExecution(void) {
	return _adapter.sessionPrepared &&
	       _adapter.session.state == GBA_LINK_V2_SESSION_READY &&
	       _adapter.runtimeInitialized;
}

bool mLibretroNetpacketV2RunFrame(uint16_t keys) {
	if (!mLibretroNetpacketV2OwnsExecution()) {
		return false;
	}
	bool waited = _adapter.verificationPending;
	uint64_t waitStarted = waited ? _monotonicTimeMs(&_adapter) : 0;
	while (mLibretroNetpacketV2OwnsExecution() &&
	       _adapter.verificationPending) {
		uint64_t generation = _adapter.callbackGeneration;
		retro_netpacket_poll_receive_t poll = _adapter.pollReceive;
		if (!poll ||
		    !GBALinkV2SessionUpdate(&_adapter.session, true)) {
			break;
		}
		if (!_adapter.frontendStarted ||
		    _adapter.callbackGeneration != generation ||
		    _adapter.pollReceive != poll) {
			break;
		}
		_yield(&_adapter);
	}
	if (!mLibretroNetpacketV2OwnsExecution() ||
	    _adapter.verificationPending) {
		_finishFailed();
		return false;
	}
	if (waited) {
		uint64_t duration =
		    _monotonicTimeMs(&_adapter) - waitStarted;
		_recordRendezvous(&_adapter, duration);
	}
	if (!_adapter.runtime.authoredCurrentFrame) {
		struct GBALinkV2Packet packets[
		    GBA_REPLICATED_RUNTIME_MAX_AUTHOR_PACKETS];
		uint8_t count = 0;
		if (GBAReplicatedRuntimeAuthorInput(
		        &_adapter.runtime, keys, packets, &count) !=
		    GBA_REPLICATED_RUNTIME_OK) {
			GBALinkV2SessionFail(
			    &_adapter.session,
			    GBA_LINK_V2_REASON_INPUT_CONFLICT,
			    "local input authoring failed");
			_finishFailed();
			return false;
		}
		for (unsigned i = 0; i < count; ++i) {
			if (!GBALinkV2SessionSendRuntime(
			        &_adapter.session, &packets[i],
			        GBA_LINK_V2_DEADLINE_INPUT)) {
				_finishFailed();
				return false;
			}
		}
	}
	waited = !GBAReplicatedRuntimeFrameReady(&_adapter.runtime);
	waitStarted = waited ? _monotonicTimeMs(&_adapter) : 0;
	while (mLibretroNetpacketV2OwnsExecution() &&
	       !GBAReplicatedRuntimeFrameReady(&_adapter.runtime)) {
		uint64_t generation = _adapter.callbackGeneration;
		retro_netpacket_poll_receive_t poll = _adapter.pollReceive;
		if (!poll ||
		    !GBALinkV2SessionUpdate(&_adapter.session, true)) {
			break;
		}
		if (!_adapter.frontendStarted ||
		    _adapter.callbackGeneration != generation ||
		    _adapter.pollReceive != poll) {
			break;
		}
		_yield(&_adapter);
	}
	if (!mLibretroNetpacketV2OwnsExecution() ||
	    !GBAReplicatedRuntimeFrameReady(&_adapter.runtime)) {
		_finishFailed();
		return false;
	}
	if (waited) {
		uint64_t duration =
		    _monotonicTimeMs(&_adapter) - waitStarted;
		_recordRendezvous(&_adapter, duration);
	}
	GBALinkV2SessionRuntimeDeadlineSatisfied(
	    &_adapter.session, GBA_LINK_V2_DEADLINE_INPUT);
	enum GBAReplicatedRuntimeResult runResult =
	    GBAReplicatedRuntimeRunFrame(&_adapter.runtime);
	if (runResult != GBA_REPLICATED_RUNTIME_OK) {
		struct GBAReplicatedPair* pair = &_adapter.pair;
		struct GBA* p0 = pair->players[0].core
		    ? pair->players[0].core->board : NULL;
		struct GBA* p1 = pair->players[1].core
		    ? pair->players[1].core->board : NULL;
		char detail[512];
		snprintf(detail, sizeof(detail),
		    "runtime=%s pair=%s frame=%" PRIu64
		    " iteration=%" PRIu64 " counters=%" PRIu32
		    "/%" PRIu32 "->%" PRIu32 "/%" PRIu32
		    " targets=%" PRIu32 "/%" PRIu32
		    " recovered=%" PRIu64 "/%" PRIu64
		    " asleep=%u/%u sio_mode=%d/%d siocnt=%04x/%04x"
		    " complete=%u/%u lockstep=%u/%u waiting=%08x"
		    " active=%u transfer_mode=%d cable=%08x",
		    GBAReplicatedRuntimeResultName(runResult),
		    GBAReplicatedPairResultName(_adapter.runtime.lastPairResult),
		    pair->frameNumber, pair->failureIteration,
		    pair->startingFrames[0], pair->startingFrames[1],
		    pair->observedFrames[0], pair->observedFrames[1],
		    pair->nextFrameCounters[0], pair->nextFrameCounters[1],
		    pair->recoveredFrameLeads[0],
		    pair->recoveredFrameLeads[1],
		    pair->players[0].user.asleep,
		    pair->players[1].user.asleep,
		    p0 ? (int) p0->sio.mode : -1,
		    p1 ? (int) p1->sio.mode : -1,
		    p0 ? p0->sio.siocnt : 0, p1 ? p1->sio.siocnt : 0,
		    p0 && mTimingIsScheduled(
		        &p0->timing, &p0->sio.completeEvent),
		    p1 && mTimingIsScheduled(
		        &p1->timing, &p1->sio.completeEvent),
		    p0 && mTimingIsScheduled(
		        &p0->timing, &pair->players[0].driver.event),
		    p1 && mTimingIsScheduled(
		        &p1->timing, &pair->players[1].driver.event),
		    pair->coordinator.waiting,
		    pair->coordinator.transferActive,
		    pair->coordinator.transferMode,
		    (uint32_t) pair->coordinator.cycle);
		_log(RETRO_LOG_ERROR, detail);
		GBALinkV2SessionFail(
		    &_adapter.session,
		    GBA_LINK_V2_REASON_INVALID_TRANSITION,
		    detail);
		_finishFailed();
		return false;
	}
	GBAReplicatedPairDrainShadowAudio(
	    &_adapter.pair,
	    _playerForRole(_adapter.session.localRole));
	uint64_t frame = _adapter.runtime.metrics.framesReleased;
	if (frame % NETPACKET_V2_VERIFICATION_INTERVAL == 0 &&
	    !_beginVerification(&_adapter, frame)) {
		_finishFailed();
		return false;
	}
	if (frame >= _adapter.lastReportedFrame + 600) {
		_adapter.lastReportedFrame = frame;
		_logRuntimeSummary(&_adapter, "periodic");
	}
	return true;
}

bool mLibretroNetpacketV2ExecutionBlocked(void) {
	return _adapter.frontendStarted && _adapter.sessionPrepared &&
	       (_adapter.paused ||
	        (_adapter.session.state == GBA_LINK_V2_SESSION_READY &&
	         !_adapter.runtimeInitialized));
}

struct mCore* mLibretroNetpacketV2PresentedCore(void) {
	if (!mLibretroNetpacketV2OwnsExecution()) {
		return NULL;
	}
	uint8_t player = _playerForRole(
	    _adapter.session.localRole);
	return GBAReplicatedPairCore(&_adapter.pair, player);
}

mColor* mLibretroNetpacketV2PresentedVideo(void) {
	if (!mLibretroNetpacketV2OwnsExecution()) {
		return NULL;
	}
	uint8_t player = _playerForRole(
	    _adapter.session.localRole);
	return GBAReplicatedPairVideoBuffer(&_adapter.pair, player);
}

void mLibretroNetpacketV2ReportAudio(size_t samples) {
	if (!mLibretroNetpacketV2OwnsExecution()) {
		return;
	}
	++_adapter.metrics.audioFrames;
	_adapter.metrics.audioSamples += samples;
	if (!samples) {
		++_adapter.metrics.emptyAudioFrames;
	}
}

void mLibretroNetpacketV2Reset(void) {
	if (_adapter.sessionPrepared &&
	    GBALinkV2SessionIsLive(&_adapter.session)) {
		GBALinkV2SessionFail(
		    &_adapter.session, GBA_LINK_V2_REASON_RESET,
		    "core reset");
	}
	_invalidateFrontend(&_adapter);
	_deinitSession(&_adapter);
}

void mLibretroNetpacketV2Unload(void) {
	if (_adapter.sessionPrepared &&
	    GBALinkV2SessionIsLive(&_adapter.session)) {
		GBALinkV2SessionFail(
		    &_adapter.session, GBA_LINK_V2_REASON_UNLOAD,
		    "content unloaded");
	}
	_invalidateFrontend(&_adapter);
	_deinitSession(&_adapter);
	memset(&_adapter, 0, sizeof(_adapter));
	_adapter.localId = NETPACKET_V2_NO_CLIENT;
	_adapter.remoteId = NETPACKET_V2_NO_CLIENT;
}

bool mLibretroNetpacketV2SessionActive(void) {
	return _adapter.frontendStarted ||
	       (_adapter.sessionPrepared &&
	        _adapter.session.state !=
	            GBA_LINK_V2_SESSION_DISCONNECTED);
}

bool mLibretroNetpacketV2RejectOperation(const char* operation) {
	if (!mLibretroNetpacketV2SessionActive()) {
		return false;
	}
	char message[192];
	snprintf(message, sizeof(message),
	    "%s is unavailable during GBA replicated link",
	    operation ? operation : "This operation");
	_log(RETRO_LOG_WARN, message);
	_frontendMessage(RETRO_LOG_WARN, message);
	return true;
}

#ifdef M_LIBRETRO_NETPACKET_V2_TEST
bool mLibretroNetpacketV2TestPollReceive(void) {
	return _adapter.sessionPrepared &&
	       GBALinkTransportPoll(&_adapter.transport);
}

void mLibretroNetpacketV2TestSetTimeMs(uint64_t nowMs) {
	_adapter.testClockEnabled = true;
	_adapter.testNowMs = nowMs;
}

uint64_t mLibretroNetpacketV2TestCallbackGeneration(void) {
	return _adapter.callbackGeneration;
}

size_t mLibretroNetpacketV2TestPendingPacketCount(void) {
	size_t count = _adapter.preAdmission.size;
	if (_adapter.sessionPrepared) {
		count += _adapter.transport.inbound.size;
		count += _adapter.transport.outbound.size;
	}
	return count;
}

uint8_t mLibretroNetpacketV2TestPlayerForRole(enum GBALinkRole role) {
	return _playerForRole(role);
}

bool mLibretroNetpacketV2TestInstallPair(
		const struct GBAReplicaManifest manifests[2],
		const struct GBAReplicaPayload payloads[2],
		enum GBALinkRole role, uint64_t generation) {
	if (!_adapter.registered || !manifests || !payloads || !generation ||
	    (role != GBA_LINK_ROLE_HOST && role != GBA_LINK_ROLE_CLIENT) ||
	    _adapter.sessionPrepared || _adapter.pairInitialized) {
		return false;
	}
	GBALinkTransportInit(
	    &_adapter.transport, &_transportVTable, &_adapter);
	GBALinkV2SessionInit(&_adapter.session, &_adapter.transport);
	_adapter.sessionPrepared = true;
	_adapter.session.state = GBA_LINK_V2_SESSION_READY;
	_adapter.session.localRole = role;
	_adapter.session.snapshotGeneration = generation;
	_adapter.session.inputDelay = 2;
	_adapter.session.firstFrame = 0;
	_adapter.session.config.callbacks = &_sessionCallbacks;
	_adapter.session.config.callbackContext = &_adapter;
	if (!_installPair(&_adapter, manifests, payloads)) {
		_deinitSession(&_adapter);
		return false;
	}
	_adapter.session.pairInstalled = true;
	if (!_commitPair(&_adapter)) {
		_discardPair(&_adapter, false);
		_adapter.session.pairInstalled = false;
		_deinitSession(&_adapter);
		return false;
	}
	_adapter.session.pairCommitted = true;
	return true;
}

struct mCore* mLibretroNetpacketV2TestPairCore(uint8_t player) {
	return _adapter.pairInitialized
	    ? GBAReplicatedPairCore(&_adapter.pair, player) : NULL;
}

void mLibretroNetpacketV2TestFail(enum GBALinkV2Reason reason) {
	if (_adapter.sessionPrepared &&
	    GBALinkV2SessionIsLive(&_adapter.session)) {
		GBALinkV2SessionFail(
		    &_adapter.session, reason, "injected test teardown");
		_finishFailed();
	}
}
#endif
