/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/gba/sio/netplay/input-sync.h>

#include <mgba-util/sha256.h>

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

static void _digest16(struct SHA256Context* context, uint16_t value) {
	uint8_t bytes[] = { value, value >> 8 };
	sha256Update(context, bytes, sizeof(bytes));
}

static void _digest32(struct SHA256Context* context, uint32_t value) {
	uint8_t bytes[] = {
		value, value >> 8, value >> 16, value >> 24,
	};
	sha256Update(context, bytes, sizeof(bytes));
}

static void _digest64(struct SHA256Context* context, uint64_t value) {
	_digest32(context, value);
	_digest32(context, value >> 32);
}

bool GBALinkInputCalibrationDigest(
	const struct GBALinkInputCalibration* calibration,
	uint8_t digest[MGBA_SHA256_DIGEST_SIZE]) {
	if (!calibration || !digest || !calibration->hostConnectionNonce ||
	    !calibration->clientConnectionNonce ||
	    !calibration->provisionalSessionId || !calibration->generation ||
	    calibration->calibrationPolicyVersion !=
	        GBA_LINK_CALIBRATION_POLICY_VERSION ||
	    calibration->selectorPolicyVersion !=
	        GBA_LINK_INPUT_SELECTOR_POLICY_VERSION) {
		return false;
	}
	for (unsigned i = 0; i < GBA_LINK_CALIBRATION_SAMPLE_COUNT; ++i) {
		if (calibration->samples[i] >
		    GBA_LINK_CALIBRATION_MAX_SAMPLE_US) {
			return false;
		}
	}
	static const char protocol[] = "mgba-gba-link-replicated-v2";
	static const char domain[] = "latency-calibration-vector-v1";
	struct SHA256Context context;
	sha256Init(&context);
	sha256Update(&context, protocol, sizeof(protocol));
	sha256Update(&context, domain, sizeof(domain));
	_digest64(&context, calibration->hostConnectionNonce);
	_digest64(&context, calibration->clientConnectionNonce);
	_digest64(&context, calibration->provisionalSessionId);
	_digest64(&context, calibration->generation);
	_digest32(&context, calibration->calibrationPolicyVersion);
	_digest32(&context, calibration->selectorPolicyVersion);
	_digest16(&context, GBA_LINK_CALIBRATION_SAMPLE_COUNT);
	_digest16(&context, GBA_LINK_INPUT_LATENCY_UNIT_MICROSECONDS);
	for (unsigned i = 0; i < GBA_LINK_CALIBRATION_SAMPLE_COUNT; ++i) {
		_digest32(&context, calibration->samples[i]);
	}
	sha256Finalize(digest, &context);
	return true;
}

static void _sortSamples(uint32_t samples[GBA_LINK_CALIBRATION_SAMPLE_COUNT]) {
	for (unsigned i = 1; i < GBA_LINK_CALIBRATION_SAMPLE_COUNT; ++i) {
		uint32_t value = samples[i];
		unsigned j = i;
		while (j && samples[j - 1] > value) {
			samples[j] = samples[j - 1];
			--j;
		}
		samples[j] = value;
	}
}

enum GBALinkInputSelectionResult GBALinkInputSelectCalibratedDelay(
	const struct GBALinkInputCalibration* calibration,
	uint16_t overlappingMinimum, uint16_t overlappingMaximum,
	uint16_t productionFloor, struct GBALinkInputSelection* selection) {
	if (!calibration || !selection || !productionFloor ||
	    overlappingMinimum > overlappingMaximum ||
	    overlappingMaximum > GBA_LINK_V2_MAX_INPUT_DELAY ||
	    productionFloor > overlappingMaximum) {
		return GBA_LINK_INPUT_SELECTION_INVALID_ARGUMENT;
	}
	struct GBALinkInputSelection result = {
		.overlappingMinimum = overlappingMinimum,
		.overlappingMaximum = overlappingMaximum,
		.productionFloor = productionFloor,
	};
	if (!GBALinkInputCalibrationDigest(calibration, result.digest)) {
		return GBA_LINK_INPUT_SELECTION_INVALID_ARGUMENT;
	}
	uint32_t sorted[GBA_LINK_CALIBRATION_SAMPLE_COUNT];
	memcpy(sorted, calibration->samples, sizeof(sorted));
	_sortSamples(sorted);
	result.minimumRttUs = sorted[0];
	result.p50RttUs = sorted[11];
	result.p95RttUs = sorted[22];
	result.maximumRttUs = sorted[23];

	uint64_t base = ((uint64_t) result.minimumRttUs + 1) / 2;
	uint64_t variation = result.p95RttUs - result.minimumRttUs;
	uint64_t budget = base + variation + 1000;
	if (budget > UINT32_MAX ||
	    budget > UINT64_MAX / UINT64_C(16777216)) {
		return GBA_LINK_INPUT_SELECTION_ARITHMETIC;
	}
	uint64_t numerator = budget * UINT64_C(16777216);
	const uint64_t denominator = UINT64_C(280896) * UINT64_C(1000000);
	uint64_t candidate = numerator / denominator;
	if (numerator % denominator) {
		++candidate;
	}
	if (!candidate) {
		candidate = 1;
	}
	uint16_t minimum = overlappingMinimum > productionFloor
	    ? overlappingMinimum
	    : productionFloor;
	result.reason = GBA_LINK_INPUT_SELECTION_CALIBRATED;
	if (candidate < minimum) {
		candidate = minimum;
		result.reason = GBA_LINK_INPUT_SELECTION_RAISED_TO_MINIMUM;
	}
	if (candidate > overlappingMaximum) {
		return GBA_LINK_INPUT_SELECTION_OUT_OF_RANGE;
	}
	result.budgetUs = budget;
	result.selectedDelay = candidate;
	*selection = result;
	return GBA_LINK_INPUT_SELECTION_OK;
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
