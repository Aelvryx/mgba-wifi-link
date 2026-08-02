/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "util/test/suite.h"

#include <mgba/internal/gba/sio/netplay/session.h>
#include <mgba/internal/gba/sio/netplay/transport.h>

struct FakeTransport {
	struct GBALinkTransport* transport;
	bool sendResult;
	bool pollResult;
	bool stopDuringSend;
	bool stopDuringPoll;
	bool queueDuringPoll;
	bool reenterPoll;
	uint64_t now;
	unsigned sendCalls;
	unsigned pollCalls;
	unsigned stopCalls;
	unsigned diagnosticCalls;
	enum GBALinkReason diagnosticReason;
	size_t sentSize;
	uint8_t sent[GBA_LINK_TRANSPORT_MAX_PACKET_SIZE];
};

static bool _sendReliable(void* context, const void* data, size_t size, bool flush) {
	struct FakeTransport* fake = context;
	++fake->sendCalls;
	assert_true(flush);
	fake->sentSize = size;
	memcpy(fake->sent, data, size);
	if (fake->stopDuringSend) {
		GBALinkTransportInvalidate(
		    fake->transport, GBA_LINK_REASON_TRANSPORT_STOP,
		    "synchronous stop during send");
	}
	return fake->sendResult;
}

static bool _pollReceive(void* context) {
	struct FakeTransport* fake = context;
	++fake->pollCalls;
	if (fake->queueDuringPoll) {
		const uint8_t packet[] = { 1, 2, 3, 4 };
		assert_true(GBALinkTransportQueueInbound(
		    fake->transport, fake->transport->generation,
		    packet, sizeof(packet)));
	}
	if (fake->reenterPoll) {
		assert_false(GBALinkTransportPoll(fake->transport));
	}
	if (fake->stopDuringPoll) {
		GBALinkTransportInvalidate(
		    fake->transport, GBA_LINK_REASON_TRANSPORT_STOP,
		    "synchronous stop during poll");
	}
	return fake->pollResult;
}

static uint64_t _monotonicTimeMs(void* context) {
	struct FakeTransport* fake = context;
	return fake->now;
}

static bool _monotonicTimeUs(void* context, uint64_t* timestamp) {
	struct FakeTransport* fake = context;
	if (!timestamp || fake->now > UINT64_MAX / 1000) {
		return false;
	}
	*timestamp = fake->now * 1000;
	return true;
}

static void _diagnostic(
    void* context, enum GBALinkDiagnosticLevel level,
    enum GBALinkReason reason, const char* message) {
	struct FakeTransport* fake = context;
	assert_true(level >= GBA_LINK_DIAGNOSTIC_INFO);
	assert_non_null(message);
	++fake->diagnosticCalls;
	fake->diagnosticReason = reason;
}

static void _stop(void* context) {
	struct FakeTransport* fake = context;
	++fake->stopCalls;
}

static const struct GBALinkTransportVTable _vtable = {
	.sendReliable = _sendReliable,
	.pollReceive = _pollReceive,
	.monotonicTimeMs = _monotonicTimeMs,
	.monotonicTimeUs = _monotonicTimeUs,
	.diagnostic = _diagnostic,
	.stop = _stop,
};

static void _init(
    struct GBALinkTransport* transport, struct FakeTransport* fake) {
	memset(fake, 0, sizeof(*fake));
	fake->transport = transport;
	fake->sendResult = true;
	fake->pollResult = true;
	fake->now = 1234;
	GBALinkTransportInit(transport, &_vtable, fake);
	assert_true(GBALinkTransportStart(transport, 1, GBA_LINK_ROLE_HOST));
}

M_TEST_DEFINE(sequenceDomainsAreIndependentAndNeverWrap) {
	struct GBALinkSequenceState sequences;
	GBALinkSequenceStateInit(&sequences);
	for (enum GBALinkSequenceDomain domain = GBA_LINK_SEQUENCE_PACKET;
	     domain < GBA_LINK_SEQUENCE_DOMAIN_COUNT;
	     domain = (enum GBALinkSequenceDomain) (domain + 1)) {
		uint64_t value = 0;
		assert_true(GBALinkSequenceTake(&sequences, domain, &value));
		assert_int_equal(value, 1);
	}

	uint64_t value = 0;
	assert_true(GBALinkSequenceTake(&sequences, GBA_LINK_SEQUENCE_PACKET, &value));
	assert_int_equal(value, 2);
	assert_true(GBALinkSequenceTake(&sequences, GBA_LINK_SEQUENCE_TRANSFER, &value));
	assert_int_equal(value, 2);
	assert_int_equal(sequences.next[GBA_LINK_SEQUENCE_MODE], 2);

	sequences.next[GBA_LINK_SEQUENCE_COMPLETION] = UINT64_MAX;
	assert_true(GBALinkSequenceTake(
	    &sequences, GBA_LINK_SEQUENCE_COMPLETION, &value));
	assert_int_equal(value, UINT64_MAX);
	assert_true(GBALinkSequenceIsExhausted(
	    &sequences, GBA_LINK_SEQUENCE_COMPLETION));
	assert_false(GBALinkSequenceTake(
	    &sequences, GBA_LINK_SEQUENCE_COMPLETION, &value));
	assert_int_equal(sequences.next[GBA_LINK_SEQUENCE_COMPLETION], UINT64_MAX);
}

M_TEST_DEFINE(reliableFlushedSendCopiesBeforeCallback) {
	struct GBALinkTransport transport;
	struct FakeTransport fake;
	_init(&transport, &fake);
	uint8_t packet[] = { 1, 2, 3, 4 };
	const uint8_t expected[] = { 1, 2, 3, 4 };
	assert_true(GBALinkTransportSend(
	    &transport, packet, sizeof(packet), true));
	memset(packet, 0, sizeof(packet));
	assert_int_equal(fake.sendCalls, 1);
	assert_int_equal(fake.sentSize, 4);
	assert_memory_equal(fake.sent, expected, sizeof(expected));
	assert_true(GBALinkTransportIsActive(&transport, 1));
	GBALinkTransportDeinit(&transport);
}

M_TEST_DEFINE(sendFailureFailsClosed) {
	struct GBALinkTransport transport;
	struct FakeTransport fake;
	_init(&transport, &fake);
	fake.sendResult = false;
	const uint8_t packet[] = { 1 };
	assert_false(GBALinkTransportSend(
	    &transport, packet, sizeof(packet), true));
	assert_false(transport.active);
	assert_int_equal(transport.failureReason, GBA_LINK_REASON_SEND_FAILURE);
	assert_int_equal(fake.diagnosticReason, GBA_LINK_REASON_SEND_FAILURE);
	GBALinkTransportDeinit(&transport);
}

M_TEST_DEFINE(synchronousStopDuringSendInvalidatesGeneration) {
	struct GBALinkTransport transport;
	struct FakeTransport fake;
	_init(&transport, &fake);
	fake.stopDuringSend = true;
	const uint8_t packet[] = { 1 };
	assert_false(GBALinkTransportSend(
	    &transport, packet, sizeof(packet), true));
	assert_false(transport.active);
	assert_false(GBALinkTransportIsActive(&transport, 1));
	assert_int_equal(transport.failureReason, GBA_LINK_REASON_TRANSPORT_STOP);
	GBALinkTransportDeinit(&transport);
}

M_TEST_DEFINE(pollCopiesInboundBeforeReturn) {
	struct GBALinkTransport transport;
	struct FakeTransport fake;
	_init(&transport, &fake);
	fake.queueDuringPoll = true;
	assert_true(GBALinkTransportPoll(&transport));
	assert_int_equal(fake.pollCalls, 1);

	struct GBALinkCopiedPacket packet;
	const uint8_t expected[] = { 1, 2, 3, 4 };
	assert_true(GBALinkTransportPopInbound(&transport, &packet));
	assert_int_equal(packet.generation, 1);
	assert_int_equal(packet.size, 4);
	assert_memory_equal(packet.data, expected, sizeof(expected));
	GBALinkCopiedPacketDeinit(&packet);
	assert_false(GBALinkTransportPopInbound(&transport, &packet));
	GBALinkTransportDeinit(&transport);
}

M_TEST_DEFINE(synchronousStopDuringPollIsRechecked) {
	struct GBALinkTransport transport;
	struct FakeTransport fake;
	_init(&transport, &fake);
	fake.stopDuringPoll = true;
	assert_false(GBALinkTransportPoll(&transport));
	assert_false(transport.active);
	assert_int_equal(transport.failureReason, GBA_LINK_REASON_TRANSPORT_STOP);
	GBALinkTransportDeinit(&transport);
}

M_TEST_DEFINE(reentrantPollFailsClosed) {
	struct GBALinkTransport transport;
	struct FakeTransport fake;
	_init(&transport, &fake);
	fake.reenterPoll = true;
	assert_false(GBALinkTransportPoll(&transport));
	assert_false(transport.active);
	assert_int_equal(
	    transport.failureReason, GBA_LINK_REASON_INVALID_TRANSITION);
	GBALinkTransportDeinit(&transport);
}

M_TEST_DEFINE(inboundQueueExhaustionFailsClosed) {
	struct GBALinkTransport transport;
	struct FakeTransport fake;
	_init(&transport, &fake);
	const uint8_t packet[] = { 1 };
	for (unsigned i = 0; i < GBA_LINK_MAX_COPIED_PACKETS; ++i) {
		assert_true(GBALinkTransportQueueInbound(
		    &transport, 1, packet, sizeof(packet)));
	}
	assert_false(GBALinkTransportQueueInbound(
	    &transport, 1, packet, sizeof(packet)));
	assert_false(transport.active);
	assert_int_equal(
	    transport.failureReason, GBA_LINK_REASON_QUEUE_EXHAUSTED);
	assert_int_equal(transport.inbound.size, 0);
	GBALinkTransportDeinit(&transport);
}

M_TEST_DEFINE(oversizedPacketsFailClosed) {
	struct GBALinkTransport transport;
	struct FakeTransport fake;
	_init(&transport, &fake);
	uint8_t packet[GBA_LINK_TRANSPORT_MAX_PACKET_SIZE + 1] = {0};
	assert_false(GBALinkTransportQueueInbound(
	    &transport, 1, packet, sizeof(packet)));
	assert_int_equal(
	    transport.failureReason, GBA_LINK_REASON_OVERSIZED_PACKET);

	assert_true(GBALinkTransportStart(&transport, 2, GBA_LINK_ROLE_HOST));
	assert_false(GBALinkTransportSend(
	    &transport, packet, sizeof(packet), true));
	assert_int_equal(
	    transport.failureReason, GBA_LINK_REASON_OVERSIZED_PACKET);
	GBALinkTransportDeinit(&transport);
}

M_TEST_DEFINE(oldGenerationCannotEnterNewSession) {
	struct GBALinkTransport transport;
	struct FakeTransport fake;
	_init(&transport, &fake);
	const uint8_t packet[] = { 1 };
	assert_true(GBALinkTransportQueueInbound(
	    &transport, 1, packet, sizeof(packet)));
	GBALinkTransportInvalidate(
	    &transport, GBA_LINK_REASON_TRANSPORT_STOP, "test stop");
	assert_true(GBALinkTransportStart(
	    &transport, 2, GBA_LINK_ROLE_CLIENT));
	assert_false(GBALinkTransportQueueInbound(
	    &transport, 1, packet, sizeof(packet)));
	struct GBALinkCopiedPacket copied;
	assert_false(GBALinkTransportPopInbound(&transport, &copied));
	assert_true(transport.active);
	assert_int_equal(GBALinkTransportMonotonicTimeMs(&transport), 1234);
	uint64_t nowUs = 0;
	assert_true(GBALinkTransportMonotonicTimeUs(&transport, &nowUs));
	assert_int_equal(nowUs, 1234000);
	assert_false(GBALinkTransportMonotonicTimeUs(&transport, NULL));
	GBALinkTransportDeinit(&transport);
}

M_TEST_DEFINE(explicitStopInvalidatesBeforeCallback) {
	struct GBALinkTransport transport;
	struct FakeTransport fake;
	_init(&transport, &fake);
	GBALinkTransportRequestStop(
	    &transport, GBA_LINK_REASON_USER_DISCONNECT, "user stop");
	assert_false(transport.active);
	assert_int_equal(fake.stopCalls, 1);
	assert_int_equal(
	    transport.failureReason, GBA_LINK_REASON_USER_DISCONNECT);
	GBALinkTransportDeinit(&transport);
}

M_TEST_DEFINE(sessionDeinitStopsEveryLiveState) {
	struct GBALinkTransport transport;
	struct FakeTransport fake;
	_init(&transport, &fake);
	struct GBALinkSession session;
	GBALinkSessionInit(&session, &transport);
	session.state = GBA_LINK_SESSION_ATTACH_BARRIER;
	GBALinkSessionDeinit(&session);
	assert_false(transport.active);
	assert_int_equal(fake.stopCalls, 1);
}

M_TEST_SUITE_DEFINE(GBALinkTransport,
	cmocka_unit_test(sequenceDomainsAreIndependentAndNeverWrap),
	cmocka_unit_test(reliableFlushedSendCopiesBeforeCallback),
	cmocka_unit_test(sendFailureFailsClosed),
	cmocka_unit_test(synchronousStopDuringSendInvalidatesGeneration),
	cmocka_unit_test(pollCopiesInboundBeforeReturn),
	cmocka_unit_test(synchronousStopDuringPollIsRechecked),
	cmocka_unit_test(reentrantPollFailsClosed),
	cmocka_unit_test(inboundQueueExhaustionFailsClosed),
	cmocka_unit_test(oversizedPacketsFailClosed),
	cmocka_unit_test(oldGenerationCannotEnterNewSession),
	cmocka_unit_test(explicitStopInvalidatesBeforeCallback),
	cmocka_unit_test(sessionDeinitStopsEveryLiveState))
