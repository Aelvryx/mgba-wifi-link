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
#define mLibretroNetpacketV2RejectLatencyPolicyChange \
	M_NETPACKET_V2_SYMBOL(RejectLatencyPolicyChange)
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
#define mLibretroNetpacketV2TestCaptureCheckpoint \
	M_NETPACKET_V2_SYMBOL(TestCaptureCheckpoint)
#define mLibretroNetpacketV2TestFailNextCheckpointAllocation \
	M_NETPACKET_V2_SYMBOL(TestFailNextCheckpointAllocation)
#define mLibretroNetpacketV2TestFail M_NETPACKET_V2_SYMBOL(TestFail)
#define mLibretroNetpacketV2TestGetMetrics \
	M_NETPACKET_V2_SYMBOL(TestGetMetrics)
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
#include <mgba/internal/gba/savedata.h>
#include <mgba/internal/gba/serialize.h>
#include <mgba/internal/gba/sio/netplay/identity.h>
#include <mgba/internal/gba/sio/netplay/rtc-sync.h>
#include <mgba/internal/gba/sio/netplay/session-v2.h>
#include <mgba/internal/gba/sio/netplay/transport.h>
#include <mgba-util/audio-buffer.h>

#define NETPACKET_V2_NO_CLIENT UINT16_MAX

enum {
	NETPACKET_V2_VERIFICATION_INTERVAL = 60,
	NETPACKET_V2_RENDEZVOUS_HISTOGRAM_MAX_MS = 3000,
	NETPACKET_V2_INPUT_WAIT_HISTOGRAM_MAX_US = 30000,
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
	uint64_t calibrationSentPackets;
	uint64_t calibrationSentBytes;
	uint64_t calibrationReceivedPackets;
	uint64_t calibrationReceivedBytes;
	uint64_t rendezvousCount;
	uint64_t rendezvousTotalMs;
	uint64_t rendezvousMaxMs;
	uint32_t rendezvousHistogram[
	    NETPACKET_V2_RENDEZVOUS_HISTOGRAM_MAX_MS + 1];
	uint64_t releasedFrames;
	uint64_t inputWaitedFrames;
	uint64_t inputWaitTotalUs;
	uint64_t inputWaitMaxUs;
	uint64_t inputDeadlineMisses;
	uint64_t telemetryClockFailures;
	uint32_t inputWaitHistogram[
	    NETPACKET_V2_INPUT_WAIT_HISTOGRAM_MAX_US + 2];
	uint64_t inputPollSendCount;
	uint64_t inputPollSendTotalUs;
	uint64_t inputPollSendMaxUs;
	uint64_t inputInsertions[2];
	uint64_t inputLeadFramesTotal[2];
	uint64_t inputLeadFramesMax[2];
	uint64_t inputLeadUsTotal[2];
	uint64_t inputLeadUsMax[2];
	uint64_t lastInsertionFrame[2];
	bool hasLastInsertionFrame[2];
	uint64_t verificationMatches;
	uint64_t audioFrames;
	uint64_t audioSamples;
	uint64_t emptyAudioFrames;
	size_t queueHighWater;
};

struct mLibretroNetpacketV2Checkpoint {
	struct GBASerializedState* state;
	uint8_t* saveData;
	size_t saveSize;
	size_t saveBackingSize;
	enum GBASavedataType saveType;
	enum mRTCGenericType rtcType;
	int64_t rtcValue;
	enum mRTCGenericType restoreRtcType;
	int64_t restoreRtcValue;
	time_t cartridgeRtcLastLatch;
	time_t cartridgeRtcOffset;
	uint64_t frame;
	uint64_t saveGeneration;
	bool valid;
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
	bool deferHostHello;
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
	struct mLibretroNetpacketV2Checkpoint verifiedCheckpoint;
	struct GBALinkV2RTCNormalization rtcNormalization;
	bool rtcNormalizationValid;
	uint64_t localSaveGeneration;
	struct mLibretroNetpacketV2Metrics metrics;
#ifdef M_LIBRETRO_NETPACKET_V2_TEST
	bool testClockEnabled;
	uint64_t testNowMs;
	bool testFailCheckpointAllocation;
#endif
};

static struct mLibretroNetpacketV2Adapter _adapter = {
	.localId = NETPACKET_V2_NO_CLIENT,
	.remoteId = NETPACKET_V2_NO_CLIENT,
};

static uint64_t _monotonicTimeMs(void* context);
static bool _monotonicTimeUs(void* context, uint64_t* timestamp);
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

static void _recordInputWait(
		struct mLibretroNetpacketV2Adapter* adapter,
		uint64_t durationUs) {
	++adapter->metrics.inputWaitedFrames;
	adapter->metrics.inputWaitTotalUs += durationUs;
	if (durationUs > adapter->metrics.inputWaitMaxUs) {
		adapter->metrics.inputWaitMaxUs = durationUs;
	}
	size_t bucket = durationUs > NETPACKET_V2_INPUT_WAIT_HISTOGRAM_MAX_US
	    ? NETPACKET_V2_INPUT_WAIT_HISTOGRAM_MAX_US + 1
	    : (size_t) durationUs;
	++adapter->metrics.inputWaitHistogram[bucket];
}

static uint64_t _inputWaitPercentile(
		const struct mLibretroNetpacketV2Adapter* adapter,
		unsigned percentile) {
	if (!adapter->metrics.inputWaitedFrames) {
		return 0;
	}
	uint64_t target =
	    (adapter->metrics.inputWaitedFrames * percentile + 99) / 100;
	uint64_t seen = 0;
	for (size_t i = 0;
	     i <= NETPACKET_V2_INPUT_WAIT_HISTOGRAM_MAX_US + 1; ++i) {
		seen += adapter->metrics.inputWaitHistogram[i];
		if (seen >= target) {
			return i;
		}
	}
	return NETPACKET_V2_INPUT_WAIT_HISTOGRAM_MAX_US + 1;
}

static uint64_t _leadMicroseconds(uint64_t frames) {
#if defined(__SIZEOF_INT128__)
	__uint128_t value = (__uint128_t) frames * 280896 * 1000000;
	return value / 16777216;
#else
	if (frames > UINT64_MAX / 280896) {
		return UINT64_MAX;
	}
	uint64_t cycles = frames * 280896;
	uint64_t seconds = cycles / 16777216;
	uint64_t remainder = cycles % 16777216;
	if (seconds > UINT64_MAX / 1000000) {
		return UINT64_MAX;
	}
	return seconds * 1000000 + remainder * 1000000 / 16777216;
#endif
}

static void _recordInputInsertions(
		struct mLibretroNetpacketV2Adapter* adapter,
		const struct GBALinkV2InputBatch* batch) {
	if (!adapter || !batch || batch->player > 1 ||
	    !adapter->runtimeInitialized) {
		return;
	}
	uint8_t player = batch->player;
	for (unsigned i = 0; i < batch->count; ++i) {
		uint64_t frame = batch->records[i].frame;
		if (frame < adapter->runtime.input.nextFrame ||
		    (adapter->metrics.hasLastInsertionFrame[player] &&
		     frame <= adapter->metrics.lastInsertionFrame[player])) {
			continue;
		}
		uint64_t leadFrames = frame - adapter->runtime.input.nextFrame;
		uint64_t leadUs = _leadMicroseconds(leadFrames);
		++adapter->metrics.inputInsertions[player];
		adapter->metrics.inputLeadFramesTotal[player] += leadFrames;
		adapter->metrics.inputLeadUsTotal[player] += leadUs;
		if (leadFrames > adapter->metrics.inputLeadFramesMax[player]) {
			adapter->metrics.inputLeadFramesMax[player] = leadFrames;
		}
		if (leadUs > adapter->metrics.inputLeadUsMax[player]) {
			adapter->metrics.inputLeadUsMax[player] = leadUs;
		}
		adapter->metrics.lastInsertionFrame[player] = frame;
		adapter->metrics.hasLastInsertionFrame[player] = true;
	}
}

static uint8_t _playerForRole(enum GBALinkRole role) {
	return role == GBA_LINK_ROLE_HOST ? 0 : 1;
}

static bool _calibrationWirePacket(const void* data, size_t size) {
	if (!data || size < GBA_LINK_V2_HEADER_SIZE) {
		return false;
	}
	const uint8_t* bytes = data;
	uint16_t type = bytes[6] | (uint16_t) bytes[7] << 8;
	return type >= GBA_LINK_V2_MESSAGE_CALIBRATION_BEGIN &&
	       type <= GBA_LINK_V2_MESSAGE_LATENCY_REPORT;
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

static bool _monotonicTimeUs(void* context, uint64_t* timestamp) {
	UNUSED(context);
	if (!timestamp) {
		return false;
	}
#ifdef M_LIBRETRO_NETPACKET_V2_TEST
	if (_adapter.testClockEnabled) {
		if (_adapter.testNowMs > UINT64_MAX / 1000) {
			return false;
		}
		*timestamp = _adapter.testNowMs * 1000;
		return true;
	}
#endif
#ifdef _WIN32
	LARGE_INTEGER counter;
	LARGE_INTEGER frequency;
	if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0 ||
	    !QueryPerformanceCounter(&counter) || counter.QuadPart < 0) {
		return false;
	}
	uint64_t ticks = counter.QuadPart;
	uint64_t frequencyValue = frequency.QuadPart;
	uint64_t seconds = ticks / frequencyValue;
	uint64_t remainder = ticks % frequencyValue;
	if (seconds > UINT64_MAX / UINT64_C(1000000) ||
	    remainder > UINT64_MAX / UINT64_C(1000000)) {
		return false;
	}
	*timestamp = seconds * UINT64_C(1000000) +
	             remainder * UINT64_C(1000000) / frequencyValue;
	return true;
#else
	struct timespec now;
	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec < 0 ||
	    (uint64_t) now.tv_sec > UINT64_MAX / UINT64_C(1000000)) {
		return false;
	}
	*timestamp = (uint64_t) now.tv_sec * UINT64_C(1000000) +
	             (uint64_t) now.tv_nsec / 1000;
	return true;
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
		if (_calibrationWirePacket(data, size)) {
			++adapter->metrics.calibrationSentPackets;
			adapter->metrics.calibrationSentBytes += size;
		}
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
	adapter->deferHostHello = false;
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
	.monotonicTimeUs = _monotonicTimeUs,
	.diagnostic = _diagnostic,
	.stop = _transportStop,
};

static bool _quiescentBoundary(void* context) {
	struct mLibretroNetpacketV2Adapter* adapter = context;
	if (!adapter || !adapter->gba || adapter->gba->sio.driver ||
	    adapter->deferHostHello) {
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
	if (!adapter->sessionConfig.deterministicCapabilities.contentRequiresRtc) {
		return GBAReplicaCapture(
		    adapter->core, player, generation, encoding, chunkSize, bundle);
	}
	struct GBALinkV2RTCNormalization normalization;
	enum GBALinkV2RTCResult rtcResult = GBALinkV2RTCNormalize(
	    &adapter->core->rtc, adapter->gba->video.frameCounter,
	    adapter->core->frameCycles(adapter->core),
	    adapter->core->frequency(adapter->core), &normalization);
	if (rtcResult != GBA_LINK_V2_RTC_OK ||
	    !GBALinkV2RTCApplyNormalized(
	        &adapter->core->rtc, &normalization)) {
		return GBA_REPLICA_UNSUPPORTED;
	}
	enum GBAReplicaResult result = GBAReplicaCapture(
	    adapter->core, player, generation, encoding, chunkSize, bundle);
	if (!GBALinkV2RTCRestoreOriginal(
	        &adapter->core->rtc, &normalization)) {
		GBAReplicaBundleDeinit(bundle);
		return GBA_REPLICA_RESTORE_FAILED;
	}
	if (result == GBA_REPLICA_OK) {
		adapter->rtcNormalization = normalization;
		adapter->rtcNormalizationValid = true;
	}
	return result;
}

static void _checkpointDeinit(
		struct mLibretroNetpacketV2Checkpoint* checkpoint) {
	if (!checkpoint) {
		return;
	}
	free(checkpoint->saveData);
	free(checkpoint->state);
	memset(checkpoint, 0, sizeof(*checkpoint));
}

static bool _captureLocalCheckpoint(
		struct mLibretroNetpacketV2Adapter* adapter,
		struct mCore* core, uint64_t frame, uint64_t saveGeneration) {
	if (!adapter || !core || !core->board || !adapter->saveData ||
	    !adapter->saveCapacity) {
		return false;
	}
	struct GBA* gba = core->board;
	struct mLibretroNetpacketV2Checkpoint replacement;
	memset(&replacement, 0, sizeof(replacement));
	replacement.saveType = gba->memory.savedata.type;
	replacement.saveSize = GBASavedataSize(&gba->memory.savedata);
	replacement.saveBackingSize = adapter->saveCapacity;
	if (replacement.saveSize > replacement.saveBackingSize) {
		return false;
	}
#ifdef M_LIBRETRO_NETPACKET_V2_TEST
	if (adapter->testFailCheckpointAllocation) {
		adapter->testFailCheckpointAllocation = false;
		return false;
	}
#endif
	replacement.state = calloc(1, sizeof(*replacement.state));
	replacement.saveData = malloc(replacement.saveBackingSize);
	if (!replacement.state || !replacement.saveData) {
		_checkpointDeinit(&replacement);
		return false;
	}
	GBASerialize(gba, replacement.state);
	memcpy(replacement.saveData, adapter->saveData,
	    replacement.saveBackingSize);
	replacement.rtcType = core->rtc.override;
	replacement.rtcValue = core->rtc.value;
	replacement.restoreRtcType = adapter->rtcNormalizationValid
	    ? adapter->rtcNormalization.originalType
	    : core->rtc.override;
	replacement.restoreRtcValue = adapter->rtcNormalizationValid
	    ? adapter->rtcNormalization.originalValue
	    : core->rtc.value;
	replacement.cartridgeRtcLastLatch = gba->memory.hw.rtc.lastLatch;
	replacement.cartridgeRtcOffset = gba->memory.hw.rtc.offset;
	replacement.frame = frame;
	replacement.saveGeneration = saveGeneration;
	replacement.valid = true;

	_checkpointDeinit(&adapter->verifiedCheckpoint);
	adapter->verifiedCheckpoint = replacement;
	return true;
}

static bool _installPair(
	void* context, const struct GBAReplicaManifest manifests[2],
	const struct GBAReplicaPayload payloads[2]) {
	struct mLibretroNetpacketV2Adapter* adapter = context;
	if (adapter->pairInitialized) {
		return false;
	}
	_checkpointDeinit(&adapter->verifiedCheckpoint);
	if (!_captureLocalCheckpoint(
	        adapter, adapter->core,
	        adapter->gba->video.frameCounter, 0)) {
		return false;
	}
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
	_checkpointDeinit(&adapter->verifiedCheckpoint);
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

static void _restoreVerifiedLocalState(
		struct mLibretroNetpacketV2Adapter* adapter) {
	if (!adapter || !adapter->verifiedCheckpoint.valid ||
	    !adapter->verifiedCheckpoint.state ||
	    !adapter->verifiedCheckpoint.saveData || !adapter->gba ||
	    adapter->gba->sio.driver || !adapter->saveData) {
		_log(RETRO_LOG_WARN,
		    "verified checkpoint unavailable during teardown");
		return;
	}
	if (adapter->verifiedCheckpoint.saveBackingSize !=
	        adapter->saveCapacity ||
	    adapter->verifiedCheckpoint.saveSize > adapter->saveCapacity ||
	    adapter->verifiedCheckpoint.state->savedata.type !=
	        (uint8_t) adapter->verifiedCheckpoint.saveType) {
		char message[192];
		snprintf(message, sizeof(message),
		    "verified checkpoint metadata is inconsistent "
		    "backing=%zu/%zu save=%zu type=%u/%u",
		    adapter->verifiedCheckpoint.saveBackingSize,
		    adapter->saveCapacity,
		    adapter->verifiedCheckpoint.saveSize,
		    (unsigned) adapter->verifiedCheckpoint.state->savedata.type,
		    (unsigned) adapter->verifiedCheckpoint.saveType);
		_log(RETRO_LOG_WARN, message);
		return;
	}
	if (!GBADeserialize(
	        adapter->gba, adapter->verifiedCheckpoint.state)) {
		_log(RETRO_LOG_WARN,
		    "verified local state restore failed; preserving frozen core");
		return;
	}
	if (adapter->gba->memory.savedata.type !=
	        adapter->verifiedCheckpoint.saveType ||
	    GBASavedataSize(&adapter->gba->memory.savedata) !=
	        adapter->verifiedCheckpoint.saveSize) {
		_log(RETRO_LOG_WARN,
		    "verified save controller restore mismatch; preserving save");
		return;
	}
	memcpy(adapter->saveData, adapter->verifiedCheckpoint.saveData,
	    adapter->saveCapacity);
	if (adapter->verifiedCheckpoint.saveSize &&
	    adapter->gba->memory.savedata.data &&
	    adapter->gba->memory.savedata.data != adapter->saveData) {
		memcpy(adapter->gba->memory.savedata.data,
		    adapter->verifiedCheckpoint.saveData,
		    adapter->verifiedCheckpoint.saveSize);
	}
	adapter->core->rtc.override =
	    adapter->verifiedCheckpoint.restoreRtcType;
	adapter->core->rtc.value =
	    adapter->verifiedCheckpoint.restoreRtcValue;
	adapter->gba->memory.hw.rtc.lastLatch =
	    adapter->verifiedCheckpoint.cartridgeRtcLastLatch;
	adapter->gba->memory.hw.rtc.offset =
	    adapter->verifiedCheckpoint.cartridgeRtcOffset;
	GBASIOMultiplayerMaterializeLines(
	    &adapter->gba->sio, 0, false, true, true);
	char message[160];
	snprintf(message, sizeof(message),
	    "restored verified local state frame=%" PRIu64
	    " save_generation=%" PRIu64,
	    adapter->verifiedCheckpoint.frame,
	    adapter->verifiedCheckpoint.saveGeneration);
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
	uint64_t waitFreePpm = adapter->metrics.releasedFrames &&
	                              adapter->metrics.inputWaitedFrames <=
	                                  adapter->metrics.releasedFrames
	    ? (adapter->metrics.releasedFrames -
	       adapter->metrics.inputWaitedFrames) * UINT64_C(1000000) /
	          adapter->metrics.releasedFrames
	    : 0;
	char inputTiming[192];
	snprintf(inputTiming, sizeof(inputTiming),
	    "%s input-wait P%u released=%" PRIu64 " waited=%" PRIu64
	    " wait-free-ppm=%" PRIu64,
	    event ? event : "runtime", adapter->session.localRole,
	    adapter->metrics.releasedFrames,
	    adapter->metrics.inputWaitedFrames, waitFreePpm);
	_log(RETRO_LOG_INFO, inputTiming);
	snprintf(inputTiming, sizeof(inputTiming),
	    "%s input-tail P%u p95=%" PRIu64 "us max=%" PRIu64
	    "us total=%" PRIu64 "us",
	    event ? event : "runtime", adapter->session.localRole,
	    _inputWaitPercentile(adapter, 95),
	    adapter->metrics.inputWaitMaxUs,
	    adapter->metrics.inputWaitTotalUs);
	_log(RETRO_LOG_INFO, inputTiming);
	snprintf(inputTiming, sizeof(inputTiming),
	    "%s input-health P%u deadline-miss=%" PRIu64
	    " clock-failure=%" PRIu64,
	    event ? event : "runtime", adapter->session.localRole,
	    adapter->metrics.inputDeadlineMisses,
	    adapter->metrics.telemetryClockFailures);
	_log(RETRO_LOG_INFO, inputTiming);
	snprintf(inputTiming, sizeof(inputTiming),
	    "%s poll-send P%u count=%" PRIu64 " avg=%" PRIu64
	    "us max=%" PRIu64 "us",
	    event ? event : "runtime", adapter->session.localRole,
	    adapter->metrics.inputPollSendCount,
	    adapter->metrics.inputPollSendCount
	        ? adapter->metrics.inputPollSendTotalUs /
	              adapter->metrics.inputPollSendCount
	        : 0,
	    adapter->metrics.inputPollSendMaxUs);
	_log(RETRO_LOG_INFO, inputTiming);
	snprintf(inputTiming, sizeof(inputTiming),
	    "%s calibration-traffic P%u packets=%" PRIu64 "/%" PRIu64
	    " bytes=%" PRIu64 "/%" PRIu64 " queue-high=%zu",
	    event ? event : "runtime", adapter->session.localRole,
	    adapter->metrics.calibrationSentPackets,
	    adapter->metrics.calibrationReceivedPackets,
	    adapter->metrics.calibrationSentBytes,
	    adapter->metrics.calibrationReceivedBytes,
	    adapter->metrics.queueHighWater);
	_log(RETRO_LOG_INFO, inputTiming);
	char inputLead[192];
	snprintf(inputLead, sizeof(inputLead),
	    "%s input-lead-frame P%u inserts=%" PRIu64 "/%" PRIu64
	    " frames-avg=%" PRIu64 "/%" PRIu64
	    " frames-max=%" PRIu64 "/%" PRIu64,
	    event ? event : "runtime", adapter->session.localRole,
	    adapter->metrics.inputInsertions[0],
	    adapter->metrics.inputInsertions[1],
	    adapter->metrics.inputInsertions[0]
	        ? adapter->metrics.inputLeadFramesTotal[0] /
	              adapter->metrics.inputInsertions[0]
	        : 0,
	    adapter->metrics.inputInsertions[1]
	        ? adapter->metrics.inputLeadFramesTotal[1] /
	              adapter->metrics.inputInsertions[1]
	        : 0,
	    adapter->metrics.inputLeadFramesMax[0],
	    adapter->metrics.inputLeadFramesMax[1]);
	_log(RETRO_LOG_INFO, inputLead);
	snprintf(inputLead, sizeof(inputLead),
	    "%s input-lead-time P%u us-avg=%" PRIu64 "/%" PRIu64
	    " us-max=%" PRIu64 "/%" PRIu64,
	    event ? event : "runtime", adapter->session.localRole,
	    adapter->metrics.inputInsertions[0]
	        ? adapter->metrics.inputLeadUsTotal[0] /
	              adapter->metrics.inputInsertions[0]
	        : 0,
	    adapter->metrics.inputInsertions[1]
	        ? adapter->metrics.inputLeadUsTotal[1] /
	              adapter->metrics.inputInsertions[1]
	        : 0,
	    adapter->metrics.inputLeadUsMax[0],
	    adapter->metrics.inputLeadUsMax[1]);
	_log(RETRO_LOG_INFO, inputLead);
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
	adapter->rtcNormalizationValid = false;
	memset(&adapter->rtcNormalization, 0, sizeof(adapter->rtcNormalization));
	_checkpointDeinit(&adapter->verifiedCheckpoint);
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
		struct GBAReplicatedPairMetrics metrics;
		if (GBAReplicatedPairGetMetrics(
		        &adapter->pair, &metrics)) {
			adapter->localSaveGeneration =
			    metrics.saveGenerations[localPlayer];
			if (!_captureLocalCheckpoint(
			        adapter,
			        GBAReplicatedPairCore(
			            &adapter->pair, localPlayer),
			        adapter->verificationFrame,
			        adapter->localSaveGeneration)) {
				_log(RETRO_LOG_WARN,
				    "verified checkpoint allocation failed; retaining prior checkpoint");
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
	_recordInputInsertions(adapter, &packet->payload.inputBatch);
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
	if (reason == GBA_LINK_V2_REASON_INPUT_TIMEOUT) {
		++adapter->metrics.inputDeadlineMisses;
	}
	char detail[224];
	size_t detailLength = GBALinkV2SessionFormatFailureDetail(
	    &adapter->session, reason, detail, sizeof(detail));
	char message[480];
	snprintf(message, sizeof(message),
	    "protocol-v2 session failed: reason=%u state=%s frame=%" PRIu64
	    "%s%s",
	    reason, GBALinkV2SessionStateName(adapter->session.state),
	    adapter->runtimeInitialized ? adapter->runtime.input.nextFrame : 0,
	    detailLength ? " " : "", detailLength ? detail : "");
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

static enum GBALinkV2IdlePolicy _idlePolicy(const struct GBA* gba) {
	switch (gba->idleOptimization) {
	case IDLE_LOOP_REMOVE:
		return GBA_LINK_V2_IDLE_REMOVE;
	case IDLE_LOOP_DETECT:
		return GBA_LINK_V2_IDLE_DETECT;
	case IDLE_LOOP_IGNORE:
	default:
		return GBA_LINK_V2_IDLE_NONE;
	}
}

static enum GBALinkV2ProductPolicy _productPolicy(
	struct mLibretroNetpacketV2Adapter* adapter) {
	struct retro_variable variable = {
		.key = "mgba_link_netplay_latency",
	};
	if (adapter->environment && adapter->environment(
	        RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value &&
	    !strcmp(variable.value, "low_latency")) {
		return GBA_LINK_V2_PRODUCT_LOW_LATENCY;
	}
	return GBA_LINK_V2_PRODUCT_STABLE;
}

static bool _manualSolarControl(
		struct mLibretroNetpacketV2Adapter* adapter) {
	if (!adapter || !adapter->gba ||
	    !(adapter->gba->memory.hw.devices & HW_LIGHT_SENSOR)) {
		return false;
	}
	struct retro_variable variable = {
		.key = "mgba_solar_sensor_level",
	};
	return adapter->environment && adapter->environment(
	    RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value &&
	    strcmp(variable.value, "sensor");
}

static bool _buildConfig(
	struct mLibretroNetpacketV2Adapter* adapter) {
	memset(&adapter->sessionConfig, 0, sizeof(adapter->sessionConfig));
	if (adapter->gba->memory.hw.devices & HW_EREADER) {
		const char* message =
		    "e-Reader cartridge data is not synchronized by GBA link netplay";
		_log(RETRO_LOG_ERROR, message);
		_frontendMessage(RETRO_LOG_ERROR, message);
		return false;
	}
	if (_cheatsEnabled(adapter->core) ||
	    !GBALinkContentIdentityFromCore(
	        adapter->core, &adapter->sessionConfig.identity)) {
		return false;
	}
	struct GBALinkV2DeterminismProfileInput profile = {
		.biosMode = adapter->gba->biosVf
		    ? GBA_LINK_V2_BIOS_EXTERNAL
		    : GBA_LINK_V2_BIOS_HLE,
		.emulationCompatibilityVersion =
		    GBA_REPLICA_EMULATION_COMPATIBILITY_VERSION,
		.timingModelFlags =
		    (adapter->core->opts.skipBios ? 1U : 0U) |
		    (adapter->core->opts.useBios ? 2U : 0U),
		.overclockQ16 = 0x10000,
		.speedHackMask = 0,
		.idlePolicy = _idlePolicy(adapter->gba),
		.allowOpposingDirections = adapter->gba->allowOpposingDirections,
		.rtcNormalizationPolicyVersion = 1,
		.fakeEpochArithmeticVersion = 1,
		.rtcSemanticsModelVersion = 1,
		.cheatsEnabled = false,
		.authoritativeInputFormatVersion = 1,
		.cartridgeRequiredInputMask =
		    GBALinkV2RequiredInputMaskForHardware(
		        adapter->gba->memory.hw.devices,
		        _manualSolarControl(adapter)),
	};
	if (profile.biosMode == GBA_LINK_V2_BIOS_EXTERNAL) {
		sha256Buffer(adapter->gba->memory.bios, GBA_SIZE_BIOS,
		    profile.biosSha256);
	}
	if (!GBALinkV2DeterminismProfileBuild(
	        &profile, &adapter->sessionConfig.determinismProfile)) {
		return false;
	}
	adapter->sessionConfig.cartridgeRequiredInputMask =
	    profile.cartridgeRequiredInputMask;
	adapter->sessionConfig.deterministicCapabilities =
	    (struct GBALinkV2DeterminismCapabilities) {
		.supportedRtcSourceMask = GBALinkV2SupportedRTCSourceMask(),
		.timeSemanticsCapabilityMask = GBALinkV2HasSigned64BitTimeT()
		    ? GBA_LINK_V2_TIME_SIGNED_64BIT_TIME_T_V1
		    : 0,
		.authoritativePlayerRtcSource = adapter->core->rtc.override,
		.contentRequiresRtc =
		    (adapter->gba->memory.hw.devices & HW_RTC) != 0,
		.synchronizedInputCapabilityMask = GBA_LINK_V2_INPUT_DIGITAL,
	};
	adapter->sessionConfig.capabilities =
	    GBA_LINK_V2_REQUIRED_CAPABILITIES;
	adapter->sessionConfig.supportedEncodings =
	    GBA_LINK_V2_ENCODING_NONE;
	adapter->sessionConfig.emulationCompatibilityVersion =
	    GBA_REPLICA_EMULATION_COMPATIBILITY_VERSION;
	adapter->sessionConfig.maxChunkSize =
	    GBA_REPLICA_DEFAULT_CHUNK_SIZE;
	adapter->sessionConfig.productPolicy = _productPolicy(adapter);
	adapter->sessionConfig.minimumInputDelay =
	    adapter->sessionConfig.productPolicy ==
	            GBA_LINK_V2_PRODUCT_LOW_LATENCY
	        ? GBA_LINK_INPUT_LOW_LATENCY_FLOOR
	        : GBA_LINK_INPUT_STABLE_FLOOR;
	adapter->sessionConfig.maximumInputDelay = 8;
	adapter->sessionConfig.estimatedJitterMs = 5;
	adapter->sessionConfig.experimentalRuntime = true;
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
	if (_calibrationWirePacket(data, size)) {
		++_adapter.metrics.calibrationReceivedPackets;
		_adapter.metrics.calibrationReceivedBytes += size;
	}
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
	/* RetroArch invokes connected() before the joining peer is fully
	 * admitted for host-to-client packets. Start the bounded session now,
	 * but defer a quiescent HELLO until the next ordinary run boundary. */
	_adapter.deferHostHello = true;
	bool entered = _enterRendezvous(&_adapter);
	_adapter.deferHostHello = false;
	if (!entered) {
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
	uint64_t monotonicProbe;
	if (!environment || !core || !core->platform ||
	    core->platform(core) != mPLATFORM_GBA || !core->board ||
	    !saveData || saveCapacity < GBA_SIZE_FLASH1M ||
	    !_monotonicTimeUs(NULL, &monotonicProbe)) {
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

static bool _profileIdentityText(
		const struct GBALinkV2DeterminismProfile* profile,
		char text[MGBA_SHA256_DIGEST_SIZE * 2 + 1]) {
	uint8_t encoded[GBA_LINK_V2_PROFILE_MAX_ENCODED_SIZE];
	size_t size = 0;
	uint8_t digest[MGBA_SHA256_DIGEST_SIZE];
	if (!GBALinkV2DeterminismProfileEncode(
	        profile, encoded, sizeof(encoded), &size)) {
		return false;
	}
	sha256Buffer(encoded, size, digest);
	_digestText(digest, text);
	return true;
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
		    "attach P%u policy=%u delay=%u calibration=%" PRIu64
		    "ms provisional=%" PRIu64 " generation=%" PRIu64,
		    _adapter.localId, _adapter.session.productPolicy,
		    _adapter.session.inputDelay,
		    _adapter.metrics.readyAtMs -
		        _adapter.metrics.startedAtMs,
		    _adapter.session.sessionId,
		    _adapter.session.calibration.generation);
		_log(RETRO_LOG_INFO, message);
		char digest[MGBA_SHA256_DIGEST_SIZE * 2 + 1];
		_digestText(_adapter.session.selection.digest, digest);
		char calibration[192];
		snprintf(calibration, sizeof(calibration),
		    "calibration P%u provisional=%" PRIu64
		    " generation=%" PRIu64 " samples=%u",
		    _adapter.localId, _adapter.session.sessionId,
		    _adapter.session.calibration.generation,
		    GBA_LINK_CALIBRATION_SAMPLE_COUNT);
		_log(RETRO_LOG_INFO, calibration);
		snprintf(calibration, sizeof(calibration),
		    "cal-rtt P%u s=%" PRIu64 " min=%" PRIu32 "us p50=%" PRIu32
		    "us p95=%" PRIu32 "us max=%" PRIu32 "us",
		    _adapter.localId, _adapter.session.sessionId,
		    _adapter.session.selection.minimumRttUs,
		    _adapter.session.selection.p50RttUs,
		    _adapter.session.selection.p95RttUs,
		    _adapter.session.selection.maximumRttUs);
		_log(RETRO_LOG_INFO, calibration);
		snprintf(calibration, sizeof(calibration),
		    "cal-select P%u s=%" PRIu64 " selector=%u floor=%u range=%u-%u"
		    " delay=%u reason=%u",
		    _adapter.localId, _adapter.session.sessionId,
		    GBA_LINK_INPUT_SELECTOR_POLICY_VERSION,
		    _adapter.session.selection.productionFloor,
		    _adapter.session.selection.overlappingMinimum,
		    _adapter.session.selection.overlappingMaximum,
		    _adapter.session.selection.selectedDelay,
		    _adapter.session.selection.reason);
		_log(RETRO_LOG_INFO, calibration);
		snprintf(calibration, sizeof(calibration),
		    "cal-digest-a P%u s=%" PRIu64 " d=%.32s",
		    _adapter.localId, _adapter.session.sessionId, digest);
		_log(RETRO_LOG_INFO, calibration);
		snprintf(calibration, sizeof(calibration),
		    "cal-digest-b P%u s=%" PRIu64 " d=%.32s",
		    _adapter.localId, _adapter.session.sessionId, digest + 32);
		_log(RETRO_LOG_INFO, calibration);
		char profileDigest[MGBA_SHA256_DIGEST_SIZE * 2 + 1];
		if (_profileIdentityText(
		        &_adapter.session.config.determinismProfile,
		        profileDigest)) {
			char deterministic[320];
			snprintf(deterministic, sizeof(deterministic),
			    "determinism P%u profile=%s schema=%u records=%u"
			    " rtc-source=%u rtc-sources=%08" PRIx32
			    " time=%08" PRIx32 " required-input=%016" PRIx64
			    " synchronized-input=%016" PRIx64,
			    _adapter.localId, profileDigest,
			    _adapter.session.config.determinismProfile.schemaVersion,
			    _adapter.session.config.determinismProfile.recordCount,
			    _adapter.session.config.deterministicCapabilities
			        .authoritativePlayerRtcSource,
			    _adapter.session.config.deterministicCapabilities
			        .supportedRtcSourceMask,
			    _adapter.session.config.deterministicCapabilities
			        .timeSemanticsCapabilityMask,
			    _adapter.session.config.cartridgeRequiredInputMask,
			    _adapter.session.config.deterministicCapabilities
			        .synchronizedInputCapabilityMask);
			_log(RETRO_LOG_INFO, deterministic);
		}
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
	uint64_t inputSampledAtUs = 0;
	bool inputSampleTimeValid =
	    _monotonicTimeUs(&_adapter, &inputSampledAtUs);
	if (!inputSampleTimeValid) {
		++_adapter.metrics.telemetryClockFailures;
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
			_recordInputInsertions(
			    &_adapter, &packets[i].payload.inputBatch);
		}
		uint64_t sentAtUs;
		if (inputSampleTimeValid &&
		    _monotonicTimeUs(&_adapter, &sentAtUs) &&
		    sentAtUs >= inputSampledAtUs) {
			uint64_t duration = sentAtUs - inputSampledAtUs;
			++_adapter.metrics.inputPollSendCount;
			_adapter.metrics.inputPollSendTotalUs += duration;
			if (duration > _adapter.metrics.inputPollSendMaxUs) {
				_adapter.metrics.inputPollSendMaxUs = duration;
			}
		} else {
			++_adapter.metrics.telemetryClockFailures;
		}
	}
	waited = !GBAReplicatedRuntimeFrameReady(&_adapter.runtime);
	uint64_t inputWaitStartedUs = 0;
	bool inputWaitClockValid = !waited ||
	    _monotonicTimeUs(&_adapter, &inputWaitStartedUs);
	if (waited && !inputWaitClockValid) {
		++_adapter.metrics.telemetryClockFailures;
	}
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
		uint64_t completedAtUs;
		if (inputWaitClockValid &&
		    _monotonicTimeUs(&_adapter, &completedAtUs) &&
		    completedAtUs >= inputWaitStartedUs) {
			uint64_t durationUs = completedAtUs - inputWaitStartedUs;
			_recordInputWait(&_adapter, durationUs);
			_recordRendezvous(&_adapter, (durationUs + 999) / 1000);
		} else {
			++_adapter.metrics.telemetryClockFailures;
		}
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
	++_adapter.metrics.releasedFrames;
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
	_checkpointDeinit(&_adapter.verifiedCheckpoint);
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

bool mLibretroNetpacketV2RejectLatencyPolicyChange(const char* value) {
	if (!mLibretroNetpacketV2SessionActive() || !value ||
	    !_adapter.sessionPrepared) {
		return false;
	}
	enum GBALinkV2ProductPolicy requested =
	    !strcmp(value, "low_latency")
	        ? GBA_LINK_V2_PRODUCT_LOW_LATENCY
	        : GBA_LINK_V2_PRODUCT_STABLE;
	if (requested == _adapter.sessionConfig.productPolicy) {
		return false;
	}
	return mLibretroNetpacketV2RejectOperation(
	    "Changing latency policy");
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

bool mLibretroNetpacketV2TestCaptureCheckpoint(uint64_t frame) {
	if (!_adapter.runtimeInitialized || !_adapter.pairInitialized) {
		return false;
	}
	uint8_t localPlayer = _playerForRole(_adapter.session.localRole);
	struct GBAReplicatedPairMetrics metrics;
	if (!GBAReplicatedPairGetMetrics(&_adapter.pair, &metrics)) {
		return false;
	}
	_adapter.localSaveGeneration = metrics.saveGenerations[localPlayer];
	return _captureLocalCheckpoint(
	    &_adapter, GBAReplicatedPairCore(&_adapter.pair, localPlayer),
	    frame, _adapter.localSaveGeneration);
}

void mLibretroNetpacketV2TestFailNextCheckpointAllocation(void) {
	_adapter.testFailCheckpointAllocation = true;
}

void mLibretroNetpacketV2TestFail(enum GBALinkV2Reason reason) {
	if (_adapter.sessionPrepared &&
	    GBALinkV2SessionIsLive(&_adapter.session)) {
		GBALinkV2SessionFail(
		    &_adapter.session, reason, "injected test teardown");
		_finishFailed();
	}
}

bool mLibretroNetpacketV2TestGetMetrics(
		struct mLibretroNetpacketV2TestMetrics* metrics) {
	if (!metrics) {
		return false;
	}
	struct GBAReplicatedPairMetrics pairMetrics;
	memset(&pairMetrics, 0, sizeof(pairMetrics));
	if (_adapter.pairInitialized) {
		GBAReplicatedPairGetMetrics(&_adapter.pair, &pairMetrics);
	}
	*metrics = (struct mLibretroNetpacketV2TestMetrics) {
		.selectedDelay = _adapter.session.inputDelay,
		.productPolicy = _adapter.session.productPolicy,
		.releasedFrames = _adapter.metrics.releasedFrames,
		.inputWaitedFrames = _adapter.metrics.inputWaitedFrames,
		.inputWaitP95Us = _inputWaitPercentile(&_adapter, 95),
		.inputWaitMaxUs = _adapter.metrics.inputWaitMaxUs,
		.inputDeadlineMisses = _adapter.metrics.inputDeadlineMisses,
		.telemetryClockFailures = _adapter.metrics.telemetryClockFailures,
		.inputPollSendCount = _adapter.metrics.inputPollSendCount,
		.inputInsertions = {
			_adapter.metrics.inputInsertions[0],
			_adapter.metrics.inputInsertions[1],
		},
		.cableTransferStarts = pairMetrics.transferStarts,
		.cableTransferCompletions = pairMetrics.transferCompletions,
		.cableTransferredWords = pairMetrics.transferredWords,
	};
	return true;
}
#endif
