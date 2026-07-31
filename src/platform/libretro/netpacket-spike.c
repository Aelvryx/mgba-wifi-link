/* Copyright (c) 2026 mGBA Wi-Fi link contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "netpacket-spike.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef __ANDROID__
#include <android/log.h>
#endif

#define SPIKE_PACKET_SIZE 17
#define SPIKE_QUEUE_CAPACITY 16
#define SPIKE_NO_CLIENT UINT16_MAX
#ifndef SPIKE_POLL_INTERVAL_NS
#define SPIKE_POLL_INTERVAL_NS 250000000
#endif
#ifndef SPIKE_POLL_WINDOW_NS
#define SPIKE_POLL_WINDOW_NS 200000000
#endif

#ifndef MGBA_NETPACKET_SPIKE_PROTOCOL
#define MGBA_NETPACKET_SPIKE_PROTOCOL "mgba-netpacket-spike-v1"
#endif

enum SpikePacketType {
	SPIKE_PACKET_PING = 1,
	SPIKE_PACKET_ACK = 2,
};

struct SpikePacket {
	uint8_t bytes[SPIKE_PACKET_SIZE];
	uint16_t sender;
};

struct SpikeState {
	bool active;
	bool polling;
	uint16_t localId;
	uint16_t remoteId;
	uint64_t generation;
	uint32_t nextSequence;
	uint32_t callbackCount;
	uint64_t lastPingNs;
	uint64_t lastPollNs;
	retro_netpacket_send_t send;
	retro_netpacket_poll_receive_t pollReceive;
	struct SpikePacket queue[SPIKE_QUEUE_CAPACITY];
	size_t queueRead;
	size_t queueSize;
};

static struct SpikeState _state = {
	.localId = SPIKE_NO_CLIENT,
	.remoteId = SPIKE_NO_CLIENT,
};

static void _spikeLog(const char* format, ...) {
	va_list args;
#ifdef __ANDROID__
	va_list androidArgs;
#endif
	va_start(args, format);
#ifdef __ANDROID__
	va_copy(androidArgs, args);
#endif
	vfprintf(stderr, format, args);
	fflush(stderr);
#ifdef __ANDROID__
	__android_log_vprint(ANDROID_LOG_INFO, "mgba-netpacket-spike", format, androidArgs);
	va_end(androidArgs);
#endif
	va_end(args);
}

static uint32_t _load32le(const uint8_t* bytes) {
	return bytes[0] |
	       ((uint32_t) bytes[1] << 8) |
	       ((uint32_t) bytes[2] << 16) |
	       ((uint32_t) bytes[3] << 24);
}

static uint64_t _load64le(const uint8_t* bytes) {
	uint64_t value = 0;
	unsigned i;
	for (i = 0; i < 8; ++i) {
		value |= (uint64_t) bytes[i] << (i * 8);
	}
	return value;
}

static void _store32le(uint8_t* bytes, uint32_t value) {
	unsigned i;
	for (i = 0; i < 4; ++i) {
		bytes[i] = value >> (i * 8);
	}
}

static void _store64le(uint8_t* bytes, uint64_t value) {
	unsigned i;
	for (i = 0; i < 8; ++i) {
		bytes[i] = value >> (i * 8);
	}
}

static uint64_t _monotonicNs(void) {
	struct timespec time;
	if (clock_gettime(CLOCK_MONOTONIC, &time) != 0) {
		return 0;
	}
	return (uint64_t) time.tv_sec * 1000000000 + time.tv_nsec;
}

static void _resetTransport(void) {
	_state.active = false;
	_state.polling = false;
	_state.localId = SPIKE_NO_CLIENT;
	_state.remoteId = SPIKE_NO_CLIENT;
	_state.send = NULL;
	_state.pollReceive = NULL;
	_state.queueRead = 0;
	_state.queueSize = 0;
	++_state.generation;
}

static bool _decode(const uint8_t* bytes, enum SpikePacketType* type, uint32_t* sequence, uint64_t* sentNs) {
	if (memcmp(bytes, "MNP0", 4) != 0) {
		return false;
	}
	if (bytes[4] != SPIKE_PACKET_PING && bytes[4] != SPIKE_PACKET_ACK) {
		return false;
	}
	*type = bytes[4];
	*sequence = _load32le(&bytes[5]);
	*sentNs = _load64le(&bytes[9]);
	return true;
}

static void _send(enum SpikePacketType type, uint32_t sequence, uint64_t sentNs, uint16_t target) {
	uint8_t bytes[SPIKE_PACKET_SIZE];
	if (!_state.active || !_state.send || target == SPIKE_NO_CLIENT) {
		return;
	}
	memcpy(bytes, "MNP0", 4);
	bytes[4] = type;
	_store32le(&bytes[5], sequence);
	_store64le(&bytes[9], sentNs);
	_state.send(RETRO_NETPACKET_RELIABLE | RETRO_NETPACKET_FLUSH_HINT,
	            bytes, sizeof(bytes), target);
	_spikeLog("[mgba-netpacket-spike] event=send type=%s sequence=%" PRIu32
	          " target=%" PRIu16 " generation=%" PRIu64 " time_ns=%" PRIu64 "\n",
	          type == SPIKE_PACKET_PING ? "ping" : "ack", sequence, target,
	          _state.generation, _monotonicNs());
}

static void _failClosed(const char* reason) {
	_spikeLog("[mgba-netpacket-spike] event=local_failure reason=%s generation=%" PRIu64
	          " time_ns=%" PRIu64 "\n",
	          reason, _state.generation, _monotonicNs());
	_resetTransport();
}

static void _drainQueue(const char* boundary) {
	while (_state.active && _state.queueSize) {
		struct SpikePacket packet = _state.queue[_state.queueRead];
		enum SpikePacketType type;
		uint32_t sequence;
		uint64_t sentNs;
		_state.queueRead = (_state.queueRead + 1) % SPIKE_QUEUE_CAPACITY;
		--_state.queueSize;
		if (!_decode(packet.bytes, &type, &sequence, &sentNs)) {
			_failClosed("invalid_packet");
			return;
		}
		_spikeLog("[mgba-netpacket-spike] event=process boundary=%s type=%s"
		          " sequence=%" PRIu32 " sender=%" PRIu16 " generation=%" PRIu64
		          " time_ns=%" PRIu64 "\n",
		          boundary, type == SPIKE_PACKET_PING ? "ping" : "ack",
		          sequence, packet.sender, _state.generation, _monotonicNs());
		if (type == SPIKE_PACKET_PING) {
			_send(SPIKE_PACKET_ACK, sequence, sentNs, packet.sender);
		} else {
			uint64_t now = _monotonicNs();
			uint64_t rtt = now >= sentNs ? now - sentNs : 0;
			_spikeLog("[mgba-netpacket-spike] event=rtt sequence=%" PRIu32
			          " sender=%" PRIu16 " rtt_ns=%" PRIu64
			          " generation=%" PRIu64 "\n",
			          sequence, packet.sender, rtt, _state.generation);
		}
	}
}

static void _start(uint16_t clientId, retro_netpacket_send_t send,
                   retro_netpacket_poll_receive_t pollReceive) {
	_resetTransport();
	_state.active = true;
	_state.localId = clientId;
	_state.remoteId = clientId ? 0 : SPIKE_NO_CLIENT;
	_state.send = send;
	_state.pollReceive = pollReceive;
	_state.nextSequence = 1;
	_state.callbackCount = 0;
	_state.lastPingNs = 0;
	_state.lastPollNs = 0;
	_spikeLog("[mgba-netpacket-spike] event=start local_id=%" PRIu16
	          " role=%s generation=%" PRIu64 " poll_receive=%s time_ns=%" PRIu64 "\n",
	          clientId, clientId ? "client" : "host", _state.generation,
	          pollReceive ? "yes" : "no", _monotonicNs());
}

static void _receive(const void* buffer, size_t length, uint16_t clientId) {
	size_t index;
	if (!_state.active) {
		return;
	}
	if (length != SPIKE_PACKET_SIZE) {
		_failClosed("invalid_size");
		return;
	}
	if (_state.queueSize == SPIKE_QUEUE_CAPACITY) {
		_failClosed("queue_full");
		return;
	}
	index = (_state.queueRead + _state.queueSize) % SPIKE_QUEUE_CAPACITY;
	memcpy(_state.queue[index].bytes, buffer, length);
	_state.queue[index].sender = clientId;
	++_state.queueSize;
	_spikeLog("[mgba-netpacket-spike] event=receive_queued sender=%" PRIu16
	          " queue_size=%zu polling=%s generation=%" PRIu64
	          " time_ns=%" PRIu64 "\n",
	          clientId, _state.queueSize, _state.polling ? "yes" : "no",
	          _state.generation, _monotonicNs());
}

static void _stop(void) {
	uint64_t generation = _state.generation;
	bool polling = _state.polling;
	_resetTransport();
	_spikeLog("[mgba-netpacket-spike] event=stop old_generation=%" PRIu64
	          " new_generation=%" PRIu64 " during_poll=%s time_ns=%" PRIu64 "\n",
	          generation, _state.generation, polling ? "yes" : "no", _monotonicNs());
}

static void _poll(void) {
	uint64_t now;
	if (!_state.active) {
		return;
	}
	++_state.callbackCount;
	_drainQueue("frontend_poll");
	now = _monotonicNs();
	if (_state.active && _state.remoteId != SPIKE_NO_CLIENT &&
	    (!_state.lastPingNs || now - _state.lastPingNs >= 250000000)) {
		uint32_t sequence = _state.nextSequence++;
		_state.lastPingNs = now;
		_send(SPIKE_PACKET_PING, sequence, now, _state.remoteId);
	}
}

static bool _connected(uint16_t clientId) {
	if (!_state.active || _state.localId != 0 || _state.remoteId != SPIKE_NO_CLIENT) {
		_spikeLog("[mgba-netpacket-spike] event=connect_rejected client_id=%" PRIu16
		          " generation=%" PRIu64 " time_ns=%" PRIu64 "\n",
		          clientId, _state.generation, _monotonicNs());
		return false;
	}
	_state.remoteId = clientId;
	_spikeLog("[mgba-netpacket-spike] event=connected client_id=%" PRIu16
	          " generation=%" PRIu64 " time_ns=%" PRIu64 "\n",
	          clientId, _state.generation, _monotonicNs());
	return true;
}

static void _disconnected(uint16_t clientId) {
	_spikeLog("[mgba-netpacket-spike] event=disconnected client_id=%" PRIu16
	          " generation=%" PRIu64 " time_ns=%" PRIu64 "\n",
	          clientId, _state.generation, _monotonicNs());
	if (_state.remoteId == clientId) {
		_state.remoteId = SPIKE_NO_CLIENT;
	}
}

void mNetpacketSpikeRegister(retro_environment_t environment) {
	static const struct retro_netpacket_callback callbacks = {
		.start = _start,
		.receive = _receive,
		.stop = _stop,
		.poll = _poll,
		.connected = _connected,
		.disconnected = _disconnected,
		.protocol_version = MGBA_NETPACKET_SPIKE_PROTOCOL,
	};
	bool registered = environment(RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE,
	                              (void*) &callbacks);
	_spikeLog("[mgba-netpacket-spike] event=register command=78 result=%s"
	          " protocol=%s time_ns=%" PRIu64 "\n",
	          registered ? "accepted" : "rejected", callbacks.protocol_version,
	          _monotonicNs());
}

void mNetpacketSpikeTimingBoundary(void) {
	uint64_t deadline;
	uint64_t generation;
	uint64_t now;
	retro_netpacket_poll_receive_t pollReceive;
	if (!_state.active) {
		return;
	}
	_drainQueue("timing_boundary");
	now = _monotonicNs();
	if (!_state.active || !_state.pollReceive ||
	    (_state.lastPollNs && now - _state.lastPollNs < SPIKE_POLL_INTERVAL_NS)) {
		return;
	}
	_state.lastPollNs = now;
	generation = _state.generation;
	pollReceive = _state.pollReceive;
	_state.polling = true;
	_spikeLog("[mgba-netpacket-spike] event=poll_receive_enter generation=%" PRIu64
	          " time_ns=%" PRIu64 "\n",
	          generation, _monotonicNs());
	deadline = _monotonicNs() + SPIKE_POLL_WINDOW_NS;
	do {
		pollReceive();
		if (_state.generation != generation) {
			_spikeLog("[mgba-netpacket-spike] event=poll_receive_invalidated"
			          " old_generation=%" PRIu64 " new_generation=%" PRIu64
			          " time_ns=%" PRIu64 "\n",
			          generation, _state.generation, _monotonicNs());
			return;
		}
	} while (_monotonicNs() < deadline);
	_state.polling = false;
	_drainQueue("poll_receive");
	_spikeLog("[mgba-netpacket-spike] event=poll_receive_exit generation=%" PRIu64
	          " time_ns=%" PRIu64 "\n",
	          generation, _monotonicNs());
}

void mNetpacketSpikeUnload(void) {
	_resetTransport();
}
