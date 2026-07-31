/* Copyright (c) 2026 mGBA Wi-Fi link contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "util/test/suite.h"

#include "../libretro/netpacket.h"

#include <mgba/core/core.h>
#include <mgba/gba/core.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/sio.h>
#include <mgba/internal/gba/sio/netplay/protocol.h>
#include <mgba/internal/gba/sio/netplay/session.h>
#include <mgba-util/vfs.h>

struct FakeFrontend {
	struct retro_netpacket_callback callbacks;
	bool supportsNetpacket;
	bool startDuringRegistration;
	bool stopDuringPoll;
	bool stopDuringSend;
	unsigned registrations;
	unsigned messages;
	unsigned sends;
	unsigned polls;
	unsigned callbackOrder;
	unsigned sendOrder;
	unsigned pollOrder;
	int lastFlags;
	uint16_t lastTarget;
	uint8_t lastPacket[GBA_LINK_MAX_PACKET_SIZE];
	size_t lastPacketSize;
	char lastMessage[384];
};

struct AdapterFixture {
	struct FakeFrontend frontend;
	struct mCore* core;
	uint8_t rom[0x200];
};

static struct FakeFrontend* _frontend;

static void RETRO_CALLCONV _send(
    int flags, const void* data, size_t size,
    uint16_t clientId);
static void RETRO_CALLCONV _pollReceive(void);

static bool RETRO_CALLCONV _environment(
    unsigned command, void* data) {
	switch (command) {
	case RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE:
		++_frontend->registrations;
		_frontend->callbacks =
		    *(const struct retro_netpacket_callback*) data;
		if (_frontend->supportsNetpacket &&
		    _frontend->startDuringRegistration) {
			_frontend->callbacks.start(
			    0, _send, _pollReceive);
		}
		return _frontend->supportsNetpacket;
	case RETRO_ENVIRONMENT_SET_MESSAGE: {
		++_frontend->messages;
		const struct retro_message* message = data;
		snprintf(
		    _frontend->lastMessage,
		    sizeof(_frontend->lastMessage), "%s",
		    message && message->msg ? message->msg : "");
		return true;
	}
	case RETRO_ENVIRONMENT_SET_MESSAGE_EXT: {
		++_frontend->messages;
		const struct retro_message_ext* message = data;
		snprintf(
		    _frontend->lastMessage,
		    sizeof(_frontend->lastMessage), "%s",
		    message && message->msg ? message->msg : "");
		return true;
	}
	default:
		return false;
	}
}

static void RETRO_CALLCONV _send(
    int flags, const void* data, size_t size,
    uint16_t clientId) {
	assert_non_null(_frontend);
	assert_non_null(data);
	assert_true(size <= sizeof(_frontend->lastPacket));
	++_frontend->sends;
	_frontend->lastFlags = flags;
	_frontend->lastTarget = clientId;
	_frontend->lastPacketSize = size;
	memcpy(_frontend->lastPacket, data, size);
	_frontend->sendOrder =
	    ++_frontend->callbackOrder;
	if (_frontend->stopDuringSend) {
		_frontend->stopDuringSend = false;
		_frontend->callbacks.stop();
	}
}

static void RETRO_CALLCONV _pollReceive(void) {
	++_frontend->polls;
	_frontend->pollOrder =
	    ++_frontend->callbackOrder;
	if (_frontend->stopDuringPoll) {
		_frontend->stopDuringPoll = false;
		_frontend->callbacks.stop();
	}
}

static void _makeRom(uint8_t* rom, size_t size) {
	for (size_t i = 0; i < size; ++i) {
		rom[i] = i * 29 + 7;
	}
	memcpy(&rom[0xA0], "NETPACKETTST", 12);
	memcpy(&rom[0xAC], "NPTE", 4);
}

static int _setup(void** state) {
	struct AdapterFixture* fixture =
	    calloc(1, sizeof(*fixture));
	assert_non_null(fixture);
	fixture->frontend.supportsNetpacket = true;
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
	*state = fixture;
	return 0;
}

static int _teardown(void** state) {
	struct AdapterFixture* fixture = *state;
	mLibretroNetpacketUnload();
	mCoreConfigDeinit(&fixture->core->config);
	fixture->core->deinit(fixture->core);
	free(fixture);
	_frontend = NULL;
	return 0;
}

M_TEST_DEFINE(registersOnlyWhenFrontendSupportsCommand78) {
	struct AdapterFixture* fixture = *state;
	fixture->frontend.supportsNetpacket = false;
	assert_false(mLibretroNetpacketRegister(
	    _environment, fixture->core));
	assert_int_equal(
	    fixture->frontend.registrations, 1);
	assert_string_equal(
	    fixture->frontend.callbacks.protocol_version,
	    GBA_LINK_PROTOCOL_NAME);
	assert_false(mLibretroNetpacketSessionActive());
}

M_TEST_DEFINE(hostStartsOnlyAfterOneClientIsAdmitted) {
	struct AdapterFixture* fixture = *state;
	assert_true(mLibretroNetpacketRegister(
	    _environment, fixture->core));
	fixture->frontend.callbacks.start(
	    0, _send, _pollReceive);
	assert_true(mLibretroNetpacketSessionActive());
	assert_false(
	    mLibretroNetpacketExecutionBlocked());
	assert_int_equal(fixture->frontend.sends, 0);
	assert_true(
	    fixture->frontend.callbacks.connected(1));
	assert_true(
	    mLibretroNetpacketExecutionBlocked());
	assert_int_equal(fixture->frontend.sends, 1);
	assert_int_equal(fixture->frontend.lastTarget, 1);
	assert_true(
	    fixture->frontend.lastFlags &
	    RETRO_NETPACKET_RELIABLE);
	assert_true(
	    fixture->frontend.lastFlags &
	    RETRO_NETPACKET_FLUSH_HINT);
	assert_false(
	    fixture->frontend.callbacks.connected(2));
}

M_TEST_DEFINE(hostMayStartSynchronouslyDuringRegistration) {
	struct AdapterFixture* fixture = *state;
	fixture->frontend.startDuringRegistration = true;
	assert_true(mLibretroNetpacketRegister(
	    _environment, fixture->core));
	assert_true(mLibretroNetpacketSessionActive());
	assert_true(
	    fixture->frontend.callbacks.connected(1));
	assert_int_equal(fixture->frontend.sends, 1);
	assert_int_equal(fixture->frontend.lastTarget, 1);
}

M_TEST_DEFINE(hostQueuesPacketDeliveredBeforeAdmissionCallback) {
	struct AdapterFixture* fixture = *state;
	assert_true(mLibretroNetpacketRegister(
	    _environment, fixture->core));
	fixture->frontend.callbacks.start(
	    0, _send, _pollReceive);

	uint8_t earlyPacket[] = { 0xAA, 0x55 };
	fixture->frontend.callbacks.receive(
	    earlyPacket, sizeof(earlyPacket), 1);
	memset(earlyPacket, 0, sizeof(earlyPacket));

	assert_true(mLibretroNetpacketSessionActive());
	assert_true(
	    fixture->frontend.callbacks.connected(1));
	assert_int_equal(fixture->frontend.sends, 1);
	assert_int_equal(fixture->frontend.lastTarget, 1);
}

M_TEST_DEFINE(clientStartsHandshakeTowardHost) {
	struct AdapterFixture* fixture = *state;
	assert_true(mLibretroNetpacketRegister(
	    _environment, fixture->core));
	fixture->frontend.callbacks.start(
	    1, _send, _pollReceive);
	assert_true(mLibretroNetpacketSessionActive());
	assert_int_equal(fixture->frontend.sends, 1);
	assert_int_equal(fixture->frontend.lastTarget, 0);

	struct GBALinkPacket hello;
	assert_int_equal(
	    GBALinkPacketDecode(
	        fixture->frontend.lastPacket,
	        fixture->frontend.lastPacketSize,
	        GBA_LINK_ROLE_CLIENT, &hello),
	    GBA_LINK_DECODE_OK);
	assert_int_equal(
	    hello.header.type,
	    GBA_LINK_MESSAGE_HELLO);
	assert_int_equal(hello.header.sessionId, 0);
}

M_TEST_DEFINE(scheduledCompletionReachesQuiescenceBeforeDeadline) {
	struct AdapterFixture* fixture = *state;
	struct GBA* gba = fixture->core->board;
	assert_true(mLibretroNetpacketRegister(
	    _environment, fixture->core));
	mLibretroNetpacketTestSetTimeMs(100);
	GBASIOWriteRCNT(&gba->sio, 0);
	GBASIOWriteSIOCNT(&gba->sio, 0x2000);
	GBASIOWriteSIOCNT(&gba->sio, 0x2080);
	assert_true(mTimingIsScheduled(
	    &gba->timing, &gba->sio.completeEvent));

	fixture->frontend.callbacks.start(
	    1, _send, _pollReceive);
	assert_int_equal(
	    mLibretroNetpacketTestSessionState(),
	    GBA_LINK_SESSION_TRANSPORT_STARTED);
	assert_int_equal(fixture->frontend.sends, 0);
	assert_false(mLibretroNetpacketExecutionBlocked());

	int32_t duration = mTimingUntil(
	    &gba->timing, &gba->sio.completeEvent);
	assert_true(duration >= 0);
	gba->timing.masterCycles += (uint32_t) duration;
	mTimingDeschedule(
	    &gba->timing, &gba->sio.completeEvent);
	gba->sio.completeEvent.callback(
	    &gba->timing,
	    gba->sio.completeEvent.context, 0);
	mLibretroNetpacketRunBegin();

	assert_int_equal(fixture->frontend.sends, 1);
	assert_true(mLibretroNetpacketExecutionBlocked());
	struct GBALinkPacket hello;
	assert_int_equal(
	    GBALinkPacketDecode(
	        fixture->frontend.lastPacket,
	        fixture->frontend.lastPacketSize,
	        GBA_LINK_ROLE_CLIENT, &hello),
	    GBA_LINK_DECODE_OK);
	assert_int_equal(
	    hello.header.type, GBA_LINK_MESSAGE_HELLO);
}

M_TEST_DEFINE(permanentlyBusySioTimesOutFromPeerAdmission) {
	struct AdapterFixture* fixture = *state;
	struct GBA* gba = fixture->core->board;
	assert_true(mLibretroNetpacketRegister(
	    _environment, fixture->core));
	mLibretroNetpacketTestSetTimeMs(100);
	fixture->frontend.callbacks.start(
	    0, _send, _pollReceive);
	GBASIOWriteRCNT(&gba->sio, 0);
	GBASIOWriteSIOCNT(&gba->sio, 0x2000);
	gba->sio.siocnt =
	    GBASIOMultiplayerFillBusy(gba->sio.siocnt);

	uint8_t provisional = 0xA5;
	fixture->frontend.callbacks.receive(
	    &provisional, sizeof(provisional), 1);
	assert_int_equal(
	    mLibretroNetpacketTestPendingPacketCount(), 1);
	assert_true(
	    fixture->frontend.callbacks.connected(1));
	assert_int_equal(
	    mLibretroNetpacketTestSessionState(),
	    GBA_LINK_SESSION_TRANSPORT_STARTED);
	assert_int_equal(fixture->frontend.sends, 0);
	assert_false(mLibretroNetpacketExecutionBlocked());
	assert_int_equal(
	    mLibretroNetpacketTestPendingPacketCount(), 1);
	uint64_t generation =
	    mLibretroNetpacketTestCallbackGeneration();

	mLibretroNetpacketTestSetTimeMs(3100);
	mLibretroNetpacketRunBegin();
	assert_false(mLibretroNetpacketSessionActive());
	assert_false(mLibretroNetpacketExecutionBlocked());
	assert_int_equal(fixture->frontend.sends, 0);
	assert_int_equal(
	    mLibretroNetpacketTestPendingPacketCount(), 0);
	assert_true(
	    mLibretroNetpacketTestCallbackGeneration() !=
	    generation);
	assert_true(fixture->frontend.messages > 0);
	assert_non_null(strstr(
	    fixture->frontend.lastMessage,
	    "attachment timed out"));
	assert_non_null(strstr(
	    fixture->frontend.lastMessage,
	    "quiescent SIO rendezvous"));

	fixture->frontend.callbacks.poll();
	mLibretroNetpacketRunEnd();
	assert_int_equal(fixture->frontend.sends, 0);
}

M_TEST_DEFINE(missingReceivePollingFailsBeforeAttachment) {
	struct AdapterFixture* fixture = *state;
	assert_true(mLibretroNetpacketRegister(
	    _environment, fixture->core));
	fixture->frontend.callbacks.start(
	    1, _send, NULL);
	assert_false(mLibretroNetpacketSessionActive());
	assert_true(fixture->frontend.messages > 0);
	assert_int_equal(fixture->frontend.sends, 0);
}

M_TEST_DEFINE(stopDuringPollInvalidatesSavedCallbacks) {
	struct AdapterFixture* fixture = *state;
	assert_true(mLibretroNetpacketRegister(
	    _environment, fixture->core));
	fixture->frontend.callbacks.start(
	    1, _send, _pollReceive);
	assert_int_equal(fixture->frontend.sends, 1);
	fixture->frontend.stopDuringPoll = true;
	assert_false(
	    mLibretroNetpacketTestPollReceive());
	assert_int_equal(fixture->frontend.polls, 1);
	mLibretroNetpacketRunBegin();
	assert_false(mLibretroNetpacketSessionActive());
	assert_int_equal(fixture->frontend.sends, 1);
}

M_TEST_DEFINE(stopDuringSendIsAnAdapterSendFailure) {
	struct AdapterFixture* fixture = *state;
	assert_true(mLibretroNetpacketRegister(
	    _environment, fixture->core));
	fixture->frontend.stopDuringSend = true;
	fixture->frontend.callbacks.start(
	    1, _send, _pollReceive);
	assert_int_equal(fixture->frontend.sends, 1);
	mLibretroNetpacketRunBegin();
	assert_false(mLibretroNetpacketSessionActive());
	assert_int_equal(fixture->frontend.sends, 1);
}

M_TEST_DEFINE(reliableFlushSendPrecedesReceivePoll) {
	struct AdapterFixture* fixture = *state;
	assert_true(mLibretroNetpacketRegister(
	    _environment, fixture->core));
	fixture->frontend.callbacks.start(
	    1, _send, _pollReceive);
	assert_true(
	    fixture->frontend.lastFlags &
	    RETRO_NETPACKET_RELIABLE);
	assert_true(
	    fixture->frontend.lastFlags &
	    RETRO_NETPACKET_FLUSH_HINT);
	assert_true(mLibretroNetpacketTestPollReceive());
	assert_true(
	    fixture->frontend.sendOrder <
	    fixture->frontend.pollOrder);
}

M_TEST_DEFINE(copiedQueueFailsClosedOnExhaustion) {
	struct AdapterFixture* fixture = *state;
	assert_true(mLibretroNetpacketRegister(
	    _environment, fixture->core));
	fixture->frontend.callbacks.start(
	    1, _send, _pollReceive);
	uint8_t byte = 0;
	for (unsigned i = 0;
	     i < GBA_LINK_MAX_COPIED_PACKETS + 1;
	     ++i) {
		fixture->frontend.callbacks.receive(
		    &byte, sizeof(byte), 0);
	}
	assert_true(mLibretroNetpacketSessionActive());
	mLibretroNetpacketRunBegin();
	assert_false(mLibretroNetpacketSessionActive());
}

M_TEST_DEFINE(oversizedAndWrongSenderPacketsFailClosed) {
	struct AdapterFixture* fixture = *state;
	assert_true(mLibretroNetpacketRegister(
	    _environment, fixture->core));
	fixture->frontend.callbacks.start(
	    1, _send, _pollReceive);
	uint8_t oversized[GBA_LINK_MAX_PACKET_SIZE + 1] = {0};
	fixture->frontend.callbacks.receive(
	    oversized, sizeof(oversized), 0);
	mLibretroNetpacketRunBegin();
	assert_false(mLibretroNetpacketSessionActive());

	fixture->frontend.callbacks.start(
	    1, _send, _pollReceive);
	fixture->frontend.callbacks.receive(
	    oversized, 1, 7);
	mLibretroNetpacketRunBegin();
	assert_false(mLibretroNetpacketSessionActive());
}

static void _deliver(
    struct FakeFrontend* frontend,
    const struct GBALinkPacket* packet) {
	uint8_t data[GBA_LINK_MAX_PACKET_SIZE];
	size_t size = 0;
	assert_true(GBALinkPacketEncode(
	    packet, data, sizeof(data), &size));
	frontend->callbacks.receive(
	    data, size, 0);
	frontend->callbacks.poll();
}

M_TEST_DEFINE(receiveCallbackCopiesBeforeFrontendReturns) {
	struct AdapterFixture* fixture = *state;
	assert_true(mLibretroNetpacketRegister(
	    _environment, fixture->core));
	fixture->frontend.callbacks.start(
	    1, _send, _pollReceive);

	struct GBALinkPacket localHello;
	assert_int_equal(
	    GBALinkPacketDecode(
	        fixture->frontend.lastPacket,
	        fixture->frontend.lastPacketSize,
	        GBA_LINK_ROLE_CLIENT, &localHello),
	    GBA_LINK_DECODE_OK);
	struct GBALinkPacket remoteHello = localHello;
	remoteHello.header.packetSequence = 1;
	uint8_t data[GBA_LINK_MAX_PACKET_SIZE];
	size_t size = 0;
	assert_true(GBALinkPacketEncode(
	    &remoteHello, data, sizeof(data), &size));
	fixture->frontend.callbacks.receive(
	    data, size, 0);
	memset(data, 0xA5, size);
	fixture->frontend.callbacks.poll();
	assert_int_equal(
	    mLibretroNetpacketTestSessionState(),
	    GBA_LINK_SESSION_HELLO_EXCHANGED);
	assert_true(mLibretroNetpacketSessionActive());
}

M_TEST_DEFINE(copiedPacketsDriveClientAttachmentReplies) {
	struct AdapterFixture* fixture = *state;
	assert_true(mLibretroNetpacketRegister(
	    _environment, fixture->core));
	fixture->frontend.callbacks.start(
	    1, _send, _pollReceive);

	struct GBALinkPacket localHello;
	assert_int_equal(
	    GBALinkPacketDecode(
	        fixture->frontend.lastPacket,
	        fixture->frontend.lastPacketSize,
	        GBA_LINK_ROLE_CLIENT, &localHello),
	    GBA_LINK_DECODE_OK);
	struct GBALinkPacket remoteHello = localHello;
	remoteHello.header.packetSequence = 1;
	_deliver(&fixture->frontend, &remoteHello);
	assert_int_equal(fixture->frontend.sends, 1);

	struct GBALinkPacket accept;
	memset(&accept, 0, sizeof(accept));
	accept.header.type = GBA_LINK_MESSAGE_ACCEPT;
	accept.header.packetSequence = 2;
	accept.payload.accept.proposedSessionId = 1;
	accept.payload.accept.hostTransportId = 0;
	accept.payload.accept.clientTransportId = 1;
	accept.payload.accept.policy =
	    GBA_LINK_COMPATIBILITY_EXACT_ROM;
	accept.payload.accept.attachCycle =
	    localHello.payload.hello.rendezvousCycle;
	accept.payload.accept.initialModeGeneration = 1;
	_deliver(&fixture->frontend, &accept);
	assert_int_equal(fixture->frontend.sends, 2);

	struct GBALinkPacket reply;
	assert_int_equal(
	    GBALinkPacketDecode(
	        fixture->frontend.lastPacket,
	        fixture->frontend.lastPacketSize,
	        GBA_LINK_ROLE_CLIENT, &reply),
	    GBA_LINK_DECODE_OK);
	assert_int_equal(
	    reply.header.type,
	    GBA_LINK_MESSAGE_ACCEPT_ACK);
	assert_int_equal(reply.header.sessionId, 1);
}

M_TEST_DEFINE(liveSessionGuardsAndResetTeardown) {
	struct AdapterFixture* fixture = *state;
	assert_true(mLibretroNetpacketRegister(
	    _environment, fixture->core));
	assert_false(
	    mLibretroNetpacketRejectStateOperation(
	        "Saving state"));
	assert_false(
	    mLibretroNetpacketRejectTimingChange(
	        "timing"));
	assert_false(
	    mLibretroNetpacketRejectCheatChange());

	fixture->frontend.callbacks.start(
	    0, _send, _pollReceive);
	assert_true(
	    mLibretroNetpacketRejectStateOperation(
	        "Saving state"));
	assert_true(
	    mLibretroNetpacketRejectTimingChange(
	        "timing"));
	assert_true(
	    mLibretroNetpacketRejectCheatChange());
	mLibretroNetpacketReset();
	assert_false(mLibretroNetpacketSessionActive());
	assert_false(
	    mLibretroNetpacketRejectStateOperation(
	        "Saving state"));
}

M_TEST_DEFINE(stateOperationsAreRejectedInEveryLiveState) {
	const enum GBALinkSessionState states[] = {
		GBA_LINK_SESSION_TRANSPORT_STARTED,
		GBA_LINK_SESSION_HELLO_EXCHANGED,
		GBA_LINK_SESSION_ACCEPTED,
		GBA_LINK_SESSION_ATTACH_BARRIER,
		GBA_LINK_SESSION_READY,
		GBA_LINK_SESSION_TRANSFERRING,
		GBA_LINK_SESSION_FAILED,
	};
	for (size_t i = 0;
	     i < sizeof(states) / sizeof(states[0]); ++i) {
		struct AdapterFixture* fixture = *state;
		assert_true(mLibretroNetpacketRegister(
		    _environment, fixture->core));
		fixture->frontend.callbacks.start(
		    1, _send, _pollReceive);
		mLibretroNetpacketTestSetSessionState(
		    states[i]);
		assert_true(
		    mLibretroNetpacketRejectStateOperation(
		        "Saving state"));
		assert_true(
		    mLibretroNetpacketRejectStateOperation(
		        "Loading state"));
		assert_true(
		    mLibretroNetpacketRejectTimingChange(
		        "CPU timing"));
		assert_true(
		    mLibretroNetpacketRejectCheatChange());
	mLibretroNetpacketReset();
	assert_false(
	    mLibretroNetpacketSessionActive());
	assert_false(
	    mLibretroNetpacketExecutionBlocked());
	assert_false(
	    mLibretroNetpacketRejectStateOperation(
		        "Saving state"));
	}
}

M_TEST_DEFINE(disconnectAndUnloadInvalidateCallbacks) {
	struct AdapterFixture* fixture = *state;
	assert_true(mLibretroNetpacketRegister(
	    _environment, fixture->core));
	fixture->frontend.callbacks.start(
	    0, _send, _pollReceive);
	assert_true(
	    fixture->frontend.callbacks.connected(1));
	unsigned sends = fixture->frontend.sends;
	fixture->frontend.callbacks.disconnected(1);
	mLibretroNetpacketRunBegin();
	assert_false(mLibretroNetpacketSessionActive());

	assert_true(mLibretroNetpacketRegister(
	    _environment, fixture->core));
	fixture->frontend.callbacks.start(
	    1, _send, _pollReceive);
	assert_true(mLibretroNetpacketSessionActive());
	mLibretroNetpacketUnload();
	assert_false(mLibretroNetpacketSessionActive());
	fixture->frontend.callbacks.poll();
	assert_int_equal(
	    fixture->frontend.sends, sends + 1);
}

M_TEST_SUITE_DEFINE(
    LibretroNetpacket,
    cmocka_unit_test_setup_teardown(
        registersOnlyWhenFrontendSupportsCommand78,
        _setup, _teardown),
    cmocka_unit_test_setup_teardown(
        hostStartsOnlyAfterOneClientIsAdmitted,
        _setup, _teardown),
    cmocka_unit_test_setup_teardown(
        hostMayStartSynchronouslyDuringRegistration,
        _setup, _teardown),
    cmocka_unit_test_setup_teardown(
        hostQueuesPacketDeliveredBeforeAdmissionCallback,
        _setup, _teardown),
    cmocka_unit_test_setup_teardown(
        clientStartsHandshakeTowardHost,
        _setup, _teardown),
    cmocka_unit_test_setup_teardown(
        scheduledCompletionReachesQuiescenceBeforeDeadline,
        _setup, _teardown),
    cmocka_unit_test_setup_teardown(
        permanentlyBusySioTimesOutFromPeerAdmission,
        _setup, _teardown),
    cmocka_unit_test_setup_teardown(
        missingReceivePollingFailsBeforeAttachment,
        _setup, _teardown),
    cmocka_unit_test_setup_teardown(
        stopDuringPollInvalidatesSavedCallbacks,
        _setup, _teardown),
    cmocka_unit_test_setup_teardown(
        stopDuringSendIsAnAdapterSendFailure,
        _setup, _teardown),
    cmocka_unit_test_setup_teardown(
        reliableFlushSendPrecedesReceivePoll,
        _setup, _teardown),
    cmocka_unit_test_setup_teardown(
        copiedQueueFailsClosedOnExhaustion,
        _setup, _teardown),
    cmocka_unit_test_setup_teardown(
        oversizedAndWrongSenderPacketsFailClosed,
        _setup, _teardown),
    cmocka_unit_test_setup_teardown(
        receiveCallbackCopiesBeforeFrontendReturns,
        _setup, _teardown),
    cmocka_unit_test_setup_teardown(
        copiedPacketsDriveClientAttachmentReplies,
        _setup, _teardown),
    cmocka_unit_test_setup_teardown(
        liveSessionGuardsAndResetTeardown,
        _setup, _teardown),
    cmocka_unit_test_setup_teardown(
        stateOperationsAreRejectedInEveryLiveState,
        _setup, _teardown),
    cmocka_unit_test_setup_teardown(
        disconnectAndUnloadInvalidateCallbacks,
        _setup, _teardown))
