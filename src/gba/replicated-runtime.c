/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/gba/replicated-runtime.h>

const char* GBAReplicatedRuntimeResultName(
	enum GBAReplicatedRuntimeResult result) {
	switch (result) {
	case GBA_REPLICATED_RUNTIME_OK: return "ok";
	case GBA_REPLICATED_RUNTIME_INVALID_ARGUMENT: return "invalid argument";
	case GBA_REPLICATED_RUNTIME_INVALID_STATE: return "invalid runtime state";
	case GBA_REPLICATED_RUNTIME_INPUT_FAILED: return "input synchronization failed";
	case GBA_REPLICATED_RUNTIME_PAIR_FAILED: return "replicated pair failed";
	}
	return "unknown";
}

bool GBAReplicatedRuntimeInit(
	struct GBAReplicatedRuntime* runtime, struct GBAReplicatedPair* pair,
	uint64_t snapshotGeneration, enum GBALinkRole localRole,
	uint16_t inputDelay, uint64_t firstFrame) {
	if (!runtime || !pair || !pair->installed || pair->stopped ||
	    pair->frameNumber != firstFrame ||
	    (localRole != GBA_LINK_ROLE_HOST &&
	     localRole != GBA_LINK_ROLE_CLIENT)) {
		return false;
	}
	memset(runtime, 0, sizeof(*runtime));
	runtime->pair = pair;
	runtime->localRole = localRole;
	uint8_t localPlayer = localRole == GBA_LINK_ROLE_HOST ? 0 : 1;
	if (!GBALinkInputSyncInit(
	        &runtime->input, snapshotGeneration, localPlayer,
	        inputDelay, firstFrame)) {
		return false;
	}
	runtime->metrics.nextFrame = firstFrame;
	runtime->initialized = true;
	return true;
}

static void _countAuthored(
	struct GBAReplicatedRuntime* runtime,
	const struct GBALinkV2Packet* packet) {
	++runtime->metrics.authoredPackets;
	runtime->metrics.authoredRecords +=
	    packet->payload.inputBatch.count;
}

enum GBAReplicatedRuntimeResult GBAReplicatedRuntimeAuthorInput(
	struct GBAReplicatedRuntime* runtime, uint16_t keys,
	struct GBALinkV2Packet packets[GBA_REPLICATED_RUNTIME_MAX_AUTHOR_PACKETS],
	uint8_t* packetCount) {
	if (!runtime || !packets || !packetCount || !runtime->initialized ||
	    runtime->authoredCurrentFrame ||
	    (keys & ~GBA_LINK_V2_INPUT_KEY_MASK)) {
		return GBA_REPLICATED_RUNTIME_INVALID_STATE;
	}
	*packetCount = 0;
	if (!runtime->seeded) {
		enum GBALinkInputResult result = GBALinkInputSyncSeedLocal(
		    &runtime->input, keys, &packets[*packetCount]);
		if (result != GBA_LINK_INPUT_OK) {
			return GBA_REPLICATED_RUNTIME_INPUT_FAILED;
		}
		_countAuthored(runtime, &packets[*packetCount]);
		++*packetCount;
		runtime->seeded = true;
	}
	enum GBALinkInputResult result = GBALinkInputSyncAuthor(
	    &runtime->input, runtime->input.nextFrame, keys,
	    &packets[*packetCount]);
	if (result != GBA_LINK_INPUT_OK &&
	    result != GBA_LINK_INPUT_DUPLICATE) {
		return GBA_REPLICATED_RUNTIME_INPUT_FAILED;
	}
	_countAuthored(runtime, &packets[*packetCount]);
	++*packetCount;
	runtime->authoredCurrentFrame = true;
	return GBA_REPLICATED_RUNTIME_OK;
}

enum GBAReplicatedRuntimeResult GBAReplicatedRuntimeHandleInput(
	struct GBAReplicatedRuntime* runtime, enum GBALinkRole senderRole,
	const struct GBALinkV2InputBatch* batch) {
	if (!runtime || !batch || !runtime->initialized) {
		return GBA_REPLICATED_RUNTIME_INVALID_ARGUMENT;
	}
	enum GBALinkInputResult result = GBALinkInputSyncHandleBatch(
	    &runtime->input, senderRole, batch);
	if (result != GBA_LINK_INPUT_OK &&
	    result != GBA_LINK_INPUT_DUPLICATE) {
		return GBA_REPLICATED_RUNTIME_INPUT_FAILED;
	}
	++runtime->metrics.receivedPackets;
	runtime->metrics.receivedRecords += batch->count;
	if (result == GBA_LINK_INPUT_DUPLICATE) {
		++runtime->metrics.exactDuplicates;
	}
	return GBA_REPLICATED_RUNTIME_OK;
}

bool GBAReplicatedRuntimeFrameReady(
	const struct GBAReplicatedRuntime* runtime) {
	return runtime && runtime->initialized &&
	       runtime->authoredCurrentFrame &&
	       GBALinkInputSyncReady(
	           &runtime->input, runtime->input.nextFrame);
}

enum GBAReplicatedRuntimeResult GBAReplicatedRuntimeRunFrame(
	struct GBAReplicatedRuntime* runtime) {
	if (!GBAReplicatedRuntimeFrameReady(runtime) ||
	    runtime->pair->frameNumber != runtime->input.nextFrame) {
		return GBA_REPLICATED_RUNTIME_INVALID_STATE;
	}
	uint64_t frame = runtime->input.nextFrame;
	uint16_t keys[2];
	if (GBALinkInputSyncConsume(
	        &runtime->input, frame, keys) != GBA_LINK_INPUT_OK) {
		return GBA_REPLICATED_RUNTIME_INPUT_FAILED;
	}
	if (GBAReplicatedPairSetInputs(
	        runtime->pair, frame, keys[0], keys[1]) !=
	        GBA_REPLICATED_PAIR_OK ||
	    GBAReplicatedPairRunFrame(runtime->pair) !=
	        GBA_REPLICATED_PAIR_OK) {
		return GBA_REPLICATED_RUNTIME_PAIR_FAILED;
	}
	runtime->authoredCurrentFrame = false;
	++runtime->metrics.framesReleased;
	runtime->metrics.nextFrame = runtime->input.nextFrame;
	return GBA_REPLICATED_RUNTIME_OK;
}

bool GBAReplicatedRuntimeGetMetrics(
	const struct GBAReplicatedRuntime* runtime,
	struct GBAReplicatedRuntimeMetrics* metrics) {
	if (!runtime || !metrics || !runtime->initialized) {
		return false;
	}
	*metrics = runtime->metrics;
	return true;
}

void GBAReplicatedRuntimeDeinit(struct GBAReplicatedRuntime* runtime) {
	if (!runtime) {
		return;
	}
	memset(runtime, 0, sizeof(*runtime));
}
