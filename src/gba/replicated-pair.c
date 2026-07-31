/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/gba/replicated-pair.h>

#include <mgba/core/core.h>
#include <mgba/gba/core.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/serialize.h>
#include <mgba-util/vfs.h>

enum {
	GBA_REPLICATED_PAIR_MAX_RUN_LOOPS = 4000000,
	GBA_REPLICATED_PAIR_VIDEO_WIDTH = 256,
	GBA_REPLICATED_PAIR_VIDEO_HEIGHT = 224,
};

static int _requestedId(struct mLockstepUser* user) {
	return ((struct GBAReplicatedPairUser*) user)->requestedId;
}

static void _sleep(struct mLockstepUser* user) {
	struct GBAReplicatedPairUser* pairUser =
	    (struct GBAReplicatedPairUser*) user;
	pairUser->asleep = true;
	++pairUser->sleeps;
}

static void _wake(struct mLockstepUser* user) {
	struct GBAReplicatedPairUser* pairUser =
	    (struct GBAReplicatedPairUser*) user;
	pairUser->asleep = false;
	++pairUser->wakes;
}

const char* GBAReplicatedPairResultName(enum GBAReplicatedPairResult result) {
	switch (result) {
	case GBA_REPLICATED_PAIR_OK: return "ok";
	case GBA_REPLICATED_PAIR_INVALID_ARGUMENT: return "invalid argument";
	case GBA_REPLICATED_PAIR_INVALID_STATE: return "invalid pair state";
	case GBA_REPLICATED_PAIR_ALLOCATION_FAILED: return "allocation failed";
	case GBA_REPLICATED_PAIR_CORE_FAILED: return "logical core creation failed";
	case GBA_REPLICATED_PAIR_RESTORE_FAILED: return "replica restore failed";
	case GBA_REPLICATED_PAIR_INPUT_CONFLICT: return "input frame conflict";
	case GBA_REPLICATED_PAIR_DEADLOCK: return "cooperative scheduler deadlock";
	case GBA_REPLICATED_PAIR_FRAME_OVERSHOOT: return "logical frame overshoot";
	}
	return "unknown";
}

void GBAReplicatedPairInit(struct GBAReplicatedPair* pair) {
	if (!pair) {
		return;
	}
	memset(pair, 0, sizeof(*pair));
	GBASIOLockstepCoordinatorInit(&pair->coordinator);
	pair->initialized = true;
}

static bool _copyTemplateAssets(
	struct GBAReplicatedPair* pair, struct mCore* templateCore) {
	if (!templateCore || !templateCore->platform ||
	    templateCore->platform(templateCore) != mPLATFORM_GBA ||
	    !templateCore->board || !templateCore->romSize) {
		return false;
	}
	const struct GBA* gba = templateCore->board;
	pair->romSize = templateCore->romSize(templateCore);
	if (!pair->romSize || !gba->memory.rom) {
		return false;
	}
	pair->rom = malloc(pair->romSize);
	if (!pair->rom) {
		return false;
	}
	memcpy(pair->rom, gba->memory.rom, pair->romSize);
	if (gba->biosVf) {
		pair->biosSize = GBA_SIZE_BIOS;
		pair->bios = malloc(pair->biosSize);
		if (!pair->bios) {
			return false;
		}
		memcpy(pair->bios, gba->memory.bios, pair->biosSize);
	}
	return true;
}

static enum GBAReplicatedPairResult _createPlayer(
	struct GBAReplicatedPair* pair, unsigned playerId,
	struct mCore* templateCore,
	const struct GBAReplicaManifest* manifest,
	const struct GBAReplicaPayload* payload) {
	struct GBAReplicatedPairPlayer* player = &pair->players[playerId];
	player->core = GBACoreCreate();
	if (!player->core || !player->core->init(player->core)) {
		return GBA_REPLICATED_PAIR_CORE_FAILED;
	}
	mCoreInitConfig(player->core, NULL);
	player->configInitialized = true;
	mCoreLoadForeignConfig(player->core, &templateCore->config);
	struct VFile* rom = VFileFromConstMemory(pair->rom, pair->romSize);
	if (!rom || !player->core->loadROM(player->core, rom)) {
		if (rom) {
			rom->close(rom);
		}
		return GBA_REPLICATED_PAIR_CORE_FAILED;
	}
	if (pair->bios) {
		struct VFile* bios = VFileFromConstMemory(pair->bios, pair->biosSize);
		if (!bios || !player->core->loadBIOS(player->core, bios, 0)) {
			if (bios) {
				bios->close(bios);
			}
			return GBA_REPLICATED_PAIR_CORE_FAILED;
		}
	}
	player->temporarySave = VFileMemChunk(NULL, 0);
	if (!player->temporarySave ||
	    !player->core->loadTemporarySave(
	        player->core, player->temporarySave)) {
		return GBA_REPLICATED_PAIR_CORE_FAILED;
	}
	player->videoBuffer = malloc(
	    GBA_REPLICATED_PAIR_VIDEO_WIDTH *
	    GBA_REPLICATED_PAIR_VIDEO_HEIGHT *
	    sizeof(*player->videoBuffer));
	if (!player->videoBuffer) {
		return GBA_REPLICATED_PAIR_ALLOCATION_FAILED;
	}
	player->core->setVideoBuffer(
	    player->core, player->videoBuffer,
	    GBA_REPLICATED_PAIR_VIDEO_WIDTH);
	player->core->setAudioBufferSize(
	    player->core, templateCore->getAudioBufferSize(templateCore));
	player->core->reset(player->core);
	if (GBAReplicaRestore(
	        player->core, manifest, payload, playerId,
	        pair->snapshotGeneration) != GBA_REPLICA_OK) {
		return GBA_REPLICATED_PAIR_RESTORE_FAILED;
	}
	struct GBA* gba = player->core->board;
	player->observedSaveType = gba->memory.savedata.type;
	player->observedSaveDirty = gba->memory.savedata.dirty;
	player->observedSaveDirtAge = gba->memory.savedata.dirtAge;
	player->saveGeneration = 1;
	player->user.requestedId = playerId;
	player->user.d.requestedId = _requestedId;
	player->user.d.sleep = _sleep;
	player->user.d.wake = _wake;
	GBASIOLockstepDriverCreate(&player->driver, &player->user.d);
	GBASIOLockstepCoordinatorAttach(&pair->coordinator, &player->driver);
	player->core->setPeripheral(
	    player->core, mPERIPH_GBA_LINK_PORT, &player->driver.d);
	player->attached = true;
	return GBA_REPLICATED_PAIR_OK;
}

enum GBAReplicatedPairResult GBAReplicatedPairInstall(
	struct GBAReplicatedPair* pair, struct mCore* templateCore,
	const struct GBAReplicaManifest manifests[2],
	const struct GBAReplicaPayload payloads[2], uint64_t generation) {
	if (!pair || !templateCore || !manifests || !payloads || !generation) {
		return GBA_REPLICATED_PAIR_INVALID_ARGUMENT;
	}
	if (!pair->initialized || pair->installed || pair->stopped) {
		return GBA_REPLICATED_PAIR_INVALID_STATE;
	}
	if (manifests[0].player != 0 || manifests[1].player != 1 ||
	    manifests[0].generation != generation ||
	    manifests[1].generation != generation) {
		return GBA_REPLICATED_PAIR_INVALID_ARGUMENT;
	}
	pair->snapshotGeneration = generation;
	if (!_copyTemplateAssets(pair, templateCore)) {
		GBAReplicatedPairStop(pair);
		return pair->rom ? GBA_REPLICATED_PAIR_ALLOCATION_FAILED
		                 : GBA_REPLICATED_PAIR_CORE_FAILED;
	}
	for (unsigned i = 0; i < 2; ++i) {
		enum GBAReplicatedPairResult result = _createPlayer(
		    pair, i, templateCore, &manifests[i], &payloads[i]);
		if (result != GBA_REPLICATED_PAIR_OK) {
			GBAReplicatedPairStop(pair);
			return result;
		}
	}
	if (GBASIOLockstepCoordinatorAttached(&pair->coordinator) != 2 ||
	    pair->players[0].driver.d.deviceId(&pair->players[0].driver.d) != 0 ||
	    pair->players[1].driver.d.deviceId(&pair->players[1].driver.d) != 1) {
		GBAReplicatedPairStop(pair);
		return GBA_REPLICATED_PAIR_CORE_FAILED;
	}
	pair->installed = true;
	return GBA_REPLICATED_PAIR_OK;
}

enum GBAReplicatedPairResult GBAReplicatedPairSetInputs(
	struct GBAReplicatedPair* pair, uint64_t frame,
	uint16_t player0, uint16_t player1) {
	if (!pair) {
		return GBA_REPLICATED_PAIR_INVALID_ARGUMENT;
	}
	if (!pair->installed || pair->stopped || frame != pair->frameNumber ||
	    (player0 & ~0x03FF) || (player1 & ~0x03FF)) {
		return GBA_REPLICATED_PAIR_INVALID_STATE;
	}
	if (pair->inputsReady) {
		return pair->inputFrame == frame && pair->inputs[0] == player0 &&
		               pair->inputs[1] == player1
		           ? GBA_REPLICATED_PAIR_OK
		           : GBA_REPLICATED_PAIR_INPUT_CONFLICT;
	}
	pair->inputFrame = frame;
	pair->inputs[0] = player0;
	pair->inputs[1] = player1;
	pair->inputsReady = true;
	return GBA_REPLICATED_PAIR_OK;
}

static void _recordTrace(
	struct GBAReplicatedPair* pair, unsigned playerId) {
	struct GBASerializedState* state = malloc(sizeof(*state));
	if (!state) {
		memset(pair->stateTrace[playerId], 0,
		    sizeof(pair->stateTrace[playerId]));
		return;
	}
	GBASerialize(pair->players[playerId].core->board, state);
	struct SHA256Context context;
	sha256Init(&context);
	sha256Update(&context, pair->stateTrace[playerId],
	    sizeof(pair->stateTrace[playerId]));
	uint8_t frame[8];
	for (unsigned i = 0; i < sizeof(frame); ++i) {
		frame[i] = pair->frameNumber >> (i * 8);
	}
	sha256Update(&context, frame, sizeof(frame));
	sha256Update(&context, state, sizeof(*state));
	sha256Finalize(pair->stateTrace[playerId], &context);
	free(state);
}

static void _updateSaveGeneration(
		struct GBAReplicatedPairPlayer* player) {
	struct GBASavedata* savedata =
	    &((struct GBA*) player->core->board)->memory.savedata;
	if (savedata->type != player->observedSaveType ||
	    savedata->dirty != player->observedSaveDirty ||
	    savedata->dirtAge != player->observedSaveDirtAge) {
		++player->saveGeneration;
		player->observedSaveType = savedata->type;
		player->observedSaveDirty = savedata->dirty;
		player->observedSaveDirtAge = savedata->dirtAge;
	}
}

enum GBAReplicatedPairResult GBAReplicatedPairRunFrame(
	struct GBAReplicatedPair* pair) {
	if (!pair || !pair->installed || pair->stopped || !pair->inputsReady ||
	    pair->inputFrame != pair->frameNumber) {
		return GBA_REPLICATED_PAIR_INVALID_STATE;
	}
	uint32_t startingFrames[2];
	for (unsigned i = 0; i < 2; ++i) {
		startingFrames[i] = pair->players[i].core->frameCounter(
		    pair->players[i].core);
		pair->players[i].core->setKeys(
		    pair->players[i].core, pair->inputs[i]);
	}
	for (uint64_t iteration = 0;
	     iteration < GBA_REPLICATED_PAIR_MAX_RUN_LOOPS; ++iteration) {
		bool complete[2];
		for (unsigned i = 0; i < 2; ++i) {
			uint32_t frame = pair->players[i].core->frameCounter(
			    pair->players[i].core);
			if (frame != startingFrames[i] &&
			    frame != startingFrames[i] + 1) {
				return GBA_REPLICATED_PAIR_FRAME_OVERSHOOT;
			}
			complete[i] = frame != startingFrames[i];
		}
		if (complete[0] && complete[1]) {
			++pair->frameNumber;
			pair->inputsReady = false;
			_updateSaveGeneration(&pair->players[0]);
			_updateSaveGeneration(&pair->players[1]);
			_recordTrace(pair, 0);
			_recordTrace(pair, 1);
			return GBA_REPLICATED_PAIR_OK;
		}
		bool ran = false;
		for (unsigned i = 0; i < 2; ++i) {
			struct GBAReplicatedPairPlayer* player = &pair->players[i];
			if (!player->user.asleep) {
				uint32_t before = player->core->frameCounter(player->core);
				player->core->runLoop(player->core);
				++player->runLoops;
				ran = true;
				if (complete[i] &&
				    player->core->frameCounter(player->core) != before) {
					return GBA_REPLICATED_PAIR_FRAME_OVERSHOOT;
				}
			}
		}
		if (!ran) {
			return GBA_REPLICATED_PAIR_DEADLOCK;
		}
	}
	return GBA_REPLICATED_PAIR_DEADLOCK;
}

struct mCore* GBAReplicatedPairCore(
	struct GBAReplicatedPair* pair, uint8_t player) {
	return pair && pair->installed && !pair->stopped && player < 2
	    ? pair->players[player].core
	    : NULL;
}

const struct mCore* GBAReplicatedPairCoreConst(
	const struct GBAReplicatedPair* pair, uint8_t player) {
	return pair && pair->installed && !pair->stopped && player < 2
	    ? pair->players[player].core
	    : NULL;
}

mColor* GBAReplicatedPairVideoBuffer(
	struct GBAReplicatedPair* pair, uint8_t player) {
	return pair && pair->installed && !pair->stopped && player < 2
	    ? pair->players[player].videoBuffer
	    : NULL;
}

bool GBAReplicatedPairAssignFrontend(
	struct GBAReplicatedPair* pair, uint8_t player,
	struct mCore* frontendCore) {
	if (!pair || !pair->installed || pair->stopped || player > 1 ||
	    !frontendCore || !frontendCore->getPeripheral) {
		return false;
	}
	const int peripherals[] = {
		mPERIPH_RUMBLE,
		mPERIPH_ROTATION,
		mPERIPH_GBA_LUMINANCE,
	};
	for (unsigned logical = 0; logical < 2; ++logical) {
		for (unsigned i = 0;
		     i < sizeof(peripherals) / sizeof(*peripherals); ++i) {
			void* peripheral = logical == player
			    ? frontendCore->getPeripheral(
			          frontendCore, peripherals[i])
			    : NULL;
			pair->players[logical].core->setPeripheral(
			    pair->players[logical].core,
			    peripherals[i], peripheral);
		}
	}
	return true;
}

bool GBAReplicatedPairAssignSaveBacking(
	struct GBAReplicatedPair* pair, uint8_t player,
	void* data, size_t capacity) {
	if (!pair || !pair->installed || pair->stopped || player > 1 ||
	    !data || capacity < GBA_SIZE_FLASH1M) {
		return false;
	}
	struct VFile* backing = VFileFromMemory(data, capacity);
	if (!backing ||
	    !pair->players[player].core->loadTemporarySave(
	        pair->players[player].core, backing)) {
		if (backing) {
			backing->close(backing);
		}
		return false;
	}
	/*
	 * GBASavedataMask closed the previous temporary file. The pair owns
	 * this replacement until core teardown.
	 */
	pair->players[player].temporarySave = backing;
	struct GBASavedata* savedata =
	    &((struct GBA*) pair->players[player].core->board)->memory.savedata;
	pair->players[player].observedSaveType = savedata->type;
	pair->players[player].observedSaveDirty = savedata->dirty;
	pair->players[player].observedSaveDirtAge = savedata->dirtAge;
	return true;
}

static void _digestU32(struct SHA256Context* context, uint32_t value) {
	uint8_t encoded[4];
	for (unsigned i = 0; i < sizeof(encoded); ++i) {
		encoded[i] = value >> (i * 8);
	}
	sha256Update(context, encoded, sizeof(encoded));
}

static void _digestU64(struct SHA256Context* context, uint64_t value) {
	uint8_t encoded[8];
	for (unsigned i = 0; i < sizeof(encoded); ++i) {
		encoded[i] = value >> (i * 8);
	}
	sha256Update(context, encoded, sizeof(encoded));
}

static bool _digestSave(
		struct SHA256Context* context,
		struct GBASavedata* savedata, size_t size) {
	if (!size) {
		return true;
	}
	if (savedata->data) {
		sha256Update(context, savedata->data, size);
		return true;
	}
	if (!savedata->vf) {
		return false;
	}
	off_t position = savedata->vf->seek(
	    savedata->vf, 0, SEEK_CUR);
	if (position < 0 ||
	    savedata->vf->seek(savedata->vf, 0, SEEK_SET) != 0) {
		return false;
	}
	uint8_t buffer[2048];
	size_t remaining = size;
	bool valid = true;
	while (remaining) {
		size_t request = remaining < sizeof(buffer)
		    ? remaining : sizeof(buffer);
		ssize_t read = savedata->vf->read(
		    savedata->vf, buffer, request);
		if (read != (ssize_t) request) {
			valid = false;
			break;
		}
		sha256Update(context, buffer, request);
		remaining -= request;
	}
	if (savedata->vf->seek(
	        savedata->vf, position, SEEK_SET) != position) {
		valid = false;
	}
	return valid;
}

bool GBAReplicatedPairStateDigest(
		const struct GBAReplicatedPair* pair, uint8_t playerId,
		uint8_t digest[MGBA_SHA256_DIGEST_SIZE]) {
	if (!pair || !digest || !pair->installed || pair->stopped ||
	    playerId > 1) {
		return false;
	}
	const struct GBAReplicatedPairPlayer* player =
	    &pair->players[playerId];
	struct GBA* gba = player->core->board;
	struct GBASerializedState* state = calloc(1, sizeof(*state));
	void* driverState = NULL;
	size_t driverSize = 0;
	if (!state || !gba || !gba->sio.driver ||
	    !gba->sio.driver->driverId || !gba->sio.driver->saveState) {
		free(state);
		return false;
	}
	GBASerialize(gba, state);
	gba->sio.driver->saveState(
	    gba->sio.driver, &driverState, &driverSize);
	if (!driverState || !driverSize) {
		free(driverState);
		free(state);
		return false;
	}

	struct GBASavedata* savedata = &gba->memory.savedata;
	size_t saveSize = GBASavedataSize(savedata);

	static const char domain[] = "mGBA replicated-pair state digest";
	struct SHA256Context context;
	sha256Init(&context);
	sha256Update(&context, domain, sizeof(domain));
	_digestU32(&context, GBA_REPLICATED_PAIR_DIGEST_VERSION);
	_digestU32(&context, GBASavestateMagic + GBASavestateVersion);
	_digestU64(&context, sizeof(*state));
	sha256Update(&context, state, sizeof(*state));
	_digestU32(&context, player->core->rtc.override);
	_digestU64(&context, player->core->rtc.value);
	_digestU32(&context, savedata->type);
	_digestU64(&context, saveSize);
	if (!_digestSave(&context, savedata, saveSize)) {
		free(driverState);
		free(state);
		return false;
	}
	_digestU32(&context, gba->sio.driver->driverId(gba->sio.driver));
	_digestU64(&context, driverSize);
	sha256Update(&context, driverState, driverSize);
	sha256Finalize(digest, &context);

	free(driverState);
	free(state);
	return true;
}

bool GBAReplicatedPairGetMetrics(
	const struct GBAReplicatedPair* pair,
	struct GBAReplicatedPairMetrics* metrics) {
	if (!pair || !metrics || !pair->installed || pair->stopped) {
		return false;
	}
	memset(metrics, 0, sizeof(*metrics));
	metrics->frameNumber = pair->frameNumber;
	memcpy(metrics->stateTrace, pair->stateTrace,
	    sizeof(metrics->stateTrace));
	for (unsigned i = 0; i < 2; ++i) {
		metrics->runLoops[i] = pair->players[i].runLoops;
		metrics->sleeps[i] = pair->players[i].user.sleeps;
		metrics->wakes[i] = pair->players[i].user.wakes;
		metrics->saveGenerations[i] =
		    pair->players[i].saveGeneration;
	}
	metrics->transferStarts = pair->coordinator.transferStarts;
	metrics->transferCompletions = pair->coordinator.transferCompletions;
	metrics->transferredWords = pair->coordinator.transferredWords;
	metrics->waitEvents = pair->coordinator.waitEvents;
	return true;
}

void GBAReplicatedPairStop(struct GBAReplicatedPair* pair) {
	if (!pair || !pair->initialized || pair->stopped) {
		return;
	}
	for (int i = 1; i >= 0; --i) {
		struct GBAReplicatedPairPlayer* player = &pair->players[i];
		if (player->attached && player->core) {
			player->core->setPeripheral(
			    player->core, mPERIPH_GBA_LINK_PORT, NULL);
			GBASIOLockstepCoordinatorDetach(
			    &pair->coordinator, &player->driver);
			player->attached = false;
		}
	}
	for (unsigned i = 0; i < 2; ++i) {
		struct GBAReplicatedPairPlayer* player = &pair->players[i];
		if (player->core) {
			if (player->configInitialized) {
				mCoreConfigDeinit(&player->core->config);
				player->configInitialized = false;
			}
			player->core->deinit(player->core);
			player->core = NULL;
		}
		if (player->temporarySave) {
			player->temporarySave->close(player->temporarySave);
			player->temporarySave = NULL;
		}
		free(player->videoBuffer);
		player->videoBuffer = NULL;
	}
	GBASIOLockstepCoordinatorDeinit(&pair->coordinator);
	free(pair->bios);
	free(pair->rom);
	pair->bios = NULL;
	pair->rom = NULL;
	pair->installed = false;
	pair->stopped = true;
}
