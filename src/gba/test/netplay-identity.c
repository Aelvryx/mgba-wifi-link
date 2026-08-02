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

static struct GBALinkV2DeterminismProfileInput _v2Profile(void) {
	return (struct GBALinkV2DeterminismProfileInput) {
		.biosMode = GBA_LINK_V2_BIOS_HLE,
		.emulationCompatibilityVersion = 1,
		.timingModelFlags = 0,
		.overclockQ16 = 0x10000,
		.speedHackMask = 0,
		.idlePolicy = GBA_LINK_V2_IDLE_DETECT,
		.allowOpposingDirections = true,
		.rtcNormalizationPolicyVersion = 1,
		.fakeEpochArithmeticVersion = 1,
		.rtcSemanticsModelVersion = 1,
		.cheatsEnabled = false,
		.authoritativeInputFormatVersion = 1,
		.cartridgeRequiredInputMask = GBA_LINK_V2_INPUT_DIGITAL,
	};
}

M_TEST_DEFINE(v2ProfileHasCanonicalGoldenDigests) {
	static const uint8_t expected[][MGBA_SHA256_DIGEST_SIZE] = {
		{ 0xd7, 0x33, 0x27, 0xeb, 0xa1, 0x0f, 0x3c, 0xfb, 0x92, 0xf8, 0x06, 0xdd, 0x2c, 0xae, 0xcb, 0x48, 0x35, 0xb9, 0x55, 0x08, 0xb6, 0x20, 0x90, 0xc4, 0x3b, 0xc8, 0x86, 0xfb, 0xa6, 0xc9, 0x34, 0x57 },
		{ 0x5c, 0x2c, 0xff, 0x4b, 0x10, 0x26, 0x10, 0x3a, 0xc5, 0x3b, 0x25, 0xc4, 0x75, 0x96, 0xb0, 0xb4, 0xbd, 0x15, 0x7e, 0x8a, 0x57, 0xed, 0x4e, 0x8f, 0xcc, 0x88, 0x0a, 0xe6, 0x0e, 0xee, 0x8e, 0x28 },
		{ 0x70, 0x20, 0xb7, 0xfa, 0xa8, 0x79, 0x2e, 0x8d, 0x39, 0x9b, 0x06, 0x81, 0xf2, 0x54, 0xed, 0x21, 0xfb, 0x70, 0x40, 0xd6, 0x70, 0x32, 0x84, 0x17, 0x7d, 0x07, 0x5c, 0x42, 0x0d, 0x63, 0x02, 0x76 },
		{ 0x4a, 0x61, 0xe7, 0xf0, 0xf0, 0x32, 0xcc, 0x88, 0xaf, 0xcd, 0xf5, 0xe9, 0xb4, 0x30, 0x2e, 0x0f, 0x40, 0xf5, 0x1f, 0x5a, 0xc7, 0x5a, 0x7f, 0xd6, 0xfa, 0x2e, 0x41, 0x54, 0xc6, 0x83, 0x66, 0x2d },
		{ 0xdc, 0x18, 0xee, 0x89, 0x37, 0xb1, 0x5e, 0x5b, 0x2e, 0x89, 0xcc, 0x10, 0x60, 0x6c, 0x41, 0x4b, 0x06, 0xf7, 0xa2, 0xec, 0x72, 0x11, 0x68, 0x4e, 0x8f, 0xcc, 0xc5, 0xb0, 0x21, 0x96, 0xb6, 0xde },
		{ 0xd5, 0xea, 0xe8, 0x1b, 0xc6, 0xd0, 0xa2, 0x5c, 0x26, 0xb5, 0x28, 0xfb, 0xe6, 0xc7, 0x93, 0x5a, 0xd5, 0x80, 0x34, 0x79, 0x23, 0x1c, 0x53, 0x9f, 0xb4, 0xa3, 0xea, 0x7b, 0x1f, 0x60, 0xdd, 0xf2 },
		{ 0xf5, 0xdf, 0x6a, 0x30, 0xc2, 0x8e, 0x11, 0x49, 0x1f, 0xb1, 0x54, 0x25, 0x38, 0xe9, 0x80, 0x05, 0x7a, 0x9f, 0x4e, 0xfc, 0xb6, 0xb1, 0xf3, 0x34, 0x60, 0x73, 0x06, 0xf8, 0x6d, 0x14, 0xfa, 0x03 },
	};
	struct GBALinkV2DeterminismProfileInput input = _v2Profile();
	struct GBALinkV2DeterminismProfile profile;
	memset(&profile, 0xA5, sizeof(profile));
	assert_true(GBALinkV2DeterminismProfileBuild(&input, &profile));
	assert_int_equal(profile.schemaVersion,
	    GBA_LINK_V2_PROFILE_SCHEMA_VERSION);
	assert_int_equal(profile.recordCount,
	    GBA_LINK_V2_PROFILE_REQUIRED_RECORDS);
	for (unsigned i = 0; i < profile.recordCount; ++i) {
		assert_int_equal(profile.records[i].category, i + 1);
		assert_int_equal(profile.records[i].flags,
		    GBA_LINK_V2_PROFILE_RECORD_REQUIRED);
		assert_memory_equal(profile.records[i].digest, expected[i],
		    MGBA_SHA256_DIGEST_SIZE);
	}
	assert_true(GBALinkV2DeterminismProfileValidate(&profile));
}

M_TEST_DEFINE(v2ProfileValidationAndCompatibilityAreBounded) {
	struct GBALinkV2DeterminismProfileInput input = _v2Profile();
	struct GBALinkV2DeterminismProfile local;
	struct GBALinkV2DeterminismProfile remote;
	assert_true(GBALinkV2DeterminismProfileBuild(&input, &local));
	assert_true(GBALinkV2DeterminismProfileBuild(&input, &remote));
	uint16_t mismatch = UINT16_MAX;
	assert_true(GBALinkV2DeterminismProfilesCompatible(
	    &local, &remote, &mismatch));
	assert_int_equal(mismatch, 0);

	remote.recordCount = GBA_LINK_V2_PROFILE_MAX_RECORDS;
	for (unsigned i = GBA_LINK_V2_PROFILE_REQUIRED_RECORDS;
	     i < remote.recordCount; ++i) {
		remote.records[i].category = 100 + i;
		remote.records[i].flags = 0;
		memset(remote.records[i].digest, i,
		    sizeof(remote.records[i].digest));
	}
	assert_true(GBALinkV2DeterminismProfilesCompatible(
	    &local, &remote, &mismatch));

	remote.records[0].digest[0] ^= 1;
	assert_false(GBALinkV2DeterminismProfilesCompatible(
	    &local, &remote, &mismatch));
	assert_int_equal(mismatch, GBA_LINK_V2_PROFILE_BIOS);
	remote.records[0].digest[0] ^= 1;

	remote.records[7].flags = GBA_LINK_V2_PROFILE_RECORD_REQUIRED;
	assert_false(GBALinkV2DeterminismProfileValidate(&remote));
	remote.records[7].flags = 0;
	remote.records[7].category = remote.records[6].category;
	assert_false(GBALinkV2DeterminismProfileValidate(&remote));
	remote.recordCount = GBA_LINK_V2_PROFILE_MAX_RECORDS + 1;
	assert_false(GBALinkV2DeterminismProfileValidate(&remote));
}

M_TEST_DEFINE(v2ProfileRejectsNoncanonicalInputs) {
	struct GBALinkV2DeterminismProfileInput input = _v2Profile();
	struct GBALinkV2DeterminismProfile output;
	input.biosMode = GBA_LINK_V2_BIOS_HLE;
	input.biosSha256[0] = 1;
	assert_false(GBALinkV2DeterminismProfileBuild(&input, &output));
	input = _v2Profile();
	input.timingModelFlags = 4;
	assert_false(GBALinkV2DeterminismProfileBuild(&input, &output));
	input = _v2Profile();
	input.speedHackMask = 1;
	assert_false(GBALinkV2DeterminismProfileBuild(&input, &output));
	input = _v2Profile();
	input.cartridgeRequiredInputMask = UINT64_C(1) << 63;
	assert_false(GBALinkV2DeterminismProfileBuild(&input, &output));
	input = _v2Profile();
	input.cheatsEnabled = true;
	assert_false(GBALinkV2DeterminismProfileBuild(&input, &output));
}

M_TEST_DEFINE(v2ProfileWireEncodingIgnoresNativePadding) {
	struct GBALinkV2DeterminismProfileInput input = _v2Profile();
	struct GBALinkV2DeterminismProfile clean;
	struct GBALinkV2DeterminismProfile poisoned;
	memset(&clean, 0, sizeof(clean));
	memset(&poisoned, 0xA5, sizeof(poisoned));
	assert_true(GBALinkV2DeterminismProfileBuild(&input, &clean));
	assert_true(GBALinkV2DeterminismProfileBuild(&input, &poisoned));

	uint8_t cleanBytes[GBA_LINK_V2_PROFILE_MAX_ENCODED_SIZE];
	uint8_t poisonedBytes[GBA_LINK_V2_PROFILE_MAX_ENCODED_SIZE];
	size_t cleanSize = 0;
	size_t poisonedSize = 0;
	assert_true(GBALinkV2DeterminismProfileEncode(
	    &clean, cleanBytes, sizeof(cleanBytes), &cleanSize));
	assert_true(GBALinkV2DeterminismProfileEncode(
	    &poisoned, poisonedBytes, sizeof(poisonedBytes), &poisonedSize));
	assert_int_equal(cleanSize, 256);
	assert_int_equal(cleanSize, poisonedSize);
	assert_memory_equal(cleanBytes, poisonedBytes, cleanSize);
	assert_int_equal(cleanBytes[0], 1);
	assert_int_equal(cleanBytes[2], 7);
	assert_int_equal(cleanBytes[4], 1);
	assert_int_equal(cleanBytes[6], 1);

	struct GBALinkV2DeterminismProfile decoded;
	memset(&decoded, 0x5A, sizeof(decoded));
	assert_true(GBALinkV2DeterminismProfileDecode(
	    cleanBytes, cleanSize, &decoded));
	assert_true(GBALinkV2DeterminismProfilesCompatible(
	    &clean, &decoded, NULL));
	assert_false(GBALinkV2DeterminismProfileDecode(
	    cleanBytes, cleanSize - 1, &decoded));
	cleanBytes[2] = GBA_LINK_V2_PROFILE_MAX_RECORDS + 1;
	assert_false(GBALinkV2DeterminismProfileDecode(
	    cleanBytes, cleanSize, &decoded));
}

M_TEST_DEFINE(v2CapabilitySupersetsRemainCompatible) {
	struct GBALinkV2DeterminismCapabilities host = {
		.supportedRtcSourceMask = GBA_LINK_V2_RTC_SOURCE_KNOWN_MASK,
		.timeSemanticsCapabilityMask =
		    GBA_LINK_V2_TIME_SIGNED_64BIT_TIME_T_V1,
		.authoritativePlayerRtcSource = 0,
		.contentRequiresRtc = false,
		.synchronizedInputCapabilityMask =
		    GBA_LINK_V2_INPUT_DIGITAL | GBA_LINK_V2_INPUT_TILT,
	};
	struct GBALinkV2DeterminismCapabilities client = {
		.supportedRtcSourceMask = GBA_LINK_V2_RTC_SOURCE_NO_OVERRIDE |
		                              GBA_LINK_V2_RTC_SOURCE_FIXED,
		.authoritativePlayerRtcSource = 1,
		.contentRequiresRtc = false,
		.synchronizedInputCapabilityMask = GBA_LINK_V2_INPUT_DIGITAL,
	};
	enum GBALinkV2CapabilityMismatch mismatch = UINT16_MAX;
	assert_true(GBALinkV2DeterminismCapabilitiesCompatible(
	    &host, &client, GBA_LINK_V2_INPUT_DIGITAL, &mismatch));
	assert_int_equal(mismatch, GBA_LINK_V2_CAPABILITY_MATCH);

	host.contentRequiresRtc = true;
	client.contentRequiresRtc = true;
	assert_false(GBALinkV2DeterminismCapabilitiesCompatible(
	    &host, &client, GBA_LINK_V2_INPUT_DIGITAL, &mismatch));
	assert_int_equal(mismatch,
	    GBA_LINK_V2_CAPABILITY_RTC_TIME_SEMANTICS);
	client.timeSemanticsCapabilityMask =
	    GBA_LINK_V2_TIME_SIGNED_64BIT_TIME_T_V1;
	assert_true(GBALinkV2DeterminismCapabilitiesCompatible(
	    &host, &client, GBA_LINK_V2_INPUT_DIGITAL, &mismatch));

	assert_false(GBALinkV2DeterminismCapabilitiesCompatible(
	    &host, &client,
	    GBA_LINK_V2_INPUT_DIGITAL | GBA_LINK_V2_INPUT_TILT, &mismatch));
	assert_int_equal(mismatch, GBA_LINK_V2_CAPABILITY_EXTERNAL_INPUT);
	client.supportedRtcSourceMask = GBA_LINK_V2_RTC_SOURCE_NO_OVERRIDE;
	assert_false(GBALinkV2DeterminismCapabilitiesCompatible(
	    &host, &client, GBA_LINK_V2_INPUT_DIGITAL, &mismatch));
	assert_int_equal(mismatch, GBA_LINK_V2_CAPABILITY_PROFILE);
}

M_TEST_SUITE_DEFINE(GBALinkIdentity,
	cmocka_unit_test(memoryLoadedContentIdentity),
	cmocka_unit_test(fileBackedContentIdentity),
	cmocka_unit_test(extractedMemoryMatchesFileBackedIdentity),
	cmocka_unit_test(frontendPatchedBytesDefineIdentity),
	cmocka_unit_test(v2ProfileHasCanonicalGoldenDigests),
	cmocka_unit_test(v2ProfileValidationAndCompatibilityAreBounded),
	cmocka_unit_test(v2ProfileRejectsNoncanonicalInputs),
	cmocka_unit_test(v2ProfileWireEncodingIgnoresNativePadding),
	cmocka_unit_test(v2CapabilitySupersetsRemainCompatible))
