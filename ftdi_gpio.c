/**
 * ftdi_gpio.c - Simple GPIO output using FTDI FT232X
 *
 * Copyright (c) 2020 David Imhoff <dimhoff.devel@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <ftdi.h>

#define VID 0x0403
#define PID 0x6014

int main(int argc, char *argv[])
{
	struct ftdi_context *ftdi;
	int ret;
	size_t len;

	uint8_t data;
	uint8_t mask;

	int retval = EXIT_FAILURE;

	if (argc != 3) {
		fprintf(stderr, "usage: %s <output_mask> <output_data>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	mask = strtol(argv[1], NULL, 0);
	data = strtol(argv[2], NULL, 0);

	printf("mask: %02x, data: %02x\n", mask, data);

	ftdi = ftdi_new();
	if (ftdi == NULL) {
		fprintf(stderr, "Unable to allocate memory for FTDI context\n");
		exit(EXIT_FAILURE);
	}

	if (ftdi_usb_open_desc(ftdi, VID, PID, NULL, NULL) < 0) {
		fprintf(stderr, "ftdi_usb_open_desc failed: %s\n",
				ftdi_get_error_string(ftdi));
		goto bad1;
	}

	/*
	if (ftdi_set_baudrate(ftdi, BAUD / 16) < 0) {
		//TODO: c2_bus_set_error(stderr, "ftdi_set_baudrate failed: %s\n", ftdi_get_error_string(ftdi));
		c2_bus_set_error(bus, "ftdi_set_baudrate failed");
		goto bad2;
	}
	*/

	if (ftdi_set_bitmode(ftdi, mask, BITMODE_BITBANG) < 0) {
		fprintf(stderr, "ftdi_set_bitmode failed: %s\n", ftdi_get_error_string(ftdi));
		goto bad2;
	}

	ret = ftdi_write_data(ftdi, &data, 1);
	if (ret != 1) {
		fprintf(stderr, "ftdi_write_data failed: %s\n", ftdi_get_error_string(ftdi));
		goto bad2;
	}

	ret = ftdi_read_pins(ftdi, &data);
	if (ret < 0) {
		fprintf(stderr, "ftdi_read_pins failed: %s\n", ftdi_get_error_string(ftdi));
		goto bad2;
	}

	printf("value=%02x\n", data);

	retval = EXIT_SUCCESS;

bad2:
	ftdi_usb_close(ftdi);
bad1:
	ftdi_free(ftdi);
	return retval;
}
