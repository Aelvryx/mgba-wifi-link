/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef M_LIBRETRO_REPLICATED_PAIR_SPIKE_H
#define M_LIBRETRO_REPLICATED_PAIR_SPIKE_H

#include <mgba-util/common.h>

CXX_GUARD_START

struct mCore;

bool mLibretroReplicatedPairSpikeStart(struct mCore* primary);
bool mLibretroReplicatedPairSpikeRunFrame(uint16_t keys);
bool mLibretroReplicatedPairSpikeIsActive(void);
void mLibretroReplicatedPairSpikeStop(void);

CXX_GUARD_END

#endif
