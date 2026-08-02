/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/gba/sio/netplay/identity-v1.h>

#include <mgba/core/core.h>
#include <mgba-util/sha1.h>
#include <mgba-util/sha256.h>

const char* GBALinkV2DeterminismCategoryName(uint16_t category) {
	switch (category) {
	case GBA_LINK_V2_PROFILE_BIOS: return "bios";
	case GBA_LINK_V2_PROFILE_CPU_TIMING: return "cpu-timing";
	case GBA_LINK_V2_PROFILE_IDLE_OPTIMIZATION: return "idle-optimization";
	case GBA_LINK_V2_PROFILE_INPUT_POLICY: return "input-policy";
	case GBA_LINK_V2_PROFILE_RTC_POLICY: return "rtc-policy";
	case GBA_LINK_V2_PROFILE_CHEATS: return "cheats";
	case GBA_LINK_V2_PROFILE_EXTERNAL_INPUT: return "external-input";
	default: return "unknown";
	}
}

const char* GBALinkV2CapabilityMismatchName(
		enum GBALinkV2CapabilityMismatch mismatch) {
	switch (mismatch) {
	case GBA_LINK_V2_CAPABILITY_MATCH: return "match";
	case GBA_LINK_V2_CAPABILITY_PROFILE: return "profile";
	case GBA_LINK_V2_CAPABILITY_RTC_CONTENT: return "rtc-content";
	case GBA_LINK_V2_CAPABILITY_RTC_TIME_SEMANTICS:
		return "rtc-time-semantics";
	case GBA_LINK_V2_CAPABILITY_RTC_SOURCE: return "rtc-source";
	case GBA_LINK_V2_CAPABILITY_EXTERNAL_INPUT: return "external-input";
	}
	return "unknown";
}

static void _update32(struct SHA1Context* context, uint32_t value) {
	const uint8_t bytes[] = {
		value,
		value >> 8,
		value >> 16,
		value >> 24,
	};
	sha1Update(context, bytes, sizeof(bytes));
}

static void _digestBegin(
    struct SHA1Context* context, const char* domain) {
	sha1Init(context);
	sha1Update(context, GBA_LINK_PROTOCOL_NAME, sizeof(GBA_LINK_PROTOCOL_NAME) - 1);
	const uint8_t separator = 0;
	sha1Update(context, &separator, sizeof(separator));
	sha1Update(context, domain, strlen(domain));
	sha1Update(context, &separator, sizeof(separator));
}

static void _digestEnd(
    struct SHA1Context* context,
    struct GBALinkDeterminismDigest* digest,
    enum GBALinkDeterminismCategory category) {
	digest->category = category;
	sha1Finalize(digest->digest, context);
}

bool GBALinkContentIdentityFromCore(
    const struct mCore* core, struct GBALinkContentIdentity* identity) {
	if (!core || !identity || !core->romSize || !core->checksum ||
	    !core->platform || core->platform(core) != mPLATFORM_GBA) {
		return false;
	}
	size_t romSize = core->romSize(core);
	if (!romSize) {
		return false;
	}
	struct GBALinkContentIdentity result = {
		.romSize = romSize,
	};
	core->checksum(core, result.romSha1, mCHECKSUM_SHA1);
	*identity = result;
	return true;
}

bool GBALinkContentIdentityEqual(
    const struct GBALinkContentIdentity* left,
    const struct GBALinkContentIdentity* right) {
	return left && right && left->romSize == right->romSize &&
	       !memcmp(left->romSha1, right->romSha1, sizeof(left->romSha1));
}

bool GBALinkDeterminismProfileBuild(
    const struct GBALinkDeterminismProfileInput* input,
    struct GBALinkDeterminismDigest digests[GBA_LINK_MAX_DETERMINISM_DIGESTS]) {
	if (!input || !digests ||
	    input->idleOptimization > GBA_LINK_IDLE_OPTIMIZATION_DETECT ||
	    input->rtcOverrideMode > GBA_LINK_RTC_OVERRIDE_WALL_CLOCK) {
		return false;
	}

	memset(digests, 0, sizeof(*digests) * GBA_LINK_MAX_DETERMINISM_DIGESTS);
	struct SHA1Context context;
	uint8_t flag;

	_digestBegin(&context, "bios-v1");
	flag = input->useBios;
	sha1Update(&context, &flag, sizeof(flag));
	if (input->useBios) {
		sha1Update(&context, input->biosSha1, sizeof(input->biosSha1));
	}
	_digestEnd(&context, &digests[0], GBA_LINK_DETERMINISM_BIOS);

	_digestBegin(&context, "cpu-timing-v1");
	_update32(&context, input->timingModel);
	_update32(&context, input->overclockQ16);
	_update32(&context, input->speedHackFlags);
	_digestEnd(&context, &digests[1], GBA_LINK_DETERMINISM_CPU_TIMING);

	_digestBegin(&context, "idle-optimization-v1");
	flag = input->idleOptimization;
	sha1Update(&context, &flag, sizeof(flag));
	_digestEnd(&context, &digests[2], GBA_LINK_DETERMINISM_IDLE_OPTIMIZATION);

	_digestBegin(&context, "rtc-override-v1");
	flag = input->rtcOverrideMode;
	sha1Update(&context, &flag, sizeof(flag));
	_digestEnd(&context, &digests[3], GBA_LINK_DETERMINISM_RTC_OVERRIDE);

	_digestBegin(&context, "cheats-v1");
	flag = input->cheatsEnabled;
	sha1Update(&context, &flag, sizeof(flag));
	_digestEnd(&context, &digests[4], GBA_LINK_DETERMINISM_CHEATS);
	return true;
}

static void _sha256Update32(
	struct SHA256Context* context, uint32_t value) {
	const uint8_t encoded[4] = {
		value, value >> 8, value >> 16, value >> 24,
	};
	sha256Update(context, encoded, sizeof(encoded));
}

static void _sha256Update64(
	struct SHA256Context* context, uint64_t value) {
	_sha256Update32(context, value);
	_sha256Update32(context, value >> 32);
}

static void _v2DigestBegin(
	struct SHA256Context* context, const char* suffix) {
	static const char schema[] = "determinism-profile-v1";
	static const uint8_t zero = 0;
	sha256Init(context);
	sha256Update(context, GBA_LINK_V2_PROFILE_DOMAIN,
	    sizeof(GBA_LINK_V2_PROFILE_DOMAIN) - 1);
	sha256Update(context, &zero, 1);
	sha256Update(context, schema, sizeof(schema) - 1);
	sha256Update(context, &zero, 1);
	sha256Update(context, suffix, strlen(suffix));
	sha256Update(context, &zero, 1);
}

static void _v2DigestEnd(
	struct SHA256Context* context,
	struct GBALinkV2ProfileRecord* record, uint16_t category) {
	record->category = category;
	record->flags = GBA_LINK_V2_PROFILE_RECORD_REQUIRED;
	sha256Finalize(record->digest, context);
}

bool GBALinkV2DeterminismProfileBuild(
	const struct GBALinkV2DeterminismProfileInput* input,
	struct GBALinkV2DeterminismProfile* profile) {
	if (!input || !profile ||
	    input->biosMode > GBA_LINK_V2_BIOS_EXTERNAL ||
	    (input->biosMode == GBA_LINK_V2_BIOS_HLE &&
	     memcmp(input->biosSha256,
	         (const uint8_t[MGBA_SHA256_DIGEST_SIZE]) { 0 },
	         MGBA_SHA256_DIGEST_SIZE)) ||
	    !input->emulationCompatibilityVersion ||
	    (input->timingModelFlags & ~UINT32_C(3)) ||
	    !input->overclockQ16 || input->speedHackMask ||
	    input->idlePolicy > GBA_LINK_V2_IDLE_DETECT ||
	    input->rtcNormalizationPolicyVersion != 1 ||
	    input->fakeEpochArithmeticVersion != 1 ||
	    input->rtcSemanticsModelVersion != 1 ||
	    input->cheatsEnabled ||
	    input->authoritativeInputFormatVersion != 1 ||
	    !input->cartridgeRequiredInputMask ||
	    (input->cartridgeRequiredInputMask &
	     ~(uint64_t) GBA_LINK_V2_KNOWN_EXTERNAL_INPUTS)) {
		return false;
	}

	memset(profile, 0, sizeof(*profile));
	profile->schemaVersion = GBA_LINK_V2_PROFILE_SCHEMA_VERSION;
	profile->recordCount = GBA_LINK_V2_PROFILE_REQUIRED_RECORDS;
	struct SHA256Context context;
	uint8_t reserved[3] = { 0 };

	_v2DigestBegin(&context, "bios-v2");
	uint8_t value8 = input->biosMode;
	sha256Update(&context, &value8, 1);
	sha256Update(&context, reserved, sizeof(reserved));
	sha256Update(&context, input->biosSha256,
	    sizeof(input->biosSha256));
	_v2DigestEnd(&context, &profile->records[0],
	    GBA_LINK_V2_PROFILE_BIOS);

	_v2DigestBegin(&context, "cpu-timing-v2");
	_sha256Update32(&context, input->emulationCompatibilityVersion);
	_sha256Update32(&context, input->timingModelFlags);
	_sha256Update32(&context, input->overclockQ16);
	_sha256Update32(&context, input->speedHackMask);
	_v2DigestEnd(&context, &profile->records[1],
	    GBA_LINK_V2_PROFILE_CPU_TIMING);

	_v2DigestBegin(&context, "idle-optimization-v2");
	value8 = input->idlePolicy;
	sha256Update(&context, &value8, 1);
	sha256Update(&context, reserved, sizeof(reserved));
	_v2DigestEnd(&context, &profile->records[2],
	    GBA_LINK_V2_PROFILE_IDLE_OPTIMIZATION);

	_v2DigestBegin(&context, "input-policy-v2");
	value8 = input->allowOpposingDirections;
	sha256Update(&context, &value8, 1);
	sha256Update(&context, reserved, sizeof(reserved));
	_v2DigestEnd(&context, &profile->records[3],
	    GBA_LINK_V2_PROFILE_INPUT_POLICY);

	_v2DigestBegin(&context, "rtc-normalization-v2");
	_sha256Update32(&context, input->rtcNormalizationPolicyVersion);
	_sha256Update32(&context, input->fakeEpochArithmeticVersion);
	_sha256Update32(&context, input->rtcSemanticsModelVersion);
	_sha256Update32(&context, 0);
	_v2DigestEnd(&context, &profile->records[4],
	    GBA_LINK_V2_PROFILE_RTC_POLICY);

	_v2DigestBegin(&context, "cheats-v2");
	value8 = input->cheatsEnabled;
	sha256Update(&context, &value8, 1);
	sha256Update(&context, reserved, sizeof(reserved));
	_v2DigestEnd(&context, &profile->records[5],
	    GBA_LINK_V2_PROFILE_CHEATS);

	_v2DigestBegin(&context, "external-input-v2");
	_sha256Update32(&context, input->authoritativeInputFormatVersion);
	_sha256Update32(&context, 0);
	_sha256Update64(&context, input->cartridgeRequiredInputMask);
	_v2DigestEnd(&context, &profile->records[6],
	    GBA_LINK_V2_PROFILE_EXTERNAL_INPUT);
	return true;
}

bool GBALinkV2DeterminismProfileValidate(
	const struct GBALinkV2DeterminismProfile* profile) {
	if (!profile ||
	    profile->schemaVersion != GBA_LINK_V2_PROFILE_SCHEMA_VERSION ||
	    !profile->recordCount ||
	    profile->recordCount > GBA_LINK_V2_PROFILE_MAX_RECORDS) {
		return false;
	}
	uint16_t previous = 0;
	unsigned requiredKnown = 0;
	for (unsigned i = 0; i < profile->recordCount; ++i) {
		const struct GBALinkV2ProfileRecord* record = &profile->records[i];
		if (!record->category || record->category <= previous ||
		    (record->flags & ~GBA_LINK_V2_PROFILE_RECORD_KNOWN_FLAGS)) {
			return false;
		}
		previous = record->category;
		if (record->category <= GBA_LINK_V2_PROFILE_REQUIRED_RECORDS) {
			if (!(record->flags & GBA_LINK_V2_PROFILE_RECORD_REQUIRED) ||
			    record->category != requiredKnown + 1) {
				return false;
			}
			++requiredKnown;
		} else if (record->flags & GBA_LINK_V2_PROFILE_RECORD_REQUIRED) {
			return false;
		}
	}
	return requiredKnown == GBA_LINK_V2_PROFILE_REQUIRED_RECORDS;
}

bool GBALinkV2DeterminismProfilesCompatible(
	const struct GBALinkV2DeterminismProfile* local,
	const struct GBALinkV2DeterminismProfile* remote,
	uint16_t* mismatchCategory) {
	if (mismatchCategory) {
		*mismatchCategory = 0;
	}
	if (!GBALinkV2DeterminismProfileValidate(local) ||
	    !GBALinkV2DeterminismProfileValidate(remote)) {
		return false;
	}
	for (unsigned category = 1;
	     category <= GBA_LINK_V2_PROFILE_REQUIRED_RECORDS; ++category) {
		const struct GBALinkV2ProfileRecord* left =
		    &local->records[category - 1];
		const struct GBALinkV2ProfileRecord* right =
		    &remote->records[category - 1];
		if (left->category != category || right->category != category ||
		    memcmp(left->digest, right->digest, sizeof(left->digest))) {
			if (mismatchCategory) {
				*mismatchCategory = category;
			}
			return false;
		}
	}
	return true;
}

size_t GBALinkV2DeterminismProfileEncodedSize(
	const struct GBALinkV2DeterminismProfile* profile) {
	if (!GBALinkV2DeterminismProfileValidate(profile)) {
		return 0;
	}
	return GBA_LINK_V2_PROFILE_HEADER_SIZE +
	       (size_t) profile->recordCount * GBA_LINK_V2_PROFILE_RECORD_SIZE;
}

bool GBALinkV2DeterminismProfileEncode(
	const struct GBALinkV2DeterminismProfile* profile,
	void* data, size_t capacity, size_t* encodedSize) {
	if (encodedSize) {
		*encodedSize = 0;
	}
	size_t size = GBALinkV2DeterminismProfileEncodedSize(profile);
	if (!size || !data || capacity < size) {
		return false;
	}
	uint8_t* output = data;
	output[0] = profile->schemaVersion;
	output[1] = profile->schemaVersion >> 8;
	output[2] = profile->recordCount;
	output[3] = profile->recordCount >> 8;
	size_t offset = GBA_LINK_V2_PROFILE_HEADER_SIZE;
	for (unsigned i = 0; i < profile->recordCount; ++i) {
		const struct GBALinkV2ProfileRecord* record = &profile->records[i];
		output[offset++] = record->category;
		output[offset++] = record->category >> 8;
		output[offset++] = record->flags;
		output[offset++] = record->flags >> 8;
		memcpy(&output[offset], record->digest, sizeof(record->digest));
		offset += sizeof(record->digest);
	}
	if (encodedSize) {
		*encodedSize = offset;
	}
	return true;
}

bool GBALinkV2DeterminismProfileDecode(
	const void* data, size_t size,
	struct GBALinkV2DeterminismProfile* profile) {
	if (!data || !profile || size < GBA_LINK_V2_PROFILE_HEADER_SIZE) {
		return false;
	}
	const uint8_t* input = data;
	uint16_t count = input[2] | (uint16_t) input[3] << 8;
	if (count > GBA_LINK_V2_PROFILE_MAX_RECORDS ||
	    size != GBA_LINK_V2_PROFILE_HEADER_SIZE +
	            (size_t) count * GBA_LINK_V2_PROFILE_RECORD_SIZE) {
		return false;
	}
	struct GBALinkV2DeterminismProfile decoded = {
		.schemaVersion = input[0] | (uint16_t) input[1] << 8,
		.recordCount = count,
	};
	size_t offset = GBA_LINK_V2_PROFILE_HEADER_SIZE;
	for (unsigned i = 0; i < count; ++i) {
		struct GBALinkV2ProfileRecord* record = &decoded.records[i];
		record->category = input[offset] |
		                   (uint16_t) input[offset + 1] << 8;
		record->flags = input[offset + 2] |
		                (uint16_t) input[offset + 3] << 8;
		offset += 4;
		memcpy(record->digest, &input[offset], sizeof(record->digest));
		offset += sizeof(record->digest);
	}
	if (!GBALinkV2DeterminismProfileValidate(&decoded)) {
		return false;
	}
	*profile = decoded;
	return true;
}

bool GBALinkV2DeterminismCapabilitiesValidate(
	const struct GBALinkV2DeterminismCapabilities* capabilities) {
	return capabilities && capabilities->supportedRtcSourceMask &&
	       !(capabilities->supportedRtcSourceMask &
	         ~GBA_LINK_V2_RTC_SOURCE_KNOWN_MASK) &&
	       !(capabilities->timeSemanticsCapabilityMask &
	         ~GBA_LINK_V2_TIME_SIGNED_64BIT_TIME_T_V1) &&
	       capabilities->authoritativePlayerRtcSource < 4 &&
	       (capabilities->supportedRtcSourceMask &
	        (UINT32_C(1) << capabilities->authoritativePlayerRtcSource)) &&
	       capabilities->synchronizedInputCapabilityMask &&
	       !(capabilities->synchronizedInputCapabilityMask &
	         ~GBA_LINK_V2_KNOWN_EXTERNAL_INPUTS);
}

bool GBALinkV2DeterminismCapabilitiesCompatible(
	const struct GBALinkV2DeterminismCapabilities* host,
	const struct GBALinkV2DeterminismCapabilities* client,
	uint64_t cartridgeRequiredInputMask,
	enum GBALinkV2CapabilityMismatch* mismatch) {
	if (mismatch) {
		*mismatch = GBA_LINK_V2_CAPABILITY_MATCH;
	}
	if (!GBALinkV2DeterminismCapabilitiesValidate(host) ||
	    !GBALinkV2DeterminismCapabilitiesValidate(client) ||
	    !cartridgeRequiredInputMask ||
	    (cartridgeRequiredInputMask & ~GBA_LINK_V2_KNOWN_EXTERNAL_INPUTS)) {
		if (mismatch) {
			*mismatch = GBA_LINK_V2_CAPABILITY_PROFILE;
		}
		return false;
	}
	if (host->contentRequiresRtc != client->contentRequiresRtc) {
		if (mismatch) {
			*mismatch = GBA_LINK_V2_CAPABILITY_RTC_CONTENT;
		}
		return false;
	}
	if (host->contentRequiresRtc &&
	    (!(host->timeSemanticsCapabilityMask &
	       GBA_LINK_V2_TIME_SIGNED_64BIT_TIME_T_V1) ||
	     !(client->timeSemanticsCapabilityMask &
	       GBA_LINK_V2_TIME_SIGNED_64BIT_TIME_T_V1))) {
		if (mismatch) {
			*mismatch = GBA_LINK_V2_CAPABILITY_RTC_TIME_SEMANTICS;
		}
		return false;
	}
	uint32_t sharedRtcSources = host->supportedRtcSourceMask &
	                            client->supportedRtcSourceMask;
	if (!(sharedRtcSources &
	      (UINT32_C(1) << host->authoritativePlayerRtcSource)) ||
	    !(sharedRtcSources &
	      (UINT32_C(1) << client->authoritativePlayerRtcSource))) {
		if (mismatch) {
			*mismatch = GBA_LINK_V2_CAPABILITY_RTC_SOURCE;
		}
		return false;
	}
	if ((cartridgeRequiredInputMask &
	     ~host->synchronizedInputCapabilityMask) ||
	    (cartridgeRequiredInputMask &
	     ~client->synchronizedInputCapabilityMask)) {
		if (mismatch) {
			*mismatch = GBA_LINK_V2_CAPABILITY_EXTERNAL_INPUT;
		}
		return false;
	}
	return true;
}
