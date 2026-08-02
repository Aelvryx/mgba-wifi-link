/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef GBA_SIO_NETPLAY_IDENTITY_V1_H
#define GBA_SIO_NETPLAY_IDENTITY_V1_H

#include <mgba/internal/gba/sio/netplay/identity.h>
#include <mgba/internal/gba/sio/netplay/protocol.h>

CXX_GUARD_START

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

bool GBALinkDeterminismProfileBuild(
    const struct GBALinkDeterminismProfileInput* input,
    struct GBALinkDeterminismDigest digests[GBA_LINK_MAX_DETERMINISM_DIGESTS]);

CXX_GUARD_END

#endif
