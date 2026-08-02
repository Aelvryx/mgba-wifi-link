/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef GBA_SIO_NETPLAY_INPUT_SYNC_H
#define GBA_SIO_NETPLAY_INPUT_SYNC_H

#include <mgba-util/common.h>

#include <mgba/internal/gba/sio/netplay/protocol-v2.h>

CXX_GUARD_START

#define GBA_LINK_INPUT_RING_CAPACITY 256
#define GBA_LINK_INPUT_REDUNDANCY 4

enum GBALinkInputSelectionReason {
	GBA_LINK_INPUT_SELECTION_CALIBRATED = 1,
	GBA_LINK_INPUT_SELECTION_RAISED_TO_MINIMUM = 2,
};

enum GBALinkInputSelectionResult {
	GBA_LINK_INPUT_SELECTION_OK = 0,
	GBA_LINK_INPUT_SELECTION_INVALID_ARGUMENT,
	GBA_LINK_INPUT_SELECTION_ARITHMETIC,
	GBA_LINK_INPUT_SELECTION_OUT_OF_RANGE,
};

struct GBALinkInputCalibration {
	uint64_t hostConnectionNonce;
	uint64_t clientConnectionNonce;
	uint64_t provisionalSessionId;
	uint64_t generation;
	uint32_t calibrationPolicyVersion;
	uint32_t selectorPolicyVersion;
	uint32_t samples[GBA_LINK_CALIBRATION_SAMPLE_COUNT];
};

struct GBALinkInputSelection {
	uint8_t digest[MGBA_SHA256_DIGEST_SIZE];
	uint32_t minimumRttUs;
	uint32_t p50RttUs;
	uint32_t p95RttUs;
	uint32_t maximumRttUs;
	uint32_t budgetUs;
	uint16_t selectedDelay;
	uint16_t overlappingMinimum;
	uint16_t overlappingMaximum;
	uint16_t productionFloor;
	enum GBALinkInputSelectionReason reason;
};

enum GBALinkInputResult {
	GBA_LINK_INPUT_OK = 0,
	GBA_LINK_INPUT_DUPLICATE,
	GBA_LINK_INPUT_INVALID_ARGUMENT,
	GBA_LINK_INPUT_INVALID_STATE,
	GBA_LINK_INPUT_WRONG_OWNER,
	GBA_LINK_INPUT_WRONG_GENERATION,
	GBA_LINK_INPUT_CONFLICT,
	GBA_LINK_INPUT_OUT_OF_WINDOW,
	GBA_LINK_INPUT_MISSING,
	GBA_LINK_INPUT_OVERFLOW,
};

struct GBALinkInputSlot {
	uint64_t frame;
	uint16_t keys;
	bool present;
};

struct GBALinkInputSync {
	struct GBALinkInputSlot rings[2][GBA_LINK_INPUT_RING_CAPACITY];
	uint64_t snapshotGeneration;
	uint64_t nextFrame;
	uint64_t lastAuthoredFrame;
	uint16_t delay;
	uint8_t localPlayer;
	bool initialized;
	bool authored;
};

const char* GBALinkInputResultName(enum GBALinkInputResult result);
uint16_t GBALinkInputSelectDelay(
	uint16_t minimum, uint16_t maximum,
	uint32_t roundTripMs, uint32_t jitterMs);
bool GBALinkInputCalibrationDigest(
	const struct GBALinkInputCalibration* calibration,
	uint8_t digest[MGBA_SHA256_DIGEST_SIZE]);
enum GBALinkInputSelectionResult GBALinkInputSelectCalibratedDelay(
	const struct GBALinkInputCalibration* calibration,
	uint16_t overlappingMinimum, uint16_t overlappingMaximum,
	uint16_t productionFloor, struct GBALinkInputSelection* selection);
bool GBALinkInputSyncInit(
	struct GBALinkInputSync* sync, uint64_t snapshotGeneration,
	uint8_t localPlayer, uint16_t delay, uint64_t firstFrame);
enum GBALinkInputResult GBALinkInputSyncSeedLocal(
	struct GBALinkInputSync* sync, uint16_t keys,
	struct GBALinkV2Packet* batch);
enum GBALinkInputResult GBALinkInputSyncAuthor(
	struct GBALinkInputSync* sync, uint64_t currentFrame,
	uint16_t keys, struct GBALinkV2Packet* batch);
enum GBALinkInputResult GBALinkInputSyncHandleBatch(
	struct GBALinkInputSync* sync, enum GBALinkRole senderRole,
	const struct GBALinkV2InputBatch* batch);
bool GBALinkInputSyncReady(
	const struct GBALinkInputSync* sync, uint64_t frame);
enum GBALinkInputResult GBALinkInputSyncConsume(
	struct GBALinkInputSync* sync, uint64_t frame,
	uint16_t keys[2]);

CXX_GUARD_END

#endif
