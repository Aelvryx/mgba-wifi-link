/* Copyright (c) 2026 mGBA Wi-Fi link contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "netpacket.h"

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
#include <mgba/internal/gba/sio/netplay/driver.h>
#include <mgba/internal/gba/sio/netplay/identity.h>
#include <mgba/internal/gba/sio/netplay/session.h>
#include <mgba/internal/gba/sio/netplay/transport.h>
#include <mgba-util/sha1.h>

#define NETPACKET_NO_CLIENT UINT16_MAX

struct mLibretroNetpacketAdapter {
	retro_environment_t environment;
	struct mCore* core;
	struct GBA* gba;
	struct GBALinkTransport transport;
	struct GBALinkSession session;
	struct GBASIONetplayDriver driver;
	struct GBALinkSessionConfig sessionConfig;
	retro_netpacket_send_t send;
	retro_netpacket_poll_receive_t pollReceive;
	struct GBALinkCopiedQueue preAdmission;
	uint64_t callbackGeneration;
	uint16_t localId;
	uint16_t remoteId;
	unsigned grantTraceCount;
	bool registered;
	bool frontendStarted;
	bool sessionPrepared;
	bool inFrontendCallback;
	bool reportedReady;
	bool protocolPending;
#ifdef M_LIBRETRO_NETPACKET_TEST
	bool testClockEnabled;
	uint64_t testNowMs;
#endif
};

static struct mLibretroNetpacketAdapter _adapter = {
	.localId = NETPACKET_NO_CLIENT,
	.remoteId = NETPACKET_NO_CLIENT,
};

static bool _failedTransferPending(void);
static bool _beginProtocol(
    struct mLibretroNetpacketAdapter* adapter);

static void _clearPreAdmission(
    struct mLibretroNetpacketAdapter* adapter) {
	memset(&adapter->preAdmission, 0,
	    sizeof(adapter->preAdmission));
}

static bool _queuePreAdmission(
    struct mLibretroNetpacketAdapter* adapter,
    const void* data, size_t size) {
	if (!data || size > GBA_LINK_MAX_PACKET_SIZE ||
	    adapter->preAdmission.size >=
	        GBA_LINK_MAX_COPIED_PACKETS) {
		return false;
	}
	size_t index =
	    (adapter->preAdmission.readIndex +
	     adapter->preAdmission.size) %
	    GBA_LINK_MAX_COPIED_PACKETS;
	struct GBALinkCopiedPacket* packet =
	    &adapter->preAdmission.packets[index];
	packet->generation = adapter->callbackGeneration;
	packet->size = size;
	memcpy(packet->data, data, size);
	++adapter->preAdmission.size;
	return true;
}

static bool _drainPreAdmission(
    struct mLibretroNetpacketAdapter* adapter) {
	while (adapter->preAdmission.size) {
		struct GBALinkCopiedPacket* packet =
		    &adapter->preAdmission.packets[
		        adapter->preAdmission.readIndex];
		if (!GBALinkTransportQueueInbound(
		        &adapter->transport,
		        adapter->callbackGeneration,
		        packet->data, packet->size)) {
			_clearPreAdmission(adapter);
			return false;
		}
		adapter->preAdmission.readIndex =
		    (adapter->preAdmission.readIndex + 1) %
		    GBA_LINK_MAX_COPIED_PACKETS;
		--adapter->preAdmission.size;
	}
	_clearPreAdmission(adapter);
	return true;
}

static const char* _reasonName(enum GBALinkReason reason) {
	switch (reason) {
	case GBA_LINK_REASON_PROTOCOL_MISMATCH: return "protocol mismatch";
	case GBA_LINK_REASON_CAPABILITY_MISMATCH: return "capability mismatch";
	case GBA_LINK_REASON_ROM_MISMATCH: return "ROM mismatch";
	case GBA_LINK_REASON_POLICY_MISMATCH: return "compatibility policy mismatch";
	case GBA_LINK_REASON_COMPATIBILITY_MISMATCH: return "emulation compatibility mismatch";
	case GBA_LINK_REASON_DETERMINISM_MISMATCH: return "determinism profile mismatch";
	case GBA_LINK_REASON_SIO_NOT_QUIESCENT: return "SIO not quiescent";
	case GBA_LINK_REASON_THIRD_PLAYER: return "third player rejected";
	case GBA_LINK_REASON_MALFORMED_PACKET: return "malformed packet";
	case GBA_LINK_REASON_TRANSPORT_STOP: return "transport stopped";
	case GBA_LINK_REASON_PEER_DETACH: return "peer detached";
	case GBA_LINK_REASON_HANDSHAKE_TIMEOUT: return "handshake timed out";
	case GBA_LINK_REASON_ATTACHMENT_TIMEOUT: return "attachment timed out";
	case GBA_LINK_REASON_MODE_TIMEOUT: return "mode barrier timed out";
	case GBA_LINK_REASON_TRANSFER_READY_TIMEOUT: return "transfer readiness timed out";
	case GBA_LINK_REASON_TRANSFER_COMMIT_TIMEOUT: return "transfer commit timed out";
	case GBA_LINK_REASON_COMPLETION_CATCHUP_TIMEOUT: return "completion catch-up timed out";
	case GBA_LINK_REASON_COMPLETION_READY_TIMEOUT: return "completion readiness timed out";
	case GBA_LINK_REASON_COMPLETION_DECISION_TIMEOUT: return "completion decision timed out";
	case GBA_LINK_REASON_DETACH_TIMEOUT: return "detach timed out";
	case GBA_LINK_REASON_MODE_DEPARTURE: return "peer left MULTI mode";
	case GBA_LINK_REASON_QUEUE_EXHAUSTED: return "packet queue exhausted";
	case GBA_LINK_REASON_OVERSIZED_PACKET: return "packet too large";
	case GBA_LINK_REASON_SEND_FAILURE: return "packet send failed";
	case GBA_LINK_REASON_SEQUENCE_EXHAUSTED: return "protocol sequence exhausted";
	case GBA_LINK_REASON_INVALID_TRANSITION: return "invalid protocol transition";
	case GBA_LINK_REASON_RESET: return "core reset";
	case GBA_LINK_REASON_UNLOAD: return "content unloaded";
	case GBA_LINK_REASON_USER_DISCONNECT: return "user disconnected";
	case GBA_LINK_REASON_GRANT_TIMEOUT: return "execution grant timed out";
	}
	return "unknown link error";
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
	_adapter.environment(
	    RETRO_ENVIRONMENT_SET_MESSAGE, &legacy);
}

static void _log(
    enum retro_log_level level, const char* message) {
	enum mLogLevel mLevel = mLOG_INFO;
	if (level == RETRO_LOG_ERROR) {
		mLevel = mLOG_ERROR;
	} else if (level == RETRO_LOG_WARN) {
		mLevel = mLOG_WARN;
	}
	mLog(
	    _mLOG_CAT_STATUS, mLevel,
	    "GBA link netplay: %s", message);
}

static uint64_t _monotonicTimeMs(void* context);

static void _tracePacket(
    const char* direction, enum GBALinkRole senderRole,
    const void* data, size_t size) {
	if (!direction || !data || !size) {
		return;
	}
	struct GBALinkPacket packet;
	enum GBALinkDecodeStatus status = GBALinkPacketDecode(
	    data, size, senderRole, &packet);
	if (status != GBA_LINK_DECODE_OK) {
		char detail[160];
		snprintf(
		    detail, sizeof(detail),
		    "%s packet decode=%s bytes=%zu",
		    direction, GBALinkDecodeStatusName(status), size);
		_log(RETRO_LOG_WARN, detail);
		return;
	}
	/*
	 * Frame-oriented grants are intentionally frequent. Log only the
	 * first few so attachment/catch-up failures remain diagnosable
	 * without turning ordinary emulation into a per-frame log stream.
	 */
	if (packet.header.type == GBA_LINK_MESSAGE_EXECUTION_GRANT ||
	    packet.header.type == GBA_LINK_MESSAGE_GRANT_ACK) {
		if (_adapter.grantTraceCount >= 16) {
			return;
		}
		++_adapter.grantTraceCount;
		char detail[224];
		snprintf(
		    detail, sizeof(detail),
		    "%s packet type=%s sequence=%" PRIu64
		    " grant=%" PRIu64 " horizon=%" PRIu64
		    " session=%" PRIu64 " bytes=%zu at_ms=%" PRIu64,
		    direction, GBALinkMessageTypeName(packet.header.type),
		    packet.header.packetSequence,
		    packet.payload.grant.grantSequence,
		    packet.payload.grant.horizon,
		    packet.header.sessionId, size,
		    _monotonicTimeMs(NULL));
		_log(RETRO_LOG_INFO, detail);
		return;
	}
	char detail[192];
	snprintf(
	    detail, sizeof(detail),
	    "%s packet type=%s sequence=%" PRIu64
	    " session=%" PRIu64 " bytes=%zu at_ms=%" PRIu64,
	    direction, GBALinkMessageTypeName(packet.header.type),
	    packet.header.packetSequence, packet.header.sessionId, size,
	    _monotonicTimeMs(NULL));
	_log(RETRO_LOG_INFO, detail);
}

static uint64_t _monotonicTimeMs(void* context) {
	UNUSED(context);
#ifdef M_LIBRETRO_NETPACKET_TEST
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
	struct mLibretroNetpacketAdapter* adapter = context;
	if (!adapter || !adapter->frontendStarted ||
	    !adapter->send ||
	    adapter->remoteId == NETPACKET_NO_CLIENT ||
	    !data || !size) {
		return false;
	}
	uint64_t generation = adapter->callbackGeneration;
	retro_netpacket_send_t send = adapter->send;
	int flags = RETRO_NETPACKET_RELIABLE;
	if (flush) {
		flags |= RETRO_NETPACKET_FLUSH_HINT;
	}
	_tracePacket(
	    "send",
	    adapter->localId == 0
	        ? GBA_LINK_ROLE_HOST
	        : GBA_LINK_ROLE_CLIENT,
	    data, size);
	send(flags, data, size, adapter->remoteId);
	return adapter->frontendStarted &&
	       adapter->callbackGeneration == generation &&
	       adapter->send == send;
}

static bool _pollReceive(void* context) {
	struct mLibretroNetpacketAdapter* adapter = context;
	if (!adapter || !adapter->frontendStarted ||
	    !adapter->pollReceive) {
		return false;
	}
	uint64_t generation = adapter->callbackGeneration;
	retro_netpacket_poll_receive_t pollReceive =
	    adapter->pollReceive;
	pollReceive();
	return adapter->frontendStarted &&
	       adapter->callbackGeneration == generation &&
	       adapter->pollReceive == pollReceive;
}

static void _yield(void* context) {
	UNUSED(context);
#ifdef _WIN32
	Sleep(0);
#else
	sched_yield();
#endif
}

static void _diagnostic(
    void* context, enum GBALinkDiagnosticLevel level,
    enum GBALinkReason reason, const char* detail) {
	struct mLibretroNetpacketAdapter* adapter = context;
	char message[384];
	const char* reasonName = _reasonName(reason);
	if (reason == GBA_LINK_REASON_GRANT_TIMEOUT &&
	    adapter && adapter->sessionPrepared) {
		snprintf(
		    message, sizeof(message),
		    "Link failed: %s (%s); role=%u paused=%u queued_mode=%u"
		    " boundary=%u local=%" PRIu64 " limit=%" PRIu64
		    " cable=%" PRIu64 " grant=%" PRIu64
		    " outstanding=%u mode_barrier=%u",
		    reasonName,
		    detail && detail[0] ? detail : "execution grant",
		    adapter->session.localRole,
		    adapter->driver.paused,
		    adapter->driver.queuedLocalModeIntent,
		    adapter->driver.boundary,
		    adapter->driver.localCycle,
		    adapter->driver.executionLimit,
		    adapter->driver.timeline.currentCableCycle,
		    adapter->driver.timeline.grantHorizon,
		    adapter->driver.timeline.grantOutstanding,
		    adapter->driver.timeline.modeBarrier);
	} else if (reason == GBA_LINK_REASON_INVALID_TRANSITION &&
	           adapter && adapter->sessionPrepared) {
		snprintf(
		    message, sizeof(message),
		    "Link failed: %s (%s); role=%u session=%u transfer_state=%u"
		    " transfer=%" PRIu64 " completion_seq=%" PRIu64
		    " last_remote_completion=%" PRIu64
		    " local=%" PRIu64 " cable=%" PRIu64
		    " start=%" PRIu64 " completion=%" PRIu64
		    " paused=%u boundary=%u",
		    reasonName,
		    detail && detail[0] ? detail : "invalid transition",
		    adapter->session.localRole,
		    adapter->session.state,
		    adapter->driver.transfer.state,
		    adapter->driver.transfer.sequence,
		    adapter->driver.transfer.completionSequence,
		    adapter->driver.lastRemoteCompletionSequence,
		    adapter->driver.localCycle,
		    adapter->driver.timeline.currentCableCycle,
		    adapter->driver.transfer.startCycle,
		    adapter->driver.transfer.completionCycle,
		    adapter->driver.paused,
		    adapter->driver.boundary);
	} else if (detail && detail[0]) {
		snprintf(
		    message, sizeof(message),
		    "Link failed: %s (%s)", reasonName, detail);
	} else {
		snprintf(
		    message, sizeof(message),
		    "Link failed: %s", reasonName);
	}
	enum retro_log_level retroLevel =
	    level == GBA_LINK_DIAGNOSTIC_INFO
	        ? RETRO_LOG_INFO
	        : level == GBA_LINK_DIAGNOSTIC_WARN
	              ? RETRO_LOG_WARN
	              : RETRO_LOG_ERROR;
	_log(retroLevel, message);
	_frontendMessage(retroLevel, message);
}

static void _invalidateFrontendFunctions(
    struct mLibretroNetpacketAdapter* adapter) {
	adapter->send = NULL;
	adapter->pollReceive = NULL;
	adapter->frontendStarted = false;
	adapter->inFrontendCallback = false;
	adapter->localId = NETPACKET_NO_CLIENT;
	adapter->remoteId = NETPACKET_NO_CLIENT;
	_clearPreAdmission(adapter);
	adapter->protocolPending = false;
	++adapter->callbackGeneration;
	if (!adapter->callbackGeneration) {
		++adapter->callbackGeneration;
	}
}

static void _transportStop(void* context) {
	struct mLibretroNetpacketAdapter* adapter = context;
	if (!adapter) {
		return;
	}
	char lifecycle[160];
	snprintf(
	    lifecycle, sizeof(lifecycle),
	    "transport requested callback invalidation: started=%u local=%" PRIu16
	    " generation=%" PRIu64,
	    adapter->frontendStarted, adapter->localId,
	    adapter->callbackGeneration);
	_log(RETRO_LOG_INFO, lifecycle);
	_invalidateFrontendFunctions(adapter);
}

static const struct GBALinkTransportVTable _transportVTable = {
	.sendReliable = _sendReliable,
	.pollReceive = _pollReceive,
	.yield = _yield,
	.monotonicTimeMs = _monotonicTimeMs,
	.diagnostic = _diagnostic,
	.stop = _transportStop,
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

static enum GBALinkIdleOptimization _idleOptimization(
    const struct GBA* gba) {
	switch (gba->idleOptimization) {
	case IDLE_LOOP_DETECT:
		return GBA_LINK_IDLE_OPTIMIZATION_DETECT;
	case IDLE_LOOP_REMOVE:
		return GBA_LINK_IDLE_OPTIMIZATION_REMOVE;
	case IDLE_LOOP_IGNORE:
	default:
		return GBA_LINK_IDLE_OPTIMIZATION_NONE;
	}
}

static bool _buildSessionConfig(
    struct mLibretroNetpacketAdapter* adapter) {
	if (!adapter || !adapter->core || !adapter->gba) {
		return false;
	}
	struct GBALinkDeterminismProfileInput profile;
	memset(&profile, 0, sizeof(profile));
	profile.useBios = adapter->gba->biosVf != NULL;
	if (profile.useBios) {
		sha1Buffer(
		    adapter->gba->memory.bios, GBA_SIZE_BIOS,
		    profile.biosSha1);
	}
	/*
	 * Version-one mGBA has no GBA overclock or speed-hack option. Keep
	 * those canonical fields at their neutral values and include boot
	 * behavior in the stable timing-model word.
	 */
	profile.timingModel =
	    (adapter->core->opts.skipBios ? 1U : 0U) |
	    (adapter->core->opts.useBios ? 2U : 0U);
	profile.overclockQ16 = 1U << 16;
	profile.speedHackFlags = 0;
	profile.idleOptimization =
	    _idleOptimization(adapter->gba);
	profile.rtcOverrideMode =
	    GBA_LINK_RTC_OVERRIDE_NONE;
	profile.cheatsEnabled =
	    _cheatsEnabled(adapter->core);

	memset(
	    &adapter->sessionConfig, 0,
	    sizeof(adapter->sessionConfig));
	if (!GBALinkContentIdentityFromCore(
	        adapter->core,
	        &adapter->sessionConfig.identity) ||
	    !GBALinkDeterminismProfileBuild(
	        &profile, adapter->sessionConfig.digests)) {
		return false;
	}
	adapter->sessionConfig.capabilities =
	    GBA_LINK_MVP_CAPABILITIES;
	adapter->sessionConfig.supportedPolicies =
	    1U << GBA_LINK_COMPATIBILITY_EXACT_ROM;
	adapter->sessionConfig.emulationCompatibilityVersion =
	    GBA_LINK_EMULATION_COMPATIBILITY_VERSION;
	adapter->sessionConfig.cheatsEnabled =
	    profile.cheatsEnabled;
	GBALinkDeadlinePolicyInit(
	    &adapter->sessionConfig.deadlines);
	adapter->sessionConfig.callbacks =
	    GBASIONetplayDriverSessionCallbacks();
	adapter->sessionConfig.callbackContext =
	    &adapter->driver;
	return !profile.cheatsEnabled;
}

static void _deinitSession(
    struct mLibretroNetpacketAdapter* adapter) {
	if (!adapter || !adapter->sessionPrepared) {
		return;
	}
	GBASIONetplayDriverDetach(&adapter->driver);
	GBALinkSessionDeinit(&adapter->session);
	GBALinkTransportDeinit(&adapter->transport);
	memset(&adapter->driver, 0, sizeof(adapter->driver));
	memset(&adapter->session, 0, sizeof(adapter->session));
	memset(&adapter->transport, 0, sizeof(adapter->transport));
	adapter->sessionPrepared = false;
	adapter->reportedReady = false;
}

static bool _prepareSession(
    struct mLibretroNetpacketAdapter* adapter) {
	_deinitSession(adapter);
	GBALinkTransportInit(
	    &adapter->transport, &_transportVTable, adapter);
	GBALinkSessionInit(
	    &adapter->session, &adapter->transport);
	GBASIONetplayDriverCreate(
	    &adapter->driver, &adapter->gba->sio,
	    &adapter->session);
	adapter->sessionPrepared = true;
	if (!_buildSessionConfig(adapter) ||
	    !GBALinkSessionConfigure(
	        &adapter->session,
	        &adapter->sessionConfig)) {
		_deinitSession(adapter);
		_log(
		    RETRO_LOG_ERROR,
		    "session rejected because content, determinism settings, or cheats are incompatible");
		_frontendMessage(
		    RETRO_LOG_ERROR,
		    "GBA link unavailable: disable cheats and use matching deterministic settings");
		return false;
	}
	return true;
}

static bool _beginProtocol(
    struct mLibretroNetpacketAdapter* adapter) {
	enum GBALinkRole role =
	    adapter->localId == 0
	        ? GBA_LINK_ROLE_HOST
	        : GBA_LINK_ROLE_CLIENT;
	if (!_prepareSession(adapter) ||
	    !GBALinkTransportStart(
	        &adapter->transport,
	        adapter->callbackGeneration, role) ||
	    !GBALinkSessionStart(
	        &adapter->session,
	        adapter->callbackGeneration, role)) {
		if (adapter->transport.active) {
			GBALinkTransportInvalidate(
			    &adapter->transport,
			    GBA_LINK_REASON_INVALID_TRANSITION,
			    "could not start link session");
		}
		return false;
	}
	_log(
	    RETRO_LOG_INFO,
	    role == GBA_LINK_ROLE_HOST
	        ? "host transport started"
	        : "client transport started");
	return true;
}

static bool _beginPendingProtocol(
    struct mLibretroNetpacketAdapter* adapter) {
	if (!adapter || !adapter->protocolPending ||
	    adapter->sessionPrepared) {
		return adapter && adapter->sessionPrepared;
	}
	if (!_beginProtocol(adapter) ||
	    !_drainPreAdmission(adapter)) {
		if (adapter->sessionPrepared &&
		    GBALinkSessionIsLive(&adapter->session)) {
			GBALinkSessionFail(
			    &adapter->session,
			    adapter->transport.failureReason
			        ? adapter->transport.failureReason
			        : GBA_LINK_REASON_QUEUE_EXHAUSTED,
			    "pre-session packets could not enter transport");
		}
		_invalidateFrontendFunctions(adapter);
		return false;
	}
	adapter->protocolPending = false;
	return true;
}

static bool _enterRendezvous(
    struct mLibretroNetpacketAdapter* adapter) {
	if (!adapter || !adapter->frontendStarted) {
		return false;
	}
	/*
	 * RetroArch starts the host-side Netpacket interface while the core is
	 * still running standalone.  That only establishes the local endpoint;
	 * it is not an attachment attempt until connected(1) admits a peer.
	 */
	if (adapter->localId == 0 &&
	    adapter->remoteId == NETPACKET_NO_CLIENT &&
	    !adapter->protocolPending) {
		return true;
	}
	if (adapter->protocolPending &&
	    !adapter->sessionPrepared) {
		return _beginPendingProtocol(adapter);
	}
	return true;
}

static void RETRO_CALLCONV _start(
    uint16_t clientId, retro_netpacket_send_t send,
    retro_netpacket_poll_receive_t pollReceive) {
	char lifecycle[192];
	snprintf(
	    lifecycle, sizeof(lifecycle),
	    "frontend start callback: client=%" PRIu16
	    " registered=%u core=%u gba=%u send=%u poll=%u generation=%" PRIu64,
	    clientId, _adapter.registered, _adapter.core != NULL,
	    _adapter.gba != NULL, send != NULL, pollReceive != NULL,
	    _adapter.callbackGeneration);
	_log(RETRO_LOG_INFO, lifecycle);
	if (_failedTransferPending()) {
		_log(
		    RETRO_LOG_WARN,
		    "frontend start rejected: failed transfer is still pending");
		_frontendMessage(
		    RETRO_LOG_ERROR,
		    "GBA link cannot restart until the failed transfer completes");
		return;
	}
	_deinitSession(&_adapter);
	_invalidateFrontendFunctions(&_adapter);
	if (!_adapter.registered || !_adapter.core ||
	    !_adapter.gba || !send || !pollReceive ||
	    (clientId != 0 && clientId != 1)) {
		char rejected[192];
		snprintf(
		    rejected, sizeof(rejected),
		    "frontend start rejected after teardown: client=%" PRIu16
		    " registered=%u core=%u gba=%u send=%u poll=%u",
		    clientId, _adapter.registered,
		    _adapter.core != NULL, _adapter.gba != NULL,
		    send != NULL, pollReceive != NULL);
		_log(RETRO_LOG_ERROR, rejected);
		_frontendMessage(
		    RETRO_LOG_ERROR,
		    "GBA link failed: unsupported Netpacket start");
		return;
	}
	_adapter.frontendStarted = true;
	_adapter.localId = clientId;
	_adapter.remoteId =
	    clientId == 0 ? NETPACKET_NO_CLIENT : 0;
	_adapter.send = send;
	_adapter.pollReceive = pollReceive;
	++_adapter.callbackGeneration;
	if (!_adapter.callbackGeneration) {
		++_adapter.callbackGeneration;
	}
	if (clientId != 0) {
		_adapter.protocolPending = true;
		_enterRendezvous(&_adapter);
	}
	snprintf(
	    lifecycle, sizeof(lifecycle),
	    "frontend start installed: started=%u local=%" PRIu16
	    " remote=%" PRIu16 " generation=%" PRIu64,
	    _adapter.frontendStarted, _adapter.localId,
	    _adapter.remoteId, _adapter.callbackGeneration);
	_log(RETRO_LOG_INFO, lifecycle);
}

static void RETRO_CALLCONV _receive(
    const void* data, size_t size, uint16_t clientId) {
	if (!_adapter.frontendStarted) {
		return;
	}
	_tracePacket(
	    "receive",
	    clientId == 0
	        ? GBA_LINK_ROLE_HOST
	        : GBA_LINK_ROLE_CLIENT,
	    data, size);
	/*
	 * RetroArch can deliver the client's first core packet immediately
	 * before invoking connected(1) on the host. Preserve admission
	 * ordering by copying those packets into a bounded provisional
	 * queue, then hand them to the real transport only after connected()
	 * accepts player two.
	 */
	if (!_adapter.sessionPrepared &&
	    ((_adapter.localId == 0 &&
	      _adapter.remoteId == NETPACKET_NO_CLIENT &&
	      clientId == 1) ||
	     (_adapter.protocolPending &&
	      clientId == _adapter.remoteId))) {
		if (!_queuePreAdmission(
		        &_adapter, data, size)) {
			enum GBALinkReason reason =
			    size > GBA_LINK_MAX_PACKET_SIZE
			        ? GBA_LINK_REASON_OVERSIZED_PACKET
			        : GBA_LINK_REASON_QUEUE_EXHAUSTED;
			_diagnostic(
			    &_adapter,
			    GBA_LINK_DIAGNOSTIC_ERROR,
			    reason,
			    "pre-admission packet queue rejected data");
			_invalidateFrontendFunctions(&_adapter);
		}
		return;
	}
	if (!_adapter.sessionPrepared ||
	    clientId != _adapter.remoteId) {
		if (_adapter.sessionPrepared &&
		    GBALinkSessionIsLive(&_adapter.session)) {
			GBALinkSessionFail(
			    &_adapter.session,
			    GBA_LINK_REASON_INVALID_TRANSITION,
			    "receive callback used an unexpected sender or ordering");
		}
		_invalidateFrontendFunctions(&_adapter);
		return;
	}
	uint64_t generation = _adapter.callbackGeneration;
	_adapter.inFrontendCallback = true;
	if (!GBALinkTransportQueueInbound(
	        &_adapter.transport, generation, data, size)) {
		_adapter.inFrontendCallback = false;
		if (GBALinkSessionIsLive(&_adapter.session)) {
			GBALinkSessionFail(
			    &_adapter.session,
			    _adapter.transport.failureReason
			        ? _adapter.transport.failureReason
			        : GBA_LINK_REASON_MALFORMED_PACKET,
			    "Netpacket receive callback rejected packet");
		}
		_invalidateFrontendFunctions(&_adapter);
		return;
	}
	_adapter.inFrontendCallback = false;
}

static void RETRO_CALLCONV _stop(void) {
	char lifecycle[160];
	snprintf(
	    lifecycle, sizeof(lifecycle),
	    "frontend stop callback: started=%u local=%" PRIu16
	    " generation=%" PRIu64,
	    _adapter.frontendStarted, _adapter.localId,
	    _adapter.callbackGeneration);
	_log(RETRO_LOG_INFO, lifecycle);
	bool sessionLive =
	    _adapter.sessionPrepared &&
	    GBALinkSessionIsLive(&_adapter.session);
	_invalidateFrontendFunctions(&_adapter);
	if (_adapter.sessionPrepared) {
		GBALinkTransportInvalidate(
		    &_adapter.transport,
		    GBA_LINK_REASON_TRANSPORT_STOP, NULL);
		if (sessionLive) {
			GBALinkSessionFail(
			    &_adapter.session,
			    GBA_LINK_REASON_TRANSPORT_STOP,
			    "frontend stopped Netpacket transport");
		}
	}
}

static void RETRO_CALLCONV _poll(void) {
	if (_adapter.sessionPrepared &&
	    GBALinkSessionIsLive(&_adapter.session)) {
		GBASIONetplayDriverPump(
		    &_adapter.driver, false);
	}
}

static bool RETRO_CALLCONV _connected(uint16_t clientId) {
	if (!_adapter.frontendStarted ||
	    _adapter.localId != 0 ||
	    _adapter.remoteId != NETPACKET_NO_CLIENT ||
	    clientId != 1) {
		char detail[256];
		snprintf(
		    detail, sizeof(detail),
		    "connection rejected: started=%u local=%" PRIu16
		    " remote=%" PRIu16 " candidate=%" PRIu16
		    " registered=%u core=%u",
		    _adapter.frontendStarted,
		    _adapter.localId, _adapter.remoteId,
		    clientId, _adapter.registered,
		    _adapter.core != NULL);
		_diagnostic(
		    &_adapter, GBA_LINK_DIAGNOSTIC_WARN,
		    GBA_LINK_REASON_THIRD_PLAYER,
		    detail);
		return false;
	}
	_adapter.remoteId = clientId;
	_adapter.protocolPending = true;
	if (!_enterRendezvous(&_adapter)) {
		_adapter.remoteId = NETPACKET_NO_CLIENT;
		_adapter.protocolPending = false;
		return false;
	}
	return true;
}

static void RETRO_CALLCONV _disconnected(uint16_t clientId) {
	if (!_adapter.frontendStarted ||
	    clientId != _adapter.remoteId) {
		return;
	}
	_adapter.remoteId = NETPACKET_NO_CLIENT;
	if (_adapter.sessionPrepared &&
	    GBALinkSessionIsLive(&_adapter.session)) {
		GBALinkSessionFail(
		    &_adapter.session,
		    GBA_LINK_REASON_PEER_DETACH,
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
	.protocol_version = GBA_LINK_PROTOCOL_NAME,
};

bool mLibretroNetpacketRegister(
    retro_environment_t environment, struct mCore* core) {
	mLibretroNetpacketUnload();
	if (!environment || !core || !core->platform ||
	    core->platform(core) != mPLATFORM_GBA ||
	    !core->board) {
		return false;
	}
	_adapter.environment = environment;
	_adapter.core = core;
	_adapter.gba = core->board;
	/*
	 * A frontend with an already-active host may invoke start(0)
	 * synchronously from SET_NETPACKET_INTERFACE. Mark the adapter
	 * provisionally registered so that callback is valid before the
	 * environment call returns.
	 */
	_adapter.registered = true;
	if (!environment(
	        RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE,
	        (void*) &_callbacks)) {
		mLibretroNetpacketUnload();
		return false;
	}
	_log(
	    RETRO_LOG_INFO,
	    "registered RetroArch Netpacket command 78");
	return true;
}

static bool _failedTransferPending(void) {
	return _adapter.sessionPrepared &&
	       _adapter.session.state ==
	           GBA_LINK_SESSION_FAILED &&
	       _adapter.driver.transfer.state !=
	           GBA_SIO_NETPLAY_TRANSFER_IDLE &&
	       _adapter.driver.transfer.state !=
	           GBA_SIO_NETPLAY_TRANSFER_FINISHED;
}

static void _finishFailedTeardown(void) {
	if (_adapter.sessionPrepared &&
	    _adapter.session.state ==
	        GBA_LINK_SESSION_FAILED &&
	    !_failedTransferPending()) {
		_deinitSession(&_adapter);
	}
}

static void _reportReady(void) {
	if (!_adapter.sessionPrepared ||
	    _adapter.reportedReady ||
	    _adapter.session.state !=
	        GBA_LINK_SESSION_READY) {
		return;
	}
	_adapter.reportedReady = true;
	const char* message =
	    _adapter.session.localRole ==
	            GBA_LINK_ROLE_HOST
	        ? "GBA link ready: player 1 (host)"
	        : "GBA link ready: player 2 (client)";
	_log(RETRO_LOG_INFO, message);
	_frontendMessage(RETRO_LOG_INFO, message);
	char detail[240];
	snprintf(
	    detail, sizeof(detail),
	    "ready state: attached=%u observable=%u paused=%u joint=%u"
	    " modes=%u/%u generation=%" PRIu64 " barrier=%u grant=%u",
	    _adapter.driver.attached,
	    _adapter.driver.observable,
	    _adapter.driver.paused,
	    _adapter.driver.jointlyReady,
	    _adapter.driver.committedLocalMode,
	    _adapter.driver.committedRemoteMode,
	    _adapter.driver.committedModeGeneration,
	    _adapter.driver.timeline.modeBarrier,
	    _adapter.driver.timeline.grantOutstanding);
	_log(RETRO_LOG_INFO, detail);
}

void mLibretroNetpacketRunBegin(void) {
	_enterRendezvous(&_adapter);
	if (_adapter.sessionPrepared &&
	    GBALinkSessionIsLive(&_adapter.session)) {
		GBASIONetplayDriverPump(
		    &_adapter.driver, false);
	}
	_reportReady();
	_finishFailedTeardown();
}

void mLibretroNetpacketRunEnd(void) {
	_enterRendezvous(&_adapter);
	if (_adapter.sessionPrepared &&
	    GBALinkSessionIsLive(&_adapter.session)) {
		if (_adapter.session.localRole ==
		    GBA_LINK_ROLE_HOST) {
			GBASIONetplayDriverHostFrameBoundary(
			    &_adapter.driver);
		}
		GBASIONetplayDriverPump(
		    &_adapter.driver, false);
	}
	_reportReady();
	_finishFailedTeardown();
}

bool mLibretroNetpacketExecutionBlocked(void) {
	if (!_adapter.frontendStarted ||
	    !_adapter.sessionPrepared) {
		return false;
	}
	return GBASIONetplayDriverIsPaused(
	    &_adapter.driver);
}

void mLibretroNetpacketReset(void) {
	if (_adapter.frontendStarted ||
	    _adapter.sessionPrepared) {
		char lifecycle[160];
		snprintf(
		    lifecycle, sizeof(lifecycle),
		    "core reset teardown: started=%u session=%u generation=%" PRIu64,
		    _adapter.frontendStarted,
		    _adapter.sessionPrepared,
		    _adapter.callbackGeneration);
		_log(RETRO_LOG_INFO, lifecycle);
	}
	if (_adapter.sessionPrepared &&
	    _adapter.session.state !=
	        GBA_LINK_SESSION_DISCONNECTED) {
		GBASIONetplayDriverCancel(
		    &_adapter.driver,
		    GBA_LINK_REASON_RESET);
	}
	_invalidateFrontendFunctions(&_adapter);
	_deinitSession(&_adapter);
}

void mLibretroNetpacketUnload(void) {
	if (_adapter.registered || _adapter.frontendStarted ||
	    _adapter.sessionPrepared) {
		char lifecycle[160];
		snprintf(
		    lifecycle, sizeof(lifecycle),
		    "content unload teardown: registered=%u started=%u session=%u",
		    _adapter.registered, _adapter.frontendStarted,
		    _adapter.sessionPrepared);
		_log(RETRO_LOG_INFO, lifecycle);
	}
	if (_adapter.sessionPrepared &&
	    _adapter.session.state !=
	        GBA_LINK_SESSION_DISCONNECTED) {
		GBASIONetplayDriverCancel(
		    &_adapter.driver,
		    GBA_LINK_REASON_UNLOAD);
	}
	_invalidateFrontendFunctions(&_adapter);
	_deinitSession(&_adapter);
	memset(&_adapter, 0, sizeof(_adapter));
	_adapter.localId = NETPACKET_NO_CLIENT;
	_adapter.remoteId = NETPACKET_NO_CLIENT;
}

bool mLibretroNetpacketSessionActive(void) {
	return _adapter.frontendStarted ||
	       (_adapter.sessionPrepared &&
	        _adapter.session.state !=
	            GBA_LINK_SESSION_DISCONNECTED);
}

bool mLibretroNetpacketRejectStateOperation(
    const char* operation) {
	if (!mLibretroNetpacketSessionActive()) {
		return false;
	}
	char message[160];
	snprintf(
	    message, sizeof(message),
	    "%s is unavailable during GBA link netplay",
	    operation ? operation : "This operation");
	_log(RETRO_LOG_WARN, message);
	_frontendMessage(RETRO_LOG_WARN, message);
	return true;
}

bool mLibretroNetpacketRejectTimingChange(
    const char* category) {
	if (!mLibretroNetpacketSessionActive()) {
		return false;
	}
	char message[160];
	snprintf(
	    message, sizeof(message),
	    "GBA link kept the accepted %s setting",
	    category ? category : "timing-sensitive");
	_log(RETRO_LOG_WARN, message);
	_frontendMessage(RETRO_LOG_WARN, message);
	return true;
}

bool mLibretroNetpacketRejectCheatChange(void) {
	if (!mLibretroNetpacketSessionActive()) {
		return false;
	}
	_log(
	    RETRO_LOG_WARN,
	    "cheat changes are unavailable during link netplay");
	_frontendMessage(
	    RETRO_LOG_WARN,
	    "Cheat changes are unavailable during GBA link netplay");
	return true;
}

#ifdef M_LIBRETRO_NETPACKET_TEST
bool mLibretroNetpacketTestPollReceive(void) {
	if (!_adapter.sessionPrepared) {
		return false;
	}
	return GBALinkTransportPoll(
	    &_adapter.transport);
}

int mLibretroNetpacketTestSessionState(void) {
	return _adapter.sessionPrepared
	    ? _adapter.session.state
	    : GBA_LINK_SESSION_DISCONNECTED;
}

void mLibretroNetpacketTestSetSessionState(
    int state) {
	if (_adapter.sessionPrepared) {
		_adapter.session.state =
		    (enum GBALinkSessionState) state;
	}
}

void mLibretroNetpacketTestSetTimeMs(uint64_t nowMs) {
	_adapter.testClockEnabled = true;
	_adapter.testNowMs = nowMs;
}

uint64_t mLibretroNetpacketTestCallbackGeneration(void) {
	return _adapter.callbackGeneration;
}

size_t mLibretroNetpacketTestPendingPacketCount(void) {
	size_t count = _adapter.preAdmission.size;
	if (_adapter.sessionPrepared) {
		count += _adapter.transport.inbound.size;
		count += _adapter.transport.outbound.size;
	}
	return count;
}
#endif
