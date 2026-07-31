/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "netpacket-spike.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum FakePollMode {
	FAKE_POLL_RECEIVE,
	FAKE_POLL_STOP,
};

static struct retro_netpacket_callback callbacks;
static enum FakePollMode pollMode;
static unsigned pollCalls;
static unsigned sendCalls;

static bool fakeEnvironment(unsigned command, void* data) {
	assert(command == RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE);
	callbacks = *(const struct retro_netpacket_callback*) data;
	return true;
}

static void fakeSend(int flags, const void* buffer, size_t length, uint16_t clientId) {
	const uint8_t* bytes = buffer;
	assert(flags == (RETRO_NETPACKET_RELIABLE | RETRO_NETPACKET_FLUSH_HINT));
	assert(length == 17);
	assert(clientId == 0);
	assert(memcmp(bytes, "MNP0", 4) == 0);
	assert(bytes[4] == 2);
	assert(bytes[5] == 7);
	++sendCalls;
}

static void fakePollReceive(void) {
	static const uint8_t ping[17] = {
		'M', 'N', 'P', '0', 1, 7, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
	};
	++pollCalls;
	if (pollMode == FAKE_POLL_RECEIVE) {
		callbacks.receive(ping, sizeof(ping), 0);
	} else {
		callbacks.stop();
	}
}

int main(void) {
	mNetpacketSpikeRegister(fakeEnvironment);
	assert(callbacks.start);
	assert(callbacks.receive);
	assert(callbacks.stop);
	assert(callbacks.poll);
	assert(callbacks.connected);
	assert(callbacks.disconnected);
	assert(strcmp(callbacks.protocol_version, "mgba-netpacket-spike-v1") == 0);

	pollMode = FAKE_POLL_RECEIVE;
	callbacks.start(1, fakeSend, fakePollReceive);
	mNetpacketSpikeTimingBoundary();
	assert(pollCalls == 1);
	assert(sendCalls == 1);
	callbacks.stop();

	pollMode = FAKE_POLL_STOP;
	pollCalls = 0;
	callbacks.start(1, fakeSend, fakePollReceive);
	mNetpacketSpikeTimingBoundary();
	assert(pollCalls == 1);
	mNetpacketSpikeTimingBoundary();
	assert(pollCalls == 1);

	puts("netpacket-spike-test: pass");
	return 0;
}
