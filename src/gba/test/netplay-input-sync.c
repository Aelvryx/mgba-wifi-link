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

static struct GBALinkInputSync _syncAt(
	uint8_t localPlayer, uint16_t delay, uint64_t firstFrame) {
	struct GBALinkInputSync sync;
	assert_true(GBALinkInputSyncInit(
	    &sync, 77, localPlayer, delay, firstFrame));
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

static struct GBALinkInputCalibration _calibration(void) {
	struct GBALinkInputCalibration calibration = {
		.hostConnectionNonce = 11,
		.clientConnectionNonce = 22,
		.provisionalSessionId = 33,
		.generation = 44,
		.calibrationPolicyVersion = GBA_LINK_CALIBRATION_POLICY_VERSION,
		.selectorPolicyVersion = GBA_LINK_INPUT_SELECTOR_POLICY_VERSION,
	};
	for (unsigned i = 0; i < GBA_LINK_CALIBRATION_SAMPLE_COUNT; ++i) {
		calibration.samples[i] = i * 1000;
	}
	return calibration;
}

M_TEST_DEFINE(calibrationDigestAndSelectorHaveGoldenResults) {
	static const uint8_t expectedDigest[MGBA_SHA256_DIGEST_SIZE] = {
		0xb3, 0x5e, 0x4c, 0xa8, 0xbe, 0x87, 0xe7, 0x1f,
		0xb6, 0x2b, 0xe6, 0x00, 0x35, 0xd4, 0x81, 0xd9,
		0xa6, 0xaf, 0x0f, 0x89, 0x6a, 0x15, 0x25, 0x3b,
		0x21, 0x81, 0x80, 0xce, 0x31, 0xf1, 0xc1, 0x74,
	};
	struct GBALinkInputCalibration calibration = _calibration();
	struct GBALinkInputSelection selection;
	assert_int_equal(GBALinkInputSelectCalibratedDelay(
	    &calibration, 1, 8, 1, &selection), GBA_LINK_INPUT_SELECTION_OK);
	assert_memory_equal(selection.digest, expectedDigest,
	    sizeof(expectedDigest));
	assert_int_equal(selection.minimumRttUs, 0);
	assert_int_equal(selection.p50RttUs, 11000);
	assert_int_equal(selection.p95RttUs, 22000);
	assert_int_equal(selection.maximumRttUs, 23000);
	assert_int_equal(selection.budgetUs, 23000);
	assert_int_equal(selection.selectedDelay, 2);
	assert_int_equal(selection.reason, GBA_LINK_INPUT_SELECTION_CALIBRATED);
}

M_TEST_DEFINE(selectorExcludesOneMaximumAndHonorsProductFloor) {
	struct GBALinkInputCalibration calibration = _calibration();
	for (unsigned i = 0; i < GBA_LINK_CALIBRATION_SAMPLE_COUNT; ++i) {
		calibration.samples[i] = 1000;
	}
	calibration.samples[23] = GBA_LINK_CALIBRATION_MAX_SAMPLE_US;
	struct GBALinkInputSelection selection;
	assert_int_equal(GBALinkInputSelectCalibratedDelay(
	    &calibration, 1, 8, GBA_LINK_INPUT_STABLE_FLOOR, &selection),
	    GBA_LINK_INPUT_SELECTION_OK);
	assert_int_equal(selection.p95RttUs, 1000);
	assert_int_equal(selection.maximumRttUs,
	    GBA_LINK_CALIBRATION_MAX_SAMPLE_US);
	assert_int_equal(selection.selectedDelay, GBA_LINK_INPUT_STABLE_FLOOR);
	assert_int_equal(selection.reason,
	    GBA_LINK_INPUT_SELECTION_RAISED_TO_MINIMUM);

	calibration.samples[22] = GBA_LINK_CALIBRATION_MAX_SAMPLE_US;
	assert_int_equal(GBALinkInputSelectCalibratedDelay(
	    &calibration, 1, GBA_LINK_V2_MAX_INPUT_DELAY, 1, &selection),
	    GBA_LINK_INPUT_SELECTION_OUT_OF_RANGE);
}

M_TEST_DEFINE(selectorRejectsInvalidRangesAndSamples) {
	struct GBALinkInputCalibration calibration = _calibration();
	struct GBALinkInputSelection selection;
	assert_int_equal(GBALinkInputSelectCalibratedDelay(
	    &calibration, 8, 2, 1, &selection),
	    GBA_LINK_INPUT_SELECTION_INVALID_ARGUMENT);
	assert_int_equal(GBALinkInputSelectCalibratedDelay(
	    &calibration, 1, 8, 9, &selection),
	    GBA_LINK_INPUT_SELECTION_INVALID_ARGUMENT);
	calibration.samples[0] = GBA_LINK_CALIBRATION_MAX_SAMPLE_US + 1;
	assert_int_equal(GBALinkInputSelectCalibratedDelay(
	    &calibration, 1, 8, 1, &selection),
	    GBA_LINK_INPUT_SELECTION_INVALID_ARGUMENT);
	calibration = _calibration();
	calibration.generation = 0;
	assert_false(GBALinkInputCalibrationDigest(
	    &calibration, selection.digest));
}

M_TEST_DEFINE(selectorRationalFrameEdgesAreExact) {
	struct GBALinkInputCalibration calibration = _calibration();
	struct GBALinkInputSelection selection;
	memset(calibration.samples, 0, sizeof(calibration.samples));
	assert_int_equal(GBALinkInputSelectCalibratedDelay(
	    &calibration, 1, 8, 1, &selection), GBA_LINK_INPUT_SELECTION_OK);
	assert_int_equal(selection.budgetUs, 1000);
	assert_int_equal(selection.selectedDelay, 1);

	for (unsigned i = 0; i < GBA_LINK_CALIBRATION_SAMPLE_COUNT; ++i) {
		calibration.samples[i] = 31484;
	}
	assert_int_equal(GBALinkInputSelectCalibratedDelay(
	    &calibration, 1, 8, 1, &selection), GBA_LINK_INPUT_SELECTION_OK);
	assert_int_equal(selection.budgetUs, 16742);
	assert_int_equal(selection.selectedDelay, 1);
	for (unsigned i = 0; i < GBA_LINK_CALIBRATION_SAMPLE_COUNT; ++i) {
		calibration.samples[i] = 31485;
	}
	assert_int_equal(GBALinkInputSelectCalibratedDelay(
	    &calibration, 1, 8, 1, &selection), GBA_LINK_INPUT_SELECTION_OK);
	assert_int_equal(selection.budgetUs, 16743);
	assert_int_equal(selection.selectedDelay, 2);

	for (unsigned i = 0; i < GBA_LINK_CALIBRATION_SAMPLE_COUNT; ++i) {
		calibration.samples[i] = 1000;
	}
	calibration.samples[22] = 249000;
	calibration.samples[23] = 1000000;
	assert_int_equal(GBALinkInputSelectCalibratedDelay(
	    &calibration, 1, 15, 1, &selection), GBA_LINK_INPUT_SELECTION_OK);
	assert_int_equal(selection.p95RttUs, 249000);
	assert_int_equal(selection.maximumRttUs, 1000000);
	assert_true(selection.selectedDelay <= 15);
	calibration.samples[22] = 1000000;
	assert_int_equal(GBALinkInputSelectCalibratedDelay(
	    &calibration, 1, 15, 1, &selection),
	    GBA_LINK_INPUT_SELECTION_OUT_OF_RANGE);
}

M_TEST_DEFINE(selectorFailureDoesNotPartiallyReplaceOutput) {
	struct GBALinkInputCalibration calibration = _calibration();
	struct GBALinkInputSelection selection;
	memset(&selection, 0xA5, sizeof(selection));
	struct GBALinkInputSelection original = selection;
	calibration.samples[0] = GBA_LINK_CALIBRATION_MAX_SAMPLE_US + 1;
	assert_int_equal(GBALinkInputSelectCalibratedDelay(
	    &calibration, 1, 8, 1, &selection),
	    GBA_LINK_INPUT_SELECTION_INVALID_ARGUMENT);
	assert_memory_equal(&selection, &original, sizeof(selection));
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

M_TEST_DEFINE(oneAndTwoFramePoliciesMapExactlyOnceAcrossRingWrap) {
	for (uint16_t delay = 1; delay <= 2; ++delay) {
		const uint64_t firstFrame = GBA_LINK_INPUT_RING_CAPACITY - 3;
		struct GBALinkInputSync sync = _syncAt(0, delay, firstFrame);
		struct GBALinkV2Packet local;
		assert_int_equal(
		    GBALinkInputSyncSeedLocal(&sync, 1, &local), GBA_LINK_INPUT_OK);
		struct GBALinkV2Packet remote;
		memset(&remote, 0, sizeof(remote));
		remote.payload.inputBatch.snapshotGeneration = 77;
		remote.payload.inputBatch.player = 1;
		remote.payload.inputBatch.count = delay;
		for (unsigned i = 0; i < delay; ++i) {
			remote.payload.inputBatch.records[i].frame = firstFrame + i;
			remote.payload.inputBatch.records[i].keys = 2;
		}
		assert_int_equal(GBALinkInputSyncHandleBatch(
		    &sync, GBA_LINK_ROLE_CLIENT, &remote.payload.inputBatch),
		    GBA_LINK_INPUT_OK);

		for (uint64_t frame = firstFrame;
		     frame < firstFrame + GBA_LINK_INPUT_RING_CAPACITY + 8; ++frame) {
			uint16_t localKeys = 4 | (frame & 3);
			uint16_t remoteKeys = 8 | (frame & 3);
			assert_int_equal(GBALinkInputSyncAuthor(
			    &sync, frame, localKeys, &local), GBA_LINK_INPUT_OK);
			memset(&remote, 0, sizeof(remote));
			remote.payload.inputBatch.snapshotGeneration = 77;
			remote.payload.inputBatch.player = 1;
			remote.payload.inputBatch.count = 1;
			remote.payload.inputBatch.records[0].frame = frame + delay;
			remote.payload.inputBatch.records[0].keys = remoteKeys;
			assert_int_equal(GBALinkInputSyncHandleBatch(
			    &sync, GBA_LINK_ROLE_CLIENT, &remote.payload.inputBatch),
			    GBA_LINK_INPUT_OK);
			assert_true(GBALinkInputSyncReady(&sync, frame));
			uint16_t keys[2];
			assert_int_equal(GBALinkInputSyncConsume(
			    &sync, frame, keys), GBA_LINK_INPUT_OK);
			if (frame < firstFrame + delay) {
				assert_int_equal(keys[0], 1);
				assert_int_equal(keys[1], 2);
			} else {
				uint64_t sampled = frame - delay;
				assert_int_equal(keys[0], 4 | (sampled & 3));
				assert_int_equal(keys[1], 8 | (sampled & 3));
			}
			assert_int_equal(GBALinkInputSyncConsume(
			    &sync, frame, keys), GBA_LINK_INPUT_INVALID_STATE);
		}
	}
}

M_TEST_DEFINE(lateArrivalChangesOnlyFutureReadiness) {
	struct GBALinkInputSync sync = _syncAt(0, 1, 0);
	struct GBALinkV2Packet local;
	struct GBALinkV2Packet remote;
	assert_int_equal(GBALinkInputSyncSeedLocal(
	    &sync, 1, &local), GBA_LINK_INPUT_OK);
	memset(&remote, 0, sizeof(remote));
	remote.payload.inputBatch.snapshotGeneration = 77;
	remote.payload.inputBatch.player = 1;
	remote.payload.inputBatch.count = 1;
	remote.payload.inputBatch.records[0].frame = 0;
	remote.payload.inputBatch.records[0].keys = 2;
	assert_int_equal(GBALinkInputSyncHandleBatch(
	    &sync, GBA_LINK_ROLE_CLIENT, &remote.payload.inputBatch),
	    GBA_LINK_INPUT_OK);
	uint16_t keys[2];
	assert_true(GBALinkInputSyncReady(&sync, 0));
	assert_int_equal(GBALinkInputSyncAuthor(
	    &sync, 0, 3, &local), GBA_LINK_INPUT_OK);
	assert_int_equal(GBALinkInputSyncConsume(
	    &sync, 0, keys), GBA_LINK_INPUT_OK);
	assert_false(GBALinkInputSyncReady(&sync, 1));
	assert_int_equal(GBALinkInputSyncConsume(
	    &sync, 1, keys), GBA_LINK_INPUT_MISSING);

	/* A later frame arriving early cannot fill or predict frame 1. */
	remote.payload.inputBatch.records[0].frame = 2;
	remote.payload.inputBatch.records[0].keys = 8;
	assert_int_equal(GBALinkInputSyncHandleBatch(
	    &sync, GBA_LINK_ROLE_CLIENT, &remote.payload.inputBatch),
	    GBA_LINK_INPUT_OK);
	assert_false(GBALinkInputSyncReady(&sync, 1));
	remote.payload.inputBatch.records[0].frame = 1;
	remote.payload.inputBatch.records[0].keys = 4;
	assert_int_equal(GBALinkInputSyncHandleBatch(
	    &sync, GBA_LINK_ROLE_CLIENT, &remote.payload.inputBatch),
	    GBA_LINK_INPUT_OK);
	assert_true(GBALinkInputSyncReady(&sync, 1));
	assert_int_equal(GBALinkInputSyncConsume(
	    &sync, 1, keys), GBA_LINK_INPUT_OK);
	assert_int_equal(keys[1], 4);
	assert_int_equal(GBALinkInputSyncConsume(
	    &sync, 1, keys), GBA_LINK_INPUT_INVALID_STATE);
}

M_TEST_SUITE_DEFINE(GBALinkInputSync,
	cmocka_unit_test(delaySelectionUsesRttJitterAndClamps),
	cmocka_unit_test(calibrationDigestAndSelectorHaveGoldenResults),
	cmocka_unit_test(selectorExcludesOneMaximumAndHonorsProductFloor),
	cmocka_unit_test(selectorRejectsInvalidRangesAndSamples),
	cmocka_unit_test(selectorRationalFrameEdgesAreExact),
	cmocka_unit_test(selectorFailureDoesNotPartiallyReplaceOutput),
	cmocka_unit_test(initialSeedMakesFirstFramesAvailableWithoutPrediction),
	cmocka_unit_test(authoredBatchHasShortOrderedRedundancyWindow),
	cmocka_unit_test(duplicatesAreIdempotentAndConflictsFail),
	cmocka_unit_test(rejectedBatchCannotPartiallyInstallInputs),
	cmocka_unit_test(ownershipGenerationWindowAndMissingInputFailClosed),
	cmocka_unit_test(oneAndTwoFramePoliciesMapExactlyOnceAcrossRingWrap),
	cmocka_unit_test(lateArrivalChangesOnlyFutureReadiness))
