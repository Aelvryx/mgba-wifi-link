/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "util/test/suite.h"

#include "../libretro/netpacket-v2.h"

#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/gba/core.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/memory.h>
#include <mgba/internal/gba/savedata.h>
#include <mgba/internal/gba/sio/netplay/protocol-v2.h>
#include <mgba-util/vfs.h>

struct V2Frontend {
	struct retro_netpacket_callback callbacks;
	bool supported;
	bool stopDuringPoll;
	unsigned registrations;
	unsigned messages;
	unsigned sends;
	unsigned polls;
	int lastFlags;
	uint16_t lastTarget;
	uint8_t lastPacket[GBA_LINK_V2_MAX_PACKET_SIZE];
	size_t lastPacketSize;
};

struct V2AdapterFixture {
	struct V2Frontend frontend;
	struct mCore* core;
	uint8_t rom[0x200];
	uint8_t save[GBA_SIZE_FLASH1M];
};

enum V2TeardownAction {
	V2_TEARDOWN_CLEAN,
	V2_TEARDOWN_TIMEOUT,
	V2_TEARDOWN_STOP,
	V2_TEARDOWN_RESET,
	V2_TEARDOWN_UNLOAD,
};

static struct V2Frontend* _frontend;
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

M_TEST_SUITE_SETUP(LibretroNetpacketV2) {
	_silentLogger.log = _discardLog;
	mLogSetDefaultLogger(&_silentLogger);
	return 0;
}

M_TEST_SUITE_TEARDOWN(LibretroNetpacketV2) {
	mLogSetDefaultLogger(NULL);
	return 0;
}

static void RETRO_CALLCONV _send(
	int flags, const void* data, size_t size, uint16_t clientId) {
	assert_non_null(data);
	assert_true(size <= sizeof(_frontend->lastPacket));
	++_frontend->sends;
	_frontend->lastFlags = flags;
	_frontend->lastTarget = clientId;
	_frontend->lastPacketSize = size;
	memcpy(_frontend->lastPacket, data, size);
}

static void RETRO_CALLCONV _pollReceive(void) {
	++_frontend->polls;
	if (_frontend->stopDuringPoll) {
		_frontend->stopDuringPoll = false;
		_frontend->callbacks.stop();
	}
}

static bool RETRO_CALLCONV _environment(
	unsigned command, void* data) {
	switch (command) {
	case RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE:
		++_frontend->registrations;
		_frontend->callbacks =
		    *(const struct retro_netpacket_callback*) data;
		return _frontend->supported;
	case RETRO_ENVIRONMENT_SET_MESSAGE:
	case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
		++_frontend->messages;
		return true;
	default:
		return false;
	}
}

static void _makeRom(uint8_t* rom, size_t size) {
	for (size_t i = 0; i < size; ++i) {
		rom[i] = i * 17 + 3;
	}
	rom[0] = 0xFE;
	rom[1] = 0xFF;
	rom[2] = 0xFF;
	rom[3] = 0xEA;
	memcpy(&rom[0xA0], "NETPACKETV2", 11);
	memcpy(&rom[0xAC], "NPV2", 4);
}

static int _setup(void** state) {
	struct V2AdapterFixture* fixture =
	    calloc(1, sizeof(*fixture));
	assert_non_null(fixture);
	fixture->frontend.supported = true;
	_frontend = &fixture->frontend;
	_makeRom(fixture->rom, sizeof(fixture->rom));
	fixture->core = GBACoreCreate();
	assert_non_null(fixture->core);
	assert_true(fixture->core->init(fixture->core));
	mCoreInitConfig(fixture->core, NULL);
	assert_true(fixture->core->loadROM(
	    fixture->core,
	    VFileFromConstMemory(
	        fixture->rom, sizeof(fixture->rom))));
	fixture->core->reset(fixture->core);
	mLibretroNetpacketV2TestSetTimeMs(100);
	*state = fixture;
	return 0;
}

static int _teardown(void** state) {
	struct V2AdapterFixture* fixture = *state;
	mLibretroNetpacketV2Unload();
	mCoreConfigDeinit(&fixture->core->config);
	fixture->core->deinit(fixture->core);
	free(fixture);
	_frontend = NULL;
	return 0;
}

M_TEST_DEFINE(registersExactReplicatedProtocol) {
	struct V2AdapterFixture* fixture = *state;
	assert_true(mLibretroNetpacketV2Register(
	    _environment, fixture->core, fixture->save,
	    sizeof(fixture->save)));
	assert_int_equal(fixture->frontend.registrations, 1);
	assert_string_equal(
	    fixture->frontend.callbacks.protocol_version,
	    GBA_LINK_V2_PROTOCOL_NAME);
	assert_false(mLibretroNetpacketV2OwnsExecution());
	assert_null(mLibretroNetpacketV2PresentedCore());
	assert_int_equal(mLibretroNetpacketV2TestPlayerForRole(
	    GBA_LINK_ROLE_HOST), 0);
	assert_int_equal(mLibretroNetpacketV2TestPlayerForRole(
	    GBA_LINK_ROLE_CLIENT), 1);
}

M_TEST_DEFINE(clientStartsWithReliableFlushedV2Hello) {
	struct V2AdapterFixture* fixture = *state;
	assert_true(mLibretroNetpacketV2Register(
	    _environment, fixture->core, fixture->save,
	    sizeof(fixture->save)));
	fixture->frontend.callbacks.start(
	    1, _send, _pollReceive);
	assert_true(mLibretroNetpacketV2SessionActive());
	assert_int_equal(fixture->frontend.sends, 1);
	assert_int_equal(fixture->frontend.lastTarget, 0);
	assert_true(
	    fixture->frontend.lastFlags & RETRO_NETPACKET_RELIABLE);
	assert_true(
	    fixture->frontend.lastFlags & RETRO_NETPACKET_FLUSH_HINT);
	struct GBALinkV2Packet packet;
	assert_int_equal(
	    GBALinkV2PacketDecode(
	        fixture->frontend.lastPacket,
	        fixture->frontend.lastPacketSize,
	        GBA_LINK_ROLE_CLIENT, &packet),
	    GBA_LINK_DECODE_OK);
	assert_int_equal(
	    packet.header.type, GBA_LINK_V2_MESSAGE_HELLO);
	assert_int_equal(packet.header.sessionId, 0);
}

M_TEST_DEFINE(hostAdmissionBoundsProvisionalTraffic) {
	struct V2AdapterFixture* fixture = *state;
	assert_true(mLibretroNetpacketV2Register(
	    _environment, fixture->core, fixture->save,
	    sizeof(fixture->save)));
	fixture->frontend.callbacks.start(
	    0, _send, _pollReceive);
	uint8_t provisional[] = { 1, 2, 3 };
	fixture->frontend.callbacks.receive(
	    provisional, sizeof(provisional), 1);
	assert_int_equal(
	    mLibretroNetpacketV2TestPendingPacketCount(), 1);
	assert_true(fixture->frontend.callbacks.connected(1));
	assert_int_equal(fixture->frontend.sends, 1);
	assert_int_equal(fixture->frontend.lastTarget, 1);
	assert_false(fixture->frontend.callbacks.connected(2));
	mLibretroNetpacketV2RunBegin();
	assert_false(mLibretroNetpacketV2SessionActive());
	assert_int_equal(
	    mLibretroNetpacketV2TestPendingPacketCount(), 0);
}

M_TEST_DEFINE(missingPollingAndSynchronousStopFailClosed) {
	struct V2AdapterFixture* fixture = *state;
	assert_true(mLibretroNetpacketV2Register(
	    _environment, fixture->core, fixture->save,
	    sizeof(fixture->save)));
	fixture->frontend.callbacks.start(1, _send, NULL);
	assert_false(mLibretroNetpacketV2SessionActive());
	assert_true(fixture->frontend.messages);

	fixture->frontend.callbacks.start(
	    1, _send, _pollReceive);
	uint64_t generation =
	    mLibretroNetpacketV2TestCallbackGeneration();
	fixture->frontend.stopDuringPoll = true;
	assert_false(mLibretroNetpacketV2TestPollReceive());
	assert_int_equal(fixture->frontend.polls, 1);
	assert_true(
	    mLibretroNetpacketV2TestCallbackGeneration() != generation);
	mLibretroNetpacketV2RunBegin();
	assert_false(mLibretroNetpacketV2SessionActive());
}

static struct mCore* _secondCore(
		struct V2AdapterFixture* fixture) {
	struct mCore* core = GBACoreCreate();
	assert_non_null(core);
	assert_true(core->init(core));
	mCoreInitConfig(core, NULL);
	assert_true(core->loadROM(core, VFileFromConstMemory(
	    fixture->rom, sizeof(fixture->rom))));
	core->reset(core);
	return core;
}

static void _teardownPairAction(
		struct V2AdapterFixture* fixture, enum GBALinkRole role,
		enum V2TeardownAction action) {
	memset(fixture->save, 0xFF, sizeof(fixture->save));
	assert_true(mLibretroNetpacketV2Register(
	    _environment, fixture->core, fixture->save,
	    sizeof(fixture->save)));
	struct mCore* second = _secondCore(fixture);
	struct mCore* sources[2] = { fixture->core, second };
	struct GBAReplicaBundle bundles[2];
	struct GBAReplicaManifest manifests[2];
	struct GBAReplicaPayload payloads[2];
	memset(bundles, 0, sizeof(bundles));
	memset(payloads, 0, sizeof(payloads));
	for (unsigned player = 0; player < 2; ++player) {
		assert_int_equal(GBAReplicaCapture(
		    sources[player], player, 77,
		    GBA_REPLICA_ENCODING_NONE,
		    GBA_REPLICA_DEFAULT_CHUNK_SIZE,
		    &bundles[player]), GBA_REPLICA_OK);
		manifests[player] = bundles[player].manifest;
		payloads[player].data = bundles[player].encodedData;
		payloads[player].size = bundles[player].encodedSize;
	}
	assert_true(mLibretroNetpacketV2TestInstallPair(
	    manifests, payloads, role, 77));
	uint8_t localPlayer = role == GBA_LINK_ROLE_HOST ? 0 : 1;
	uint8_t shadowPlayer = localPlayer ^ 1;
	struct GBA* local = mLibretroNetpacketV2TestPairCore(
	    localPlayer)->board;
	struct GBA* shadow = mLibretroNetpacketV2TestPairCore(
	    shadowPlayer)->board;
	GBASavedataForceType(
	    &local->memory.savedata, GBA_SAVEDATA_SRAM);
	GBASavedataForceType(
	    &shadow->memory.savedata, GBA_SAVEDATA_SRAM);
	local->memory.savedata.data[23] =
	    role == GBA_LINK_ROLE_HOST ? 0xA0 : 0xB1;
	shadow->memory.savedata.data[23] = 0x5C;
	assert_int_equal(fixture->save[23],
	    role == GBA_LINK_ROLE_HOST ? 0xA0 : 0xB1);

	switch (action) {
	case V2_TEARDOWN_CLEAN:
		mLibretroNetpacketV2TestFail(
		    GBA_LINK_V2_REASON_USER_DISCONNECT);
		break;
	case V2_TEARDOWN_TIMEOUT:
		mLibretroNetpacketV2TestFail(
		    GBA_LINK_V2_REASON_INPUT_TIMEOUT);
		break;
	case V2_TEARDOWN_STOP:
		fixture->frontend.callbacks.stop();
		break;
	case V2_TEARDOWN_RESET:
		mLibretroNetpacketV2Reset();
		break;
	case V2_TEARDOWN_UNLOAD:
		mLibretroNetpacketV2Unload();
		break;
	}
	assert_int_equal(fixture->save[23],
	    role == GBA_LINK_ROLE_HOST ? 0xA0 : 0xB1);
	assert_null(mLibretroNetpacketV2TestPairCore(0));
	assert_null(mLibretroNetpacketV2TestPairCore(1));

	for (unsigned player = 0; player < 2; ++player) {
		GBAReplicaBundleDeinit(&bundles[player]);
	}
	mCoreConfigDeinit(&second->config);
	second->deinit(second);
}

M_TEST_DEFINE(localRoleSaveSurvivesEveryTeardownWithoutShadowWriteback) {
	struct V2AdapterFixture* fixture = *state;
	for (enum GBALinkRole role = GBA_LINK_ROLE_HOST;
	     role <= GBA_LINK_ROLE_CLIENT; ++role) {
		for (enum V2TeardownAction action = V2_TEARDOWN_CLEAN;
		     action <= V2_TEARDOWN_UNLOAD; ++action) {
			_teardownPairAction(fixture, role, action);
			mLibretroNetpacketV2Unload();
		}
	}
}

M_TEST_SUITE_DEFINE_SETUP_TEARDOWN(LibretroNetpacketV2,
	cmocka_unit_test_setup_teardown(
	    registersExactReplicatedProtocol, _setup, _teardown),
	cmocka_unit_test_setup_teardown(
	    clientStartsWithReliableFlushedV2Hello, _setup, _teardown),
	cmocka_unit_test_setup_teardown(
	    hostAdmissionBoundsProvisionalTraffic, _setup, _teardown),
	cmocka_unit_test_setup_teardown(
	    missingPollingAndSynchronousStopFailClosed, _setup, _teardown),
	cmocka_unit_test_setup_teardown(
	    localRoleSaveSurvivesEveryTeardownWithoutShadowWriteback,
	    _setup, _teardown))
