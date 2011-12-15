/*
 *  writeloader.c
 *
 *  Copyright (C) 2011 ISEE 2007, SL
 *  Author: Javier Martinez Canillas <martinez.javier@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * Overview:
 *   Writes a loader binary to a NAND flash memory device and calculates
 *   1-bit Hamming ECC codes to fill the MTD's out-of-band (oob) area
 *   independently of the ECC technique implemented on the NAND driver.
 *   This is a workaround required for TI ARM OMAP DM3730 ROM boot to load.
 *
 */

#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <getopt.h>
#include <dirent.h>

#include <linux/types.h>
#include "mtd/mtd-user.h"

#include <fcntl.h>
#include <string.h>
#include <math.h>

#define PROGRAM "writeloader"
#define VERSION "version: 0.01"

#define SECTOR_SIZE 512
#define PAGE_SIZE   2048

#define EVEN_WHOLE  0xff
#define EVEN_HALF   0x0f
#define ODD_HALF    0xf0
#define EVEN_FOURTH 0x33
#define ODD_FOURTH  0xcc
#define EVEN_EIGHTH 0x55
#define ODD_EIGHTH  0xaa
#define ODD_WHOLE   0x00

char *input_file;
char *output_file;

const char *path = "/sys/bus/platform/devices/";

enum flash {NAND, ONENAND};

int flash_type = NAND;

unsigned char swap(unsigned char c)
{
	return (c >> 4) | (c << 4);
}

unsigned char calc_bitwise_parity(unsigned char val, unsigned char mask)
{
	unsigned char result = 0, byte_mask;
	int i;

	byte_mask = mask;

	for (i = 0; i < 8; i++) {
		if ((byte_mask & 0x1) != 0)
			result ^= (val & 1);
		byte_mask >>= 1;
		val >>= 1;
	}
	return result & 0x1;
}

unsigned char calc_row_parity_bits(unsigned char byte_parities[], int even,
				   int chunk_size)
{
	unsigned char result = 0;
	int i, j;

	for (i = (even ? 0 : chunk_size);
	     i < SECTOR_SIZE;
	     i += (2 * chunk_size)) {
		for (j = 0; j < chunk_size; j++)
			result ^= byte_parities[i + j];
	}
	return result & 0x1;
}

/* Based on Texas Instrument's C# GenECC application
   (sourceforge.net/projects/dvflashutils) */
unsigned int nand_calculate_ecc(unsigned char *buf)
{
	unsigned short odd_result = 0, even_result = 0;
	unsigned char bit_parities = 0;
	unsigned char byte_parities[SECTOR_SIZE];
	int i;
	unsigned char val;

	for (i = 0; i < SECTOR_SIZE; i++)
		bit_parities ^= buf[i];

	even_result |= ((calc_bitwise_parity(bit_parities,
					     EVEN_HALF) << 2) |
			(calc_bitwise_parity(bit_parities,
					     EVEN_FOURTH) << 1) |
			(calc_bitwise_parity(bit_parities,
					     EVEN_EIGHTH) << 0));

	odd_result |= ((calc_bitwise_parity(bit_parities,
					    ODD_HALF) << 2) |
		       (calc_bitwise_parity(bit_parities,
					    ODD_FOURTH) << 1) |
		       (calc_bitwise_parity(bit_parities,
					    ODD_EIGHTH) << 0));

	for (i = 0; i < SECTOR_SIZE; i++)
		byte_parities[i] = calc_bitwise_parity(buf[i], EVEN_WHOLE);

	for (i = 0; i < (int) log2(SECTOR_SIZE); i++) {
		val = 0;
		val = calc_row_parity_bits(byte_parities, 1, pow(2, i));
		even_result |= (val << (3 + i));

		val = calc_row_parity_bits(byte_parities, 0, pow(2, i));
		odd_result |= (val << (3 + i));
	}

	return (odd_result << 16) | even_result;
}

void display_help(void)
{
	printf("Usage: " PROGRAM " -i INPUT_FILE -o MTD_DEVICE\n"
	       "Write a loader to a NAND flash device and fills its oob area with 1-bit Hamming ECC codes\n"
	       "\n"
	       "  -i, --input         input file\n"
	       "  -o, --output        mtd device\n"
	       "      --help          display this help and exit\n"
	       "      --version       output version information and exit\n");
	exit(0);
}

void display_version(void)
{
	printf(PROGRAM " " VERSION "\n"
	       "\n"
	       "Copyright (C) 2011 ISEE 2007, SL\n"
	       "\n"
	       PROGRAM " comes with NO WARRANTY\n"
	       "to the extent permitted by law.\n"
	       "\n"
	       "You may redistribute copies of " PROGRAM "\n"
	       "under the terms of the GNU General Public Licence.\n"
	       "See the file `COPYING' for more information.\n");
	exit(0);
}

void process_options(int argc, char *argv[])
{
	int error = 0;
	input_file = NULL;
	output_file = NULL;

	for (;;) {
		int option_index = 0;
		static const char *short_options = "i:o:";
		static const struct option long_options[] = {
			{"help", no_argument, 0, 0},
			{"version", no_argument, 0, 0},
			{"input", required_argument, 0, 'i'},
			{"output", required_argument, 0, 'o'},
			{0, 0, 0, 0},
		};

		int c = getopt_long(argc, argv, short_options,
				    long_options, &option_index);

		if (c == EOF)
			break;

		switch (c) {
		case 0:
			switch (option_index) {
			case 0:
				display_help();
				break;
			case 1:
				display_version();
				break;
			}
			break;
		case 'i':
			input_file = optarg;
			break;
		case 'o':
			output_file = optarg;
			break;
		case '?':
			error = 1;
			break;
		}
	}

	if (input_file == NULL || output_file == NULL || error)
		display_help();
}

int write_ecc(int ofd, unsigned char *ecc, int sector_cnt)
{
	struct mtd_oob_buf oob;
	unsigned char oobbuf[64];
	int i;

	memset(oobbuf, 0xff, sizeof(oobbuf));

	for (i = 0; i < 12; i++)
		oobbuf[i + 2] = ecc[i];

	oob.start = (sector_cnt - 1) * SECTOR_SIZE;
	oob.ptr = oobbuf;
	oob.length = 64;

	return ioctl(ofd, MEMWRITEOOB, &oob) != 0;
}

void ecc_sector(unsigned char *sector, unsigned char *code)
{
	unsigned char *p;
	int ecc = 0;

	ecc = nand_calculate_ecc(sector);

	p = (unsigned char *)&ecc;

	code[0] = p[0];
	code[1] = p[2];
	code[2] = p[1] | (p[3] << 4);
}

int find_nand(void)
{
	DIR *dir;
	struct dirent *ent;
	dir = opendir(path);

	if (dir == NULL) {
		perror("Error opening /sys dir");
		return EXIT_FAILURE;
	}

	/* print all the files and directories within directory */
	while ((ent = readdir(dir)) != NULL) {
		if (strstr(ent->d_name, "omap2-onenand"))
			return ONENAND;
		else if (strstr(ent->d_name, "omap2-nand"))
			return NAND;
	}

	closedir(dir);

	printf("Flash memory not found in /sys");
	return -1;
}

int main(int argc, char *argv[])
{
	int fd;
	int ofd;
	unsigned char sector[SECTOR_SIZE];
	unsigned char *page;
	unsigned char code[3];
	int res = 0;
	int cnt = 0;
	unsigned char ecc[12];
	int i;
	int sector_idx = 0;
	int sector_cnt = 0;
	int sector_bytes = 0;
	int ecc_bytes = 0;
	int ret = 0;
	int len;

	process_options(argc, argv);

	flash_type = find_nand();

	if (flash_type < 0) {
		ret = EXIT_FAILURE;
		goto out;
	}

	if (flash_type == NAND)
		len = PAGE_SIZE;
	else
		len = 2 * PAGE_SIZE;

	page = (unsigned char *)malloc(len * sizeof(unsigned char));

	if (page == NULL) {
		perror("Error opening input file");
		ret = 1;
		goto out;
	}

	fd = open(input_file, O_RDWR);
	if (fd < 0) {
		perror("Error opening input file");
		ret = EXIT_FAILURE;
		goto out_malloc;
	}

	ofd = open(output_file, O_CREAT | O_RDWR, S_IRWXU | S_IRWXG);
	if (fd < 0) {
		perror("Error opening output file");
		ret = EXIT_FAILURE;
		goto out_input;
	}

	if (flash_type == NAND) {
		/* The device has to be accessed in RAW mode to fill oob area */
		res = ioctl(ofd, MTDFILEMODE, (void *) MTD_MODE_RAW);

		if (res) {
			perror("RAW mode access");
			ret = EXIT_FAILURE;
			goto out_input;
		}
	}


	if (flash_type == NAND)
		memset(ecc, 0x00, 12);

	memset(page, 0xff, len);
	memset(sector, 0xff, SECTOR_SIZE);

	while ((cnt = read(fd, sector, SECTOR_SIZE)) > 0) {

		/* Writes has to be sector aligned */
		if (cnt < SECTOR_SIZE)
			memset(sector + cnt, 0xff, SECTOR_SIZE - cnt);

		sector_bytes += SECTOR_SIZE;

		if (flash_type == NAND) {
			memset(code, 0x00, 3);
			/* Obtain ECC code for sector */
			ecc_sector(sector, code);
			for (i = 0; i < 3; i++)
				ecc[sector_idx * 3 + i] = code[i];
		}

		memcpy(page + sector_idx * SECTOR_SIZE, sector, SECTOR_SIZE);

		sector_idx++;
		sector_cnt++;

		/* After reading a complete page each PAGE
		   write the data and the oob area. */
		if (sector_idx == PAGE_SIZE / SECTOR_SIZE ||
		    cnt < SECTOR_SIZE) {

			/* The OneNAND has a 2-plane memory but the ROM boot
			   can only access one of them, so we have to double
			   copy each 2K page. */
			if (flash_type == ONENAND)
				memcpy(page + PAGE_SIZE, page, PAGE_SIZE);

			if (write(ofd, page, len) != len) {
				perror("Error writing to output file");
				ret = EXIT_FAILURE;
				goto out_output;
			}

			memset(page, 0xff, len);

			if (flash_type == NAND)
				if (write_ecc(ofd, ecc, sector_cnt)) {
					perror("Error writing ECC in OOB area");
					ret = 1;
					goto out_output;
				}

			if (flash_type == NAND) {
				ecc_bytes += 64;
				memset(ecc, 0x00, 12);
			}

			sector_idx = 0;
		}
		memset(sector, 0xff, SECTOR_SIZE);
	}

	if (cnt < 0) {
		perror("File I/O error on input file");
		ret = EXIT_FAILURE;
		goto out_output;
	}

	printf("Successfully written %s to %s\n", input_file, output_file);
	ret = EXIT_SUCCESS;

out_output:
	close(ofd);
out_input:
	close(fd);
out_malloc:
	free(page);
out:
	exit(ret);
}
