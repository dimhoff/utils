/**
 * random_ctl.c - Linux random device manipulation tool
 *
 * Copyright (c) 2016, David Imhoff <dimhoff_devel@xs4all.nl>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the author nor the names of its contributors may
 *       be used to endorse or promote products derived from this software
 *       without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/random.h>

void usage() {
	printf(
		"Usage: random_ctl [-h] <action> [<action arguments...>]\n"
		"\n"
		"Commands:\n"
		"  get_entropy_cnt           Retrieve entropy count in input pool.\n"
		"  add_entropy_cnt <N>       Inc-/Decrease input pool entropy count with <N>.\n"
		"  add_entropy <N> <file>    Add <file> to input pool and increase entropy\n"
		"                            count with <N>.\n"
		"  add_random <file>         Mix content of <file> into blocking and non-blocking\n"
		"                            pool. The entropy count is not increased.\n"
		"  clear_pool                Clear entropy count of all random pools.\n"
		"\n"
		"If '-' is used as filename, data is read from stdin.\n"
	);
}

int get_entropy_cnt(int fd, int argc, char *argv[])
{
	int val;
	int r;

	if (argc != 1) {
		fprintf(stderr, "Incorrect amount of arguments\n");
		usage();
		return EXIT_FAILURE;
	}

	r = ioctl(fd, RNDGETENTCNT, &val);
	if (r != 0) {
		perror("RNDGETENTCNT failed");
		return EXIT_FAILURE;
	}

	printf("entropy count: %u\n", val);

	return EXIT_SUCCESS;
}

int add_entropy_cnt(int fd, int argc, char *argv[])
{
	int val;
	int r;

	if (argc != 2) {
		fprintf(stderr, "Incorrect amount of arguments\n");
		usage();
		return EXIT_FAILURE;
	}

	char *endptr = NULL;
	long cnt = strtol(argv[1], &endptr, 0);
	if (*endptr != '\0') {
		fprintf(stderr, "Numeric argument contains non-numeric "
				"characters\n");
		return EXIT_FAILURE;
	}
	if (cnt == LONG_MIN) {
		fprintf(stderr, "Numeric argument to small\n");
		return EXIT_FAILURE;
	}
	if (cnt == LONG_MAX) {
		fprintf(stderr, "Numeric argument to big\n");
		return EXIT_FAILURE;
	}

	val = cnt;
	r = ioctl(fd, RNDADDTOENTCNT, &val);
	if (r != 0) {
		perror("RNDADDTOENTCNT failed");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

int add_entropy(int fd, int argc, char *argv[])
{
	int val;
	int r;
	int inp_fd;

	if (argc != 3) {
		fprintf(stderr, "Incorrect amount of arguments\n");
		usage();
		return EXIT_FAILURE;
	}

	long cnt = strtol(argv[1], NULL, 0);
	if (cnt < 0) {
		fprintf(stderr, "Numeric argument must be possitive\n");
		return EXIT_FAILURE;
	}
	if (cnt == LONG_MAX) {
		fprintf(stderr, "Numeric argument to big\n");
		return EXIT_FAILURE;
	}

	if (strcmp(argv[2], "-") == 0) {
		inp_fd = STDIN_FILENO;
	} else {
		inp_fd = open(argv[2], O_RDONLY);
		if (inp_fd == -1) {
			perror("Failed to open input file");
			return EXIT_FAILURE;
		}
	}

	// Add whole file to input pool
	uint8_t buf[4096] = { 0 };
	struct rand_pool_info *entropy_data = (struct rand_pool_info *) buf;
	ssize_t rbytes = 0;
	do {
		rbytes = read(inp_fd, entropy_data->buf,
				sizeof(buf) - sizeof(struct rand_pool_info));
		if (rbytes < 0) {
			perror("Unable to read input file");
			close(inp_fd);
			return EXIT_FAILURE;
		}
		if (rbytes != 0) {
			entropy_data->entropy_count = 0;
			entropy_data->buf_size = rbytes;

			r = ioctl(fd, RNDADDENTROPY, entropy_data);
			if (r != 0) {
				perror("RNDADDENTROPY failed");
				close(inp_fd);
				return EXIT_FAILURE;
			}
		}
	} while (rbytes > 0);

	if (inp_fd != STDIN_FILENO) {
		close(inp_fd);
	}

	// Increase entropy count after all data is added to the pool.
	val = cnt;
	r = ioctl(fd, RNDADDTOENTCNT, &val);
	if (r != 0) {
		perror("RNDADDTOENTCNT failed");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

int add_random(int fd, int argc, char *argv[])
{
	int inp_fd;

	if (argc != 2) {
		fprintf(stderr, "Incorrect amount of arguments\n");
		usage();
		return EXIT_FAILURE;
	}

	if (strcmp(argv[1], "-") == 0) {
		inp_fd = STDIN_FILENO;
	} else {
		inp_fd = open(argv[1], O_RDONLY);
		if (inp_fd == -1) {
			perror("Failed to open input file");
			return EXIT_FAILURE;
		}
	}

	uint8_t buf[4096];
	ssize_t rbytes = 0;
	do {
		rbytes = read(inp_fd, buf, sizeof(buf));
		if (rbytes < 0) {
			perror("Unable to read input file");
			close(inp_fd);
			return EXIT_FAILURE;
		}
		if (rbytes != 0) {
			uint8_t *bp = buf;
			size_t bytes_left = rbytes;
			while (bytes_left > 0) {
				ssize_t wbytes = write(fd, buf, bytes_left);
				if (wbytes < 0) {
					perror("Unable to write random file");
					close(inp_fd);
					return EXIT_FAILURE;
				}

				bytes_left -= wbytes;
				bp += wbytes;
			}
		}
	} while (rbytes > 0);

	if (inp_fd != STDIN_FILENO) {
		close(inp_fd);
	}

	return EXIT_SUCCESS;
}

int clear_pool(int fd, int argc, char *argv[])
{
	int r;

	if (argc != 1) {
		fprintf(stderr, "Incorrect amount of arguments\n");
		usage();
		return EXIT_FAILURE;
	}

	r = ioctl(fd, RNDCLEARPOOL, NULL);
	if (r != 0) {
		perror("RNDCLEARPOOL failed");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

int main(int argc, char *argv[])
{
	int retval = EXIT_FAILURE;
	int opt;
	int fd=-1;
	int val=0;
	int r;

	while ((opt = getopt(argc, argv, "h")) != -1) {
		switch (opt) {
		case 'h':
			usage();
			exit(EXIT_SUCCESS);
			break;
		default: /* '?' */
			usage();
			exit(EXIT_FAILURE);
		}
	}

	if (argc - optind < 1) {
		fprintf(stderr, "Missing action\n");
		usage();
		exit(EXIT_FAILURE);
	}
	const char *action = argv[optind];
	int action_argc = argc - optind;
	char **action_argv = &argv[optind];

	fd = open("/dev/urandom", O_RDWR);
	if (fd == -1) {
		perror("Failed to open /dev/urandom");
		exit(EXIT_FAILURE);
	}

	if (strcmp(action, "get_entropy_cnt") == 0) {
		retval = get_entropy_cnt(fd, action_argc, action_argv);
	} else if (strcmp(action, "add_entropy_cnt") == 0) {
		retval = add_entropy_cnt(fd, action_argc, action_argv);
	} else if (strcmp(action, "add_entropy") == 0) {
		retval = add_entropy(fd, action_argc, action_argv);
	} else if (strcmp(action, "add_random") == 0) {
		retval = add_random(fd, action_argc, action_argv);
	} else if (strcmp(action, "clear_pool") == 0) {
		retval = clear_pool(fd, action_argc, action_argv);
	} else {
		fprintf(stderr, "Unknown action: %s\n", action);
		usage();
		retval = EXIT_FAILURE;
	}

	close(fd);
	return retval;
}
