/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba-util/sha256.h>

static const uint32_t _roundConstants[64] = {
	0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5,
	0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
	0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3,
	0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174,
	0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC,
	0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
	0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7,
	0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967,
	0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13,
	0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
	0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3,
	0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
	0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5,
	0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
	0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208,
	0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2,
};

static uint32_t _rotateRight(uint32_t value, unsigned shift) {
	return value >> shift | value << (32 - shift);
}

static uint32_t _loadBigEndian32(const uint8_t* value) {
	return (uint32_t) value[0] << 24 |
	       (uint32_t) value[1] << 16 |
	       (uint32_t) value[2] << 8 |
	       value[3];
}

static void _storeBigEndian32(uint8_t* output, uint32_t value) {
	output[0] = value >> 24;
	output[1] = value >> 16;
	output[2] = value >> 8;
	output[3] = value;
}

static void _transform(struct SHA256Context* context, const uint8_t block[64]) {
	uint32_t words[64];
	unsigned i;
	for (i = 0; i < 16; ++i) {
		words[i] = _loadBigEndian32(&block[i * 4]);
	}
	for (; i < 64; ++i) {
		uint32_t sigma0 = _rotateRight(words[i - 15], 7) ^
		                  _rotateRight(words[i - 15], 18) ^
		                  (words[i - 15] >> 3);
		uint32_t sigma1 = _rotateRight(words[i - 2], 17) ^
		                  _rotateRight(words[i - 2], 19) ^
		                  (words[i - 2] >> 10);
		words[i] = words[i - 16] + sigma0 + words[i - 7] + sigma1;
	}

	uint32_t a = context->state[0];
	uint32_t b = context->state[1];
	uint32_t c = context->state[2];
	uint32_t d = context->state[3];
	uint32_t e = context->state[4];
	uint32_t f = context->state[5];
	uint32_t g = context->state[6];
	uint32_t h = context->state[7];
	for (i = 0; i < 64; ++i) {
		uint32_t sum1 = _rotateRight(e, 6) ^ _rotateRight(e, 11) ^
		                _rotateRight(e, 25);
		uint32_t choice = (e & f) ^ (~e & g);
		uint32_t temporary1 = h + sum1 + choice +
		                      _roundConstants[i] + words[i];
		uint32_t sum0 = _rotateRight(a, 2) ^ _rotateRight(a, 13) ^
		                _rotateRight(a, 22);
		uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
		uint32_t temporary2 = sum0 + majority;

		h = g;
		g = f;
		f = e;
		e = d + temporary1;
		d = c;
		c = b;
		b = a;
		a = temporary1 + temporary2;
	}
	context->state[0] += a;
	context->state[1] += b;
	context->state[2] += c;
	context->state[3] += d;
	context->state[4] += e;
	context->state[5] += f;
	context->state[6] += g;
	context->state[7] += h;
	memset(words, 0, sizeof(words));
}

void sha256Init(struct SHA256Context* context) {
	memset(context, 0, sizeof(*context));
	context->state[0] = 0x6A09E667;
	context->state[1] = 0xBB67AE85;
	context->state[2] = 0x3C6EF372;
	context->state[3] = 0xA54FF53A;
	context->state[4] = 0x510E527F;
	context->state[5] = 0x9B05688C;
	context->state[6] = 0x1F83D9AB;
	context->state[7] = 0x5BE0CD19;
}

void sha256Update(struct SHA256Context* context, const void* input, size_t size) {
	const uint8_t* bytes = input;
	context->size += size;
	while (size) {
		size_t available = sizeof(context->buffer) - context->bufferSize;
		size_t copy = size < available ? size : available;
		memcpy(&context->buffer[context->bufferSize], bytes, copy);
		context->bufferSize += copy;
		bytes += copy;
		size -= copy;
		if (context->bufferSize == sizeof(context->buffer)) {
			_transform(context, context->buffer);
			context->bufferSize = 0;
		}
	}
}

void sha256Finalize(uint8_t digest[MGBA_SHA256_DIGEST_SIZE], struct SHA256Context* context) {
	uint64_t bitSize = context->size * 8;
	uint8_t padding[128] = { 0x80 };
	size_t paddingSize = context->bufferSize < 56 ?
	                     56 - context->bufferSize :
	                     120 - context->bufferSize;
	sha256Update(context, padding, paddingSize);
	uint8_t encodedSize[8];
	unsigned i;
	for (i = 0; i < sizeof(encodedSize); ++i) {
		encodedSize[sizeof(encodedSize) - i - 1] = bitSize >> (i * 8);
	}
	sha256Update(context, encodedSize, sizeof(encodedSize));
	for (i = 0; i < 8; ++i) {
		_storeBigEndian32(&digest[i * 4], context->state[i]);
	}
	memset(context, 0, sizeof(*context));
}

void sha256Buffer(const void* input, size_t size, uint8_t digest[MGBA_SHA256_DIGEST_SIZE]) {
	struct SHA256Context context;
	sha256Init(&context);
	sha256Update(&context, input, size);
	sha256Finalize(digest, &context);
}
