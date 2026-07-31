/* Copyright (c) 2026 mGBA Wi-Fi link contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef M_LIBRETRO_NETPACKET_SPIKE_H
#define M_LIBRETRO_NETPACKET_SPIKE_H

#include "libretro.h"

void mNetpacketSpikeRegister(retro_environment_t environment);
void mNetpacketSpikeTimingBoundary(void);
void mNetpacketSpikeUnload(void);

#endif
