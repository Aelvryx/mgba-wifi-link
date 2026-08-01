/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef LIBRETRO_NETPACKET_H
#define LIBRETRO_NETPACKET_H

#include "libretro.h"

#include <mgba-util/common.h>

struct mCore;

bool mLibretroNetpacketRegister(
    retro_environment_t environment, struct mCore* core);
void mLibretroNetpacketRunBegin(void);
void mLibretroNetpacketRunEnd(void);
bool mLibretroNetpacketExecutionBlocked(void);
void mLibretroNetpacketReset(void);
void mLibretroNetpacketUnload(void);

bool mLibretroNetpacketSessionActive(void);
bool mLibretroNetpacketRejectStateOperation(const char* operation);
bool mLibretroNetpacketRejectTimingChange(const char* category);
bool mLibretroNetpacketRejectCheatChange(void);

#ifdef M_LIBRETRO_NETPACKET_TEST
bool mLibretroNetpacketTestPollReceive(void);
void mLibretroNetpacketTestRunBeginWithoutRendezvous(void);
int mLibretroNetpacketTestSessionState(void);
void mLibretroNetpacketTestSetSessionState(
    int state);
void mLibretroNetpacketTestSetTimeMs(uint64_t nowMs);
uint64_t mLibretroNetpacketTestCallbackGeneration(void);
size_t mLibretroNetpacketTestPendingPacketCount(void);
#endif

#endif
