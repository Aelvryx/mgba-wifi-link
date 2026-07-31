/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/gba/sio/netplay/identity.h>

#include <mgba/core/core.h>
#include <mgba-util/sha1.h>

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
