/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/gba/sio/netplay/transport.h>

void GBALinkCopiedPacketDeinit(struct GBALinkCopiedPacket* packet) {
	if (!packet) {
		return;
	}
	free(packet->data);
	memset(packet, 0, sizeof(*packet));
}

void GBALinkCopiedQueueInit(struct GBALinkCopiedQueue* queue) {
	if (queue) {
		memset(queue, 0, sizeof(*queue));
	}
}

void GBALinkCopiedQueueDeinit(struct GBALinkCopiedQueue* queue) {
	if (!queue) {
		return;
	}
	for (size_t i = 0; i < GBA_LINK_MAX_COPIED_PACKETS; ++i) {
		GBALinkCopiedPacketDeinit(&queue->packets[i]);
	}
	memset(queue, 0, sizeof(*queue));
}

bool GBALinkCopiedQueuePush(
    struct GBALinkCopiedQueue* queue, uint64_t generation,
    const void* data, size_t size) {
	if (!queue || !data || !size ||
	    size > GBA_LINK_TRANSPORT_MAX_PACKET_SIZE ||
	    queue->size >= GBA_LINK_MAX_COPIED_PACKETS) {
		return false;
	}
	size_t index = (queue->readIndex + queue->size) % GBA_LINK_MAX_COPIED_PACKETS;
	struct GBALinkCopiedPacket* packet = &queue->packets[index];
	uint8_t* copy = malloc(size);
	if (!copy) {
		return false;
	}
	memcpy(copy, data, size);
	GBALinkCopiedPacketDeinit(packet);
	packet->generation = generation;
	packet->size = size;
	packet->data = copy;
	++queue->size;
	return true;
}

bool GBALinkCopiedQueuePop(
    struct GBALinkCopiedQueue* queue, struct GBALinkCopiedPacket* packet) {
	if (!queue || !queue->size) {
		return false;
	}
	if (packet) {
		*packet = queue->packets[queue->readIndex];
	} else {
		GBALinkCopiedPacketDeinit(&queue->packets[queue->readIndex]);
	}
	memset(&queue->packets[queue->readIndex], 0,
	    sizeof(queue->packets[queue->readIndex]));
	queue->readIndex = (queue->readIndex + 1) % GBA_LINK_MAX_COPIED_PACKETS;
	--queue->size;
	return true;
}

static void _diagnostic(
    struct GBALinkTransport* transport, enum GBALinkDiagnosticLevel level,
    enum GBALinkReason reason, const char* message) {
	if (transport->vtable && transport->vtable->diagnostic) {
		transport->vtable->diagnostic(
		    transport->context, level, reason, message ? message : "");
	}
}

void GBALinkTransportInit(
    struct GBALinkTransport* transport,
    const struct GBALinkTransportVTable* vtable,
    void* context) {
	memset(transport, 0, sizeof(*transport));
	transport->vtable = vtable;
	transport->context = context;
	GBALinkCopiedQueueInit(&transport->inbound);
	GBALinkCopiedQueueInit(&transport->outbound);
}

void GBALinkTransportDeinit(struct GBALinkTransport* transport) {
	if (!transport) {
		return;
	}
	if (transport->active) {
		GBALinkTransportInvalidate(
		    transport, GBA_LINK_REASON_TRANSPORT_STOP,
		    "transport deinitialized while active");
	}
	GBALinkCopiedQueueDeinit(&transport->inbound);
	GBALinkCopiedQueueDeinit(&transport->outbound);
	memset(transport, 0, sizeof(*transport));
}

bool GBALinkTransportStart(
    struct GBALinkTransport* transport, uint64_t generation,
    enum GBALinkRole localRole) {
	if (!transport || !transport->vtable || !generation ||
	    (localRole != GBA_LINK_ROLE_HOST && localRole != GBA_LINK_ROLE_CLIENT)) {
		return false;
	}
	if (transport->active) {
		GBALinkTransportInvalidate(
		    transport, GBA_LINK_REASON_INVALID_TRANSITION,
		    "transport restarted before stop");
		return false;
	}
	transport->generation = generation;
	transport->localRole = localRole;
	transport->active = true;
	transport->inCallback = false;
	transport->failureReason = 0;
	GBALinkCopiedQueueDeinit(&transport->inbound);
	GBALinkCopiedQueueDeinit(&transport->outbound);
	return true;
}

void GBALinkTransportInvalidate(
    struct GBALinkTransport* transport, enum GBALinkReason reason,
    const char* diagnostic) {
	if (!transport) {
		return;
	}
	bool wasActive = transport->active;
	transport->active = false;
	transport->failureReason = reason;
	GBALinkCopiedQueueDeinit(&transport->inbound);
	GBALinkCopiedQueueDeinit(&transport->outbound);
	if (wasActive && diagnostic) {
		_diagnostic(transport, GBA_LINK_DIAGNOSTIC_ERROR, reason, diagnostic);
	}
}

void GBALinkTransportRequestStop(
    struct GBALinkTransport* transport, enum GBALinkReason reason,
    const char* diagnostic) {
	if (!transport || !transport->active) {
		return;
	}
	const struct GBALinkTransportVTable* vtable = transport->vtable;
	void* context = transport->context;
	GBALinkTransportInvalidate(transport, reason, diagnostic);
	if (vtable && vtable->stop) {
		vtable->stop(context);
	}
}

bool GBALinkTransportIsActive(
    const struct GBALinkTransport* transport, uint64_t generation) {
	return transport && transport->active &&
	       transport->generation == generation;
}

bool GBALinkTransportQueueInbound(
    struct GBALinkTransport* transport, uint64_t generation,
    const void* data, size_t size) {
	if (!GBALinkTransportIsActive(transport, generation) || !data) {
		return false;
	}
	if (size > GBA_LINK_TRANSPORT_MAX_PACKET_SIZE) {
		GBALinkTransportInvalidate(
		    transport, GBA_LINK_REASON_OVERSIZED_PACKET,
		    "received packet exceeds copied-packet limit");
		return false;
	}
	if (!GBALinkCopiedQueuePush(&transport->inbound, generation, data, size)) {
		GBALinkTransportInvalidate(
		    transport, GBA_LINK_REASON_QUEUE_EXHAUSTED,
		    "inbound copied-packet queue exhausted");
		return false;
	}
	return true;
}

bool GBALinkTransportPopInbound(
    struct GBALinkTransport* transport, struct GBALinkCopiedPacket* packet) {
	if (!transport || !transport->active || !packet) {
		return false;
	}
	while (GBALinkCopiedQueuePop(&transport->inbound, packet)) {
		if (packet->generation == transport->generation) {
			return true;
		}
		GBALinkCopiedPacketDeinit(packet);
	}
	return false;
}

bool GBALinkTransportSend(
    struct GBALinkTransport* transport, const void* data, size_t size,
    bool flush) {
	if (!transport || !transport->active || !data ||
	    !transport->vtable || !transport->vtable->sendReliable) {
		return false;
	}
	if (size > GBA_LINK_TRANSPORT_MAX_PACKET_SIZE) {
		GBALinkTransportInvalidate(
		    transport, GBA_LINK_REASON_OVERSIZED_PACKET,
		    "outbound packet exceeds copied-packet limit");
		return false;
	}
	uint64_t generation = transport->generation;
	if (!GBALinkCopiedQueuePush(&transport->outbound, generation, data, size)) {
		GBALinkTransportInvalidate(
		    transport, GBA_LINK_REASON_QUEUE_EXHAUSTED,
		    "outbound copied-packet queue exhausted");
		return false;
	}

	struct GBALinkCopiedPacket packet;
	memset(&packet, 0, sizeof(packet));
	if (!GBALinkCopiedQueuePop(&transport->outbound, &packet)) {
		GBALinkTransportInvalidate(
		    transport, GBA_LINK_REASON_INVALID_TRANSITION,
		    "outbound copied-packet queue ordering failed");
		return false;
	}
	bool sent = transport->vtable->sendReliable(
	    transport->context, packet.data, packet.size, flush);
	GBALinkCopiedPacketDeinit(&packet);
	if (!GBALinkTransportIsActive(transport, generation)) {
		return false;
	}
	if (!sent) {
		GBALinkTransportInvalidate(
		    transport, GBA_LINK_REASON_SEND_FAILURE,
		    "reliable packet send failed");
		return false;
	}
	return true;
}

bool GBALinkTransportPoll(struct GBALinkTransport* transport) {
	if (!transport || !transport->active || !transport->vtable ||
	    !transport->vtable->pollReceive || transport->inCallback) {
		if (transport && transport->active && transport->inCallback) {
			GBALinkTransportInvalidate(
			    transport, GBA_LINK_REASON_INVALID_TRANSITION,
			    "receive poll re-entered");
		}
		return false;
	}
	uint64_t generation = transport->generation;
	transport->inCallback = true;
	bool polled = transport->vtable->pollReceive(transport->context);
	if (!GBALinkTransportIsActive(transport, generation)) {
		return false;
	}
	transport->inCallback = false;
	if (!polled) {
		GBALinkTransportInvalidate(
		    transport, GBA_LINK_REASON_TRANSPORT_STOP,
		    "receive polling failed");
		return false;
	}
	if (!transport->inbound.size &&
	    transport->vtable->yield) {
		transport->vtable->yield(
		    transport->context);
	}
	return true;
}

uint64_t GBALinkTransportMonotonicTimeMs(
    const struct GBALinkTransport* transport) {
	if (!transport || !transport->vtable ||
	    !transport->vtable->monotonicTimeMs) {
		return 0;
	}
	return transport->vtable->monotonicTimeMs(transport->context);
}

bool GBALinkTransportMonotonicTimeUs(
	const struct GBALinkTransport* transport, uint64_t* timestamp) {
	if (!timestamp || !transport || !transport->vtable ||
	    !transport->vtable->monotonicTimeUs) {
		return false;
	}
	return transport->vtable->monotonicTimeUs(
	    transport->context, timestamp);
}
