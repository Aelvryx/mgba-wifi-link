/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "util/test/suite.h"

#include <mgba/core/core.h>
#include <mgba/gba/core.h>
#include <mgba/internal/gba/sio/netplay/identity.h>
#include <mgba-util/sha1.h>
#include <mgba-util/vfs.h>

#include <fcntl.h>

static void _makeRom(uint8_t* rom, size_t size, uint8_t seed) {
	for (size_t i = 0; i < size; ++i) {
		rom[i] = seed + i * 17;
	}
	memcpy(&rom[0xA0], "NETPLAY TEST", 12);
	memcpy(&rom[0xAC], "NTPE", 4);
}

static struct mCore* _loadCore(struct VFile* rom) {
	struct mCore* core = GBACoreCreate();
	assert_non_null(core);
	assert_true(core->init(core));
	mCoreInitConfig(core, NULL);
	assert_true(core->loadROM(core, rom));
	return core;
}

static void _destroyCore(struct mCore* core) {
	mCoreConfigDeinit(&core->config);
	core->deinit(core);
}

static void _assertIdentity(
    struct mCore* core, const uint8_t* expectedRom, size_t expectedSize) {
	struct GBALinkContentIdentity identity;
	memset(&identity, 0xA5, sizeof(identity));
	assert_true(GBALinkContentIdentityFromCore(core, &identity));
	assert_int_equal(identity.romSize, expectedSize);
	uint8_t expectedSha1[GBA_LINK_ROM_SHA1_SIZE];
	sha1Buffer(expectedRom, expectedSize, expectedSha1);
	assert_memory_equal(
	    identity.romSha1, expectedSha1, sizeof(expectedSha1));
}

M_TEST_DEFINE(memoryLoadedContentIdentity) {
	enum { ROM_SIZE = 0x200 };
	uint8_t* rom = malloc(ROM_SIZE);
	_makeRom(rom, ROM_SIZE, 1);
	struct mCore* core = _loadCore(VFileFromMemory(rom, ROM_SIZE));
	_assertIdentity(core, rom, ROM_SIZE);
	_destroyCore(core);
	free(rom);
}

M_TEST_DEFINE(fileBackedContentIdentity) {
	enum { ROM_SIZE = 0x200 };
	const char* path = "mgba-netplay-identity-file.gba";
	uint8_t rom[ROM_SIZE];
	_makeRom(rom, sizeof(rom), 2);
	struct VFile* output = VFileOpen(
	    path, O_WRONLY | O_CREAT | O_TRUNC);
	assert_non_null(output);
	assert_int_equal(output->write(output, rom, sizeof(rom)), sizeof(rom));
	assert_true(output->close(output));
	struct mCore* core = _loadCore(VFileOpen(path, O_RDONLY));
	_assertIdentity(core, rom, sizeof(rom));
	_destroyCore(core);
	assert_int_equal(remove(path), 0);
}

M_TEST_DEFINE(extractedMemoryMatchesFileBackedIdentity) {
	enum { ROM_SIZE = 0x200 };
	const char* path = "mgba-netplay-identity-extracted.gba";
	uint8_t* extracted = malloc(ROM_SIZE);
	_makeRom(extracted, ROM_SIZE, 3);
	struct mCore* memoryCore = _loadCore(
	    VFileFromMemory(extracted, ROM_SIZE));
	struct GBALinkContentIdentity memoryIdentity;
	assert_true(GBALinkContentIdentityFromCore(
	    memoryCore, &memoryIdentity));

	struct VFile* output = VFileOpen(
	    path, O_WRONLY | O_CREAT | O_TRUNC);
	assert_non_null(output);
	assert_int_equal(output->write(output, extracted, ROM_SIZE), ROM_SIZE);
	assert_true(output->close(output));
	struct mCore* fileCore = _loadCore(VFileOpen(path, O_RDONLY));
	struct GBALinkContentIdentity fileIdentity;
	assert_true(GBALinkContentIdentityFromCore(
	    fileCore, &fileIdentity));
	assert_true(GBALinkContentIdentityEqual(
	    &memoryIdentity, &fileIdentity));

	_destroyCore(fileCore);
	_destroyCore(memoryCore);
	assert_int_equal(remove(path), 0);
	free(extracted);
}

M_TEST_DEFINE(frontendPatchedBytesDefineIdentity) {
	enum { ROM_SIZE = 0x200 };
	uint8_t* original = malloc(ROM_SIZE);
	uint8_t* patched = malloc(ROM_SIZE);
	_makeRom(original, ROM_SIZE, 4);
	memcpy(patched, original, ROM_SIZE);
	patched[0x180] ^= 0x80;

	struct mCore* originalCore = _loadCore(
	    VFileFromMemory(original, ROM_SIZE));
	struct mCore* patchedCore = _loadCore(
	    VFileFromMemory(patched, ROM_SIZE));
	struct GBALinkContentIdentity originalIdentity;
	struct GBALinkContentIdentity patchedIdentity;
	assert_true(GBALinkContentIdentityFromCore(
	    originalCore, &originalIdentity));
	assert_true(GBALinkContentIdentityFromCore(
	    patchedCore, &patchedIdentity));
	assert_false(GBALinkContentIdentityEqual(
	    &originalIdentity, &patchedIdentity));
	_assertIdentity(patchedCore, patched, ROM_SIZE);

	_destroyCore(patchedCore);
	_destroyCore(originalCore);
	free(patched);
	free(original);
}

static struct GBALinkDeterminismProfileInput _profile(void) {
	return (struct GBALinkDeterminismProfileInput) {
		.useBios = false,
		.timingModel = 0,
		.overclockQ16 = 0x10000,
		.speedHackFlags = 0,
		.idleOptimization = GBA_LINK_IDLE_OPTIMIZATION_NONE,
		.rtcOverrideMode = GBA_LINK_RTC_OVERRIDE_NONE,
		.cheatsEnabled = false,
	};
}

M_TEST_DEFINE(profileCategoriesAreStableAndIndependent) {
	struct GBALinkDeterminismProfileInput input = _profile();
	struct GBALinkDeterminismDigest baseline[GBA_LINK_MAX_DETERMINISM_DIGESTS];
	assert_true(GBALinkDeterminismProfileBuild(&input, baseline));
	for (unsigned i = 0; i < GBA_LINK_MAX_DETERMINISM_DIGESTS; ++i) {
		assert_int_equal(baseline[i].category, i + 1);
	}

	struct GBALinkDeterminismDigest changed[GBA_LINK_MAX_DETERMINISM_DIGESTS];
	input.useBios = true;
	memset(input.biosSha1, 0x42, sizeof(input.biosSha1));
	assert_true(GBALinkDeterminismProfileBuild(&input, changed));
	assert_memory_not_equal(
	    baseline[0].digest, changed[0].digest, GBA_LINK_DIGEST_SIZE);
	for (unsigned i = 1; i < GBA_LINK_MAX_DETERMINISM_DIGESTS; ++i) {
		assert_memory_equal(
		    baseline[i].digest, changed[i].digest, GBA_LINK_DIGEST_SIZE);
	}

	input = _profile();
	input.overclockQ16 = 0x18000;
	assert_true(GBALinkDeterminismProfileBuild(&input, changed));
	assert_memory_not_equal(
	    baseline[1].digest, changed[1].digest, GBA_LINK_DIGEST_SIZE);
	assert_memory_equal(
	    baseline[0].digest, changed[0].digest, GBA_LINK_DIGEST_SIZE);
	assert_memory_equal(
	    baseline[2].digest, changed[2].digest, GBA_LINK_DIGEST_SIZE);
	assert_memory_equal(
	    baseline[3].digest, changed[3].digest, GBA_LINK_DIGEST_SIZE);
	assert_memory_equal(
	    baseline[4].digest, changed[4].digest, GBA_LINK_DIGEST_SIZE);

	input = _profile();
	input.cheatsEnabled = true;
	assert_true(GBALinkDeterminismProfileBuild(&input, changed));
	assert_memory_not_equal(
	    baseline[4].digest, changed[4].digest, GBA_LINK_DIGEST_SIZE);
}

M_TEST_DEFINE(invalidProfileEnumsAreRejectedWithoutOutput) {
	struct GBALinkDeterminismProfileInput input = _profile();
	input.rtcOverrideMode = 99;
	struct GBALinkDeterminismDigest output[GBA_LINK_MAX_DETERMINISM_DIGESTS];
	memset(output, 0xA5, sizeof(output));
	struct GBALinkDeterminismDigest sentinel[GBA_LINK_MAX_DETERMINISM_DIGESTS];
	memcpy(sentinel, output, sizeof(output));
	assert_false(GBALinkDeterminismProfileBuild(&input, output));
	assert_memory_equal(output, sentinel, sizeof(output));
}

M_TEST_SUITE_DEFINE(GBALinkIdentity,
	cmocka_unit_test(memoryLoadedContentIdentity),
	cmocka_unit_test(fileBackedContentIdentity),
	cmocka_unit_test(extractedMemoryMatchesFileBackedIdentity),
	cmocka_unit_test(frontendPatchedBytesDefineIdentity),
	cmocka_unit_test(profileCategoriesAreStableAndIndependent),
	cmocka_unit_test(invalidProfileEnumsAreRejectedWithoutOutput))
