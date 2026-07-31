/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "util/test/suite.h"

#include <mgba/internal/gba/sio/netplay/input-sync.h>

static struct GBALinkInputSync _sync(uint8_t localPlayer) {
	struct GBALinkInputSync sync;
	assert_true(GBALinkInputSyncInit(&sync, 77, localPlayer, 3, 0));
	return sync;
}

static enum GBALinkInputResult _seedRemote(
	struct GBALinkInputSync* receiver, uint8_t remotePlayer,
	uint16_t keys, struct GBALinkV2Packet* packet) {
	struct GBALinkInputSync remote = _sync(remotePlayer);
	assert_int_equal(
	    GBALinkInputSyncSeedLocal(&remote, keys, packet),
	    GBA_LINK_INPUT_OK);
	return GBALinkInputSyncHandleBatch(
	    receiver,
	    remotePlayer ? GBA_LINK_ROLE_CLIENT : GBA_LINK_ROLE_HOST,
	    &packet->payload.inputBatch);
}

M_TEST_DEFINE(delaySelectionUsesRttJitterAndClamps) {
	assert_int_equal(GBALinkInputSelectDelay(2, 8, 0, 0), 2);
	assert_int_equal(GBALinkInputSelectDelay(2, 8, 40, 5), 3);
	assert_int_equal(GBALinkInputSelectDelay(2, 4, 200, 50), 4);
	assert_int_equal(GBALinkInputSelectDelay(9, 8, 0, 0), UINT16_MAX);
}

M_TEST_DEFINE(initialSeedMakesFirstFramesAvailableWithoutPrediction) {
	struct GBALinkInputSync sync = _sync(0);
	struct GBALinkV2Packet local;
	struct GBALinkV2Packet remote;
	assert_int_equal(
	    GBALinkInputSyncSeedLocal(&sync, 1, &local), GBA_LINK_INPUT_OK);
	assert_int_equal(local.payload.inputBatch.count, 3);
	assert_int_equal(_seedRemote(&sync, 1, 2, &remote), GBA_LINK_INPUT_OK);
	for (uint64_t frame = 0; frame < 3; ++frame) {
		assert_true(GBALinkInputSyncReady(&sync, frame));
		uint16_t keys[2];
		assert_int_equal(
		    GBALinkInputSyncConsume(&sync, frame, keys), GBA_LINK_INPUT_OK);
		assert_int_equal(keys[0], 1);
		assert_int_equal(keys[1], 2);
	}
}

M_TEST_DEFINE(authoredBatchHasShortOrderedRedundancyWindow) {
	struct GBALinkInputSync sync = _sync(0);
	struct GBALinkV2Packet packet;
	assert_int_equal(
	    GBALinkInputSyncSeedLocal(&sync, 0, &packet), GBA_LINK_INPUT_OK);
	for (uint64_t frame = 0; frame < 3; ++frame) {
		assert_int_equal(
		    GBALinkInputSyncAuthor(&sync, frame, 4 + frame, &packet),
		    GBA_LINK_INPUT_OK);
		struct GBALinkV2Packet remote;
		assert_int_equal(_seedRemote(&sync, 1, 0, &remote),
		    frame ? GBA_LINK_INPUT_DUPLICATE : GBA_LINK_INPUT_OK);
		uint16_t keys[2];
		assert_int_equal(
		    GBALinkInputSyncConsume(&sync, frame, keys), GBA_LINK_INPUT_OK);
	}
	assert_int_equal(
	    GBALinkInputSyncAuthor(&sync, 3, 7, &packet), GBA_LINK_INPUT_OK);
	assert_true(packet.payload.inputBatch.count <= GBA_LINK_INPUT_REDUNDANCY);
	for (unsigned i = 1; i < packet.payload.inputBatch.count; ++i) {
		assert_int_equal(packet.payload.inputBatch.records[i].frame,
		    packet.payload.inputBatch.records[i - 1].frame + 1);
	}
}

M_TEST_DEFINE(duplicatesAreIdempotentAndConflictsFail) {
	struct GBALinkInputSync sync = _sync(0);
	struct GBALinkV2Packet local;
	struct GBALinkV2Packet remote;
	assert_int_equal(
	    GBALinkInputSyncSeedLocal(&sync, 0, &local), GBA_LINK_INPUT_OK);
	assert_int_equal(_seedRemote(&sync, 1, 2, &remote), GBA_LINK_INPUT_OK);
	assert_int_equal(
	    GBALinkInputSyncHandleBatch(
	        &sync, GBA_LINK_ROLE_CLIENT, &remote.payload.inputBatch),
	    GBA_LINK_INPUT_DUPLICATE);
	remote.payload.inputBatch.records[2].keys ^= 1;
	assert_int_equal(
	    GBALinkInputSyncHandleBatch(
	        &sync, GBA_LINK_ROLE_CLIENT, &remote.payload.inputBatch),
	    GBA_LINK_INPUT_CONFLICT);
}

M_TEST_DEFINE(rejectedBatchCannotPartiallyInstallInputs) {
	struct GBALinkInputSync sync = _sync(0);
	struct GBALinkV2Packet packet;
	assert_int_equal(
	    GBALinkInputSyncSeedLocal(&sync, 0, &packet), GBA_LINK_INPUT_OK);
	memset(&packet, 0, sizeof(packet));
	packet.payload.inputBatch.snapshotGeneration = 77;
	packet.payload.inputBatch.player = 1;
	packet.payload.inputBatch.count = 1;
	packet.payload.inputBatch.records[0].frame = 4;
	packet.payload.inputBatch.records[0].keys = 2;
	assert_int_equal(
	    GBALinkInputSyncHandleBatch(
	        &sync, GBA_LINK_ROLE_CLIENT, &packet.payload.inputBatch),
	    GBA_LINK_INPUT_OK);

	packet.payload.inputBatch.count = 2;
	packet.payload.inputBatch.records[0].frame = 3;
	packet.payload.inputBatch.records[0].keys = 3;
	packet.payload.inputBatch.records[1].frame = 4;
	packet.payload.inputBatch.records[1].keys = 4;
	assert_int_equal(
	    GBALinkInputSyncHandleBatch(
	        &sync, GBA_LINK_ROLE_CLIENT, &packet.payload.inputBatch),
	    GBA_LINK_INPUT_CONFLICT);

	packet.payload.inputBatch.count = 1;
	assert_int_equal(
	    GBALinkInputSyncHandleBatch(
	        &sync, GBA_LINK_ROLE_CLIENT, &packet.payload.inputBatch),
	    GBA_LINK_INPUT_OK);
}

M_TEST_DEFINE(ownershipGenerationWindowAndMissingInputFailClosed) {
	struct GBALinkInputSync sync = _sync(0);
	struct GBALinkV2Packet packet;
	assert_int_equal(
	    GBALinkInputSyncSeedLocal(&sync, 0, &packet), GBA_LINK_INPUT_OK);
	assert_false(GBALinkInputSyncReady(&sync, 0));
	uint16_t keys[2];
	assert_int_equal(
	    GBALinkInputSyncConsume(&sync, 0, keys), GBA_LINK_INPUT_MISSING);
	packet.payload.inputBatch.player = 0;
	assert_int_equal(
	    GBALinkInputSyncHandleBatch(
	        &sync, GBA_LINK_ROLE_CLIENT, &packet.payload.inputBatch),
	    GBA_LINK_INPUT_WRONG_OWNER);
	packet.payload.inputBatch.player = 1;
	packet.payload.inputBatch.snapshotGeneration++;
	assert_int_equal(
	    GBALinkInputSyncHandleBatch(
	        &sync, GBA_LINK_ROLE_CLIENT, &packet.payload.inputBatch),
	    GBA_LINK_INPUT_WRONG_GENERATION);
	packet.payload.inputBatch.snapshotGeneration = 77;
	packet.payload.inputBatch.records[0].frame =
	    GBA_LINK_INPUT_RING_CAPACITY;
	assert_int_equal(
	    GBALinkInputSyncHandleBatch(
	        &sync, GBA_LINK_ROLE_CLIENT, &packet.payload.inputBatch),
	    GBA_LINK_INPUT_OUT_OF_WINDOW);
}

M_TEST_SUITE_DEFINE(GBALinkInputSync,
	cmocka_unit_test(delaySelectionUsesRttJitterAndClamps),
	cmocka_unit_test(initialSeedMakesFirstFramesAvailableWithoutPrediction),
	cmocka_unit_test(authoredBatchHasShortOrderedRedundancyWindow),
	cmocka_unit_test(duplicatesAreIdempotentAndConflictsFail),
	cmocka_unit_test(rejectedBatchCannotPartiallyInstallInputs),
	cmocka_unit_test(ownershipGenerationWindowAndMissingInputFailClosed))
