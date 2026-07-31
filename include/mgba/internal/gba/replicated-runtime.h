/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef GBA_REPLICATED_RUNTIME_H
#define GBA_REPLICATED_RUNTIME_H

#include <mgba-util/common.h>

#include <mgba/internal/gba/replicated-pair.h>
#include <mgba/internal/gba/sio/netplay/input-sync.h>

CXX_GUARD_START

#define GBA_REPLICATED_RUNTIME_MAX_AUTHOR_PACKETS 2

enum GBAReplicatedRuntimeResult {
	GBA_REPLICATED_RUNTIME_OK = 0,
	GBA_REPLICATED_RUNTIME_INVALID_ARGUMENT,
	GBA_REPLICATED_RUNTIME_INVALID_STATE,
	GBA_REPLICATED_RUNTIME_INPUT_FAILED,
	GBA_REPLICATED_RUNTIME_PAIR_FAILED,
};

struct GBAReplicatedRuntimeMetrics {
	uint64_t framesReleased;
	uint64_t authoredPackets;
	uint64_t receivedPackets;
	uint64_t authoredRecords;
	uint64_t receivedRecords;
	uint64_t exactDuplicates;
	uint64_t nextFrame;
};

struct GBAReplicatedRuntime {
	struct GBAReplicatedPair* pair;
	struct GBALinkInputSync input;
	struct GBAReplicatedRuntimeMetrics metrics;
	enum GBALinkRole localRole;
	bool initialized;
	bool seeded;
	bool authoredCurrentFrame;
};

const char* GBAReplicatedRuntimeResultName(
	enum GBAReplicatedRuntimeResult result);
bool GBAReplicatedRuntimeInit(
	struct GBAReplicatedRuntime* runtime, struct GBAReplicatedPair* pair,
	uint64_t snapshotGeneration, enum GBALinkRole localRole,
	uint16_t inputDelay, uint64_t firstFrame);
enum GBAReplicatedRuntimeResult GBAReplicatedRuntimeAuthorInput(
	struct GBAReplicatedRuntime* runtime, uint16_t keys,
	struct GBALinkV2Packet packets[GBA_REPLICATED_RUNTIME_MAX_AUTHOR_PACKETS],
	uint8_t* packetCount);
enum GBAReplicatedRuntimeResult GBAReplicatedRuntimeHandleInput(
	struct GBAReplicatedRuntime* runtime, enum GBALinkRole senderRole,
	const struct GBALinkV2InputBatch* batch);
bool GBAReplicatedRuntimeFrameReady(
	const struct GBAReplicatedRuntime* runtime);
enum GBAReplicatedRuntimeResult GBAReplicatedRuntimeRunFrame(
	struct GBAReplicatedRuntime* runtime);
bool GBAReplicatedRuntimeGetMetrics(
	const struct GBAReplicatedRuntime* runtime,
	struct GBAReplicatedRuntimeMetrics* metrics);
void GBAReplicatedRuntimeDeinit(struct GBAReplicatedRuntime* runtime);

CXX_GUARD_END

#endif
