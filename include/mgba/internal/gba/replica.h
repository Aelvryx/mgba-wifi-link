/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef GBA_REPLICA_H
#define GBA_REPLICA_H

#include <mgba-util/common.h>

CXX_GUARD_START

#include <mgba/gba/interface.h>
#include <mgba-util/sha256.h>

enum {
	GBA_REPLICA_FORMAT_VERSION = 1,
	GBA_REPLICA_EMULATION_COMPATIBILITY_VERSION = 1,
	GBA_REPLICA_MANIFEST_SIZE = 240,
	GBA_REPLICA_DEFAULT_CHUNK_SIZE = 16 * 1024,
	GBA_REPLICA_MAX_STATE_SIZE = 0x61000,
	GBA_REPLICA_MAX_SAVE_SIZE = 0x20000,
	GBA_REPLICA_MAX_UNCOMPRESSED_SIZE = 0x81000,
	GBA_REPLICA_MAX_ENCODED_SIZE = 0x82000,
	GBA_REPLICA_MAX_CHUNK_SIZE = 48 * 1024,
	GBA_REPLICA_MAX_CHUNKS = 4096,
};

enum GBAReplicaEncoding {
	GBA_REPLICA_ENCODING_NONE = 0,
	GBA_REPLICA_ENCODING_DEFLATE = 1,
};

enum GBAReplicaResult {
	GBA_REPLICA_OK = 0,
	GBA_REPLICA_DUPLICATE,
	GBA_REPLICA_INVALID_ARGUMENT,
	GBA_REPLICA_NOT_QUIESCENT,
	GBA_REPLICA_UNSUPPORTED,
	GBA_REPLICA_INVALID_MANIFEST,
	GBA_REPLICA_LIMIT_EXCEEDED,
	GBA_REPLICA_ALLOCATION_FAILED,
	GBA_REPLICA_INVALID_RANGE,
	GBA_REPLICA_OVERLAP,
	GBA_REPLICA_CONFLICTING_DUPLICATE,
	GBA_REPLICA_HOLE,
	GBA_REPLICA_DIGEST_MISMATCH,
	GBA_REPLICA_COMPRESSION_ERROR,
	GBA_REPLICA_INVALID_STATE,
	GBA_REPLICA_WRONG_PLAYER,
	GBA_REPLICA_WRONG_GENERATION,
	GBA_REPLICA_RESTORE_FAILED,
};

struct GBAReplicaLimits {
	uint32_t maxStateSize;
	uint32_t maxSaveSize;
	uint32_t maxUncompressedSize;
	uint32_t maxEncodedSize;
	uint32_t maxChunkSize;
	uint32_t maxChunks;
};

extern MGBA_EXPORT const struct GBAReplicaLimits GBA_REPLICA_DEFAULT_LIMITS;

struct GBAReplicaManifest {
	uint16_t formatVersion;
	uint32_t emulationCompatibilityVersion;
	uint64_t generation;
	uint8_t player;
	enum GBAReplicaEncoding encoding;
	uint32_t stateVersion;
	uint32_t stateSize;
	enum GBASavedataType saveType;
	uint32_t saveSize;
	int32_t rtcType;
	int64_t rtcValue;
	uint64_t frameCounter;
	uint64_t globalCycles;
	uint32_t uncompressedSize;
	uint32_t encodedSize;
	uint32_t chunkSize;
	int64_t cartridgeRtcLastLatch;
	int64_t cartridgeRtcOffset;
	uint8_t stateDigest[MGBA_SHA256_DIGEST_SIZE];
	uint8_t saveDigest[MGBA_SHA256_DIGEST_SIZE];
	uint8_t uncompressedDigest[MGBA_SHA256_DIGEST_SIZE];
	uint8_t encodedDigest[MGBA_SHA256_DIGEST_SIZE];
};

struct GBAReplicaBundle {
	struct GBAReplicaManifest manifest;
	uint8_t* encodedData;
	size_t encodedSize;
};

struct GBAReplicaPayload {
	uint8_t* data;
	size_t size;
};

struct GBAReplicaAssembler {
	struct GBAReplicaManifest manifest;
	uint8_t* encodedData;
	uint8_t* receivedChunks;
	size_t chunkCount;
	size_t receivedBytes;
	bool finalized;
};

struct mCore;

const char* GBAReplicaResultName(enum GBAReplicaResult result);
bool GBAReplicaIsQuiescent(const struct mCore* core);

enum GBAReplicaResult GBAReplicaManifestValidate(
	const struct GBAReplicaManifest* manifest,
	const struct GBAReplicaLimits* limits);
enum GBAReplicaResult GBAReplicaManifestEncode(
	const struct GBAReplicaManifest* manifest,
	uint8_t output[GBA_REPLICA_MANIFEST_SIZE]);
enum GBAReplicaResult GBAReplicaManifestDecode(
	const uint8_t* input, size_t size,
	const struct GBAReplicaLimits* limits,
	struct GBAReplicaManifest* manifest);

enum GBAReplicaResult GBAReplicaCapture(
	struct mCore* source, uint8_t player, uint64_t generation,
	enum GBAReplicaEncoding encoding, uint32_t chunkSize,
	struct GBAReplicaBundle* bundle);
void GBAReplicaBundleDeinit(struct GBAReplicaBundle* bundle);

enum GBAReplicaResult GBAReplicaAssemblerInit(
	struct GBAReplicaAssembler* assembler,
	const struct GBAReplicaManifest* manifest,
	uint8_t expectedPlayer, uint64_t expectedGeneration,
	const struct GBAReplicaLimits* limits);
enum GBAReplicaResult GBAReplicaAssemblerAdd(
	struct GBAReplicaAssembler* assembler,
	uint8_t player, uint64_t generation, uint32_t offset,
	const void* data, size_t size);
enum GBAReplicaResult GBAReplicaAssemblerFinalize(
	struct GBAReplicaAssembler* assembler,
	struct GBAReplicaPayload* payload);
void GBAReplicaAssemblerDeinit(struct GBAReplicaAssembler* assembler);
void GBAReplicaPayloadDeinit(struct GBAReplicaPayload* payload);

enum GBAReplicaResult GBAReplicaRestore(
	struct mCore* target,
	const struct GBAReplicaManifest* manifest,
	const struct GBAReplicaPayload* payload,
	uint8_t expectedPlayer, uint64_t expectedGeneration);

CXX_GUARD_END

#endif
