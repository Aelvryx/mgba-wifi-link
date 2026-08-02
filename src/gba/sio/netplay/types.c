/* Copyright (c) 2026 mGBA contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/gba/sio/netplay/types.h>

const char* GBALinkDecodeStatusName(enum GBALinkDecodeStatus status) {
	switch (status) {
	case GBA_LINK_DECODE_OK: return "ok";
	case GBA_LINK_DECODE_NULL: return "null argument";
	case GBA_LINK_DECODE_TRUNCATED: return "truncated packet";
	case GBA_LINK_DECODE_OVERSIZED: return "oversized packet";
	case GBA_LINK_DECODE_MAGIC: return "invalid magic";
	case GBA_LINK_DECODE_VERSION: return "protocol version mismatch";
	case GBA_LINK_DECODE_TYPE: return "invalid message type";
	case GBA_LINK_DECODE_LENGTH: return "invalid packet length";
	case GBA_LINK_DECODE_RESERVED: return "nonzero reserved field";
	case GBA_LINK_DECODE_ROLE: return "message invalid for sender role";
	case GBA_LINK_DECODE_SESSION: return "invalid session ID";
	case GBA_LINK_DECODE_SEQUENCE: return "invalid packet sequence";
	case GBA_LINK_DECODE_FIELD: return "invalid message field";
	}
	return "unknown decode status";
}
