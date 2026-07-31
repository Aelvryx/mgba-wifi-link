/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/gba/replica.h>

#include <mgba/core/core.h>
#include <mgba/flags.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/memory.h>
#include <mgba/internal/gba/savedata.h>
#include <mgba/internal/gba/serialize.h>
#include <mgba/internal/gba/sio.h>

#include <mgba-util/memory.h>
#include <mgba-util/vfs.h>

#ifdef USE_ZLIB
#include <zlib.h>
#endif

#define GBA_REPLICA_MAGIC UINT32_C(0x50524247)

MGBA_EXPORT const struct GBAReplicaLimits GBA_REPLICA_DEFAULT_LIMITS = {
	.maxStateSize = GBA_REPLICA_MAX_STATE_SIZE,
	.maxSaveSize = GBA_REPLICA_MAX_SAVE_SIZE,
	.maxUncompressedSize = GBA_REPLICA_MAX_UNCOMPRESSED_SIZE,
	.maxEncodedSize = GBA_REPLICA_MAX_ENCODED_SIZE,
	.maxChunkSize = GBA_REPLICA_MAX_CHUNK_SIZE,
	.maxChunks = GBA_REPLICA_MAX_CHUNKS,
};

static uint16_t _load16(const uint8_t* input) {
	return (uint16_t) input[0] | (uint16_t) input[1] << 8;
}

static uint32_t _load32(const uint8_t* input) {
	return (uint32_t) input[0] |
	       (uint32_t) input[1] << 8 |
	       (uint32_t) input[2] << 16 |
	       (uint32_t) input[3] << 24;
}

static uint64_t _load64(const uint8_t* input) {
	return (uint64_t) _load32(input) | (uint64_t) _load32(input + 4) << 32;
}

static void _store16(uint8_t* output, uint16_t value) {
	output[0] = value;
	output[1] = value >> 8;
}

static void _store32(uint8_t* output, uint32_t value) {
	output[0] = value;
	output[1] = value >> 8;
	output[2] = value >> 16;
	output[3] = value >> 24;
}

static void _store64(uint8_t* output, uint64_t value) {
	_store32(output, value);
	_store32(output + 4, value >> 32);
}

static bool _saveTypeSize(enum GBASavedataType type, uint32_t* size, bool* flexible) {
	*flexible = false;
	switch (type) {
	case GBA_SAVEDATA_AUTODETECT:
		*size = 0;
		*flexible = true;
		return true;
	case GBA_SAVEDATA_FORCE_NONE:
		*size = 0;
		return true;
	case GBA_SAVEDATA_SRAM:
		*size = GBA_SIZE_SRAM;
		return true;
	case GBA_SAVEDATA_SRAM512:
		*size = GBA_SIZE_SRAM512;
		return true;
	case GBA_SAVEDATA_FLASH512:
		*size = GBA_SIZE_FLASH512;
		return true;
	case GBA_SAVEDATA_FLASH1M:
		*size = GBA_SIZE_FLASH1M;
		return true;
	case GBA_SAVEDATA_EEPROM:
		*size = GBA_SIZE_EEPROM;
		return true;
	case GBA_SAVEDATA_EEPROM512:
		*size = GBA_SIZE_EEPROM512;
		return true;
	}
	return false;
}

static bool _digestsEqual(const uint8_t* left, const uint8_t* right) {
	return memcmp(left, right, MGBA_SHA256_DIGEST_SIZE) == 0;
}

static enum GBAReplicaResult _validatePayloadDigests(
	const struct GBAReplicaManifest* manifest, const uint8_t* payload) {
	uint8_t digest[MGBA_SHA256_DIGEST_SIZE];
	sha256Buffer(payload, manifest->stateSize, digest);
	if (!_digestsEqual(digest, manifest->stateDigest)) {
		return GBA_REPLICA_DIGEST_MISMATCH;
	}
	sha256Buffer(payload + manifest->stateSize, manifest->saveSize, digest);
	if (!_digestsEqual(digest, manifest->saveDigest)) {
		return GBA_REPLICA_DIGEST_MISMATCH;
	}
	sha256Buffer(payload, manifest->uncompressedSize, digest);
	if (!_digestsEqual(digest, manifest->uncompressedDigest)) {
		return GBA_REPLICA_DIGEST_MISMATCH;
	}
	return GBA_REPLICA_OK;
}

const char* GBAReplicaResultName(enum GBAReplicaResult result) {
	switch (result) {
	case GBA_REPLICA_OK:
		return "ok";
	case GBA_REPLICA_DUPLICATE:
		return "duplicate";
	case GBA_REPLICA_INVALID_ARGUMENT:
		return "invalid argument";
	case GBA_REPLICA_NOT_QUIESCENT:
		return "source is not quiescent";
	case GBA_REPLICA_UNSUPPORTED:
		return "unsupported";
	case GBA_REPLICA_INVALID_MANIFEST:
		return "invalid manifest";
	case GBA_REPLICA_LIMIT_EXCEEDED:
		return "resource limit exceeded";
	case GBA_REPLICA_ALLOCATION_FAILED:
		return "allocation failed";
	case GBA_REPLICA_INVALID_RANGE:
		return "invalid range";
	case GBA_REPLICA_OVERLAP:
		return "overlapping chunk";
	case GBA_REPLICA_CONFLICTING_DUPLICATE:
		return "conflicting duplicate";
	case GBA_REPLICA_HOLE:
		return "incomplete bundle";
	case GBA_REPLICA_DIGEST_MISMATCH:
		return "digest mismatch";
	case GBA_REPLICA_COMPRESSION_ERROR:
		return "compression error";
	case GBA_REPLICA_INVALID_STATE:
		return "invalid state";
	case GBA_REPLICA_WRONG_PLAYER:
		return "wrong logical player";
	case GBA_REPLICA_WRONG_GENERATION:
		return "wrong snapshot generation";
	case GBA_REPLICA_RESTORE_FAILED:
		return "restore failed";
	}
	return "unknown";
}

bool GBAReplicaIsQuiescent(const struct mCore* core) {
	if (!core || !core->platform || core->platform(core) != mPLATFORM_GBA ||
	    !core->board) {
		return false;
	}
	const struct GBA* gba = core->board;
	return gba->memory.rom && !gba->sio.driver &&
	       !GBASIOMultiplayerIsBusy(gba->sio.siocnt) &&
	       !mTimingIsScheduled(&gba->timing, &gba->sio.completeEvent);
}

enum GBAReplicaResult GBAReplicaManifestValidate(
	const struct GBAReplicaManifest* manifest,
	const struct GBAReplicaLimits* limits) {
	if (!manifest) {
		return GBA_REPLICA_INVALID_ARGUMENT;
	}
	if (!limits) {
		limits = &GBA_REPLICA_DEFAULT_LIMITS;
	}
	if (manifest->formatVersion != GBA_REPLICA_FORMAT_VERSION ||
	    manifest->emulationCompatibilityVersion !=
	        GBA_REPLICA_EMULATION_COMPATIBILITY_VERSION ||
	    !manifest->generation || manifest->player > 1 ||
	    manifest->stateVersion != GBASavestateMagic + GBASavestateVersion ||
	    manifest->stateSize != sizeof(struct GBASerializedState) ||
	    (manifest->encoding != GBA_REPLICA_ENCODING_NONE &&
	     manifest->encoding != GBA_REPLICA_ENCODING_DEFLATE) ||
	    manifest->rtcType < RTC_NO_OVERRIDE ||
	    manifest->rtcType > RTC_WALLCLOCK_OFFSET ||
	    !manifest->chunkSize || !manifest->encodedSize) {
		return GBA_REPLICA_INVALID_MANIFEST;
	}

	uint32_t expectedSaveSize;
	bool flexibleSaveSize;
	if (!_saveTypeSize(manifest->saveType, &expectedSaveSize, &flexibleSaveSize) ||
	    (!flexibleSaveSize && manifest->saveSize != expectedSaveSize)) {
		return GBA_REPLICA_INVALID_MANIFEST;
	}
	if (manifest->stateSize > limits->maxStateSize ||
	    manifest->saveSize > limits->maxSaveSize ||
	    manifest->uncompressedSize > limits->maxUncompressedSize ||
	    manifest->encodedSize > limits->maxEncodedSize ||
	    manifest->chunkSize > limits->maxChunkSize) {
		return GBA_REPLICA_LIMIT_EXCEEDED;
	}
	if (manifest->uncompressedSize != manifest->stateSize + manifest->saveSize ||
	    (manifest->encoding == GBA_REPLICA_ENCODING_NONE &&
	     manifest->encodedSize != manifest->uncompressedSize)) {
		return GBA_REPLICA_INVALID_MANIFEST;
	}
	uint64_t chunks = ((uint64_t) manifest->encodedSize +
	                   manifest->chunkSize - 1) /
	                  manifest->chunkSize;
	if (!chunks || chunks > limits->maxChunks) {
		return GBA_REPLICA_LIMIT_EXCEEDED;
	}
	return GBA_REPLICA_OK;
}

enum GBAReplicaResult GBAReplicaManifestEncode(
	const struct GBAReplicaManifest* manifest,
	uint8_t output[GBA_REPLICA_MANIFEST_SIZE]) {
	if (!output) {
		return GBA_REPLICA_INVALID_ARGUMENT;
	}
	enum GBAReplicaResult result = GBAReplicaManifestValidate(manifest, NULL);
	if (result != GBA_REPLICA_OK) {
		return result;
	}
	memset(output, 0, GBA_REPLICA_MANIFEST_SIZE);
	_store32(&output[0], GBA_REPLICA_MAGIC);
	_store16(&output[4], manifest->formatVersion);
	_store16(&output[6], GBA_REPLICA_MANIFEST_SIZE);
	_store32(&output[8], manifest->emulationCompatibilityVersion);
	_store64(&output[16], manifest->generation);
	output[24] = manifest->player;
	output[25] = manifest->encoding;
	_store32(&output[28], manifest->stateVersion);
	_store32(&output[32], manifest->stateSize);
	_store32(&output[36], (uint32_t) manifest->saveType);
	_store32(&output[40], manifest->saveSize);
	_store32(&output[44], (uint32_t) manifest->rtcType);
	_store64(&output[48], (uint64_t) manifest->rtcValue);
	_store64(&output[56], manifest->frameCounter);
	_store64(&output[64], manifest->globalCycles);
	_store32(&output[72], manifest->uncompressedSize);
	_store32(&output[76], manifest->encodedSize);
	_store32(&output[80], manifest->chunkSize);
	_store64(&output[84], (uint64_t) manifest->cartridgeRtcLastLatch);
	_store64(&output[92], (uint64_t) manifest->cartridgeRtcOffset);
	memcpy(&output[112], manifest->stateDigest, MGBA_SHA256_DIGEST_SIZE);
	memcpy(&output[144], manifest->saveDigest, MGBA_SHA256_DIGEST_SIZE);
	memcpy(&output[176], manifest->uncompressedDigest, MGBA_SHA256_DIGEST_SIZE);
	memcpy(&output[208], manifest->encodedDigest, MGBA_SHA256_DIGEST_SIZE);
	return GBA_REPLICA_OK;
}

enum GBAReplicaResult GBAReplicaManifestDecode(
	const uint8_t* input, size_t size,
	const struct GBAReplicaLimits* limits,
	struct GBAReplicaManifest* manifest) {
	if (!input || !manifest) {
		return GBA_REPLICA_INVALID_ARGUMENT;
	}
	if (size != GBA_REPLICA_MANIFEST_SIZE ||
	    _load32(&input[0]) != GBA_REPLICA_MAGIC ||
	    _load16(&input[6]) != GBA_REPLICA_MANIFEST_SIZE ||
	    _load32(&input[12]) || _load16(&input[26])) {
		return GBA_REPLICA_INVALID_MANIFEST;
	}
	size_t i;
	for (i = 100; i < 112; ++i) {
		if (input[i]) {
			return GBA_REPLICA_INVALID_MANIFEST;
		}
	}

	memset(manifest, 0, sizeof(*manifest));
	manifest->formatVersion = _load16(&input[4]);
	manifest->emulationCompatibilityVersion = _load32(&input[8]);
	manifest->generation = _load64(&input[16]);
	manifest->player = input[24];
	manifest->encoding = input[25];
	manifest->stateVersion = _load32(&input[28]);
	manifest->stateSize = _load32(&input[32]);
	manifest->saveType = (int32_t) _load32(&input[36]);
	manifest->saveSize = _load32(&input[40]);
	manifest->rtcType = (int32_t) _load32(&input[44]);
	manifest->rtcValue = (int64_t) _load64(&input[48]);
	manifest->frameCounter = _load64(&input[56]);
	manifest->globalCycles = _load64(&input[64]);
	manifest->uncompressedSize = _load32(&input[72]);
	manifest->encodedSize = _load32(&input[76]);
	manifest->chunkSize = _load32(&input[80]);
	manifest->cartridgeRtcLastLatch = (int64_t) _load64(&input[84]);
	manifest->cartridgeRtcOffset = (int64_t) _load64(&input[92]);
	memcpy(manifest->stateDigest, &input[112], MGBA_SHA256_DIGEST_SIZE);
	memcpy(manifest->saveDigest, &input[144], MGBA_SHA256_DIGEST_SIZE);
	memcpy(manifest->uncompressedDigest, &input[176], MGBA_SHA256_DIGEST_SIZE);
	memcpy(manifest->encodedDigest, &input[208], MGBA_SHA256_DIGEST_SIZE);
	return GBAReplicaManifestValidate(manifest, limits);
}

static enum GBAReplicaResult _copySaveData(
	struct GBASavedata* savedata, uint8_t* output, size_t size) {
	if (!size) {
		return GBA_REPLICA_OK;
	}
	if (savedata->data) {
		memcpy(output, savedata->data, size);
		return GBA_REPLICA_OK;
	}
	if (!savedata->vf) {
		return GBA_REPLICA_INVALID_STATE;
	}
	off_t position = savedata->vf->seek(savedata->vf, 0, SEEK_CUR);
	if (position < 0 || savedata->vf->seek(savedata->vf, 0, SEEK_SET) < 0) {
		return GBA_REPLICA_INVALID_STATE;
	}
	ssize_t read = savedata->vf->read(savedata->vf, output, size);
	bool restored = savedata->vf->seek(savedata->vf, position, SEEK_SET) == position;
	if (read != (ssize_t) size || !restored) {
		return GBA_REPLICA_INVALID_STATE;
	}
	return GBA_REPLICA_OK;
}

enum GBAReplicaResult GBAReplicaCapture(
	struct mCore* source, uint8_t player, uint64_t generation,
	enum GBAReplicaEncoding encoding, uint32_t chunkSize,
	struct GBAReplicaBundle* bundle) {
	if (!source || !bundle || player > 1 || !generation || !chunkSize) {
		return GBA_REPLICA_INVALID_ARGUMENT;
	}
	memset(bundle, 0, sizeof(*bundle));
	if (!GBAReplicaIsQuiescent(source)) {
		return GBA_REPLICA_NOT_QUIESCENT;
	}
	struct GBA* gba = source->board;
	size_t saveSize = GBASavedataSize(&gba->memory.savedata);
	uint32_t expectedSaveSize;
	bool flexibleSaveSize;
	if (!_saveTypeSize(
	        gba->memory.savedata.type, &expectedSaveSize, &flexibleSaveSize) ||
	    (!flexibleSaveSize && saveSize != expectedSaveSize)) {
		return GBA_REPLICA_INVALID_STATE;
	}
	if (saveSize > GBA_REPLICA_MAX_SAVE_SIZE ||
	    chunkSize > GBA_REPLICA_MAX_CHUNK_SIZE ||
	    sizeof(struct GBASerializedState) + saveSize >
	        GBA_REPLICA_MAX_UNCOMPRESSED_SIZE) {
		return GBA_REPLICA_LIMIT_EXCEEDED;
	}
	if (source->rtc.override < RTC_NO_OVERRIDE ||
	    source->rtc.override > RTC_WALLCLOCK_OFFSET) {
		return GBA_REPLICA_UNSUPPORTED;
	}
	if (encoding != GBA_REPLICA_ENCODING_NONE &&
	    encoding != GBA_REPLICA_ENCODING_DEFLATE) {
		return GBA_REPLICA_UNSUPPORTED;
	}
#ifndef USE_ZLIB
	if (encoding == GBA_REPLICA_ENCODING_DEFLATE) {
		return GBA_REPLICA_UNSUPPORTED;
	}
#endif

	struct GBAReplicaManifest* manifest = &bundle->manifest;
	manifest->formatVersion = GBA_REPLICA_FORMAT_VERSION;
	manifest->emulationCompatibilityVersion =
	    GBA_REPLICA_EMULATION_COMPATIBILITY_VERSION;
	manifest->generation = generation;
	manifest->player = player;
	manifest->encoding = encoding;
	manifest->stateVersion = GBASavestateMagic + GBASavestateVersion;
	manifest->stateSize = sizeof(struct GBASerializedState);
	manifest->saveType = gba->memory.savedata.type;
	manifest->saveSize = saveSize;
	manifest->rtcType = source->rtc.override;
	manifest->rtcValue = source->rtc.value;
	manifest->frameCounter = gba->video.frameCounter;
	manifest->globalCycles = gba->timing.globalCycles;
	manifest->uncompressedSize = manifest->stateSize + manifest->saveSize;
	manifest->chunkSize = chunkSize;
	manifest->cartridgeRtcLastLatch = gba->memory.hw.rtc.lastLatch;
	manifest->cartridgeRtcOffset = gba->memory.hw.rtc.offset;

	uint8_t* uncompressed = calloc(1, manifest->uncompressedSize);
	if (!uncompressed) {
		return GBA_REPLICA_ALLOCATION_FAILED;
	}
	GBASerialize(gba, (struct GBASerializedState*) uncompressed);
	enum GBAReplicaResult result = _copySaveData(
	    &gba->memory.savedata, uncompressed + manifest->stateSize,
	    manifest->saveSize);
	if (result != GBA_REPLICA_OK) {
		free(uncompressed);
		return result;
	}

	sha256Buffer(uncompressed, manifest->stateSize, manifest->stateDigest);
	sha256Buffer(uncompressed + manifest->stateSize, manifest->saveSize,
	             manifest->saveDigest);
	sha256Buffer(uncompressed, manifest->uncompressedSize,
	             manifest->uncompressedDigest);

	switch (encoding) {
	case GBA_REPLICA_ENCODING_NONE:
		bundle->encodedData = uncompressed;
		bundle->encodedSize = manifest->uncompressedSize;
		break;
	case GBA_REPLICA_ENCODING_DEFLATE:
#ifdef USE_ZLIB
	{
		uLongf capacity = compressBound(manifest->uncompressedSize);
		if (capacity > GBA_REPLICA_MAX_ENCODED_SIZE) {
			free(uncompressed);
			return GBA_REPLICA_LIMIT_EXCEEDED;
		}
		bundle->encodedData = malloc(capacity);
		if (!bundle->encodedData) {
			free(uncompressed);
			return GBA_REPLICA_ALLOCATION_FAILED;
		}
		int zresult = compress2(
		    bundle->encodedData, &capacity, uncompressed,
		    manifest->uncompressedSize, Z_BEST_SPEED);
		free(uncompressed);
		if (zresult != Z_OK) {
			GBAReplicaBundleDeinit(bundle);
			return GBA_REPLICA_COMPRESSION_ERROR;
		}
		bundle->encodedSize = capacity;
		break;
	}
#else
		free(uncompressed);
		return GBA_REPLICA_UNSUPPORTED;
#endif
	default:
		free(uncompressed);
		return GBA_REPLICA_UNSUPPORTED;
	}
	manifest->encodedSize = bundle->encodedSize;
	sha256Buffer(bundle->encodedData, bundle->encodedSize,
	             manifest->encodedDigest);
	result = GBAReplicaManifestValidate(manifest, NULL);
	if (result != GBA_REPLICA_OK) {
		GBAReplicaBundleDeinit(bundle);
		return result;
	}
	return GBA_REPLICA_OK;
}

void GBAReplicaBundleDeinit(struct GBAReplicaBundle* bundle) {
	if (!bundle) {
		return;
	}
	free(bundle->encodedData);
	memset(bundle, 0, sizeof(*bundle));
}

enum GBAReplicaResult GBAReplicaAssemblerInit(
	struct GBAReplicaAssembler* assembler,
	const struct GBAReplicaManifest* manifest,
	uint8_t expectedPlayer, uint64_t expectedGeneration,
	const struct GBAReplicaLimits* limits) {
	if (!assembler || !manifest) {
		return GBA_REPLICA_INVALID_ARGUMENT;
	}
	memset(assembler, 0, sizeof(*assembler));
	if (manifest->player != expectedPlayer) {
		return GBA_REPLICA_WRONG_PLAYER;
	}
	if (manifest->generation != expectedGeneration) {
		return GBA_REPLICA_WRONG_GENERATION;
	}
	enum GBAReplicaResult result = GBAReplicaManifestValidate(manifest, limits);
	if (result != GBA_REPLICA_OK) {
		return result;
	}
#ifndef USE_ZLIB
	if (manifest->encoding == GBA_REPLICA_ENCODING_DEFLATE) {
		return GBA_REPLICA_UNSUPPORTED;
	}
#endif
	assembler->chunkCount =
	    (manifest->encodedSize + manifest->chunkSize - 1) /
	    manifest->chunkSize;
	assembler->encodedData = malloc(manifest->encodedSize);
	assembler->receivedChunks = calloc(assembler->chunkCount, 1);
	if (!assembler->encodedData || !assembler->receivedChunks) {
		GBAReplicaAssemblerDeinit(assembler);
		return GBA_REPLICA_ALLOCATION_FAILED;
	}
	assembler->manifest = *manifest;
	return GBA_REPLICA_OK;
}

enum GBAReplicaResult GBAReplicaAssemblerAdd(
	struct GBAReplicaAssembler* assembler,
	uint8_t player, uint64_t generation, uint32_t offset,
	const void* data, size_t size) {
	if (!assembler || !assembler->encodedData || !data || !size ||
	    assembler->finalized) {
		return GBA_REPLICA_INVALID_ARGUMENT;
	}
	if (player != assembler->manifest.player) {
		return GBA_REPLICA_WRONG_PLAYER;
	}
	if (generation != assembler->manifest.generation) {
		return GBA_REPLICA_WRONG_GENERATION;
	}
	if (offset % assembler->manifest.chunkSize ||
	    offset >= assembler->manifest.encodedSize) {
		return GBA_REPLICA_OVERLAP;
	}
	size_t chunk = offset / assembler->manifest.chunkSize;
	size_t expected = assembler->manifest.encodedSize - offset;
	if (expected > assembler->manifest.chunkSize) {
		expected = assembler->manifest.chunkSize;
	}
	if (size != expected) {
		return GBA_REPLICA_INVALID_RANGE;
	}
	if (assembler->receivedChunks[chunk]) {
		if (memcmp(&assembler->encodedData[offset], data, size) == 0) {
			return GBA_REPLICA_DUPLICATE;
		}
		return GBA_REPLICA_CONFLICTING_DUPLICATE;
	}
	memcpy(&assembler->encodedData[offset], data, size);
	assembler->receivedChunks[chunk] = 1;
	assembler->receivedBytes += size;
	return GBA_REPLICA_OK;
}

enum GBAReplicaResult GBAReplicaAssemblerFinalize(
	struct GBAReplicaAssembler* assembler,
	struct GBAReplicaPayload* payload) {
	if (!assembler || !payload || !assembler->encodedData || assembler->finalized) {
		return GBA_REPLICA_INVALID_ARGUMENT;
	}
	memset(payload, 0, sizeof(*payload));
	if (assembler->receivedBytes != assembler->manifest.encodedSize) {
		return GBA_REPLICA_HOLE;
	}
	size_t i;
	for (i = 0; i < assembler->chunkCount; ++i) {
		if (!assembler->receivedChunks[i]) {
			return GBA_REPLICA_HOLE;
		}
	}
	uint8_t digest[MGBA_SHA256_DIGEST_SIZE];
	sha256Buffer(assembler->encodedData, assembler->manifest.encodedSize, digest);
	if (!_digestsEqual(digest, assembler->manifest.encodedDigest)) {
		return GBA_REPLICA_DIGEST_MISMATCH;
	}
	payload->data = malloc(assembler->manifest.uncompressedSize);
	if (!payload->data) {
		return GBA_REPLICA_ALLOCATION_FAILED;
	}
	payload->size = assembler->manifest.uncompressedSize;

	enum GBAReplicaResult result = GBA_REPLICA_OK;
	switch (assembler->manifest.encoding) {
	case GBA_REPLICA_ENCODING_NONE:
		memcpy(payload->data, assembler->encodedData, payload->size);
		break;
	case GBA_REPLICA_ENCODING_DEFLATE:
#ifdef USE_ZLIB
	{
		z_stream stream;
		memset(&stream, 0, sizeof(stream));
		stream.next_in = assembler->encodedData;
		stream.avail_in = assembler->manifest.encodedSize;
		stream.next_out = payload->data;
		stream.avail_out = payload->size;
		int zresult = inflateInit(&stream);
		if (zresult != Z_OK) {
			result = GBA_REPLICA_COMPRESSION_ERROR;
		} else {
			zresult = inflate(&stream, Z_FINISH);
			if (zresult != Z_STREAM_END ||
			    stream.total_in != assembler->manifest.encodedSize ||
			    stream.total_out != assembler->manifest.uncompressedSize) {
				result = GBA_REPLICA_COMPRESSION_ERROR;
			}
			inflateEnd(&stream);
		}
		break;
	}
#else
		result = GBA_REPLICA_UNSUPPORTED;
		break;
#endif
	default:
		result = GBA_REPLICA_UNSUPPORTED;
		break;
	}
	if (result == GBA_REPLICA_OK) {
		result = _validatePayloadDigests(&assembler->manifest, payload->data);
	}
	if (result != GBA_REPLICA_OK) {
		GBAReplicaPayloadDeinit(payload);
		return result;
	}
	assembler->finalized = true;
	return GBA_REPLICA_OK;
}

void GBAReplicaAssemblerDeinit(struct GBAReplicaAssembler* assembler) {
	if (!assembler) {
		return;
	}
	free(assembler->encodedData);
	free(assembler->receivedChunks);
	memset(assembler, 0, sizeof(*assembler));
}

void GBAReplicaPayloadDeinit(struct GBAReplicaPayload* payload) {
	if (!payload) {
		return;
	}
	free(payload->data);
	memset(payload, 0, sizeof(*payload));
}

static enum GBAReplicaResult _validateSerializedState(
	const struct mCore* target, const struct GBAReplicaManifest* manifest,
	const uint8_t* data) {
	const struct GBASerializedState* state = (const struct GBASerializedState*) data;
	if (_load32((const uint8_t*) &state->versionMagic) != manifest->stateVersion ||
	    state->savedata.type != (uint8_t) manifest->saveType ||
	    _load32((const uint8_t*) &state->video.frameCounter) !=
	        manifest->frameCounter ||
	    _load64((const uint8_t*) &state->globalCycles) != manifest->globalCycles) {
		return GBA_REPLICA_INVALID_STATE;
	}
	const struct GBA* gba = target->board;
	if (_load32((const uint8_t*) &state->romCrc32) != gba->romCrc32 ||
	    _load32((const uint8_t*) &state->biosChecksum) != gba->biosChecksum) {
		return GBA_REPLICA_INVALID_STATE;
	}
	return GBA_REPLICA_OK;
}

static bool _restoreSaveData(
	struct GBASavedata* savedata, enum GBASavedataType type,
	const uint8_t* data, size_t size) {
	if (savedata->type != type) {
		GBASavedataForceType(savedata, type);
	}
	if (!size) {
		return true;
	}
	if (savedata->data && GBASavedataSize(savedata) == size) {
		memcpy(savedata->data, data, size);
		return true;
	}
	struct VFile* vf = VFileFromConstMemory(data, size);
	if (!vf) {
		return false;
	}
	bool success = GBASavedataLoad(savedata, vf);
	vf->close(vf);
	return success;
}

enum GBAReplicaResult GBAReplicaRestore(
	struct mCore* target,
	const struct GBAReplicaManifest* manifest,
	const struct GBAReplicaPayload* payload,
	uint8_t expectedPlayer, uint64_t expectedGeneration) {
	if (!target || !manifest || !payload || !payload->data) {
		return GBA_REPLICA_INVALID_ARGUMENT;
	}
	if (manifest->player != expectedPlayer) {
		return GBA_REPLICA_WRONG_PLAYER;
	}
	if (manifest->generation != expectedGeneration) {
		return GBA_REPLICA_WRONG_GENERATION;
	}
	enum GBAReplicaResult result = GBAReplicaManifestValidate(manifest, NULL);
	if (result != GBA_REPLICA_OK) {
		return result;
	}
	if (payload->size != manifest->uncompressedSize) {
		return GBA_REPLICA_INVALID_RANGE;
	}
	if (!GBAReplicaIsQuiescent(target)) {
		return GBA_REPLICA_NOT_QUIESCENT;
	}
	result = _validatePayloadDigests(manifest, payload->data);
	if (result != GBA_REPLICA_OK) {
		return result;
	}
	result = _validateSerializedState(target, manifest, payload->data);
	if (result != GBA_REPLICA_OK) {
		return result;
	}
	struct GBA* gba = target->board;
	if (!_restoreSaveData(
	        &gba->memory.savedata, manifest->saveType,
	        payload->data + manifest->stateSize, manifest->saveSize)) {
		return GBA_REPLICA_RESTORE_FAILED;
	}
	if (!GBADeserialize(
	        gba, (const struct GBASerializedState*) payload->data)) {
		return GBA_REPLICA_RESTORE_FAILED;
	}
	target->rtc.override = manifest->rtcType;
	target->rtc.value = manifest->rtcValue;
	gba->memory.hw.rtc.lastLatch = manifest->cartridgeRtcLastLatch;
	gba->memory.hw.rtc.offset = manifest->cartridgeRtcOffset;
	return GBA_REPLICA_OK;
}
