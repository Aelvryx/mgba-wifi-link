/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "util/test/suite.h"

#include "../libretro/libretro.h"

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
	struct mCore* PREFIX##TestPairCore(uint8_t player)

DECLARE_REPLAY_ADAPTER(mLibretroNetpacketV2ReplayHost);
DECLARE_REPLAY_ADAPTER(mLibretroNetpacketV2ReplayClient);

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
};

struct ReplayNetwork {
	Mutex mutex;
	uint64_t now;
	uint64_t sentPackets;
	uint64_t deliveredPackets;
	uint64_t typeCounts[GBA_LINK_V2_MESSAGE_REJECT + 1];
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

M_TEST_SUITE_SETUP(LibretroNetpacketV2Replay) {
	_silentLogger.log = _discardLog;
	mLogSetDefaultLogger(&_silentLogger);
	return 0;
}

M_TEST_SUITE_TEARDOWN(LibretroNetpacketV2Replay) {
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
		mLibretroNetpacketV2ReplayHostTestSetTimeMs(now);
	} else {
		mLibretroNetpacketV2ReplayClientTestSetTimeMs(now);
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
	uint64_t jitter = 1 + network->sentPackets % 5;
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
	_setEndpointTime(receiver, now);
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

static int _setup(void** state) {
	struct ReplayFixture* fixture = calloc(1, sizeof(*fixture));
	assert_non_null(fixture);
	assert_int_equal(MutexInit(&fixture->network.mutex), 0);
	_fixture = fixture;
	fixture->rom = malloc(REPLAY_ROM_SIZE);
	assert_non_null(fixture->rom);
	memset(fixture->rom, 0xFF, REPLAY_ROM_SIZE);
	struct VFile* fixtureRom = VFileOpen(
	    GBA_LINK_CONTINUOUS_ROM_PATH, O_RDONLY);
	assert_non_null(fixtureRom);
	ssize_t fixtureSize = fixtureRom->size(fixtureRom);
	assert_true(fixtureSize > 0);
	assert_true((size_t) fixtureSize < REPLAY_ROM_SIZE);
	assert_int_equal(fixtureRom->read(
	    fixtureRom, fixture->rom, fixtureSize), fixtureSize);
	assert_true(fixtureRom->close(fixtureRom));
	for (unsigned endpoint = 0; endpoint < REPLAY_ENDPOINT_COUNT;
	     ++endpoint) {
		fixture->cores[endpoint] = _createCore(fixture->rom);
		fixture->saves[endpoint] = malloc(GBA_SIZE_FLASH1M);
		assert_non_null(fixture->saves[endpoint]);
		memset(fixture->saves[endpoint], 0xFF, GBA_SIZE_FLASH1M);
		assert_true(fixture->cores[endpoint]->loadSave(
		    fixture->cores[endpoint], VFileFromMemory(
		        fixture->saves[endpoint], GBA_SIZE_FLASH1M)));
	}
	assert_true(mLibretroNetpacketV2ReplayHostRegister(
	    _hostEnvironment, fixture->cores[REPLAY_HOST],
	    fixture->saves[REPLAY_HOST], GBA_SIZE_FLASH1M));
	assert_true(mLibretroNetpacketV2ReplayClientRegister(
	    _clientEnvironment, fixture->cores[REPLAY_CLIENT],
	    fixture->saves[REPLAY_CLIENT], GBA_SIZE_FLASH1M));
	mLibretroNetpacketV2ReplayHostTestSetTimeMs(0);
	mLibretroNetpacketV2ReplayClientTestSetTimeMs(0);
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

static int _teardown(void** state) {
	struct ReplayFixture* fixture = *state;
	mLibretroNetpacketV2ReplayClientUnload();
	mLibretroNetpacketV2ReplayHostUnload();
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
	return mLibretroNetpacketV2ReplayHostOwnsExecution() &&
	       mLibretroNetpacketV2ReplayClientOwnsExecution();
}

static void _pumpAttachment(unsigned limit) {
	for (unsigned iteration = 0;
	     iteration < limit && !_bothReady(); ++iteration) {
		_hostPoll();
		_clientPoll();
		mLibretroNetpacketV2ReplayHostRunBegin();
		mLibretroNetpacketV2ReplayClientRunBegin();
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
	call->result = mLibretroNetpacketV2ReplayHostRunFrame(
	    call->keys);
	THREAD_EXIT(NULL);
}

static bool _runPairedFrame(uint16_t hostKeys, uint16_t clientKeys) {
	struct ReplayFrameCall host = { .keys = hostKeys };
	Thread thread;
	assert_int_equal(ThreadCreate(&thread, _hostFrame, &host), 0);
	bool client = mLibretroNetpacketV2ReplayClientRunFrame(clientKeys);
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
		    mLibretroNetpacketV2ReplayHostTestPairCore(player);
		struct mCore* client =
		    mLibretroNetpacketV2ReplayClientTestPairCore(player);
		assert_non_null(host);
		assert_non_null(client);
		assert_int_equal(host->frameCounter(host),
		    client->frameCounter(client));
		assert_int_equal(host->rawRead32(
		        host, 0x02000010, -1),
		    client->rawRead32(client, 0x02000010, -1));
	}
}

M_TEST_DEFINE(realAdaptersReplayLatencyJitterInputsAndStateChecks) {
	UNUSED(state);
	_attach();
	assert_ptr_equal(
	    mLibretroNetpacketV2ReplayHostPresentedCore(),
	    mLibretroNetpacketV2ReplayHostTestPairCore(0));
	assert_ptr_equal(
	    mLibretroNetpacketV2ReplayClientPresentedCore(),
	    mLibretroNetpacketV2ReplayClientTestPairCore(1));
	for (unsigned frame = 0; frame < 125; ++frame) {
		assert_true(_runPairedFrame(
		    frame & 1 ? 1 : 0, frame & 2 ? 2 : 0));
	}
	assert_true(_bothReady());
	_assertReplicasMatch();
	assert_int_equal(_fixture->network.typeCounts[
	    GBA_LINK_V2_MESSAGE_STATE_CHECK], 4);
	assert_int_equal(_fixture->network.typeCounts[
	    GBA_LINK_V2_MESSAGE_INPUT_BATCH], 252);
	assert_false(_fixture->network.failed);
}

M_TEST_DEFINE(inputAndStateCheckLossFailClosedAtRuntimeBoundaries) {
	UNUSED(state);
	_attach();
	_setDrop(REPLAY_HOST, GBA_LINK_V2_MESSAGE_INPUT_BATCH, 1);
	assert_false(_runPairedFrame(1, 2));
	assert_false(mLibretroNetpacketV2ReplayClientOwnsExecution());

	/* Recreate the complete adapter pair for the verification boundary. */
	mLibretroNetpacketV2ReplayClientUnload();
	mLibretroNetpacketV2ReplayHostUnload();
	_teardown(state);
	assert_int_equal(_setup(state), 0);
	_attach();
	for (unsigned frame = 0; frame < 59; ++frame) {
		assert_true(_runPairedFrame(0, 0));
	}
	_setDrop(REPLAY_HOST, GBA_LINK_V2_MESSAGE_STATE_CHECK, 1);
	assert_true(_runPairedFrame(0, 0));
	assert_false(_runPairedFrame(0, 0));
	assert_false(mLibretroNetpacketV2ReplayClientOwnsExecution());
}

M_TEST_DEFINE(attachmentLossFailsClosedAtEveryWireBoundary) {
	const struct {
		enum GBALinkV2MessageType type;
		enum ReplayEndpointId sender;
	} boundaries[] = {
		{ GBA_LINK_V2_MESSAGE_HELLO, REPLAY_CLIENT },
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
	assert_false(mLibretroNetpacketV2ReplayHostOwnsExecution());
	assert_false(mLibretroNetpacketV2ReplayClientOwnsExecution());
}

M_TEST_DEFINE(detachStopResetAndUnloadReleaseBothAdapters) {
	UNUSED(state);
	_attach();
	assert_true(_runPairedFrame(0, 0));
	_fixture->frontends[REPLAY_CLIENT].callbacks->disconnected(0);
	_fixture->frontends[REPLAY_HOST].callbacks->disconnected(1);
	mLibretroNetpacketV2ReplayHostRunBegin();
	mLibretroNetpacketV2ReplayClientRunBegin();
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
	mLibretroNetpacketV2ReplayHostReset();
	mLibretroNetpacketV2ReplayClientReset();
	_assertStopped();

	_teardown(state);
	assert_int_equal(_setup(state), 0);
	_attach();
	mLibretroNetpacketV2ReplayHostUnload();
	mLibretroNetpacketV2ReplayClientUnload();
	_assertStopped();
}

M_TEST_SUITE_DEFINE_SETUP_TEARDOWN(LibretroNetpacketV2Replay,
	cmocka_unit_test_setup_teardown(
	    realAdaptersReplayLatencyJitterInputsAndStateChecks,
	    _setup, _teardown),
	cmocka_unit_test_setup_teardown(
	    inputAndStateCheckLossFailClosedAtRuntimeBoundaries,
	    _setup, _teardown),
	cmocka_unit_test_setup_teardown(
	    attachmentLossFailsClosedAtEveryWireBoundary,
	    _setup, _teardown),
	cmocka_unit_test_setup_teardown(
	    detachStopResetAndUnloadReleaseBothAdapters,
	    _setup, _teardown))
