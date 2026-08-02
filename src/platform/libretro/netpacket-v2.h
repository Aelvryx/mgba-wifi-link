/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef M_LIBRETRO_NETPACKET_V2_H
#define M_LIBRETRO_NETPACKET_V2_H

#include "libretro.h"

#include <mgba-util/common.h>
#include <mgba-util/image.h>

struct mLibretroNetpacketV2TestMetrics {
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

#ifdef M_LIBRETRO_NETPACKET_V2_TEST
#include <mgba/internal/gba/sio/netplay/protocol-v2.h>
#endif

struct mCore;

bool mLibretroNetpacketV2Register(
	retro_environment_t environment, struct mCore* core,
	void* saveData, size_t saveCapacity);
void mLibretroNetpacketV2RunBegin(void);
bool mLibretroNetpacketV2RunFrame(uint16_t keys);
bool mLibretroNetpacketV2ExecutionBlocked(void);
bool mLibretroNetpacketV2OwnsExecution(void);
struct mCore* mLibretroNetpacketV2PresentedCore(void);
mColor* mLibretroNetpacketV2PresentedVideo(void);
void mLibretroNetpacketV2ReportAudio(size_t samples);
void mLibretroNetpacketV2Reset(void);
void mLibretroNetpacketV2Unload(void);
bool mLibretroNetpacketV2SessionActive(void);
bool mLibretroNetpacketV2RejectOperation(const char* operation);
bool mLibretroNetpacketV2RejectLatencyPolicyChange(const char* value);

#ifdef M_LIBRETRO_NETPACKET_V2_TEST
bool mLibretroNetpacketV2TestPollReceive(void);
void mLibretroNetpacketV2TestSetTimeMs(uint64_t nowMs);
uint64_t mLibretroNetpacketV2TestCallbackGeneration(void);
size_t mLibretroNetpacketV2TestPendingPacketCount(void);
bool mLibretroNetpacketV2TestInjectInbound(
	const void* data, size_t size);
uint8_t mLibretroNetpacketV2TestPlayerForRole(enum GBALinkRole role);
bool mLibretroNetpacketV2TestInstallPair(
	const struct GBAReplicaManifest manifests[2],
	const struct GBAReplicaPayload payloads[2],
	enum GBALinkRole role, uint64_t generation);
struct mCore* mLibretroNetpacketV2TestPairCore(uint8_t player);
bool mLibretroNetpacketV2TestCaptureCheckpoint(uint64_t frame);
void mLibretroNetpacketV2TestFailNextCheckpointAllocation(void);
void mLibretroNetpacketV2TestFail(enum GBALinkV2Reason reason);
bool mLibretroNetpacketV2TestGetMetrics(
	struct mLibretroNetpacketV2TestMetrics* metrics);
#endif

#endif
