/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef M_LIBRETRO_REPLICATED_PAIR_FRONTEND_H
#define M_LIBRETRO_REPLICATED_PAIR_FRONTEND_H

#include <mgba-util/common.h>

CXX_GUARD_START

struct mCore;

bool mLibretroReplicatedPairFrontendStart(struct mCore* primary);
bool mLibretroReplicatedPairFrontendRunFrame(uint16_t keys);
bool mLibretroReplicatedPairFrontendIsActive(void);
void mLibretroReplicatedPairFrontendStop(void);

CXX_GUARD_END

#endif
