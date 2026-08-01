/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/gba/sio/netplay/rtc-sync.h>

#include <mgba/gba/interface.h>
#include <mgba/internal/gba/sio/netplay/identity.h>

bool GBALinkV2HasSigned64BitTimeT(void) {
	return sizeof(time_t) >= sizeof(int64_t) && (time_t) -1 < (time_t) 0;
}

uint32_t GBALinkV2SupportedRTCSourceMask(void) {
	return GBA_LINK_V2_RTC_SOURCE_KNOWN_MASK;
}

uint64_t GBALinkV2RequiredInputMaskForHardware(
	uint32_t hardwareDevices, bool manualSolarControl) {
	uint64_t required = GBA_LINK_V2_INPUT_DIGITAL;
	if (hardwareDevices & HW_TILT) {
		required |= GBA_LINK_V2_INPUT_TILT;
	}
	if (hardwareDevices & HW_GYRO) {
		required |= GBA_LINK_V2_INPUT_GYROSCOPE;
	}
	if ((hardwareDevices & HW_LIGHT_SENSOR) || manualSolarControl) {
		required |= GBA_LINK_V2_INPUT_LUMINANCE;
	}
	return required;
}

static bool _elapsedMs(
	uint64_t frameCounter, uint32_t frameCycles, uint32_t frequency,
	int64_t* elapsed) {
	if (!frameCycles || !frequency || !elapsed) {
		return false;
	}
#if defined(__SIZEOF_INT128__)
	__uint128_t numerator = (__uint128_t) frameCounter * frameCycles * 1000;
	__uint128_t value = numerator / frequency;
	if (value > INT64_MAX) {
		return false;
	}
	*elapsed = value;
	return true;
#else
	if (frameCounter > UINT64_MAX / frameCycles) {
		return false;
	}
	uint64_t cycles = frameCounter * frameCycles;
	uint64_t seconds = cycles / frequency;
	uint64_t remainder = cycles % frequency;
	if (seconds > INT64_MAX / 1000 || remainder > UINT64_MAX / 1000) {
		return false;
	}
	uint64_t value = seconds * 1000 + remainder * 1000 / frequency;
	if (value > INT64_MAX) {
		return false;
	}
	*elapsed = value;
	return true;
#endif
}

static bool _validFullFrameDomain(
	int64_t epochMs, uint32_t frameCycles, uint32_t frequency) {
	int64_t maximumElapsed;
	if (!_elapsedMs(UINT32_MAX, frameCycles, frequency, &maximumElapsed)) {
		return false;
	}
	return epochMs <= INT64_MAX - maximumElapsed;
}

enum GBALinkV2RTCResult GBALinkV2RTCNormalize(
	struct mRTCGenericSource* source, uint64_t frameCounter,
	uint32_t frameCycles, uint32_t frequency,
	struct GBALinkV2RTCNormalization* normalization) {
	if (!source || !normalization || !frameCycles || !frequency) {
		return GBA_LINK_V2_RTC_INVALID_ARGUMENT;
	}
	if (source->override < RTC_NO_OVERRIDE ||
	    source->override > RTC_WALLCLOCK_OFFSET) {
		return GBA_LINK_V2_RTC_UNSUPPORTED_SOURCE;
	}
	if (!GBALinkV2HasSigned64BitTimeT()) {
		return GBA_LINK_V2_RTC_TIME_SEMANTICS;
	}
	struct GBALinkV2RTCNormalization result = {
		.originalType = source->override,
		.originalValue = source->value,
		.normalizedType = source->override,
		.normalizedValue = source->value,
		.frameCounter = frameCounter,
	};
	int64_t elapsed;
	if (!_elapsedMs(frameCounter, frameCycles, frequency, &elapsed)) {
		return GBA_LINK_V2_RTC_ARITHMETIC;
	}
	switch (source->override) {
	case RTC_NO_OVERRIDE:
	case RTC_WALLCLOCK_OFFSET: {
		if (!source->d.unixTime) {
			return GBA_LINK_V2_RTC_UNSUPPORTED_SOURCE;
		}
		if (source->d.sample) {
			source->d.sample(&source->d);
		}
		time_t sampled = source->d.unixTime(&source->d);
		result.sampledUnixSeconds = sampled;
#if defined(__SIZEOF_INT128__)
		__int128 epoch = (__int128) result.sampledUnixSeconds * 1000 - elapsed;
		if (epoch < INT64_MIN || epoch > INT64_MAX) {
			return GBA_LINK_V2_RTC_ARITHMETIC;
		}
		result.normalizedValue = epoch;
#else
		if (result.sampledUnixSeconds > INT64_MAX / 1000 ||
		    result.sampledUnixSeconds < INT64_MIN / 1000) {
			return GBA_LINK_V2_RTC_ARITHMETIC;
		}
		int64_t milliseconds = result.sampledUnixSeconds * 1000;
		if (milliseconds < INT64_MIN + elapsed) {
			return GBA_LINK_V2_RTC_ARITHMETIC;
		}
		result.normalizedValue = milliseconds - elapsed;
#endif
		result.normalizedType = RTC_FAKE_EPOCH;
		break;
	}
	case RTC_FIXED:
		result.sampledUnixSeconds = source->value / 1000;
		break;
	case RTC_FAKE_EPOCH:
		if ((elapsed > 0 && source->value > INT64_MAX - elapsed) ||
		    !source->d.unixTime) {
			return GBA_LINK_V2_RTC_ARITHMETIC;
		}
		result.sampledUnixSeconds =
		    (source->value + elapsed) / 1000;
		break;
	default:
		return GBA_LINK_V2_RTC_UNSUPPORTED_SOURCE;
	}
	if (result.normalizedType == RTC_FAKE_EPOCH &&
	    !_validFullFrameDomain(
	        result.normalizedValue, frameCycles, frequency)) {
		return GBA_LINK_V2_RTC_ARITHMETIC;
	}
	*normalization = result;
	return GBA_LINK_V2_RTC_OK;
}

bool GBALinkV2RTCApplyNormalized(
	struct mRTCGenericSource* source,
	const struct GBALinkV2RTCNormalization* normalization) {
	if (!source || !normalization ||
	    normalization->normalizedType < RTC_NO_OVERRIDE ||
	    normalization->normalizedType > RTC_WALLCLOCK_OFFSET) {
		return false;
	}
	source->override = normalization->normalizedType;
	source->value = normalization->normalizedValue;
	return true;
}

bool GBALinkV2RTCRestoreOriginal(
	struct mRTCGenericSource* source,
	const struct GBALinkV2RTCNormalization* normalization) {
	if (!source || !normalization ||
	    normalization->originalType < RTC_NO_OVERRIDE ||
	    normalization->originalType > RTC_WALLCLOCK_OFFSET) {
		return false;
	}
	source->override = normalization->originalType;
	source->value = normalization->originalValue;
	return true;
}
