/* SPDX-License-Identifier: CC0-1.0
 *
 * Generates the smallest fixture needed to keep the spike core running.
 * It is not a functional game and must not be used for cable emulation tests. */
#include <stdio.h>
#include <string.h>

int main(int argc, char** argv) {
	unsigned char rom[32768];
	FILE* output;
	if (argc != 2) {
		fprintf(stderr, "usage: %s OUTPUT.gba\n", argv[0]);
		return 2;
	}
	memset(rom, 0, sizeof(rom));
	/* ARM `b .` keeps execution inside a quiet, deterministic loop. */
	rom[0] = 0xFE;
	rom[1] = 0xFF;
	rom[2] = 0xFF;
	rom[3] = 0xEA;
	rom[0xB2] = 0x96;
	output = fopen(argv[1], "wb");
	if (!output) {
		perror("fopen");
		return 1;
	}
	if (fwrite(rom, 1, sizeof(rom), output) != sizeof(rom)) {
		perror("fwrite");
		fclose(output);
		return 1;
	}
	if (fclose(output) != 0) {
		perror("fclose");
		return 1;
	}
	return 0;
}
