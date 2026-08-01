/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef GBA_SIO_NETPLAY_IDENTITY_H
#define GBA_SIO_NETPLAY_IDENTITY_H

#include <mgba-util/common.h>
#include <mgba-util/sha256.h>

#include <mgba/internal/gba/sio/netplay/protocol.h>

CXX_GUARD_START

struct mCore;

#define GBA_LINK_EMULATION_COMPATIBILITY_VERSION 1

#define GBA_LINK_V2_PROFILE_SCHEMA_VERSION 1
#define GBA_LINK_V2_PROFILE_MAX_RECORDS 16
#define GBA_LINK_V2_PROFILE_REQUIRED_RECORDS 7
#define GBA_LINK_V2_PROFILE_RECORD_REQUIRED 1
#define GBA_LINK_V2_PROFILE_RECORD_KNOWN_FLAGS \
	GBA_LINK_V2_PROFILE_RECORD_REQUIRED
#define GBA_LINK_V2_PROFILE_HEADER_SIZE 4
#define GBA_LINK_V2_PROFILE_RECORD_SIZE 36
#define GBA_LINK_V2_PROFILE_MAX_ENCODED_SIZE \
	(GBA_LINK_V2_PROFILE_HEADER_SIZE + \
	 GBA_LINK_V2_PROFILE_MAX_RECORDS * GBA_LINK_V2_PROFILE_RECORD_SIZE)
#define GBA_LINK_V2_PROFILE_DOMAIN "mgba-gba-link-replicated-v2"

enum GBALinkV2DeterminismCategory {
	GBA_LINK_V2_PROFILE_BIOS = 1,
	GBA_LINK_V2_PROFILE_CPU_TIMING = 2,
	GBA_LINK_V2_PROFILE_IDLE_OPTIMIZATION = 3,
	GBA_LINK_V2_PROFILE_INPUT_POLICY = 4,
	GBA_LINK_V2_PROFILE_RTC_POLICY = 5,
	GBA_LINK_V2_PROFILE_CHEATS = 6,
	GBA_LINK_V2_PROFILE_EXTERNAL_INPUT = 7,
};

enum GBALinkV2BiosMode {
	GBA_LINK_V2_BIOS_HLE = 0,
	GBA_LINK_V2_BIOS_EXTERNAL = 1,
};

enum GBALinkV2IdlePolicy {
	GBA_LINK_V2_IDLE_NONE = 0,
	GBA_LINK_V2_IDLE_REMOVE = 1,
	GBA_LINK_V2_IDLE_DETECT = 2,
};

enum GBALinkV2ExternalInput {
	GBA_LINK_V2_INPUT_DIGITAL = UINT64_C(1) << 0,
	GBA_LINK_V2_INPUT_TILT = UINT64_C(1) << 1,
	GBA_LINK_V2_INPUT_GYROSCOPE = UINT64_C(1) << 2,
	GBA_LINK_V2_INPUT_LUMINANCE = UINT64_C(1) << 3,
	GBA_LINK_V2_INPUT_CAMERA = UINT64_C(1) << 4,
	GBA_LINK_V2_INPUT_MICROPHONE = UINT64_C(1) << 5,
};

enum {
	GBA_LINK_V2_KNOWN_EXTERNAL_INPUTS =
	    GBA_LINK_V2_INPUT_DIGITAL | GBA_LINK_V2_INPUT_TILT |
	    GBA_LINK_V2_INPUT_GYROSCOPE | GBA_LINK_V2_INPUT_LUMINANCE |
	    GBA_LINK_V2_INPUT_CAMERA | GBA_LINK_V2_INPUT_MICROPHONE,
};

enum GBALinkV2RTCSourceCapability {
	GBA_LINK_V2_RTC_SOURCE_NO_OVERRIDE = 1 << 0,
	GBA_LINK_V2_RTC_SOURCE_FIXED = 1 << 1,
	GBA_LINK_V2_RTC_SOURCE_FAKE_EPOCH = 1 << 2,
	GBA_LINK_V2_RTC_SOURCE_WALLCLOCK_OFFSET = 1 << 3,
};

enum {
	GBA_LINK_V2_RTC_SOURCE_KNOWN_MASK =
	    GBA_LINK_V2_RTC_SOURCE_NO_OVERRIDE |
	    GBA_LINK_V2_RTC_SOURCE_FIXED |
	    GBA_LINK_V2_RTC_SOURCE_FAKE_EPOCH |
	    GBA_LINK_V2_RTC_SOURCE_WALLCLOCK_OFFSET,
};

enum GBALinkV2TimeSemanticsCapability {
	GBA_LINK_V2_TIME_SIGNED_64BIT_TIME_T_V1 = 1 << 0,
};

enum GBALinkV2CapabilityMismatch {
	GBA_LINK_V2_CAPABILITY_MATCH = 0,
	GBA_LINK_V2_CAPABILITY_PROFILE,
	GBA_LINK_V2_CAPABILITY_RTC_CONTENT,
	GBA_LINK_V2_CAPABILITY_RTC_TIME_SEMANTICS,
	GBA_LINK_V2_CAPABILITY_RTC_SOURCE,
	GBA_LINK_V2_CAPABILITY_EXTERNAL_INPUT,
};

struct GBALinkV2ProfileRecord {
	uint16_t category;
	uint16_t flags;
	uint8_t digest[MGBA_SHA256_DIGEST_SIZE];
};

struct GBALinkV2DeterminismProfile {
	uint16_t schemaVersion;
	uint16_t recordCount;
	struct GBALinkV2ProfileRecord records[GBA_LINK_V2_PROFILE_MAX_RECORDS];
};

struct GBALinkV2DeterminismProfileInput {
	enum GBALinkV2BiosMode biosMode;
	uint8_t biosSha256[MGBA_SHA256_DIGEST_SIZE];
	uint32_t emulationCompatibilityVersion;
	uint32_t timingModelFlags;
	uint32_t overclockQ16;
	uint32_t speedHackMask;
	enum GBALinkV2IdlePolicy idlePolicy;
	bool allowOpposingDirections;
	uint32_t rtcNormalizationPolicyVersion;
	uint32_t fakeEpochArithmeticVersion;
	uint32_t rtcSemanticsModelVersion;
	bool cheatsEnabled;
	uint32_t authoritativeInputFormatVersion;
	uint64_t cartridgeRequiredInputMask;
};

struct GBALinkV2DeterminismCapabilities {
	uint32_t supportedRtcSourceMask;
	uint32_t timeSemanticsCapabilityMask;
	uint32_t authoritativePlayerRtcSource;
	bool contentRequiresRtc;
	uint64_t synchronizedInputCapabilityMask;
};

struct GBALinkContentIdentity {
	uint64_t romSize;
	uint8_t romSha1[GBA_LINK_ROM_SHA1_SIZE];
};

enum GBALinkIdleOptimization {
	GBA_LINK_IDLE_OPTIMIZATION_NONE,
	GBA_LINK_IDLE_OPTIMIZATION_REMOVE,
	GBA_LINK_IDLE_OPTIMIZATION_DETECT,
};

enum GBALinkRTCOverrideMode {
	GBA_LINK_RTC_OVERRIDE_NONE,
	GBA_LINK_RTC_OVERRIDE_FIXED,
	GBA_LINK_RTC_OVERRIDE_WALL_CLOCK,
};

struct GBALinkDeterminismProfileInput {
	bool useBios;
	uint8_t biosSha1[GBA_LINK_ROM_SHA1_SIZE];
	uint32_t timingModel;
	uint32_t overclockQ16;
	uint32_t speedHackFlags;
	enum GBALinkIdleOptimization idleOptimization;
	enum GBALinkRTCOverrideMode rtcOverrideMode;
	bool cheatsEnabled;
};

bool GBALinkContentIdentityFromCore(
    const struct mCore* core, struct GBALinkContentIdentity* identity);
bool GBALinkContentIdentityEqual(
    const struct GBALinkContentIdentity* left,
    const struct GBALinkContentIdentity* right);
bool GBALinkDeterminismProfileBuild(
    const struct GBALinkDeterminismProfileInput* input,
    struct GBALinkDeterminismDigest digests[GBA_LINK_MAX_DETERMINISM_DIGESTS]);

bool GBALinkV2DeterminismProfileBuild(
	const struct GBALinkV2DeterminismProfileInput* input,
	struct GBALinkV2DeterminismProfile* profile);
bool GBALinkV2DeterminismProfileValidate(
	const struct GBALinkV2DeterminismProfile* profile);
bool GBALinkV2DeterminismProfilesCompatible(
	const struct GBALinkV2DeterminismProfile* local,
	const struct GBALinkV2DeterminismProfile* remote,
	uint16_t* mismatchCategory);
size_t GBALinkV2DeterminismProfileEncodedSize(
	const struct GBALinkV2DeterminismProfile* profile);
bool GBALinkV2DeterminismProfileEncode(
	const struct GBALinkV2DeterminismProfile* profile,
	void* data, size_t capacity, size_t* encodedSize);
bool GBALinkV2DeterminismProfileDecode(
	const void* data, size_t size,
	struct GBALinkV2DeterminismProfile* profile);
bool GBALinkV2DeterminismCapabilitiesValidate(
	const struct GBALinkV2DeterminismCapabilities* capabilities);
bool GBALinkV2DeterminismCapabilitiesCompatible(
	const struct GBALinkV2DeterminismCapabilities* host,
	const struct GBALinkV2DeterminismCapabilities* client,
	uint64_t cartridgeRequiredInputMask,
	enum GBALinkV2CapabilityMismatch* mismatch);

CXX_GUARD_END

#endif
