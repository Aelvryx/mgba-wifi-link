/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef GBA_REPLICATED_PAIR_H
#define GBA_REPLICATED_PAIR_H

#include <mgba-util/common.h>

CXX_GUARD_START

#include <mgba/internal/gba/replica.h>
#include <mgba/internal/gba/sio/lockstep.h>

enum GBAReplicatedPairResult {
	GBA_REPLICATED_PAIR_OK = 0,
	GBA_REPLICATED_PAIR_INVALID_ARGUMENT,
	GBA_REPLICATED_PAIR_INVALID_STATE,
	GBA_REPLICATED_PAIR_ALLOCATION_FAILED,
	GBA_REPLICATED_PAIR_CORE_FAILED,
	GBA_REPLICATED_PAIR_RESTORE_FAILED,
	GBA_REPLICATED_PAIR_INPUT_CONFLICT,
	GBA_REPLICATED_PAIR_DEADLOCK,
	GBA_REPLICATED_PAIR_FRAME_OVERSHOOT,
};

struct GBAReplicatedPairUser {
	struct mLockstepUser d;
	int requestedId;
	bool asleep;
	uint64_t sleeps;
	uint64_t wakes;
};

struct GBAReplicatedPairPlayer {
	struct mCore* core;
	struct VFile* temporarySave;
	struct GBASIOLockstepDriver driver;
	struct GBAReplicatedPairUser user;
	bool attached;
	uint64_t runLoops;
	bool configInitialized;
};

struct GBAReplicatedPairMetrics {
	uint64_t frameNumber;
	uint8_t stateTrace[2][MGBA_SHA256_DIGEST_SIZE];
	uint64_t runLoops[2];
	uint64_t sleeps[2];
	uint64_t wakes[2];
	uint64_t transferStarts;
	uint64_t transferCompletions;
	uint64_t transferredWords;
	uint64_t waitEvents;
};

struct GBAReplicatedPair {
	struct GBASIOLockstepCoordinator coordinator;
	struct GBAReplicatedPairPlayer players[2];
	uint8_t* rom;
	size_t romSize;
	uint8_t* bios;
	size_t biosSize;
	uint64_t snapshotGeneration;
	uint64_t frameNumber;
	uint64_t inputFrame;
	uint16_t inputs[2];
	uint8_t stateTrace[2][MGBA_SHA256_DIGEST_SIZE];
	bool initialized;
	bool installed;
	bool inputsReady;
	bool stopped;
};

const char* GBAReplicatedPairResultName(enum GBAReplicatedPairResult result);
void GBAReplicatedPairInit(struct GBAReplicatedPair* pair);
enum GBAReplicatedPairResult GBAReplicatedPairInstall(
	struct GBAReplicatedPair* pair, const struct mCore* templateCore,
	const struct GBAReplicaManifest manifests[2],
	const struct GBAReplicaPayload payloads[2], uint64_t generation);
enum GBAReplicatedPairResult GBAReplicatedPairSetInputs(
	struct GBAReplicatedPair* pair, uint64_t frame,
	uint16_t player0, uint16_t player1);
enum GBAReplicatedPairResult GBAReplicatedPairRunFrame(
	struct GBAReplicatedPair* pair);
struct mCore* GBAReplicatedPairCore(
	struct GBAReplicatedPair* pair, uint8_t player);
const struct mCore* GBAReplicatedPairCoreConst(
	const struct GBAReplicatedPair* pair, uint8_t player);
bool GBAReplicatedPairGetMetrics(
	const struct GBAReplicatedPair* pair,
	struct GBAReplicatedPairMetrics* metrics);
void GBAReplicatedPairStop(struct GBAReplicatedPair* pair);

CXX_GUARD_END

#endif
