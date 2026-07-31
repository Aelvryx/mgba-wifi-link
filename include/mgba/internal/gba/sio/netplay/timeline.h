/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef GBA_SIO_NETPLAY_TIMELINE_H
#define GBA_SIO_NETPLAY_TIMELINE_H

#include <mgba-util/common.h>

#include <mgba/internal/gba/sio/netplay/session.h>

CXX_GUARD_START

struct GBALinkClockMapping {
	uint64_t localAnchor;
	uint64_t cableAnchor;
};

struct GBALinkTimingClock {
	uint64_t cycle;
	uint32_t rawCycle;
	bool initialized;
};

struct GBALinkTimingPolicy {
	uint32_t localSchedulerQuantum;
	uint32_t candidateHorizonCycles;
	uint32_t healthBarrierCycles;
};

struct GBALinkTimelineCallbacks {
	uint64_t (*localCycle)(void* context);
	void (*setPaused)(void* context, bool paused);
	void (*setExecutionLimit)(
	    void* context, bool enabled, uint64_t localCycle);
	void (*rebaseCableClock)(
	    void* context, uint64_t localCycle, uint64_t cableCycle);
	void (*modeCommitted)(
	    void* context, uint64_t generation,
	    enum GBALinkWireMode localMode,
	    enum GBALinkWireMode remoteMode, bool jointlyReady);
};

struct GBALinkTimeline {
	struct GBALinkSession* session;
	const struct GBALinkTimelineCallbacks* callbacks;
	void* callbackContext;
	enum GBALinkRole localRole;
	struct GBALinkClockMapping clock;
	uint64_t currentCableCycle;
	uint64_t committedModeGeneration;
	uint64_t lastGrantSequence;
	uint64_t grantSequence;
	uint64_t grantHorizon;
	bool grantOutstanding;
	bool paused;
	bool modeBarrier;
	bool localModeAck;
	bool remoteModeAck;
	bool pendingHostMode;
	uint64_t modeGeneration;
	uint64_t modeCommitCycle;
	enum GBALinkWireMode localMode;
	enum GBALinkWireMode remoteMode;
	enum GBALinkWireMode pendingHostWireMode;
	enum GBALinkWireMode pendingClientWireMode;
	bool pendingJointlyReady;
};

bool GBALinkClockLocalToCable(
    const struct GBALinkClockMapping* mapping,
    uint64_t localCycle, uint64_t* cableCycle);
bool GBALinkClockCableToLocal(
    const struct GBALinkClockMapping* mapping,
    uint64_t cableCycle, uint64_t* localCycle);
void GBALinkClockRebase(
    struct GBALinkClockMapping* mapping,
    uint64_t localCycle, uint64_t cableCycle);
void GBALinkTimingClockInit(
    struct GBALinkTimingClock* clock, int32_t rawCycle);
bool GBALinkTimingClockUpdate(
    struct GBALinkTimingClock* clock, int32_t rawCycle,
    uint64_t* localCycle);
void GBALinkTimingPolicyInit(
    struct GBALinkTimingPolicy* policy);
bool GBALinkTimingPolicyValidate(
    const struct GBALinkTimingPolicy* policy);

bool GBALinkTimelineInit(
    struct GBALinkTimeline* timeline,
    struct GBALinkSession* session,
    const struct GBALinkTimelineCallbacks* callbacks,
    void* callbackContext, uint64_t localCycle,
    uint64_t attachCycle, enum GBALinkWireMode localMode,
    enum GBALinkWireMode remoteMode);
bool GBALinkTimelineHostCommitInitialModes(
    struct GBALinkTimeline* timeline);
bool GBALinkTimelineHostCommitModesAtCurrentBoundary(
    struct GBALinkTimeline* timeline, uint64_t generation,
    enum GBALinkWireMode hostMode,
    enum GBALinkWireMode clientMode);
bool GBALinkTimelineHostReachHorizon(
    struct GBALinkTimeline* timeline, uint64_t horizon);
bool GBALinkTimelineClientReachGrant(
    struct GBALinkTimeline* timeline, uint64_t localCycle);
bool GBALinkTimelineLocalModeWrite(
    struct GBALinkTimeline* timeline,
    enum GBALinkWireMode mode, uint64_t localCycle);
bool GBALinkTimelineHandlePacket(
    struct GBALinkTimeline* timeline,
    const struct GBALinkPacket* packet);

CXX_GUARD_END

#endif
