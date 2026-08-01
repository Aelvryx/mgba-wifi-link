/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef SHA256_H
#define SHA256_H

#include <mgba-util/common.h>

CXX_GUARD_START

enum {
	MGBA_SHA256_DIGEST_SIZE = 32,
};

struct SHA256Context {
	uint32_t state[8];
	uint64_t size;
	uint8_t buffer[64];
	size_t bufferSize;
};

void sha256Init(struct SHA256Context* context);
void sha256Update(struct SHA256Context* context, const void* input, size_t size);
void sha256Finalize(uint8_t digest[MGBA_SHA256_DIGEST_SIZE], struct SHA256Context* context);
void sha256Buffer(const void* input, size_t size, uint8_t digest[MGBA_SHA256_DIGEST_SIZE]);

CXX_GUARD_END

#endif
