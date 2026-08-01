/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "util/test/suite.h"

#include <mgba/core/core.h>
#include <mgba/flags.h>
#include <mgba/gba/core.h>
#include <mgba/internal/gba/cart/gpio.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/replica.h>
#include <mgba/internal/gba/savedata.h>
#include <mgba/internal/gba/serialize.h>
#include <mgba/internal/gba/sio.h>
#include <mgba-util/sha256.h>
#include <mgba-util/vfs.h>

enum {
	TEST_ROM_SIZE = 0x400,
	TEST_GENERATION = 0x10203040,
};

struct ReplicaTestCore {
	struct mCore* core;
	uint8_t* rom;
};

static void _makeRom(uint8_t* rom, size_t size) {
	memset(rom, 0, size);
	memcpy(&rom[0xA0], "REPLICA TEST", 12);
	memcpy(&rom[0xAC], "RPTE", 4);
	for (size_t i = 0xC0; i < size; ++i) {
		rom[i] = i * 37 + 11;
	}
}

static struct ReplicaTestCore _createCore(void) {
	struct ReplicaTestCore fixture;
	memset(&fixture, 0, sizeof(fixture));
	fixture.rom = malloc(TEST_ROM_SIZE);
	assert_non_null(fixture.rom);
	_makeRom(fixture.rom, TEST_ROM_SIZE);
	fixture.core = GBACoreCreate();
	assert_non_null(fixture.core);
	assert_true(fixture.core->init(fixture.core));
	mCoreInitConfig(fixture.core, NULL);
	assert_true(fixture.core->loadROM(
	    fixture.core, VFileFromMemory(fixture.rom, TEST_ROM_SIZE)));
	fixture.core->reset(fixture.core);
	return fixture;
}

static void _destroyCore(struct ReplicaTestCore* fixture) {
	mCoreConfigDeinit(&fixture->core->config);
	fixture->core->deinit(fixture->core);
	free(fixture->rom);
	memset(fixture, 0, sizeof(*fixture));
}

static void _fillSource(
	struct mCore* core, enum GBASavedataType saveType, uint8_t seed) {
	struct GBA* gba = core->board;
	GBASavedataForceType(&gba->memory.savedata, saveType);
	size_t saveSize = GBASavedataSize(&gba->memory.savedata);
	for (size_t i = 0; i < saveSize; ++i) {
		gba->memory.savedata.data[i] = seed + i * 13;
	}
	gba->cpu->gprs[0] = 0x12340000 | seed;
	gba->cpu->gprs[7] = 0xA5A50000 | seed;
	((uint8_t*) gba->memory.wram)[0x123] = seed;
	((uint8_t*) gba->memory.wram)[0x23456] = seed ^ 0xFF;
	((uint8_t*) gba->memory.iwram)[0x456] = seed + 3;
	gba->video.frameCounter = 1000 + seed;
	gba->timing.globalCycles = UINT64_C(0x123456780000) + seed;
	gba->memory.hw.devices = HW_RTC | HW_GYRO | HW_LIGHT_SENSOR | HW_TILT;
	gba->memory.hw.pinState = seed & 7;
	gba->memory.hw.writeLatch = (seed ^ 3) & 0xF;
	gba->memory.hw.direction = 7;
	gba->memory.hw.rtc.bytesRemaining = 4;
	gba->memory.hw.rtc.bitsRead = 11;
	gba->memory.hw.rtc.bits = 0x13579;
	gba->memory.hw.rtc.control = 0x40;
	gba->memory.hw.rtc.time[0] = 0x26;
	gba->memory.hw.rtc.time[6] = seed;
	gba->memory.hw.rtc.lastLatch = INT64_C(1700000000) + seed;
	gba->memory.hw.rtc.offset = -3600 - seed;
	gba->memory.hw.gyroSample = 0x3456;
	gba->memory.hw.lightSample = seed;
	gba->memory.hw.tiltX = 0x4567;
	gba->memory.hw.tiltY = 0x5678;
	gba->memory.hw.tiltState = 2;
	core->rtc.override = RTC_FIXED;
	core->rtc.value = INT64_C(1700000000123) + seed;
	GBASIOWriteRCNT(&gba->sio, 0);
	GBASIOWriteSIOCNT(&gba->sio, 0x2000);
	assert_int_equal(gba->sio.mode, GBA_SIO_MULTI);
	assert_true(GBAReplicaIsQuiescent(core));
}

static enum GBAReplicaResult _assemble(
	const struct GBAReplicaBundle* bundle,
	struct GBAReplicaPayload* payload) {
	struct GBAReplicaAssembler assembler;
	enum GBAReplicaResult result = GBAReplicaAssemblerInit(
	    &assembler, &bundle->manifest, bundle->manifest.player,
	    bundle->manifest.generation, NULL);
	if (result != GBA_REPLICA_OK) {
		return result;
	}
	for (size_t chunk = assembler.chunkCount; chunk > 0; --chunk) {
		uint32_t offset = (chunk - 1) * bundle->manifest.chunkSize;
		size_t size = bundle->manifest.encodedSize - offset;
		if (size > bundle->manifest.chunkSize) {
			size = bundle->manifest.chunkSize;
		}
		result = GBAReplicaAssemblerAdd(
		    &assembler, bundle->manifest.player,
		    bundle->manifest.generation, offset,
		    &bundle->encodedData[offset], size);
		if (result != GBA_REPLICA_OK) {
			GBAReplicaAssemblerDeinit(&assembler);
			return result;
		}
	}
	result = GBAReplicaAssemblerFinalize(&assembler, payload);
	GBAReplicaAssemblerDeinit(&assembler);
	return result;
}

static void _assertCoreEqual(struct mCore* expected, struct mCore* actual) {
	struct GBASerializedState* expectedState = calloc(1, sizeof(*expectedState));
	struct GBASerializedState* actualState = calloc(1, sizeof(*actualState));
	assert_non_null(expectedState);
	assert_non_null(actualState);
	GBASerialize(expected->board, expectedState);
	GBASerialize(actual->board, actualState);
	assert_memory_equal(expectedState, actualState, sizeof(*expectedState));
	struct GBA* expectedGBA = expected->board;
	struct GBA* actualGBA = actual->board;
	size_t saveSize = GBASavedataSize(&expectedGBA->memory.savedata);
	assert_int_equal(saveSize, GBASavedataSize(&actualGBA->memory.savedata));
	if (saveSize) {
		assert_memory_equal(
		    expectedGBA->memory.savedata.data,
		    actualGBA->memory.savedata.data, saveSize);
	}
	assert_int_equal(expected->rtc.override, actual->rtc.override);
	assert_int_equal(expected->rtc.value, actual->rtc.value);
	assert_int_equal(
	    expectedGBA->memory.hw.rtc.lastLatch,
	    actualGBA->memory.hw.rtc.lastLatch);
	assert_int_equal(
	    expectedGBA->memory.hw.rtc.offset,
	    actualGBA->memory.hw.rtc.offset);
	free(actualState);
	free(expectedState);
}

static void _roundTrip(
	enum GBASavedataType saveType, enum GBAReplicaEncoding encoding,
	uint8_t seed) {
	struct ReplicaTestCore source = _createCore();
	struct ReplicaTestCore target = _createCore();
	_fillSource(source.core, saveType, seed);

	struct GBASerializedState* before = calloc(1, sizeof(*before));
	struct GBASerializedState* after = calloc(1, sizeof(*after));
	assert_non_null(before);
	assert_non_null(after);
	GBASerialize(source.core->board, before);
	struct GBAReplicaBundle bundle;
	assert_int_equal(
	    GBAReplicaCapture(
	        source.core, 1, TEST_GENERATION, encoding, 8192, &bundle),
	    GBA_REPLICA_OK);
	GBASerialize(source.core->board, after);
	assert_memory_equal(before, after, sizeof(*before));

	struct GBAReplicaPayload payload;
	assert_int_equal(_assemble(&bundle, &payload), GBA_REPLICA_OK);
	assert_int_equal(
	    GBAReplicaRestore(
	        target.core, &bundle.manifest, &payload, 1, TEST_GENERATION),
	    GBA_REPLICA_OK);
	_assertCoreEqual(source.core, target.core);

	GBAReplicaPayloadDeinit(&payload);
	GBAReplicaBundleDeinit(&bundle);
	free(after);
	free(before);
	_destroyCore(&target);
	_destroyCore(&source);
}

M_TEST_DEFINE(manifestEncodingIsCanonical) {
	struct ReplicaTestCore source = _createCore();
	_fillSource(source.core, GBA_SAVEDATA_SRAM, 4);
	struct GBAReplicaBundle bundle;
	assert_int_equal(
	    GBAReplicaCapture(
	        source.core, 0, TEST_GENERATION,
	        GBA_REPLICA_ENCODING_NONE, 4096, &bundle),
	    GBA_REPLICA_OK);
	uint8_t encoded[GBA_REPLICA_MANIFEST_SIZE];
	uint8_t reencoded[GBA_REPLICA_MANIFEST_SIZE];
	assert_int_equal(
	    GBAReplicaManifestEncode(&bundle.manifest, encoded),
	    GBA_REPLICA_OK);
	assert_memory_equal(encoded, "GBRP", 4);
	assert_int_equal(encoded[4], GBA_REPLICA_FORMAT_VERSION);
	assert_int_equal(encoded[6], GBA_REPLICA_MANIFEST_SIZE & 0xFF);
	struct GBAReplicaManifest decoded;
	assert_int_equal(
	    GBAReplicaManifestDecode(
	        encoded, sizeof(encoded), NULL, &decoded),
	    GBA_REPLICA_OK);
	assert_int_equal(
	    GBAReplicaManifestEncode(&decoded, reencoded), GBA_REPLICA_OK);
	assert_memory_equal(encoded, reencoded, sizeof(encoded));
	GBAReplicaBundleDeinit(&bundle);
	_destroyCore(&source);
}

M_TEST_DEFINE(allSaveTypesRoundTrip) {
	static const enum GBASavedataType types[] = {
		GBA_SAVEDATA_AUTODETECT,
		GBA_SAVEDATA_FORCE_NONE,
		GBA_SAVEDATA_SRAM,
		GBA_SAVEDATA_SRAM512,
		GBA_SAVEDATA_FLASH512,
		GBA_SAVEDATA_FLASH1M,
		GBA_SAVEDATA_EEPROM,
		GBA_SAVEDATA_EEPROM512,
	};
	for (size_t i = 0; i < sizeof(types) / sizeof(*types); ++i) {
		_roundTrip(types[i], GBA_REPLICA_ENCODING_NONE, 10 + i);
	}
}

#ifdef USE_ZLIB
M_TEST_DEFINE(compressedRoundTrip) {
	_roundTrip(GBA_SAVEDATA_FLASH1M, GBA_REPLICA_ENCODING_DEFLATE, 31);
}
#endif

M_TEST_DEFINE(differentCyclesProduceDistinctBundles) {
	struct ReplicaTestCore first = _createCore();
	struct ReplicaTestCore second = _createCore();
	_fillSource(first.core, GBA_SAVEDATA_EEPROM, 41);
	_fillSource(second.core, GBA_SAVEDATA_EEPROM, 41);
	struct GBA* secondGBA = second.core->board;
	secondGBA->timing.globalCycles += 17;
	secondGBA->video.frameCounter += 2;
	struct GBAReplicaBundle firstBundle;
	struct GBAReplicaBundle secondBundle;
	assert_int_equal(
	    GBAReplicaCapture(
	        first.core, 0, 1, GBA_REPLICA_ENCODING_NONE, 4096,
	        &firstBundle),
	    GBA_REPLICA_OK);
	assert_int_equal(
	    GBAReplicaCapture(
	        second.core, 0, 2, GBA_REPLICA_ENCODING_NONE, 4096,
	        &secondBundle),
	    GBA_REPLICA_OK);
	assert_memory_not_equal(
	    firstBundle.manifest.uncompressedDigest,
	    secondBundle.manifest.uncompressedDigest, MGBA_SHA256_DIGEST_SIZE);
	assert_int_not_equal(
	    firstBundle.manifest.globalCycles,
	    secondBundle.manifest.globalCycles);
	GBAReplicaBundleDeinit(&secondBundle);
	GBAReplicaBundleDeinit(&firstBundle);
	_destroyCore(&second);
	_destroyCore(&first);
}

M_TEST_DEFINE(captureRequiresQuiescentDetachedCore) {
	struct ReplicaTestCore source = _createCore();
	struct GBA* gba = source.core->board;
	gba->sio.siocnt = GBASIOMultiplayerFillBusy(gba->sio.siocnt);
	struct GBAReplicaBundle bundle;
	assert_int_equal(
	    GBAReplicaCapture(
	        source.core, 0, 1, GBA_REPLICA_ENCODING_NONE, 4096,
	        &bundle),
	    GBA_REPLICA_NOT_QUIESCENT);
	gba->sio.siocnt &= ~0x80;
	assert_int_equal(
	    GBAReplicaCapture(
	        source.core, 0, 1, GBA_REPLICA_ENCODING_NONE,
	        GBA_REPLICA_MAX_CHUNK_SIZE + 1, &bundle),
	    GBA_REPLICA_LIMIT_EXCEEDED);
	source.core->rtc.override = RTC_CUSTOM_START;
	assert_int_equal(
	    GBAReplicaCapture(
	        source.core, 0, 1, GBA_REPLICA_ENCODING_NONE, 4096,
	        &bundle),
	    GBA_REPLICA_UNSUPPORTED);
#ifndef USE_ZLIB
	source.core->rtc.override = RTC_NO_OVERRIDE;
	assert_int_equal(
	    GBAReplicaCapture(
	        source.core, 0, 1, GBA_REPLICA_ENCODING_DEFLATE, 4096,
	        &bundle),
	    GBA_REPLICA_UNSUPPORTED);
#endif
	_destroyCore(&source);
}

M_TEST_DEFINE(chunkAssemblyRules) {
	struct ReplicaTestCore source = _createCore();
	_fillSource(source.core, GBA_SAVEDATA_SRAM, 51);
	struct GBAReplicaBundle bundle;
	assert_int_equal(
	    GBAReplicaCapture(
	        source.core, 0, 9, GBA_REPLICA_ENCODING_NONE, 4096,
	        &bundle),
	    GBA_REPLICA_OK);
	struct GBAReplicaAssembler assembler;
	assert_int_equal(
	    GBAReplicaAssemblerInit(
	        &assembler, &bundle.manifest, 1, 9, NULL),
	    GBA_REPLICA_WRONG_PLAYER);
	assert_int_equal(
	    GBAReplicaAssemblerInit(
	        &assembler, &bundle.manifest, 0, 10, NULL),
	    GBA_REPLICA_WRONG_GENERATION);
	assert_int_equal(
	    GBAReplicaAssemblerInit(
	        &assembler, &bundle.manifest, 0, 9, NULL),
	    GBA_REPLICA_OK);
	assert_int_equal(
	    GBAReplicaAssemblerAdd(
	        &assembler, 1, 9, 0, bundle.encodedData, 4096),
	    GBA_REPLICA_WRONG_PLAYER);
	assert_int_equal(
	    GBAReplicaAssemblerAdd(
	        &assembler, 0, 10, 0, bundle.encodedData, 4096),
	    GBA_REPLICA_WRONG_GENERATION);
	assert_int_equal(
	    GBAReplicaAssemblerAdd(
	        &assembler, 0, 9, 1, &bundle.encodedData[1], 4096),
	    GBA_REPLICA_OVERLAP);
	assert_int_equal(
	    GBAReplicaAssemblerAdd(
	        &assembler, 0, 9, 0, bundle.encodedData, 4095),
	    GBA_REPLICA_INVALID_RANGE);
	assert_int_equal(
	    GBAReplicaAssemblerAdd(
	        &assembler, 0, 9, 0, bundle.encodedData, 4096),
	    GBA_REPLICA_OK);
	assert_int_equal(
	    GBAReplicaAssemblerAdd(
	        &assembler, 0, 9, 0, bundle.encodedData, 4096),
	    GBA_REPLICA_DUPLICATE);
	uint8_t conflicting[4096];
	memcpy(conflicting, bundle.encodedData, sizeof(conflicting));
	conflicting[3] ^= 1;
	assert_int_equal(
	    GBAReplicaAssemblerAdd(
	        &assembler, 0, 9, 0, conflicting, sizeof(conflicting)),
	    GBA_REPLICA_CONFLICTING_DUPLICATE);
	struct GBAReplicaPayload payload;
	assert_int_equal(
	    GBAReplicaAssemblerFinalize(&assembler, &payload),
	    GBA_REPLICA_HOLE);
	GBAReplicaAssemblerDeinit(&assembler);
	GBAReplicaBundleDeinit(&bundle);
	_destroyCore(&source);
}

M_TEST_DEFINE(malformedManifestAndDigestsFailClosed) {
	struct ReplicaTestCore source = _createCore();
	_fillSource(source.core, GBA_SAVEDATA_SRAM, 61);
	struct GBAReplicaBundle bundle;
	assert_int_equal(
	    GBAReplicaCapture(
	        source.core, 1, 11, GBA_REPLICA_ENCODING_NONE, 4096,
	        &bundle),
	    GBA_REPLICA_OK);
	uint8_t wire[GBA_REPLICA_MANIFEST_SIZE];
	assert_int_equal(
	    GBAReplicaManifestEncode(&bundle.manifest, wire), GBA_REPLICA_OK);
	struct GBAReplicaManifest manifest;
	wire[0] ^= 1;
	assert_int_equal(
	    GBAReplicaManifestDecode(wire, sizeof(wire), NULL, &manifest),
	    GBA_REPLICA_INVALID_MANIFEST);
	wire[0] ^= 1;
	wire[105] = 1;
	assert_int_equal(
	    GBAReplicaManifestDecode(wire, sizeof(wire), NULL, &manifest),
	    GBA_REPLICA_INVALID_MANIFEST);
	wire[105] = 0;
	assert_int_equal(
	    GBAReplicaManifestDecode(wire, sizeof(wire) - 1, NULL, &manifest),
	    GBA_REPLICA_INVALID_MANIFEST);

	manifest = bundle.manifest;
	manifest.encodedSize = GBA_REPLICA_MAX_ENCODED_SIZE + 1;
	assert_int_equal(
	    GBAReplicaManifestValidate(&manifest, NULL),
	    GBA_REPLICA_LIMIT_EXCEEDED);
	manifest = bundle.manifest;
	manifest.stateSize--;
	assert_int_equal(
	    GBAReplicaManifestValidate(&manifest, NULL),
	    GBA_REPLICA_INVALID_MANIFEST);

	manifest = bundle.manifest;
	manifest.encodedDigest[0] ^= 1;
	struct GBAReplicaAssembler assembler;
	assert_int_equal(
	    GBAReplicaAssemblerInit(&assembler, &manifest, 1, 11, NULL),
	    GBA_REPLICA_OK);
	for (size_t offset = 0; offset < bundle.encodedSize;
	     offset += bundle.manifest.chunkSize) {
		size_t size = bundle.encodedSize - offset;
		if (size > bundle.manifest.chunkSize) {
			size = bundle.manifest.chunkSize;
		}
		assert_int_equal(
		    GBAReplicaAssemblerAdd(
		        &assembler, 1, 11, offset,
		        &bundle.encodedData[offset], size),
		    GBA_REPLICA_OK);
	}
	struct GBAReplicaPayload payload;
	assert_int_equal(
	    GBAReplicaAssemblerFinalize(&assembler, &payload),
	    GBA_REPLICA_DIGEST_MISMATCH);
	GBAReplicaAssemblerDeinit(&assembler);
	GBAReplicaBundleDeinit(&bundle);
	_destroyCore(&source);
}

#ifdef USE_ZLIB
M_TEST_DEFINE(decompressionExpansionIsBounded) {
	struct ReplicaTestCore source = _createCore();
	_fillSource(source.core, GBA_SAVEDATA_FLASH1M, 71);
	struct GBAReplicaBundle bundle;
	assert_int_equal(
	    GBAReplicaCapture(
	        source.core, 0, 12, GBA_REPLICA_ENCODING_DEFLATE, 4096,
	        &bundle),
	    GBA_REPLICA_OK);
	struct GBAReplicaManifest manifest = bundle.manifest;
	manifest.saveType = GBA_SAVEDATA_FORCE_NONE;
	manifest.saveSize = 0;
	manifest.uncompressedSize = manifest.stateSize;
	struct GBAReplicaAssembler assembler;
	assert_int_equal(
	    GBAReplicaAssemblerInit(&assembler, &manifest, 0, 12, NULL),
	    GBA_REPLICA_OK);
	for (size_t offset = 0; offset < bundle.encodedSize;
	     offset += bundle.manifest.chunkSize) {
		size_t size = bundle.encodedSize - offset;
		if (size > bundle.manifest.chunkSize) {
			size = bundle.manifest.chunkSize;
		}
		assert_int_equal(
		    GBAReplicaAssemblerAdd(
		        &assembler, 0, 12, offset,
		        &bundle.encodedData[offset], size),
		    GBA_REPLICA_OK);
	}
	struct GBAReplicaPayload payload;
	assert_int_equal(
	    GBAReplicaAssemblerFinalize(&assembler, &payload),
	    GBA_REPLICA_COMPRESSION_ERROR);
	GBAReplicaAssemblerDeinit(&assembler);
	GBAReplicaBundleDeinit(&bundle);
	_destroyCore(&source);
}
#endif

M_TEST_DEFINE(restoreRejectsIdentityAndPayloadMismatch) {
	struct ReplicaTestCore source = _createCore();
	struct ReplicaTestCore target = _createCore();
	_fillSource(source.core, GBA_SAVEDATA_EEPROM512, 81);
	struct GBAReplicaBundle bundle;
	assert_int_equal(
	    GBAReplicaCapture(
	        source.core, 1, 13, GBA_REPLICA_ENCODING_NONE, 4096,
	        &bundle),
	    GBA_REPLICA_OK);
	struct GBAReplicaPayload payload;
	assert_int_equal(_assemble(&bundle, &payload), GBA_REPLICA_OK);
	assert_int_equal(
	    GBAReplicaRestore(target.core, &bundle.manifest, &payload, 0, 13),
	    GBA_REPLICA_WRONG_PLAYER);
	assert_int_equal(
	    GBAReplicaRestore(target.core, &bundle.manifest, &payload, 1, 14),
	    GBA_REPLICA_WRONG_GENERATION);
	payload.data[0x100] ^= 1;
	assert_int_equal(
	    GBAReplicaRestore(target.core, &bundle.manifest, &payload, 1, 13),
	    GBA_REPLICA_DIGEST_MISMATCH);
	payload.data[0x100] ^= 1;
	((struct GBA*) target.core->board)->romCrc32 ^= 1;
	assert_int_equal(
	    GBAReplicaRestore(target.core, &bundle.manifest, &payload, 1, 13),
	    GBA_REPLICA_INVALID_STATE);
	GBAReplicaPayloadDeinit(&payload);
	GBAReplicaBundleDeinit(&bundle);
	_destroyCore(&target);
	_destroyCore(&source);
}

M_TEST_SUITE_DEFINE(GBAReplica,
	cmocka_unit_test(manifestEncodingIsCanonical),
	cmocka_unit_test(allSaveTypesRoundTrip),
#ifdef USE_ZLIB
	cmocka_unit_test(compressedRoundTrip),
#endif
	cmocka_unit_test(differentCyclesProduceDistinctBundles),
	cmocka_unit_test(captureRequiresQuiescentDetachedCore),
	cmocka_unit_test(chunkAssemblyRules),
	cmocka_unit_test(malformedManifestAndDigestsFailClosed),
#ifdef USE_ZLIB
	cmocka_unit_test(decompressionExpansionIsBounded),
#endif
	cmocka_unit_test(restoreRejectsIdentityAndPayloadMismatch))
