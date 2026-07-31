/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef GBA_SIO_NETPLAY_TRANSPORT_H
#define GBA_SIO_NETPLAY_TRANSPORT_H

#include <mgba-util/common.h>

#include <mgba/internal/gba/sio/netplay/protocol.h>

CXX_GUARD_START

enum GBALinkDiagnosticLevel {
	GBA_LINK_DIAGNOSTIC_INFO,
	GBA_LINK_DIAGNOSTIC_WARN,
	GBA_LINK_DIAGNOSTIC_ERROR,
};

struct GBALinkTransportVTable {
	bool (*sendReliable)(void* context, const void* data, size_t size, bool flush);
	bool (*pollReceive)(void* context);
	void (*yield)(void* context);
	uint64_t (*monotonicTimeMs)(void* context);
	void (*diagnostic)(
	    void* context, enum GBALinkDiagnosticLevel level,
	    enum GBALinkReason reason, const char* message);
	void (*stop)(void* context);
};

struct GBALinkCopiedPacket {
	uint64_t generation;
	size_t size;
	uint8_t data[GBA_LINK_MAX_PACKET_SIZE];
};

struct GBALinkCopiedQueue {
	struct GBALinkCopiedPacket packets[GBA_LINK_MAX_COPIED_PACKETS];
	size_t readIndex;
	size_t size;
};

struct GBALinkTransport {
	const struct GBALinkTransportVTable* vtable;
	void* context;
	uint64_t generation;
	enum GBALinkRole localRole;
	bool active;
	bool inCallback;
	enum GBALinkReason failureReason;
	struct GBALinkCopiedQueue inbound;
	struct GBALinkCopiedQueue outbound;
};

void GBALinkTransportInit(
    struct GBALinkTransport* transport,
    const struct GBALinkTransportVTable* vtable,
    void* context);
void GBALinkTransportDeinit(struct GBALinkTransport* transport);
bool GBALinkTransportStart(
    struct GBALinkTransport* transport, uint64_t generation,
    enum GBALinkRole localRole);
void GBALinkTransportInvalidate(
    struct GBALinkTransport* transport, enum GBALinkReason reason,
    const char* diagnostic);
void GBALinkTransportRequestStop(
    struct GBALinkTransport* transport, enum GBALinkReason reason,
    const char* diagnostic);
bool GBALinkTransportIsActive(
    const struct GBALinkTransport* transport, uint64_t generation);
bool GBALinkTransportQueueInbound(
    struct GBALinkTransport* transport, uint64_t generation,
    const void* data, size_t size);
bool GBALinkTransportPopInbound(
    struct GBALinkTransport* transport, struct GBALinkCopiedPacket* packet);
bool GBALinkTransportSend(
    struct GBALinkTransport* transport, const void* data, size_t size,
    bool flush);
bool GBALinkTransportPoll(struct GBALinkTransport* transport);
uint64_t GBALinkTransportMonotonicTimeMs(
    const struct GBALinkTransport* transport);

CXX_GUARD_END

#endif
