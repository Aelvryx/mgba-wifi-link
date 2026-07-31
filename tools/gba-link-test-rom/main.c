/* SPDX-License-Identifier: CC0-1.0 */
#include <stdint.h>

#define REG16(address) (*(volatile uint16_t*) (address))

#define REG_DISPCNT REG16(0x04000000)
#define REG_SIOMULTI0 REG16(0x04000120)
#define REG_SIOMULTI1 REG16(0x04000122)
#define REG_SIOMULTI2 REG16(0x04000124)
#define REG_SIOMULTI3 REG16(0x04000126)
#define REG_SIOCNT REG16(0x04000128)
#define REG_SIOMLT_SEND REG16(0x0400012A)
#define REG_RCNT REG16(0x04000134)
#define REG_IF REG16(0x04000202)

#define VRAM ((volatile uint16_t*) 0x06000000)

#define SIOCNT_SLAVE (1U << 2)
#define SIOCNT_READY (1U << 3)
#define SIOCNT_ID_MASK (3U << 4)
#define SIOCNT_ERROR (1U << 6)
#define SIOCNT_BUSY (1U << 7)
#define SIOCNT_MULTI (2U << 12)
#define SIOCNT_IRQ (1U << 14)
#define IRQ_SERIAL (1U << 7)

#define RESULT_MAGIC 0x31544B4CU /* "LKT1" */
#define RESULT_VERSION 1
#define STATUS_BOOT 1
#define STATUS_WAITING 2
#define STATUS_RUNNING 3
#define STATUS_PASS 4
#define STATUS_FAIL 0x80000000U

#define TRANSFERS_PER_BAUD 4
#define SPIN_LIMIT 0x02000000U
#define PRIMARY_START_SETTLE_CYCLES 4096U

struct LinkTestResult {
	uint32_t magic;
	uint32_t version;
	uint32_t status;
	uint32_t playerId;
	uint32_t observableAttachments;
	uint32_t effectiveParticipants;
	uint32_t transfers;
	uint32_t baudMask;
	uint32_t dataErrors;
	uint32_t missedIrqs;
	uint32_t duplicateIrqs;
	uint32_t busyObservations;
	uint32_t timeouts;
	uint16_t lastSIOCNT;
	uint16_t lastRCNT;
	uint16_t expected[4];
	uint16_t received[4];
	uint32_t completionSpins[4];
};

static volatile struct LinkTestResult* const result =
    (volatile struct LinkTestResult*) 0x02000000;

static void fillScreen(uint16_t color) {
	for (unsigned i = 0; i < 240U * 160U; ++i) {
		VRAM[i] = color;
	}
}

static void fail(uint32_t code) {
	result->status = STATUS_FAIL | code;
	result->lastSIOCNT = REG_SIOCNT;
	result->lastRCNT = REG_RCNT;
	fillScreen(0x001F);
	for (;;) {
	}
}

static uint16_t outgoingWord(unsigned playerId, unsigned baud, unsigned transfer) {
	return (uint16_t) (
	    ((playerId + 1U) << 12) |
	    (baud << 4) | transfer);
}

void main(void) {
	REG_DISPCNT = 0x0403; /* Mode 3, BG2. */
	fillScreen(0x03E0);

	for (unsigned i = 0;
	     i < sizeof(*result) / sizeof(uint32_t); ++i) {
		((volatile uint32_t*) result)[i] = 0;
	}
	result->magic = RESULT_MAGIC;
	result->version = RESULT_VERSION;
	result->status = STATUS_BOOT;

	REG_RCNT = 0;
	REG_SIOCNT = SIOCNT_MULTI | SIOCNT_IRQ;
	result->status = STATUS_WAITING;
	while (!(REG_SIOCNT & SIOCNT_READY) ||
	       ((REG_SIOCNT & SIOCNT_SLAVE) &&
	        !(REG_SIOCNT & SIOCNT_ID_MASK))) {
		/*
		 * Disconnected and ready cables both expose a pulled-up ready
		 * line.  Wait for the role lines too: primary is not a slave,
		 * while the supported secondary has device ID one. Attachment
		 * is user-driven, so menu-navigation time is not a failure.
		 */
	}

	unsigned playerId =
	    (REG_SIOCNT & SIOCNT_ID_MASK) >> 4;
	if (playerId > 1) {
		fail(2);
	}
	result->playerId = playerId;
	result->observableAttachments = 1;
	result->status = STATUS_RUNNING;

	do {
		for (unsigned baud = 0; baud < 4; ++baud) {
			for (unsigned transfer = 0;
			     transfer < TRANSFERS_PER_BAUD; ++transfer) {
			uint32_t spins;
			uint16_t local =
			    outgoingWord(playerId, baud, transfer);
			uint16_t control =
			    SIOCNT_MULTI | SIOCNT_IRQ | baud;
			REG_IF = IRQ_SERIAL;
			REG_SIOMLT_SEND = local;
			REG_SIOCNT = control;

			if (playerId == 0) {
				/*
				 * A MULTI primary samples the secondary send
				 * register when it asserts START. Give the
				 * secondary program an emulated-cycle window to
				 * stage this transaction's word, as real link
				 * software does with its own ready protocol.
				 */
				for (volatile unsigned settle = 0;
				     settle <
				         PRIMARY_START_SETTLE_CYCLES;
				     ++settle) {
					__asm__ volatile("nop");
				}
				REG_SIOCNT = control | SIOCNT_BUSY;
			}

			spins = SPIN_LIMIT;
			while (!(REG_SIOCNT & SIOCNT_BUSY) && --spins) {
			}
			if (!spins) {
				++result->timeouts;
				fail(3);
			}
			++result->busyObservations;

			spins = SPIN_LIMIT;
			while ((REG_SIOCNT & SIOCNT_BUSY) && --spins) {
			}
			result->completionSpins[baud] +=
			    SPIN_LIMIT - spins;
			if (!spins) {
				++result->timeouts;
				fail(4);
			}

			result->lastSIOCNT = REG_SIOCNT;
			result->lastRCNT = REG_RCNT;
			result->received[0] = REG_SIOMULTI0;
			result->received[1] = REG_SIOMULTI1;
			result->received[2] = REG_SIOMULTI2;
			result->received[3] = REG_SIOMULTI3;
			result->expected[0] =
			    outgoingWord(0, baud, transfer);
			result->expected[1] =
			    outgoingWord(1, baud, transfer);
			result->expected[2] = 0xFFFF;
			result->expected[3] = 0xFFFF;

			for (unsigned i = 0; i < 4; ++i) {
				if (result->received[i] !=
				    result->expected[i]) {
					++result->dataErrors;
				}
			}
			if (REG_SIOCNT & SIOCNT_ERROR) {
				++result->dataErrors;
			}
			if (REG_SIOCNT & SIOCNT_BUSY) {
				++result->dataErrors;
			}
			if (!(REG_SIOCNT & SIOCNT_READY)) {
				++result->dataErrors;
			}
			if (!!(REG_SIOCNT & SIOCNT_SLAVE) !=
			    !!playerId) {
				++result->dataErrors;
			}
			if (((REG_SIOCNT & SIOCNT_ID_MASK) >> 4) !=
			    playerId) {
				++result->dataErrors;
			}
			if (!(REG_IF & IRQ_SERIAL)) {
				++result->missedIrqs;
			}
			REG_IF = IRQ_SERIAL;
			for (volatile unsigned settle = 0;
			     settle < 32; ++settle) {
				__asm__ volatile("nop");
			}
			if (REG_IF & IRQ_SERIAL) {
				++result->duplicateIrqs;
				REG_IF = IRQ_SERIAL;
			}
				++result->transfers;
				result->baudMask |= 1U << baud;
				if (result->dataErrors ||
				    result->missedIrqs ||
				    result->duplicateIrqs) {
					fail(5);
				}
			}
		}
#ifdef GBA_LINK_CONTINUOUS
	} while (1);
#else
	} while (0);

	result->effectiveParticipants =
	    result->received[0] != 0xFFFF &&
	            result->received[1] != 0xFFFF
	        ? 2
	        : 1;
	if (result->dataErrors || result->missedIrqs ||
	    result->duplicateIrqs ||
	    result->transfers != 4U * TRANSFERS_PER_BAUD ||
	    result->baudMask != 0xF ||
	    result->effectiveParticipants != 2) {
		fail(5);
	}
	result->status = STATUS_PASS;
	fillScreen(playerId ? 0x7C00 : 0x03FF);
	for (;;) {
	}
#endif
}
