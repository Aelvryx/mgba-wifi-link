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
