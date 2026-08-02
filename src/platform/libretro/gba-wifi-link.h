/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef M_LIBRETRO_GBA_WIFI_LINK_H
#define M_LIBRETRO_GBA_WIFI_LINK_H

#include "libretro.h"

#include <mgba-util/common.h>
#include <mgba-util/image.h>

#define M_LIBRETRO_GBA_WIFI_LINK_PRODUCT_ID "mgba-gba-wifi-link"
#define M_LIBRETRO_GBA_WIFI_LINK_DIAGNOSTIC_SCHEMA 1

struct mLibretroGBAWifiLinkTestMetrics {
	uint16_t selectedDelay;
	uint8_t productPolicy;
	uint64_t releasedFrames;
	uint64_t inputWaitedFrames;
	uint64_t inputWaitP95Us;
	uint64_t inputWaitMaxUs;
	uint64_t inputDeadlineMisses;
	uint64_t telemetryClockFailures;
	uint64_t inputPollSendCount;
	uint64_t inputInsertions[2];
	uint64_t cableTransferStarts;
	uint64_t cableTransferCompletions;
	uint64_t cableTransferredWords;
};

#ifdef M_LIBRETRO_GBA_WIFI_LINK_TEST
#include <mgba/internal/gba/sio/netplay/protocol-v2.h>
#endif

struct mCore;

bool mLibretroGBAWifiLinkRegister(
	retro_environment_t environment, struct mCore* core,
	void* saveData, size_t saveCapacity);
void mLibretroGBAWifiLinkRunBegin(void);
bool mLibretroGBAWifiLinkRunFrame(uint16_t keys);
bool mLibretroGBAWifiLinkExecutionBlocked(void);
bool mLibretroGBAWifiLinkOwnsExecution(void);
struct mCore* mLibretroGBAWifiLinkPresentedCore(void);
mColor* mLibretroGBAWifiLinkPresentedVideo(void);
void mLibretroGBAWifiLinkReportAudio(size_t samples);
void mLibretroGBAWifiLinkReset(void);
void mLibretroGBAWifiLinkUnload(void);
bool mLibretroGBAWifiLinkSessionActive(void);
bool mLibretroGBAWifiLinkRejectOperation(const char* operation);
bool mLibretroGBAWifiLinkRejectLatencyPolicyChange(const char* value);

#ifdef M_LIBRETRO_GBA_WIFI_LINK_TEST
bool mLibretroGBAWifiLinkTestPollReceive(void);
void mLibretroGBAWifiLinkTestSetTimeMs(uint64_t nowMs);
uint64_t mLibretroGBAWifiLinkTestCallbackGeneration(void);
size_t mLibretroGBAWifiLinkTestPendingPacketCount(void);
bool mLibretroGBAWifiLinkTestInjectInbound(
	const void* data, size_t size);
uint8_t mLibretroGBAWifiLinkTestPlayerForRole(enum GBALinkRole role);
bool mLibretroGBAWifiLinkTestInstallPair(
	const struct GBAReplicaManifest manifests[2],
	const struct GBAReplicaPayload payloads[2],
	enum GBALinkRole role, uint64_t generation);
struct mCore* mLibretroGBAWifiLinkTestPairCore(uint8_t player);
bool mLibretroGBAWifiLinkTestCaptureCheckpoint(uint64_t frame);
void mLibretroGBAWifiLinkTestFailNextCheckpointAllocation(void);
void mLibretroGBAWifiLinkTestFail(enum GBALinkV2Reason reason);
bool mLibretroGBAWifiLinkTestGetMetrics(
	struct mLibretroGBAWifiLinkTestMetrics* metrics);
#endif

#endif
