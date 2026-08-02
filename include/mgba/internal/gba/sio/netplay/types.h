/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef GBA_SIO_NETPLAY_TYPES_H
#define GBA_SIO_NETPLAY_TYPES_H

#include <mgba-util/common.h>

CXX_GUARD_START

#define GBA_LINK_MAX_COPIED_PACKETS 64
#define GBA_LINK_ROM_SHA1_SIZE 20

enum GBALinkRole {
	GBA_LINK_ROLE_HOST = 0,
	GBA_LINK_ROLE_CLIENT = 1,
};

/*
 * Generic transport reasons retain their established numeric values so
 * protocol-v2 diagnostics and callback behaviour do not change when the
 * retired protocol-v1 owner is removed.
 */
enum GBALinkReason {
	GBA_LINK_REASON_PROTOCOL_MISMATCH = 1,
	GBA_LINK_REASON_CAPABILITY_MISMATCH = 2,
	GBA_LINK_REASON_ROM_MISMATCH = 3,
	GBA_LINK_REASON_MALFORMED_PACKET = 9,
	GBA_LINK_REASON_TRANSPORT_STOP = 10,
	GBA_LINK_REASON_PEER_DETACH = 11,
	GBA_LINK_REASON_ATTACHMENT_TIMEOUT = 13,
	GBA_LINK_REASON_QUEUE_EXHAUSTED = 22,
	GBA_LINK_REASON_OVERSIZED_PACKET = 23,
	GBA_LINK_REASON_SEND_FAILURE = 24,
	GBA_LINK_REASON_INVALID_TRANSITION = 26,
	GBA_LINK_REASON_RESET = 27,
	GBA_LINK_REASON_UNLOAD = 28,
	GBA_LINK_REASON_USER_DISCONNECT = 29,
};

enum GBALinkDecodeStatus {
	GBA_LINK_DECODE_OK = 0,
	GBA_LINK_DECODE_NULL,
	GBA_LINK_DECODE_TRUNCATED,
	GBA_LINK_DECODE_OVERSIZED,
	GBA_LINK_DECODE_MAGIC,
	GBA_LINK_DECODE_VERSION,
	GBA_LINK_DECODE_TYPE,
	GBA_LINK_DECODE_LENGTH,
	GBA_LINK_DECODE_RESERVED,
	GBA_LINK_DECODE_ROLE,
	GBA_LINK_DECODE_SESSION,
	GBA_LINK_DECODE_SEQUENCE,
	GBA_LINK_DECODE_FIELD,
};

const char* GBALinkDecodeStatusName(enum GBALinkDecodeStatus status);

CXX_GUARD_END

#endif
