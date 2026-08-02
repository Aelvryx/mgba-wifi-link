/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "util/test/suite.h"

#include "../libretro/libretro.h"
#include "../libretro/gba-wifi-link.h"

#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/gba/core.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/sio/netplay/protocol-v2.h>
#include <mgba-util/threading.h>
#include <mgba-util/vfs.h>

#include <fcntl.h>

#ifndef GBA_LINK_CONTINUOUS_ROM_PATH
#error "GBA_LINK_CONTINUOUS_ROM_PATH must name the continuous link-test ROM"
#endif

#define REPLAY_WIRE_CAPACITY 256
#define REPLAY_ATTACH_LIMIT 4096
#define REPLAY_ROM_SIZE 0x40001
#define REPLAY_POLLS_PER_CLOCK_MILLISECOND 8

#define DECLARE_REPLAY_ADAPTER(PREFIX) \
	bool PREFIX##Register(retro_environment_t environment, \
	    struct mCore* core, void* saveData, size_t saveCapacity); \
	void PREFIX##RunBegin(void); \
	bool PREFIX##RunFrame(uint16_t keys); \
	bool PREFIX##OwnsExecution(void); \
	struct mCore* PREFIX##PresentedCore(void); \
	void PREFIX##Reset(void); \
	void PREFIX##Unload(void); \
	bool PREFIX##SessionActive(void); \
	void PREFIX##TestSetTimeMs(uint64_t nowMs); \
	struct mCore* PREFIX##TestPairCore(uint8_t player); \
	bool PREFIX##TestGetMetrics( \
	    struct mLibretroGBAWifiLinkTestMetrics* metrics)

DECLARE_REPLAY_ADAPTER(mLibretroGBAWifiLinkReplayHost);
DECLARE_REPLAY_ADAPTER(mLibretroGBAWifiLinkReplayClient);

enum ReplayEndpointId {
	REPLAY_HOST,
	REPLAY_CLIENT,
	REPLAY_ENDPOINT_COUNT,
};

struct ReplayWirePacket {
	uint8_t* data;
	size_t size;
	uint64_t dueAt;
};

struct ReplayWireQueue {
	struct ReplayWirePacket packets[REPLAY_WIRE_CAPACITY];
	size_t readIndex;
	size_t size;
	uint64_t lastDueAt;
};

struct ReplayFrontend {
	const struct retro_netpacket_callback* callbacks;
	const char* latencyPolicy;
};

struct ReplayNetwork {
	Mutex mutex;
	uint64_t now;
	uint64_t sentPackets;
	uint64_t deliveredPackets;
	uint64_t typeCounts[GBA_LINK_V2_MESSAGE_LATENCY_REPORT + 1];
	uint64_t jitterBase;
	struct ReplayWireQueue inbound[REPLAY_ENDPOINT_COUNT];
	enum GBALinkV2MessageType dropType;
	enum ReplayEndpointId dropSender;
	unsigned dropsRemaining;
	bool failed;
};

struct ReplayFixture {
	struct mCore* cores[REPLAY_ENDPOINT_COUNT];
	uint8_t* rom;
	uint8_t* saves[REPLAY_ENDPOINT_COUNT];
	struct ReplayFrontend frontends[REPLAY_ENDPOINT_COUNT];
	struct ReplayNetwork network;
};

struct ReplayFrameCall {
	uint16_t keys;
	bool result;
};

static struct ReplayFixture* _fixture;
static struct mLogger _silentLogger;

static void _discardLog(
		struct mLogger* logger, int category,
		enum mLogLevel level, const char* format, va_list args) {
	UNUSED(logger);
	UNUSED(category);
	UNUSED(level);
	UNUSED(format);
	UNUSED(args);
}

M_TEST_SUITE_SETUP(LibretroGBAWifiLinkReplay) {
	_silentLogger.log = _discardLog;
	mLogSetDefaultLogger(&_silentLogger);
	return 0;
}

M_TEST_SUITE_TEARDOWN(LibretroGBAWifiLinkReplay) {
	mLogSetDefaultLogger(NULL);
	return 0;
}

static struct ReplayWireQueue* _queueFor(
		enum ReplayEndpointId receiver) {
	return &_fixture->network.inbound[receiver];
}

static bool _environment(
		enum ReplayEndpointId endpoint, unsigned command, void* data) {
	if (command == RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE) {
		_fixture->frontends[endpoint].callbacks = data;
		return true;
	}
	if (command == RETRO_ENVIRONMENT_SET_MESSAGE_EXT ||
	    command == RETRO_ENVIRONMENT_SET_MESSAGE) {
		return true;
	}
	if (command == RETRO_ENVIRONMENT_GET_VARIABLE) {
		struct retro_variable* variable = data;
		if (!strcmp(variable->key, "mgba_link_netplay_latency")) {
			variable->value =
			    _fixture->frontends[endpoint].latencyPolicy;
			return variable->value != NULL;
		}
		return false;
	}
	return false;
}

static bool RETRO_CALLCONV _hostEnvironment(
		unsigned command, void* data) {
	return _environment(REPLAY_HOST, command, data);
}

static bool RETRO_CALLCONV _clientEnvironment(
		unsigned command, void* data) {
	return _environment(REPLAY_CLIENT, command, data);
}

static void _setEndpointTime(
		enum ReplayEndpointId endpoint, uint64_t now) {
	if (endpoint == REPLAY_HOST) {
		mLibretroGBAWifiLinkReplayHostTestSetTimeMs(now);
	} else {
		mLibretroGBAWifiLinkReplayClientTestSetTimeMs(now);
	}
}

static void _wireSend(
		enum ReplayEndpointId sender, const void* data, size_t size,
		uint16_t clientId) {
	struct ReplayNetwork* network = &_fixture->network;
	enum ReplayEndpointId receiver = sender ^ 1;
	if ((sender == REPLAY_HOST && clientId != 1) ||
	    (sender == REPLAY_CLIENT && clientId != 0)) {
		MutexLock(&network->mutex);
		network->failed = true;
		MutexUnlock(&network->mutex);
		return;
	}
	struct GBALinkV2Packet decoded;
	enum GBALinkRole senderRole = sender == REPLAY_HOST
	    ? GBA_LINK_ROLE_HOST : GBA_LINK_ROLE_CLIENT;
	if (GBALinkV2PacketDecode(
	        data, size, senderRole, &decoded) != GBA_LINK_DECODE_OK) {
		MutexLock(&network->mutex);
		network->failed = true;
		MutexUnlock(&network->mutex);
		return;
	}

	MutexLock(&network->mutex);
	++network->sentPackets;
	++network->typeCounts[decoded.header.type];
	if (network->dropsRemaining && sender == network->dropSender &&
	    decoded.header.type == network->dropType) {
		--network->dropsRemaining;
		MutexUnlock(&network->mutex);
		return;
	}
	struct ReplayWireQueue* queue = _queueFor(receiver);
	if (queue->size == REPLAY_WIRE_CAPACITY ||
	    size > GBA_LINK_V2_MAX_PACKET_SIZE) {
		network->failed = true;
		MutexUnlock(&network->mutex);
		return;
	}
	uint8_t* copy = malloc(size);
	if (!copy) {
		network->failed = true;
		MutexUnlock(&network->mutex);
		return;
	}
	memcpy(copy, data, size);
	uint64_t jitter = network->jitterBase + network->sentPackets % 5;
	uint64_t dueAt = network->now + jitter;
	if (dueAt <= queue->lastDueAt) {
		dueAt = queue->lastDueAt + 1;
	}
	queue->lastDueAt = dueAt;
	size_t index = (queue->readIndex + queue->size) %
	               REPLAY_WIRE_CAPACITY;
	queue->packets[index] = (struct ReplayWirePacket) {
		.data = copy,
		.size = size,
		.dueAt = dueAt,
	};
	++queue->size;
	MutexUnlock(&network->mutex);
}

static void RETRO_CALLCONV _hostSend(
		int flags, const void* data, size_t size, uint16_t clientId) {
	assert_true(flags & RETRO_NETPACKET_RELIABLE);
	_wireSend(REPLAY_HOST, data, size, clientId);
}

static void RETRO_CALLCONV _clientSend(
		int flags, const void* data, size_t size, uint16_t clientId) {
	assert_true(flags & RETRO_NETPACKET_RELIABLE);
	_wireSend(REPLAY_CLIENT, data, size, clientId);
}

static void _wirePoll(enum ReplayEndpointId receiver) {
	struct ReplayNetwork* network = &_fixture->network;
	struct ReplayWirePacket packet;
	memset(&packet, 0, sizeof(packet));
	MutexLock(&network->mutex);
	++network->now;
	uint64_t now = network->now;
	struct ReplayWireQueue* queue = _queueFor(receiver);
	if (queue->size &&
	    queue->packets[queue->readIndex].dueAt <= now) {
		packet = queue->packets[queue->readIndex];
		memset(&queue->packets[queue->readIndex], 0,
		    sizeof(queue->packets[queue->readIndex]));
		queue->readIndex = (queue->readIndex + 1) %
		                   REPLAY_WIRE_CAPACITY;
		--queue->size;
		++network->deliveredPackets;
	}
	MutexUnlock(&network->mutex);
	/* Delivery ticks are intentionally finer than the injected monotonic
	 * clock. A busy receive loop must not consume a three-second deadline
	 * merely because its peer thread has not yet been scheduled by a parallel
	 * test runner. */
	_setEndpointTime(
	    receiver, now / REPLAY_POLLS_PER_CLOCK_MILLISECOND);
	if (!packet.data) {
		return;
	}
	const struct retro_netpacket_callback* callbacks =
	    _fixture->frontends[receiver].callbacks;
	assert_non_null(callbacks);
	callbacks->receive(
	    packet.data, packet.size,
	    receiver == REPLAY_HOST ? 1 : 0);
	free(packet.data);
}

static void RETRO_CALLCONV _hostPoll(void) {
	_wirePoll(REPLAY_HOST);
}

static void RETRO_CALLCONV _clientPoll(void) {
	_wirePoll(REPLAY_CLIENT);
}

static struct mCore* _createCore(uint8_t* rom) {
	struct mCore* core = GBACoreCreate();
	assert_non_null(core);
	assert_true(core->init(core));
	mCoreInitConfig(core, NULL);
	assert_true(core->loadROM(
	    core, VFileFromMemory(rom, REPLAY_ROM_SIZE)));
	core->reset(core);
	return core;
}

static void _clearWire(struct ReplayNetwork* network) {
	for (unsigned endpoint = 0; endpoint < REPLAY_ENDPOINT_COUNT;
	     ++endpoint) {
		struct ReplayWireQueue* queue = &network->inbound[endpoint];
		while (queue->size) {
			free(queue->packets[queue->readIndex].data);
			memset(&queue->packets[queue->readIndex], 0,
			    sizeof(queue->packets[queue->readIndex]));
			queue->readIndex = (queue->readIndex + 1) %
			                   REPLAY_WIRE_CAPACITY;
			--queue->size;
		}
	}
}

static int _setupFixture(void** state, bool rtcSafe) {
	struct ReplayFixture* fixture = calloc(1, sizeof(*fixture));
	assert_non_null(fixture);
	assert_int_equal(MutexInit(&fixture->network.mutex), 0);
	fixture->network.jitterBase = 1;
	_fixture = fixture;
	fixture->rom = malloc(REPLAY_ROM_SIZE);
	assert_non_null(fixture->rom);
	memset(fixture->rom, 0xFF, REPLAY_ROM_SIZE);
	const char* replayRomPath = getenv("GBA_LINK_REPLAY_ROM_PATH");
	if (!replayRomPath || !replayRomPath[0]) {
		replayRomPath = GBA_LINK_CONTINUOUS_ROM_PATH;
	}
	struct VFile* fixtureRom = VFileOpen(replayRomPath, O_RDONLY);
	assert_non_null(fixtureRom);
	ssize_t fixtureSize = fixtureRom->size(fixtureRom);
	assert_true(fixtureSize > 0);
	assert_true((size_t) fixtureSize < REPLAY_ROM_SIZE);
	assert_int_equal(fixtureRom->read(
	    fixtureRom, fixture->rom, fixtureSize), fixtureSize);
	assert_true(fixtureRom->close(fixtureRom));
	if (rtcSafe) {
		/* The link fixture's tiny entry routine overlaps the GBA cartridge
		 * GPIO window. Use a ROM-resident idle loop for the RTC integration
		 * run so instruction fetches cannot become GPIO reads. */
		static const uint8_t branchToIdle[] = {
			0x7E, 0x00, 0x00, 0xEA,
		};
		static const uint8_t idleLoop[] = {
			0xFE, 0xFF, 0xFF, 0xEA,
		};
		memcpy(fixture->rom, branchToIdle, sizeof(branchToIdle));
		memcpy(fixture->rom + 0x200, idleLoop, sizeof(idleLoop));
	}
	for (unsigned endpoint = 0; endpoint < REPLAY_ENDPOINT_COUNT;
	     ++endpoint) {
		fixture->frontends[endpoint].latencyPolicy = "stable";
		fixture->cores[endpoint] = _createCore(fixture->rom);
		fixture->saves[endpoint] = malloc(GBA_SIZE_FLASH1M);
		assert_non_null(fixture->saves[endpoint]);
		memset(fixture->saves[endpoint], 0xFF, GBA_SIZE_FLASH1M);
		assert_true(fixture->cores[endpoint]->loadSave(
		    fixture->cores[endpoint], VFileFromMemory(
		        fixture->saves[endpoint], GBA_SIZE_FLASH1M)));
	}
	assert_true(mLibretroGBAWifiLinkReplayHostRegister(
	    _hostEnvironment, fixture->cores[REPLAY_HOST],
	    fixture->saves[REPLAY_HOST], GBA_SIZE_FLASH1M));
	assert_true(mLibretroGBAWifiLinkReplayClientRegister(
	    _clientEnvironment, fixture->cores[REPLAY_CLIENT],
	    fixture->saves[REPLAY_CLIENT], GBA_SIZE_FLASH1M));
	mLibretroGBAWifiLinkReplayHostTestSetTimeMs(0);
	mLibretroGBAWifiLinkReplayClientTestSetTimeMs(0);
	assert_non_null(fixture->frontends[REPLAY_HOST].callbacks);
	assert_non_null(fixture->frontends[REPLAY_CLIENT].callbacks);
	assert_string_equal(
	    fixture->frontends[REPLAY_HOST].callbacks->protocol_version,
	    GBA_LINK_V2_PROTOCOL_NAME);
	assert_string_equal(
	    fixture->frontends[REPLAY_CLIENT].callbacks->protocol_version,
	    GBA_LINK_V2_PROTOCOL_NAME);
	*state = fixture;
	return 0;
}

static int _setup(void** state) {
	return _setupFixture(state, false);
}

static int _setupRtc(void** state) {
	return _setupFixture(state, true);
}

static int _teardown(void** state) {
	struct ReplayFixture* fixture = *state;
	mLibretroGBAWifiLinkReplayClientUnload();
	mLibretroGBAWifiLinkReplayHostUnload();
	MutexLock(&fixture->network.mutex);
	_clearWire(&fixture->network);
	MutexUnlock(&fixture->network.mutex);
	for (unsigned endpoint = 0; endpoint < REPLAY_ENDPOINT_COUNT;
	     ++endpoint) {
		mCoreConfigDeinit(&fixture->cores[endpoint]->config);
		fixture->cores[endpoint]->deinit(fixture->cores[endpoint]);
		free(fixture->saves[endpoint]);
	}
	free(fixture->rom);
	MutexDeinit(&fixture->network.mutex);
	free(fixture);
	_fixture = NULL;
	return 0;
}

static void _beginFrontendSession(void) {
	const struct retro_netpacket_callback* host =
	    _fixture->frontends[REPLAY_HOST].callbacks;
	const struct retro_netpacket_callback* client =
	    _fixture->frontends[REPLAY_CLIENT].callbacks;
	host->start(0, _hostSend, _hostPoll);
	client->start(1, _clientSend, _clientPoll);
	assert_true(host->connected(1));
}

static bool _bothReady(void) {
	return mLibretroGBAWifiLinkReplayHostOwnsExecution() &&
	       mLibretroGBAWifiLinkReplayClientOwnsExecution();
}

static void _pumpAttachment(unsigned limit) {
	for (unsigned iteration = 0;
	     iteration < limit && !_bothReady(); ++iteration) {
		_hostPoll();
		_clientPoll();
		mLibretroGBAWifiLinkReplayHostRunBegin();
		mLibretroGBAWifiLinkReplayClientRunBegin();
	}
}

static void _attach(void) {
	_beginFrontendSession();
	_pumpAttachment(REPLAY_ATTACH_LIMIT);
	assert_true(_bothReady());
	assert_false(_fixture->network.failed);
	assert_true(_fixture->network.typeCounts[
	    GBA_LINK_V2_MESSAGE_REPLICA_CHUNK] > 2);
}

static THREAD_ENTRY _hostFrame(void* context) {
	struct ReplayFrameCall* call = context;
	call->result = mLibretroGBAWifiLinkReplayHostRunFrame(
	    call->keys);
	THREAD_EXIT(NULL);
}

static bool _runPairedFrame(uint16_t hostKeys, uint16_t clientKeys) {
	struct ReplayFrameCall host = { .keys = hostKeys };
	Thread thread;
	assert_int_equal(ThreadCreate(&thread, _hostFrame, &host), 0);
	bool client = mLibretroGBAWifiLinkReplayClientRunFrame(clientKeys);
	assert_int_equal(ThreadJoin(&thread), 0);
	return host.result && client;
}

static void _setDrop(
		enum ReplayEndpointId sender,
		enum GBALinkV2MessageType type, unsigned count) {
	MutexLock(&_fixture->network.mutex);
	_fixture->network.dropSender = sender;
	_fixture->network.dropType = type;
	_fixture->network.dropsRemaining = count;
	MutexUnlock(&_fixture->network.mutex);
}

static void _assertReplicasMatch(void) {
	for (unsigned player = 0; player < 2; ++player) {
		struct mCore* host =
		    mLibretroGBAWifiLinkReplayHostTestPairCore(player);
		struct mCore* client =
		    mLibretroGBAWifiLinkReplayClientTestPairCore(player);
		assert_non_null(host);
		assert_non_null(client);
		assert_int_equal(host->frameCounter(host),
		    client->frameCounter(client));
		assert_int_equal(host->rawRead32(
		        host, 0x02000010, -1),
		    client->rawRead32(client, 0x02000010, -1));
	}
}

static void _assertCableWorkRemainsLocal(void) {
	struct mLibretroGBAWifiLinkTestMetrics hostMetrics;
	struct mLibretroGBAWifiLinkTestMetrics clientMetrics;
	assert_true(mLibretroGBAWifiLinkReplayHostTestGetMetrics(&hostMetrics));
	assert_true(mLibretroGBAWifiLinkReplayClientTestGetMetrics(&clientMetrics));
	assert_true(hostMetrics.cableTransferStarts > 0);
	assert_int_equal(hostMetrics.cableTransferStarts,
	    clientMetrics.cableTransferStarts);
	assert_int_equal(hostMetrics.cableTransferCompletions,
	    hostMetrics.cableTransferStarts);
	assert_int_equal(clientMetrics.cableTransferCompletions,
	    hostMetrics.cableTransferStarts);
	assert_int_equal(hostMetrics.cableTransferredWords,
	    clientMetrics.cableTransferredWords);

	/* One packet per owner per authored frame, plus the initial records. */
	assert_int_equal(_fixture->network.typeCounts[
	    GBA_LINK_V2_MESSAGE_INPUT_BATCH], 252);
	/* The replay wire accepts only protocol-v2 packets in _sendPacket(). */
	assert_false(_fixture->network.failed);
}

M_TEST_DEFINE(realAdaptersReplayLatencyJitterInputsAndStateChecks) {
	UNUSED(state);
	_attach();
	assert_ptr_equal(
	    mLibretroGBAWifiLinkReplayHostPresentedCore(),
	    mLibretroGBAWifiLinkReplayHostTestPairCore(0));
	assert_ptr_equal(
	    mLibretroGBAWifiLinkReplayClientPresentedCore(),
	    mLibretroGBAWifiLinkReplayClientTestPairCore(1));
	for (unsigned frame = 0; frame < 125; ++frame) {
		assert_true(_runPairedFrame(
		    frame & 1 ? 1 : 0, frame & 2 ? 2 : 0));
	}
	assert_true(_bothReady());
	_assertReplicasMatch();
	_assertCableWorkRemainsLocal();
	assert_int_equal(_fixture->network.typeCounts[
	    GBA_LINK_V2_MESSAGE_STATE_CHECK], 4);
	struct mLibretroGBAWifiLinkTestMetrics hostMetrics;
	struct mLibretroGBAWifiLinkTestMetrics clientMetrics;
	assert_true(mLibretroGBAWifiLinkReplayHostTestGetMetrics(&hostMetrics));
	assert_true(mLibretroGBAWifiLinkReplayClientTestGetMetrics(&clientMetrics));
	assert_int_equal(hostMetrics.productPolicy,
	    GBA_LINK_V2_PRODUCT_STABLE);
	assert_int_equal(clientMetrics.productPolicy,
	    GBA_LINK_V2_PRODUCT_STABLE);
	assert_int_equal(hostMetrics.selectedDelay,
	    GBA_LINK_INPUT_STABLE_FLOOR);
	assert_int_equal(clientMetrics.selectedDelay,
	    GBA_LINK_INPUT_STABLE_FLOOR);
	assert_int_equal(hostMetrics.releasedFrames, 125);
	assert_int_equal(clientMetrics.releasedFrames, 125);
	assert_true(hostMetrics.inputWaitedFrames <= hostMetrics.releasedFrames);
	assert_true(clientMetrics.inputWaitedFrames <= clientMetrics.releasedFrames);
	assert_true(hostMetrics.inputWaitP95Us <= hostMetrics.inputWaitMaxUs);
	assert_true(clientMetrics.inputWaitP95Us <= clientMetrics.inputWaitMaxUs);
	assert_int_equal(hostMetrics.inputDeadlineMisses, 0);
	assert_int_equal(clientMetrics.inputDeadlineMisses, 0);
	assert_int_equal(hostMetrics.telemetryClockFailures, 0);
	assert_int_equal(clientMetrics.telemetryClockFailures, 0);
	assert_int_equal(hostMetrics.inputPollSendCount, 125);
	assert_int_equal(clientMetrics.inputPollSendCount, 125);
	assert_true(hostMetrics.inputInsertions[0] >= 125);
	assert_true(hostMetrics.inputInsertions[1] >= 125);
	assert_false(_fixture->network.failed);
}

M_TEST_DEFINE(lowLatencyPolicyRunsExactOneFrameMapping) {
	UNUSED(state);
	_fixture->frontends[REPLAY_HOST].latencyPolicy = "low_latency";
	_fixture->frontends[REPLAY_CLIENT].latencyPolicy = "low_latency";
	_attach();
	for (unsigned frame = 0; frame < 125; ++frame) {
		assert_true(_runPairedFrame(
		    frame & 1 ? 1 : 0, frame & 2 ? 2 : 0));
	}
	_assertReplicasMatch();
	_assertCableWorkRemainsLocal();
	struct mLibretroGBAWifiLinkTestMetrics hostMetrics;
	struct mLibretroGBAWifiLinkTestMetrics clientMetrics;
	assert_true(mLibretroGBAWifiLinkReplayHostTestGetMetrics(&hostMetrics));
	assert_true(mLibretroGBAWifiLinkReplayClientTestGetMetrics(&clientMetrics));
	assert_int_equal(hostMetrics.productPolicy,
	    GBA_LINK_V2_PRODUCT_LOW_LATENCY);
	assert_int_equal(clientMetrics.productPolicy,
	    GBA_LINK_V2_PRODUCT_LOW_LATENCY);
	assert_int_equal(hostMetrics.selectedDelay,
	    GBA_LINK_INPUT_LOW_LATENCY_FLOOR);
	assert_int_equal(clientMetrics.selectedDelay,
	    GBA_LINK_INPUT_LOW_LATENCY_FLOOR);
	assert_int_equal(hostMetrics.releasedFrames, 125);
	assert_int_equal(clientMetrics.releasedFrames, 125);
	assert_false(_fixture->network.failed);
}

M_TEST_DEFINE(calibratedHigherDelayPreservesContinuousFixtureTrace) {
	UNUSED(state);
	_fixture->network.jitterBase = 280;
	_beginFrontendSession();
	_pumpAttachment(32768);
	assert_true(_bothReady());
	for (unsigned frame = 0; frame < 125; ++frame) {
		assert_true(_runPairedFrame(
		    frame & 1 ? 1 : 0, frame & 2 ? 2 : 0));
	}
	_assertReplicasMatch();
	_assertCableWorkRemainsLocal();
	struct mLibretroGBAWifiLinkTestMetrics hostMetrics;
	struct mLibretroGBAWifiLinkTestMetrics clientMetrics;
	assert_true(mLibretroGBAWifiLinkReplayHostTestGetMetrics(&hostMetrics));
	assert_true(mLibretroGBAWifiLinkReplayClientTestGetMetrics(&clientMetrics));
	assert_true(hostMetrics.selectedDelay > GBA_LINK_INPUT_STABLE_FLOOR);
	assert_int_equal(hostMetrics.selectedDelay, clientMetrics.selectedDelay);
	assert_int_equal(hostMetrics.releasedFrames, 125);
	assert_int_equal(clientMetrics.releasedFrames, 125);
	assert_false(_fixture->network.failed);
}

M_TEST_DEFINE(inputAndStateCheckLossFailClosedAtRuntimeBoundaries) {
	UNUSED(state);
	_attach();
	_setDrop(REPLAY_HOST, GBA_LINK_V2_MESSAGE_INPUT_BATCH, 1);
	assert_false(_runPairedFrame(1, 2));
	assert_false(mLibretroGBAWifiLinkReplayClientOwnsExecution());

	/* Recreate the complete adapter pair for the verification boundary. */
	mLibretroGBAWifiLinkReplayClientUnload();
	mLibretroGBAWifiLinkReplayHostUnload();
	_teardown(state);
	assert_int_equal(_setup(state), 0);
	_attach();
	for (unsigned frame = 0; frame < 59; ++frame) {
		assert_true(_runPairedFrame(0, 0));
	}
	_setDrop(REPLAY_HOST, GBA_LINK_V2_MESSAGE_STATE_CHECK, 1);
	assert_true(_runPairedFrame(0, 0));
	assert_false(_runPairedFrame(0, 0));
	assert_false(mLibretroGBAWifiLinkReplayClientOwnsExecution());
}

M_TEST_DEFINE(rtcSourcesNormalizePerPlayerAndRestoreOriginalSemantics) {
	UNUSED(state);
	struct GBA* hostGba = _fixture->cores[REPLAY_HOST]->board;
	struct GBA* clientGba = _fixture->cores[REPLAY_CLIENT]->board;
	hostGba->memory.hw.devices |= HW_RTC;
	clientGba->memory.hw.devices |= HW_RTC;
	_fixture->cores[REPLAY_HOST]->rtc.override = RTC_NO_OVERRIDE;
	_fixture->cores[REPLAY_HOST]->rtc.value = 111;
	_fixture->cores[REPLAY_CLIENT]->rtc.override = RTC_FIXED;
	_fixture->cores[REPLAY_CLIENT]->rtc.value = 987654000;

	_attach();
	struct mCore* hostP0 =
	    mLibretroGBAWifiLinkReplayHostTestPairCore(0);
	struct mCore* clientP0 =
	    mLibretroGBAWifiLinkReplayClientTestPairCore(0);
	struct mCore* hostP1 =
	    mLibretroGBAWifiLinkReplayHostTestPairCore(1);
	struct mCore* clientP1 =
	    mLibretroGBAWifiLinkReplayClientTestPairCore(1);
	assert_int_equal(hostP0->rtc.override, RTC_FAKE_EPOCH);
	assert_int_equal(clientP0->rtc.override, RTC_FAKE_EPOCH);
	assert_int_equal(hostP0->rtc.value, clientP0->rtc.value);
	assert_int_equal(hostP1->rtc.override, RTC_FIXED);
	assert_int_equal(clientP1->rtc.override, RTC_FIXED);
	assert_int_equal(hostP1->rtc.value, 987654000);
	assert_int_equal(clientP1->rtc.value, 987654000);
	time_t firstP0 = hostP0->rtc.d.unixTime(&hostP0->rtc.d);
	for (unsigned frame = 0; frame < 185; ++frame) {
		assert_true(_runPairedFrame(0, 0));
		if (!(frame % 60)) {
			assert_int_equal(hostP0->rtc.d.unixTime(&hostP0->rtc.d),
			    clientP0->rtc.d.unixTime(&clientP0->rtc.d));
			assert_int_equal(hostP1->rtc.d.unixTime(&hostP1->rtc.d),
			    clientP1->rtc.d.unixTime(&clientP1->rtc.d));
		}
	}
	assert_true(hostP0->rtc.d.unixTime(&hostP0->rtc.d) >= firstP0 + 3);
	_fixture->frontends[REPLAY_CLIENT].callbacks->disconnected(0);
	_fixture->frontends[REPLAY_HOST].callbacks->disconnected(1);
	mLibretroGBAWifiLinkReplayHostRunBegin();
	mLibretroGBAWifiLinkReplayClientRunBegin();
	assert_false(mLibretroGBAWifiLinkReplayHostOwnsExecution());
	assert_false(mLibretroGBAWifiLinkReplayClientOwnsExecution());
	assert_int_equal(
	    _fixture->cores[REPLAY_HOST]->rtc.override, RTC_NO_OVERRIDE);
	assert_int_equal(_fixture->cores[REPLAY_HOST]->rtc.value, 111);
	assert_int_equal(
	    _fixture->cores[REPLAY_CLIENT]->rtc.override, RTC_FIXED);
	assert_int_equal(
	    _fixture->cores[REPLAY_CLIENT]->rtc.value, 987654000);
}

M_TEST_DEFINE(attachmentLossFailsClosedAtEveryWireBoundary) {
	const struct {
		enum GBALinkV2MessageType type;
		enum ReplayEndpointId sender;
	} boundaries[] = {
		{ GBA_LINK_V2_MESSAGE_HELLO, REPLAY_CLIENT },
		{ GBA_LINK_V2_MESSAGE_CALIBRATION_BEGIN, REPLAY_HOST },
		{ GBA_LINK_V2_MESSAGE_LATENCY_PROBE, REPLAY_HOST },
		{ GBA_LINK_V2_MESSAGE_LATENCY_ACK, REPLAY_CLIENT },
		{ GBA_LINK_V2_MESSAGE_LATENCY_REPORT, REPLAY_HOST },
		{ GBA_LINK_V2_MESSAGE_LATENCY_PROBE, REPLAY_CLIENT },
		{ GBA_LINK_V2_MESSAGE_LATENCY_ACK, REPLAY_HOST },
		{ GBA_LINK_V2_MESSAGE_LATENCY_REPORT, REPLAY_CLIENT },
		{ GBA_LINK_V2_MESSAGE_ACCEPT, REPLAY_HOST },
		{ GBA_LINK_V2_MESSAGE_ACCEPT_ACK, REPLAY_CLIENT },
		{ GBA_LINK_V2_MESSAGE_REPLICA_MANIFEST, REPLAY_HOST },
		{ GBA_LINK_V2_MESSAGE_REPLICA_CHUNK, REPLAY_HOST },
		{ GBA_LINK_V2_MESSAGE_REPLICA_INSTALLED, REPLAY_CLIENT },
		{ GBA_LINK_V2_MESSAGE_SESSION_READY, REPLAY_HOST },
		{ GBA_LINK_V2_MESSAGE_SESSION_READY_ACK, REPLAY_CLIENT },
		{ GBA_LINK_V2_MESSAGE_INPUT_WINDOW, REPLAY_HOST },
	};
	for (unsigned i = 0; i < sizeof(boundaries) / sizeof(*boundaries);
	     ++i) {
		if (i) {
			_teardown(state);
			assert_int_equal(_setup(state), 0);
		}
		_setDrop(boundaries[i].sender, boundaries[i].type, 1);
		_beginFrontendSession();
		_pumpAttachment(REPLAY_ATTACH_LIMIT);
		assert_false(_bothReady());
		assert_int_equal(_fixture->network.dropsRemaining, 0);
		assert_false(_fixture->network.failed);
	}
}

static void _assertStopped(void) {
	assert_false(mLibretroGBAWifiLinkReplayHostOwnsExecution());
	assert_false(mLibretroGBAWifiLinkReplayClientOwnsExecution());
}

M_TEST_DEFINE(detachStopResetAndUnloadReleaseBothAdapters) {
	UNUSED(state);
	_attach();
	assert_true(_runPairedFrame(0, 0));
	_fixture->frontends[REPLAY_CLIENT].callbacks->disconnected(0);
	_fixture->frontends[REPLAY_HOST].callbacks->disconnected(1);
	mLibretroGBAWifiLinkReplayHostRunBegin();
	mLibretroGBAWifiLinkReplayClientRunBegin();
	_assertStopped();

	_teardown(state);
	assert_int_equal(_setup(state), 0);
	_attach();
	_fixture->frontends[REPLAY_HOST].callbacks->stop();
	_fixture->frontends[REPLAY_CLIENT].callbacks->stop();
	_assertStopped();

	_teardown(state);
	assert_int_equal(_setup(state), 0);
	_attach();
	mLibretroGBAWifiLinkReplayHostReset();
	mLibretroGBAWifiLinkReplayClientReset();
	_assertStopped();

	_teardown(state);
	assert_int_equal(_setup(state), 0);
	_attach();
	mLibretroGBAWifiLinkReplayHostUnload();
	mLibretroGBAWifiLinkReplayClientUnload();
	_assertStopped();
}

M_TEST_SUITE_DEFINE_SETUP_TEARDOWN(LibretroGBAWifiLinkReplay,
	cmocka_unit_test_setup_teardown(
	    realAdaptersReplayLatencyJitterInputsAndStateChecks,
	    _setup, _teardown),
	cmocka_unit_test_setup_teardown(
	    lowLatencyPolicyRunsExactOneFrameMapping,
	    _setup, _teardown),
	cmocka_unit_test_setup_teardown(
	    calibratedHigherDelayPreservesContinuousFixtureTrace,
	    _setup, _teardown),
	cmocka_unit_test_setup_teardown(
	    inputAndStateCheckLossFailClosedAtRuntimeBoundaries,
	    _setup, _teardown),
	cmocka_unit_test_setup_teardown(
	    rtcSourcesNormalizePerPlayerAndRestoreOriginalSemantics,
	    _setupRtc, _teardown),
	cmocka_unit_test_setup_teardown(
	    attachmentLossFailsClosedAtEveryWireBoundary,
	    _setup, _teardown),
	cmocka_unit_test_setup_teardown(
	    detachStopResetAndUnloadReleaseBothAdapters,
	    _setup, _teardown))
