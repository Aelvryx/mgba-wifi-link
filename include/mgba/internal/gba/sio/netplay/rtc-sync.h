/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef GBA_SIO_NETPLAY_RTC_SYNC_H
#define GBA_SIO_NETPLAY_RTC_SYNC_H

#include <mgba-util/common.h>

#include <mgba/core/interface.h>
#include <mgba/internal/gba/sio/netplay/identity.h>

CXX_GUARD_START

enum GBALinkV2RTCResult {
	GBA_LINK_V2_RTC_OK = 0,
	GBA_LINK_V2_RTC_INVALID_ARGUMENT,
	GBA_LINK_V2_RTC_UNSUPPORTED_SOURCE,
	GBA_LINK_V2_RTC_TIME_SEMANTICS,
	GBA_LINK_V2_RTC_ARITHMETIC,
};

struct GBALinkV2RTCNormalization {
	enum mRTCGenericType originalType;
	int64_t originalValue;
	enum mRTCGenericType normalizedType;
	int64_t normalizedValue;
	int64_t sampledUnixSeconds;
	uint64_t frameCounter;
};

bool GBALinkV2HasSigned64BitTimeT(void);
uint32_t GBALinkV2SupportedRTCSourceMask(void);
uint64_t GBALinkV2RequiredInputMaskForHardware(
	uint32_t hardwareDevices, bool manualSolarControl);
enum GBALinkV2RTCResult GBALinkV2RTCNormalize(
	struct mRTCGenericSource* source, uint64_t frameCounter,
	uint32_t frameCycles, uint32_t frequency,
	struct GBALinkV2RTCNormalization* normalization);
bool GBALinkV2RTCApplyNormalized(
	struct mRTCGenericSource* source,
	const struct GBALinkV2RTCNormalization* normalization);
bool GBALinkV2RTCRestoreOriginal(
	struct mRTCGenericSource* source,
	const struct GBALinkV2RTCNormalization* normalization);

CXX_GUARD_END

#endif
