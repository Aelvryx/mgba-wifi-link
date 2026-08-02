/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "util/test/suite.h"

#include <mgba/gba/interface.h>
#include <mgba/internal/gba/sio/netplay/rtc-sync.h>

enum {
	TEST_FRAME_CYCLES = 280896,
	TEST_FREQUENCY = 16777216,
};

struct TestRTC {
	struct mRTCGenericSource source;
	time_t sampled;
	unsigned sampleCalls;
};

static void _sample(struct mRTCSource* source) {
	++((struct TestRTC*) source)->sampleCalls;
}

static time_t _unixTime(struct mRTCSource* source) {
	return ((struct TestRTC*) source)->sampled;
}

static struct TestRTC _rtc(enum mRTCGenericType type, int64_t value) {
	struct TestRTC rtc;
	memset(&rtc, 0, sizeof(rtc));
	rtc.source.d.sample = _sample;
	rtc.source.d.unixTime = _unixTime;
	rtc.source.override = type;
	rtc.source.value = value;
	return rtc;
}

M_TEST_DEFINE(platformAndHardwareCapabilitiesAreCanonical) {
	assert_true(GBALinkV2HasSigned64BitTimeT());
	assert_int_equal(GBALinkV2SupportedRTCSourceMask(),
	    GBA_LINK_V2_RTC_SOURCE_KNOWN_MASK);
	assert_int_equal(GBALinkV2RequiredInputMaskForHardware(0, false),
	    GBA_LINK_V2_INPUT_DIGITAL);
	assert_int_equal(GBALinkV2RequiredInputMaskForHardware(
	    HW_TILT | HW_GYRO | HW_LIGHT_SENSOR | HW_RUMBLE, false),
	    GBA_LINK_V2_INPUT_DIGITAL | GBA_LINK_V2_INPUT_TILT |
	        GBA_LINK_V2_INPUT_GYROSCOPE | GBA_LINK_V2_INPUT_LUMINANCE);
	assert_int_equal(GBALinkV2RequiredInputMaskForHardware(HW_RUMBLE, true),
	    GBA_LINK_V2_INPUT_DIGITAL | GBA_LINK_V2_INPUT_LUMINANCE);
	assert_int_equal(GBALinkV2RequiredInputMaskForHardware(HW_TILT, false),
	    GBA_LINK_V2_INPUT_DIGITAL | GBA_LINK_V2_INPUT_TILT);
	assert_int_equal(GBALinkV2RequiredInputMaskForHardware(HW_GYRO, false),
	    GBA_LINK_V2_INPUT_DIGITAL | GBA_LINK_V2_INPUT_GYROSCOPE);
	assert_int_equal(GBALinkV2RequiredInputMaskForHardware(
	    HW_LIGHT_SENSOR, false),
	    GBA_LINK_V2_INPUT_DIGITAL | GBA_LINK_V2_INPUT_LUMINANCE);
	assert_int_equal(GBALinkV2RequiredInputMaskForHardware(HW_RUMBLE, false),
	    GBA_LINK_V2_INPUT_DIGITAL);
}

M_TEST_DEFINE(wallClockSourcesNormalizeAndRestoreTransactionally) {
	struct TestRTC rtc = _rtc(RTC_NO_OVERRIDE, 77);
	rtc.sampled = -123;
	struct GBALinkV2RTCNormalization normalization;
	assert_int_equal(GBALinkV2RTCNormalize(
	    &rtc.source, 100, TEST_FRAME_CYCLES, TEST_FREQUENCY,
	    &normalization), GBA_LINK_V2_RTC_OK);
	assert_int_equal(rtc.sampleCalls, 1);
	assert_int_equal(normalization.originalType, RTC_NO_OVERRIDE);
	assert_int_equal(normalization.originalValue, 77);
	assert_int_equal(normalization.normalizedType, RTC_FAKE_EPOCH);
	assert_int_equal(normalization.sampledUnixSeconds, -123);
	assert_true(GBALinkV2RTCApplyNormalized(&rtc.source, &normalization));
	assert_int_equal(rtc.source.override, RTC_FAKE_EPOCH);
	assert_int_equal((rtc.source.value +
	    (int64_t) 100 * TEST_FRAME_CYCLES * 1000 / TEST_FREQUENCY) / 1000,
	    -123);
	assert_true(GBALinkV2RTCRestoreOriginal(&rtc.source, &normalization));
	assert_int_equal(rtc.source.override, RTC_NO_OVERRIDE);
	assert_int_equal(rtc.source.value, 77);

	rtc = _rtc(RTC_WALLCLOCK_OFFSET, 5000);
	rtc.sampled = 456;
	assert_int_equal(GBALinkV2RTCNormalize(
	    &rtc.source, 0, TEST_FRAME_CYCLES, TEST_FREQUENCY,
	    &normalization), GBA_LINK_V2_RTC_OK);
	assert_int_equal(normalization.normalizedValue, 456000);
}

M_TEST_DEFINE(fixedAndFakeEpochRemainCanonical) {
	struct TestRTC rtc = _rtc(RTC_FIXED, -123456);
	struct GBALinkV2RTCNormalization normalization;
	assert_int_equal(GBALinkV2RTCNormalize(
	    &rtc.source, 999, TEST_FRAME_CYCLES, TEST_FREQUENCY,
	    &normalization), GBA_LINK_V2_RTC_OK);
	assert_int_equal(normalization.normalizedType, RTC_FIXED);
	assert_int_equal(normalization.normalizedValue, -123456);
	assert_int_equal(normalization.sampledUnixSeconds, -123);
	assert_int_equal(rtc.sampleCalls, 0);

	rtc = _rtc(RTC_FAKE_EPOCH, -9000);
	assert_int_equal(GBALinkV2RTCNormalize(
	    &rtc.source, 60, TEST_FRAME_CYCLES, TEST_FREQUENCY,
	    &normalization), GBA_LINK_V2_RTC_OK);
	assert_int_equal(normalization.normalizedType, RTC_FAKE_EPOCH);
	assert_int_equal(normalization.normalizedValue, -9000);
}

M_TEST_DEFINE(unsupportedAndOverflowingSourcesFailClosed) {
	struct TestRTC rtc = _rtc(RTC_CUSTOM_START, 0);
	struct GBALinkV2RTCNormalization normalization;
	assert_int_equal(GBALinkV2RTCNormalize(
	    &rtc.source, 0, TEST_FRAME_CYCLES, TEST_FREQUENCY,
	    &normalization), GBA_LINK_V2_RTC_UNSUPPORTED_SOURCE);
	rtc = _rtc(RTC_FAKE_EPOCH, INT64_MAX - 1);
	assert_int_equal(GBALinkV2RTCNormalize(
	    &rtc.source, 1, TEST_FRAME_CYCLES, TEST_FREQUENCY,
	    &normalization), GBA_LINK_V2_RTC_ARITHMETIC);
	rtc = _rtc(RTC_NO_OVERRIDE, 0);
	assert_int_equal(GBALinkV2RTCNormalize(
	    &rtc.source, 0, 0, TEST_FREQUENCY, &normalization),
	    GBA_LINK_V2_RTC_INVALID_ARGUMENT);
}

M_TEST_SUITE_DEFINE(GBALinkRTCSync,
	cmocka_unit_test(platformAndHardwareCapabilitiesAreCanonical),
	cmocka_unit_test(wallClockSourcesNormalizeAndRestoreTransactionally),
	cmocka_unit_test(fixedAndFakeEpochRemainCanonical),
	cmocka_unit_test(unsupportedAndOverflowingSourcesFailClosed))
