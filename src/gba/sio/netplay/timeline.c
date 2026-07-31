/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/gba/sio/netplay/timeline.h>

static bool _validMode(enum GBALinkWireMode mode) {
	switch (mode) {
	case GBA_LINK_MODE_NORMAL_8:
	case GBA_LINK_MODE_NORMAL_32:
	case GBA_LINK_MODE_MULTI:
	case GBA_LINK_MODE_UART:
	case GBA_LINK_MODE_GPIO:
	case GBA_LINK_MODE_JOYBUS:
		return true;
	}
	return false;
}

bool GBALinkClockLocalToCable(
    const struct GBALinkClockMapping* mapping,
    uint64_t localCycle, uint64_t* cableCycle) {
	if (!mapping || !cableCycle ||
	    localCycle < mapping->localAnchor) {
		return false;
	}
	uint64_t delta = localCycle - mapping->localAnchor;
	if (UINT64_MAX - mapping->cableAnchor < delta) {
		return false;
	}
	*cableCycle = mapping->cableAnchor + delta;
	return true;
}

bool GBALinkClockCableToLocal(
    const struct GBALinkClockMapping* mapping,
    uint64_t cableCycle, uint64_t* localCycle) {
	if (!mapping || !localCycle ||
	    cableCycle < mapping->cableAnchor) {
		return false;
	}
	uint64_t delta = cableCycle - mapping->cableAnchor;
	if (UINT64_MAX - mapping->localAnchor < delta) {
		return false;
	}
	*localCycle = mapping->localAnchor + delta;
	return true;
}

void GBALinkClockRebase(
    struct GBALinkClockMapping* mapping,
    uint64_t localCycle, uint64_t cableCycle) {
	mapping->localAnchor = localCycle;
	mapping->cableAnchor = cableCycle;
}

void GBALinkTimingClockInit(
    struct GBALinkTimingClock* clock, int32_t rawCycle) {
	if (!clock) {
		return;
	}
	uint32_t raw = (uint32_t) rawCycle;
	clock->cycle = raw;
	clock->rawCycle = raw;
	clock->initialized = true;
}

bool GBALinkTimingClockUpdate(
    struct GBALinkTimingClock* clock, int32_t rawCycle,
    uint64_t* localCycle) {
	if (!clock || !localCycle) {
		return false;
	}
	if (!clock->initialized) {
		GBALinkTimingClockInit(clock, rawCycle);
		*localCycle = clock->cycle;
		return true;
	}
	uint32_t raw = (uint32_t) rawCycle;
	uint32_t elapsed = raw - clock->rawCycle;
	if (UINT64_MAX - clock->cycle < elapsed) {
		return false;
	}
	clock->cycle += elapsed;
	clock->rawCycle = raw;
	*localCycle = clock->cycle;
	return true;
}

void GBALinkTimingPolicyInit(
    struct GBALinkTimingPolicy* policy) {
	if (!policy) {
		return;
	}
	policy->localSchedulerQuantum = 4096;
	policy->candidateHorizonCycles = 280896;
	policy->healthBarrierCycles = 0;
}

bool GBALinkTimingPolicyValidate(
    const struct GBALinkTimingPolicy* policy) {
	return policy && policy->localSchedulerQuantum &&
	       policy->localSchedulerQuantum <= INT32_MAX &&
	       policy->candidateHorizonCycles &&
	       policy->candidateHorizonCycles <= INT32_MAX &&
	       policy->healthBarrierCycles <= INT32_MAX;
}

static void _setPaused(
    struct GBALinkTimeline* timeline, bool paused) {
	if (timeline->paused == paused) {
		return;
	}
	timeline->paused = paused;
	if (timeline->callbacks->setPaused) {
		timeline->callbacks->setPaused(
		    timeline->callbackContext, paused);
	}
}

static void _setExecutionLimit(
    struct GBALinkTimeline* timeline, bool enabled,
    uint64_t cableCycle) {
	if (!timeline->callbacks->setExecutionLimit) {
		return;
	}
	uint64_t localCycle = 0;
	if (enabled &&
	    !GBALinkClockCableToLocal(
	        &timeline->clock, cableCycle, &localCycle)) {
		GBALinkSessionFail(
		    timeline->session, GBA_LINK_REASON_INVALID_TRANSITION,
		    "execution grant cannot map to local cycle");
		return;
	}
	timeline->callbacks->setExecutionLimit(
	    timeline->callbackContext, enabled, localCycle);
}

static void _rebase(
    struct GBALinkTimeline* timeline, uint64_t cableCycle) {
	uint64_t localCycle =
	    timeline->callbacks->localCycle(timeline->callbackContext);
	GBALinkClockRebase(&timeline->clock, localCycle, cableCycle);
	timeline->currentCableCycle = cableCycle;
	if (timeline->callbacks->rebaseCableClock) {
		timeline->callbacks->rebaseCableClock(
		    timeline->callbackContext, localCycle, cableCycle);
	}
}

static bool _sendModeAck(struct GBALinkTimeline* timeline) {
	struct GBALinkPacket packet;
	memset(&packet, 0, sizeof(packet));
	packet.header.type = GBA_LINK_MESSAGE_MODE_ACK;
	packet.payload.modeAck.modeGeneration =
	    timeline->modeGeneration;
	packet.payload.modeAck.commitCycle =
	    timeline->modeCommitCycle;
	return GBALinkSessionSendRuntime(
	    timeline->session, &packet, GBA_LINK_DEADLINE_MODE);
}

static void _installPendingMode(
    struct GBALinkTimeline* timeline) {
	if (timeline->localRole == GBA_LINK_ROLE_HOST) {
		timeline->localMode = timeline->pendingHostWireMode;
		timeline->remoteMode = timeline->pendingClientWireMode;
	} else {
		timeline->localMode = timeline->pendingClientWireMode;
		timeline->remoteMode = timeline->pendingHostWireMode;
	}
	timeline->committedModeGeneration = timeline->modeGeneration;
	timeline->currentCableCycle = timeline->modeCommitCycle;
	timeline->modeBarrier = false;
	timeline->pendingHostMode = false;
	GBALinkSessionSetDeadline(
	    timeline->session, GBA_LINK_DEADLINE_NONE);
	if (timeline->callbacks->modeCommitted) {
		timeline->callbacks->modeCommitted(
		    timeline->callbackContext,
		    timeline->committedModeGeneration,
		    timeline->localMode, timeline->remoteMode,
		    timeline->pendingJointlyReady);
	}
	if (timeline->localRole == GBA_LINK_ROLE_HOST) {
		_setPaused(timeline, false);
	}
}

static bool _startModeBarrier(
    struct GBALinkTimeline* timeline, uint64_t generation,
    uint64_t commitCycle, enum GBALinkWireMode hostMode,
    enum GBALinkWireMode clientMode) {
	if (timeline->localRole != GBA_LINK_ROLE_HOST ||
	    timeline->modeBarrier ||
	    generation != timeline->committedModeGeneration + 1 ||
	    commitCycle != timeline->currentCableCycle) {
		return false;
	}
	timeline->modeBarrier = true;
	timeline->modeGeneration = generation;
	timeline->modeCommitCycle = commitCycle;
	timeline->pendingHostWireMode = hostMode;
	timeline->pendingClientWireMode = clientMode;
	timeline->pendingJointlyReady =
	    hostMode == GBA_LINK_MODE_MULTI &&
	    clientMode == GBA_LINK_MODE_MULTI;
	timeline->localModeAck = true;
	timeline->remoteModeAck = false;
	_setPaused(timeline, true);

	struct GBALinkPacket commit;
	memset(&commit, 0, sizeof(commit));
	commit.header.type = GBA_LINK_MESSAGE_MODE_COMMIT;
	commit.payload.modeCommit.modeGeneration = generation;
	commit.payload.modeCommit.commitCycle = commitCycle;
	commit.payload.modeCommit.hostMode = hostMode;
	commit.payload.modeCommit.clientMode = clientMode;
	commit.payload.modeCommit.jointlyReady =
	    timeline->pendingJointlyReady;
	if (!GBALinkSessionSendRuntime(
	        timeline->session, &commit,
	        GBA_LINK_DEADLINE_MODE)) {
		return false;
	}
	return _sendModeAck(timeline);
}

bool GBALinkTimelineInit(
    struct GBALinkTimeline* timeline,
    struct GBALinkSession* session,
    const struct GBALinkTimelineCallbacks* callbacks,
    void* callbackContext, uint64_t localCycle,
    uint64_t attachCycle, enum GBALinkWireMode localMode,
    enum GBALinkWireMode remoteMode) {
	if (!timeline || !session || !callbacks ||
	    !callbacks->localCycle || !_validMode(localMode) ||
	    !_validMode(remoteMode) || !session->sessionId) {
		return false;
	}
	memset(timeline, 0, sizeof(*timeline));
	timeline->session = session;
	timeline->callbacks = callbacks;
	timeline->callbackContext = callbackContext;
	timeline->localRole = session->localRole;
	timeline->clock.localAnchor = localCycle;
	timeline->clock.cableAnchor = attachCycle;
	timeline->currentCableCycle = attachCycle;
	timeline->localMode = localMode;
	timeline->remoteMode = remoteMode;
	timeline->paused = true;
	return true;
}

bool GBALinkTimelineHostCommitInitialModes(
    struct GBALinkTimeline* timeline) {
	if (!timeline ||
	    timeline->localRole != GBA_LINK_ROLE_HOST ||
	    timeline->session->state != GBA_LINK_SESSION_READY ||
	    timeline->committedModeGeneration ||
	    !timeline->session->initialModeGeneration) {
		return false;
	}
	return _startModeBarrier(
	    timeline, timeline->session->initialModeGeneration,
	    timeline->currentCableCycle,
	    timeline->localMode, timeline->remoteMode);
}

bool GBALinkTimelineHostCommitModesAtCurrentBoundary(
    struct GBALinkTimeline* timeline, uint64_t generation,
    enum GBALinkWireMode hostMode,
    enum GBALinkWireMode clientMode) {
	if (!timeline || !_validMode(hostMode) ||
	    !_validMode(clientMode)) {
		return false;
	}
	return _startModeBarrier(
	    timeline, generation,
	    timeline->currentCableCycle,
	    hostMode, clientMode);
}

bool GBALinkTimelineHostReachHorizon(
    struct GBALinkTimeline* timeline, uint64_t horizon) {
	if (!timeline ||
	    timeline->localRole != GBA_LINK_ROLE_HOST ||
	    timeline->session->state != GBA_LINK_SESSION_READY ||
	    timeline->grantOutstanding || timeline->modeBarrier ||
	    horizon < timeline->currentCableCycle) {
		return false;
	}
	uint64_t reachedCycle = 0;
	if (!GBALinkClockLocalToCable(
	        &timeline->clock,
	        timeline->callbacks->localCycle(
	            timeline->callbackContext),
	        &reachedCycle) ||
	    reachedCycle != horizon) {
		return false;
	}
	uint64_t sequence;
	if (!GBALinkSequenceTake(
	        &timeline->session->sequences,
	        GBA_LINK_SEQUENCE_GRANT, &sequence)) {
		GBALinkSessionFail(
		    timeline->session, GBA_LINK_REASON_SEQUENCE_EXHAUSTED,
		    "execution grant sequence exhausted");
		return false;
	}
	timeline->currentCableCycle = horizon;
	timeline->grantSequence = sequence;
	timeline->grantHorizon = horizon;
	timeline->grantOutstanding = true;
	_setPaused(timeline, true);

	struct GBALinkPacket packet;
	memset(&packet, 0, sizeof(packet));
	packet.header.type = GBA_LINK_MESSAGE_EXECUTION_GRANT;
	packet.payload.grant.grantSequence = sequence;
	packet.payload.grant.horizon = horizon;
	return GBALinkSessionSendRuntime(
	    timeline->session, &packet, GBA_LINK_DEADLINE_GRANT);
}

bool GBALinkTimelineClientReachGrant(
    struct GBALinkTimeline* timeline, uint64_t localCycle) {
	if (!timeline ||
	    timeline->localRole != GBA_LINK_ROLE_CLIENT ||
	    !timeline->grantOutstanding || timeline->modeBarrier) {
		return false;
	}
	uint64_t cableCycle;
	if (!GBALinkClockLocalToCable(
	        &timeline->clock, localCycle, &cableCycle) ||
	    cableCycle != timeline->grantHorizon) {
		return false;
	}
	timeline->currentCableCycle = cableCycle;
	_setExecutionLimit(timeline, false, 0);
	_setPaused(timeline, true);

	struct GBALinkPacket packet;
	memset(&packet, 0, sizeof(packet));
	packet.header.type = GBA_LINK_MESSAGE_GRANT_ACK;
	packet.payload.grant.grantSequence =
	    timeline->grantSequence;
	packet.payload.grant.horizon = timeline->grantHorizon;
	timeline->grantOutstanding = false;
	timeline->lastGrantSequence = timeline->grantSequence;
	return GBALinkSessionSendRuntime(
	    timeline->session, &packet, GBA_LINK_DEADLINE_GRANT);
}

static bool _handleGrant(
    struct GBALinkTimeline* timeline,
    const struct GBALinkPacket* packet) {
	if (timeline->localRole != GBA_LINK_ROLE_CLIENT ||
	    timeline->grantOutstanding || timeline->modeBarrier ||
	    packet->payload.grant.grantSequence !=
	        timeline->lastGrantSequence + 1 ||
	    packet->payload.grant.horizon <
	        timeline->currentCableCycle) {
		return false;
	}
	timeline->grantSequence =
	    packet->payload.grant.grantSequence;
	timeline->grantHorizon = packet->payload.grant.horizon;
	timeline->grantOutstanding = true;
	_setExecutionLimit(
	    timeline, true, timeline->grantHorizon);
	_setPaused(timeline, false);
	GBALinkSessionSetDeadline(
	    timeline->session, GBA_LINK_DEADLINE_GRANT);
	return true;
}

static bool _handleGrantAck(
    struct GBALinkTimeline* timeline,
    const struct GBALinkPacket* packet) {
	if (timeline->localRole != GBA_LINK_ROLE_HOST ||
	    !timeline->grantOutstanding ||
	    packet->payload.grant.grantSequence !=
	        timeline->grantSequence ||
	    packet->payload.grant.horizon !=
	        timeline->grantHorizon) {
		return false;
	}
	timeline->grantOutstanding = false;
	timeline->lastGrantSequence = timeline->grantSequence;
	GBALinkSessionSetDeadline(
	    timeline->session, GBA_LINK_DEADLINE_NONE);
	if (timeline->pendingHostMode) {
		return _startModeBarrier(
		    timeline, timeline->committedModeGeneration + 1,
		    timeline->currentCableCycle, timeline->localMode,
		    timeline->remoteMode);
	}
	_setPaused(timeline, false);
	return true;
}

bool GBALinkTimelineLocalModeWrite(
    struct GBALinkTimeline* timeline,
    enum GBALinkWireMode mode, uint64_t localCycle) {
	if (!timeline || !_validMode(mode) ||
	    timeline->modeBarrier ||
	    mode == timeline->localMode) {
		return false;
	}
	uint64_t cableCycle;
	if (!GBALinkClockLocalToCable(
	        &timeline->clock, localCycle, &cableCycle)) {
		return false;
	}
	if (timeline->localRole == GBA_LINK_ROLE_HOST) {
		if (timeline->grantOutstanding ||
		    cableCycle < timeline->currentCableCycle) {
			return false;
		}
		timeline->localMode = mode;
		_setPaused(timeline, true);
		timeline->pendingHostMode = true;
		return GBALinkTimelineHostReachHorizon(
		    timeline, cableCycle);
	}
	if (!timeline->grantOutstanding ||
	    cableCycle > timeline->grantHorizon) {
		return false;
	}
	timeline->localMode = mode;
	_setPaused(timeline, true);
	struct GBALinkPacket packet;
	memset(&packet, 0, sizeof(packet));
	packet.header.type = GBA_LINK_MESSAGE_MODE_INTENT;
	packet.payload.modeIntent.modeGeneration =
	    timeline->committedModeGeneration + 1;
	packet.payload.modeIntent.localCycle = cableCycle;
	packet.payload.modeIntent.localMode = mode;
	packet.payload.modeIntent.deferred = false;
	timeline->grantOutstanding = false;
	timeline->lastGrantSequence = timeline->grantSequence;
	_setExecutionLimit(timeline, false, 0);
	return GBALinkSessionSendRuntime(
	    timeline->session, &packet, GBA_LINK_DEADLINE_MODE);
}

static bool _handleModeIntent(
    struct GBALinkTimeline* timeline,
    const struct GBALinkPacket* packet) {
	if (timeline->localRole != GBA_LINK_ROLE_HOST ||
	    !timeline->grantOutstanding || timeline->modeBarrier ||
	    packet->payload.modeIntent.deferred ||
	    packet->payload.modeIntent.modeGeneration !=
	        timeline->committedModeGeneration + 1 ||
	    packet->payload.modeIntent.localCycle >
	        timeline->grantHorizon) {
		return false;
	}
	timeline->grantOutstanding = false;
	timeline->lastGrantSequence = timeline->grantSequence;
	timeline->remoteMode = packet->payload.modeIntent.localMode;
	timeline->pendingHostMode = false;
	return _startModeBarrier(
	    timeline, packet->payload.modeIntent.modeGeneration,
	    timeline->currentCableCycle, timeline->localMode,
	    timeline->remoteMode);
}

static bool _handleModeCommit(
    struct GBALinkTimeline* timeline,
    const struct GBALinkPacket* packet) {
	if (timeline->localRole != GBA_LINK_ROLE_CLIENT ||
	    timeline->modeBarrier ||
	    packet->payload.modeCommit.modeGeneration !=
	        timeline->committedModeGeneration + 1 ||
	    packet->payload.modeCommit.commitCycle <
	        timeline->currentCableCycle) {
		return false;
	}
	timeline->modeBarrier = true;
	timeline->modeGeneration =
	    packet->payload.modeCommit.modeGeneration;
	timeline->modeCommitCycle =
	    packet->payload.modeCommit.commitCycle;
	timeline->pendingHostWireMode =
	    packet->payload.modeCommit.hostMode;
	timeline->pendingClientWireMode =
	    packet->payload.modeCommit.clientMode;
	timeline->pendingJointlyReady =
	    packet->payload.modeCommit.jointlyReady;
	timeline->localModeAck = true;
	timeline->remoteModeAck = false;
	_rebase(timeline, timeline->modeCommitCycle);
	_setPaused(timeline, true);
	return _sendModeAck(timeline);
}

static bool _handleModeAck(
    struct GBALinkTimeline* timeline,
    const struct GBALinkPacket* packet) {
	if (!timeline->modeBarrier ||
	    packet->payload.modeAck.modeGeneration !=
	        timeline->modeGeneration ||
	    packet->payload.modeAck.commitCycle !=
	        timeline->modeCommitCycle) {
		return false;
	}
	timeline->remoteModeAck = true;
	if (timeline->localModeAck &&
	    timeline->remoteModeAck) {
		_installPendingMode(timeline);
	}
	return true;
}

bool GBALinkTimelineHandlePacket(
    struct GBALinkTimeline* timeline,
    const struct GBALinkPacket* packet) {
	if (!timeline || !packet ||
	    packet->header.sessionId != timeline->session->sessionId) {
		return false;
	}
	switch (packet->header.type) {
	case GBA_LINK_MESSAGE_EXECUTION_GRANT:
		return _handleGrant(timeline, packet);
	case GBA_LINK_MESSAGE_GRANT_ACK:
		return _handleGrantAck(timeline, packet);
	case GBA_LINK_MESSAGE_MODE_INTENT:
		return _handleModeIntent(timeline, packet);
	case GBA_LINK_MESSAGE_MODE_COMMIT:
		return _handleModeCommit(timeline, packet);
	case GBA_LINK_MESSAGE_MODE_ACK:
		return _handleModeAck(timeline, packet);
	default:
		return false;
	}
}
