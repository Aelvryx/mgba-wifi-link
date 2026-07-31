/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/gba/sio/netplay/input-sync.h>

const char* GBALinkInputResultName(enum GBALinkInputResult result) {
	switch (result) {
	case GBA_LINK_INPUT_OK: return "ok";
	case GBA_LINK_INPUT_DUPLICATE: return "duplicate";
	case GBA_LINK_INPUT_INVALID_ARGUMENT: return "invalid argument";
	case GBA_LINK_INPUT_INVALID_STATE: return "invalid input state";
	case GBA_LINK_INPUT_WRONG_OWNER: return "wrong input owner";
	case GBA_LINK_INPUT_WRONG_GENERATION: return "wrong snapshot generation";
	case GBA_LINK_INPUT_CONFLICT: return "conflicting duplicate input";
	case GBA_LINK_INPUT_OUT_OF_WINDOW: return "input outside bounded window";
	case GBA_LINK_INPUT_MISSING: return "input missing";
	case GBA_LINK_INPUT_OVERFLOW: return "input frame overflow";
	}
	return "unknown";
}

uint16_t GBALinkInputSelectDelay(
	uint16_t minimum, uint16_t maximum,
	uint32_t roundTripMs, uint32_t jitterMs) {
	if (minimum > maximum || maximum > GBA_LINK_V2_MAX_INPUT_DELAY) {
		return UINT16_MAX;
	}
	/*
	 * Budget one-way transit plus two jitter envelopes. A further frame
	 * prevents a packet arriving exactly on the execution boundary.
	 */
	uint64_t budgetUs = (uint64_t) roundTripMs * 500 +
	                    (uint64_t) jitterMs * 2000;
	uint64_t frames = (budgetUs + 16742 - 1) / 16742 + 1;
	if (frames < minimum) {
		frames = minimum;
	}
	if (frames > maximum) {
		frames = maximum;
	}
	return frames;
}

bool GBALinkInputSyncInit(
	struct GBALinkInputSync* sync, uint64_t snapshotGeneration,
	uint8_t localPlayer, uint16_t delay, uint64_t firstFrame) {
	if (!sync || !snapshotGeneration || localPlayer > 1 ||
	    delay > GBA_LINK_V2_MAX_INPUT_DELAY ||
	    firstFrame > UINT64_MAX - delay) {
		return false;
	}
	memset(sync, 0, sizeof(*sync));
	sync->snapshotGeneration = snapshotGeneration;
	sync->localPlayer = localPlayer;
	sync->delay = delay;
	sync->nextFrame = firstFrame;
	sync->initialized = true;
	return true;
}

static enum GBALinkInputResult _checkInsert(
	const struct GBALinkInputSync* sync, uint8_t player,
	uint64_t frame, uint16_t keys) {
	if (keys & ~GBA_LINK_V2_INPUT_KEY_MASK) {
		return GBA_LINK_INPUT_INVALID_ARGUMENT;
	}
	const struct GBALinkInputSlot* slot =
	    &sync->rings[player][frame % GBA_LINK_INPUT_RING_CAPACITY];
	if (frame < sync->nextFrame) {
		if (slot->present && slot->frame == frame) {
			return slot->keys == keys
			    ? GBA_LINK_INPUT_DUPLICATE
			    : GBA_LINK_INPUT_CONFLICT;
		}
		return GBA_LINK_INPUT_OUT_OF_WINDOW;
	}
	if (frame - sync->nextFrame >= GBA_LINK_INPUT_RING_CAPACITY) {
		return GBA_LINK_INPUT_OUT_OF_WINDOW;
	}
	if (slot->present) {
		if (slot->frame != frame && slot->frame >= sync->nextFrame) {
			return GBA_LINK_INPUT_OUT_OF_WINDOW;
		}
		if (slot->frame == frame) {
			return slot->keys == keys
			    ? GBA_LINK_INPUT_DUPLICATE
			    : GBA_LINK_INPUT_CONFLICT;
		}
	}
	return GBA_LINK_INPUT_OK;
}

static enum GBALinkInputResult _insert(
	struct GBALinkInputSync* sync, uint8_t player,
	uint64_t frame, uint16_t keys) {
	enum GBALinkInputResult result = _checkInsert(
	    sync, player, frame, keys);
	if (result != GBA_LINK_INPUT_OK) {
		return result;
	}
	struct GBALinkInputSlot* slot =
	    &sync->rings[player][frame % GBA_LINK_INPUT_RING_CAPACITY];
	slot->frame = frame;
	slot->keys = keys;
	slot->present = true;
	return GBA_LINK_INPUT_OK;
}

static void _beginBatch(
	struct GBALinkInputSync* sync, struct GBALinkV2Packet* packet) {
	memset(packet, 0, sizeof(*packet));
	packet->header.type = GBA_LINK_V2_MESSAGE_INPUT_BATCH;
	packet->payload.inputBatch.snapshotGeneration =
	    sync->snapshotGeneration;
	packet->payload.inputBatch.player = sync->localPlayer;
}

static void _appendPresentRange(
	struct GBALinkInputSync* sync, uint64_t first, uint64_t last,
	struct GBALinkV2InputBatch* batch) {
	for (uint64_t frame = first; frame <= last; ++frame) {
		const struct GBALinkInputSlot* slot =
		    &sync->rings[sync->localPlayer]
	                [frame % GBA_LINK_INPUT_RING_CAPACITY];
		if (!slot->present || slot->frame != frame ||
		    batch->count >= GBA_LINK_V2_MAX_INPUT_RECORDS) {
			continue;
		}
		struct GBALinkV2InputRecord* record =
		    &batch->records[batch->count++];
		record->frame = frame;
		record->keys = slot->keys;
		if (frame == UINT64_MAX) {
			break;
		}
	}
}

enum GBALinkInputResult GBALinkInputSyncSeedLocal(
	struct GBALinkInputSync* sync, uint16_t keys,
	struct GBALinkV2Packet* packet) {
	if (!sync || !packet || !sync->initialized || sync->authored) {
		return GBA_LINK_INPUT_INVALID_STATE;
	}
	uint64_t last = sync->delay
	    ? sync->nextFrame + sync->delay - 1
	    : sync->nextFrame;
	for (uint64_t frame = sync->nextFrame; frame <= last; ++frame) {
		enum GBALinkInputResult result = _insert(
		    sync, sync->localPlayer, frame, keys);
		if (result != GBA_LINK_INPUT_OK) {
			return result;
		}
	}
	_beginBatch(sync, packet);
	_appendPresentRange(sync, sync->nextFrame, last,
	    &packet->payload.inputBatch);
	sync->authored = true;
	sync->lastAuthoredFrame = last;
	return GBA_LINK_INPUT_OK;
}

enum GBALinkInputResult GBALinkInputSyncAuthor(
	struct GBALinkInputSync* sync, uint64_t currentFrame,
	uint16_t keys, struct GBALinkV2Packet* packet) {
	if (!sync || !packet || !sync->initialized || !sync->authored ||
	    currentFrame != sync->nextFrame) {
		return GBA_LINK_INPUT_INVALID_STATE;
	}
	if (currentFrame > UINT64_MAX - sync->delay) {
		return GBA_LINK_INPUT_OVERFLOW;
	}
	uint64_t target = currentFrame + sync->delay;
	enum GBALinkInputResult result = _insert(
	    sync, sync->localPlayer, target, keys);
	if (result != GBA_LINK_INPUT_OK && result != GBA_LINK_INPUT_DUPLICATE) {
		return result;
	}
	_beginBatch(sync, packet);
	uint64_t first = target >= GBA_LINK_INPUT_REDUNDANCY - 1
	    ? target - (GBA_LINK_INPUT_REDUNDANCY - 1)
	    : 0;
	if (first < sync->nextFrame) {
		first = sync->nextFrame;
	}
	_appendPresentRange(sync, first, target, &packet->payload.inputBatch);
	if (!packet->payload.inputBatch.count) {
		return GBA_LINK_INPUT_INVALID_STATE;
	}
	sync->lastAuthoredFrame = target;
	return result;
}

enum GBALinkInputResult GBALinkInputSyncHandleBatch(
	struct GBALinkInputSync* sync, enum GBALinkRole senderRole,
	const struct GBALinkV2InputBatch* batch) {
	if (!sync || !batch || !sync->initialized || !batch->count ||
	    batch->count > GBA_LINK_V2_MAX_INPUT_RECORDS) {
		return GBA_LINK_INPUT_INVALID_ARGUMENT;
	}
	if (batch->snapshotGeneration != sync->snapshotGeneration) {
		return GBA_LINK_INPUT_WRONG_GENERATION;
	}
	uint8_t remotePlayer = sync->localPlayer ^ 1;
	uint8_t senderPlayer = senderRole == GBA_LINK_ROLE_HOST ? 0 : 1;
	if (batch->player != senderPlayer || batch->player != remotePlayer) {
		return GBA_LINK_INPUT_WRONG_OWNER;
	}
	bool inserted = false;
	/* Validate the complete batch before mutating the authoritative ring. */
	for (unsigned i = 0; i < batch->count; ++i) {
		if (i && (batch->records[i - 1].frame == UINT64_MAX ||
		          batch->records[i].frame !=
		              batch->records[i - 1].frame + 1)) {
			return GBA_LINK_INPUT_INVALID_ARGUMENT;
		}
		enum GBALinkInputResult result = _checkInsert(
		    sync, remotePlayer, batch->records[i].frame,
		    batch->records[i].keys);
		if (result == GBA_LINK_INPUT_OK) {
			inserted = true;
		} else if (result != GBA_LINK_INPUT_DUPLICATE) {
			return result;
		}
	}
	for (unsigned i = 0; i < batch->count; ++i) {
		enum GBALinkInputResult result = _insert(
		    sync, remotePlayer, batch->records[i].frame,
		    batch->records[i].keys);
		if (result != GBA_LINK_INPUT_OK &&
		    result != GBA_LINK_INPUT_DUPLICATE) {
			/* The preflight above makes this unreachable. */
			return GBA_LINK_INPUT_INVALID_STATE;
		}
	}
	return inserted ? GBA_LINK_INPUT_OK : GBA_LINK_INPUT_DUPLICATE;
}

bool GBALinkInputSyncReady(
	const struct GBALinkInputSync* sync, uint64_t frame) {
	if (!sync || !sync->initialized || frame != sync->nextFrame) {
		return false;
	}
	for (unsigned player = 0; player < 2; ++player) {
		const struct GBALinkInputSlot* slot =
		    &sync->rings[player][frame % GBA_LINK_INPUT_RING_CAPACITY];
		if (!slot->present || slot->frame != frame) {
			return false;
		}
	}
	return true;
}

enum GBALinkInputResult GBALinkInputSyncConsume(
	struct GBALinkInputSync* sync, uint64_t frame,
	uint16_t keys[2]) {
	if (!sync || !keys || !sync->initialized || frame != sync->nextFrame) {
		return GBA_LINK_INPUT_INVALID_STATE;
	}
	if (!GBALinkInputSyncReady(sync, frame)) {
		return GBA_LINK_INPUT_MISSING;
	}
	for (unsigned player = 0; player < 2; ++player) {
		struct GBALinkInputSlot* slot =
		    &sync->rings[player][frame % GBA_LINK_INPUT_RING_CAPACITY];
		keys[player] = slot->keys;
	}
	if (sync->nextFrame == UINT64_MAX) {
		return GBA_LINK_INPUT_OVERFLOW;
	}
	++sync->nextFrame;
	return GBA_LINK_INPUT_OK;
}
