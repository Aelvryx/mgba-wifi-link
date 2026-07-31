/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef GBA_SIO_NETPLAY_DRIVER_H
#define GBA_SIO_NETPLAY_DRIVER_H

#include <mgba-util/common.h>

#include <mgba/gba/interface.h>
#include <mgba/internal/gba/sio/netplay/timeline.h>

CXX_GUARD_START

enum GBASIONetplayBoundary {
	GBA_SIO_NETPLAY_BOUNDARY_NONE,
	GBA_SIO_NETPLAY_BOUNDARY_GRANT,
	GBA_SIO_NETPLAY_BOUNDARY_TRANSFER_START,
	GBA_SIO_NETPLAY_BOUNDARY_COMPLETION,
};

enum GBASIONetplayTransferState {
	GBA_SIO_NETPLAY_TRANSFER_IDLE,
	GBA_SIO_NETPLAY_TRANSFER_HOST_WAIT_READY,
	GBA_SIO_NETPLAY_TRANSFER_CLIENT_CATCHUP_START,
	GBA_SIO_NETPLAY_TRANSFER_CLIENT_WAIT_COMMIT,
	GBA_SIO_NETPLAY_TRANSFER_COMMITTED,
	GBA_SIO_NETPLAY_TRANSFER_HOST_WAIT_COMPLETION_READY,
	GBA_SIO_NETPLAY_TRANSFER_CLIENT_CATCHUP_COMPLETION,
	GBA_SIO_NETPLAY_TRANSFER_CLIENT_WAIT_DECISION,
	GBA_SIO_NETPLAY_TRANSFER_HOST_WAIT_DECISION_ACK,
	GBA_SIO_NETPLAY_TRANSFER_DECIDED,
	GBA_SIO_NETPLAY_TRANSFER_FINISHED,
};

struct GBASIONetplayTransfer {
	enum GBASIONetplayTransferState state;
	uint64_t sequence;
	uint64_t completionSequence;
	uint64_t startCycle;
	uint64_t completionCycle;
	uint16_t startSIOCNT;
	uint16_t localWord;
	uint16_t remoteWord;
	uint16_t pendingWords[4];
	uint16_t decidedWords[4];
	enum GBALinkTransferOutcome outcome;
	enum GBALinkReason abortReason;
	bool startEmitted;
	bool startAccepted;
	bool commitAccepted;
	bool abortPending;
	bool abortSent;
	bool decisionAccepted;
	bool decisionAckPending;
	bool localDeferredMode;
	bool remoteDeferredMode;
	uint64_t localDeferredCycle;
	uint64_t remoteDeferredCycle;
	enum GBALinkWireMode localDeferredWireMode;
	enum GBALinkWireMode remoteDeferredWireMode;
};

struct GBASIONetplayDriver {
	struct GBASIODriver d;
	struct GBASIO* sio;
	struct GBALinkSession* session;
	struct GBALinkTimeline timeline;
	struct GBALinkTimingClock timingClock;
	struct GBALinkTimingPolicy timingPolicy;
	struct mTimingEvent schedulerEvent;
	struct GBASIONetplayTransfer transfer;
	uint64_t lastRemoteTransferSequence;
	uint64_t lastRemoteCompletionSequence;
	uint64_t localCycle;
	uint64_t executionLimit;
	enum GBASIONetplayBoundary boundary;
	enum GBALinkWireMode committedLocalMode;
	enum GBALinkWireMode committedRemoteMode;
	enum GBALinkWireMode queuedLocalMode;
	uint64_t committedModeGeneration;
	uint64_t queuedLocalModeCycle;
	int topologicalPeerCount;
	bool timelineInitialized;
	bool attached;
	bool observable;
	bool jointlyReady;
	bool paused;
	bool executionLimitEnabled;
	bool resetting;
	bool unloading;
	bool processingEvent;
	bool queuedLocalModeIntent;
};

void GBASIONetplayDriverCreate(
    struct GBASIONetplayDriver* driver, struct GBASIO* sio,
    struct GBALinkSession* session);
void GBASIONetplayDriverSetTimingPolicy(
    struct GBASIONetplayDriver* driver,
    const struct GBALinkTimingPolicy* policy);
const struct GBALinkSessionCallbacks*
GBASIONetplayDriverSessionCallbacks(void);
bool GBASIONetplayDriverQuiescentSnapshot(
    struct GBASIONetplayDriver* driver,
    enum GBALinkWireMode* mode, uint64_t* localCycle);
bool GBASIONetplayDriverPump(
    struct GBASIONetplayDriver* driver, bool pollReceive);
bool GBASIONetplayDriverHostFrameBoundary(
    struct GBASIONetplayDriver* driver);
void GBASIONetplayAbortTransfer(
    struct GBASIONetplayDriver* driver,
    enum GBALinkReason reason);
void GBASIONetplayDriverCancel(
    struct GBASIONetplayDriver* driver,
    enum GBALinkReason reason);
void GBASIONetplayDriverDetach(
    struct GBASIONetplayDriver* driver);
bool GBASIONetplayDriverIsAttached(
    const struct GBASIONetplayDriver* driver);
bool GBASIONetplayDriverIsObservable(
    const struct GBASIONetplayDriver* driver);
bool GBASIONetplayDriverIsPaused(
    const struct GBASIONetplayDriver* driver);

CXX_GUARD_END

#endif
