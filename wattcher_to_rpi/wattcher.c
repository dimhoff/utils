/**
 * wattcher.c - Wattcher to GPIO logger
 *
 * Copyright (c) 2014, David Imhoff <dimhoff_devel@xs4all.nl>
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
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <sys/socket.h>
#include <sys/un.h>

#include <sys/epoll.h>

#define ENABLE_RPI_PULLUP 1

#define GPIO_PINNUM "4"
#define TRIGGER_EDGE "rising"

#define CVALUE 600

#define SOCK_PATH "/tmp/wattcher.sock"
#define EPOLL_QUEUE_LEN 100
#define EPOLL_MAX_EVENTS_PER_RUN 2
#define EPOLL_RUN_TIMEOUT 1000

int terminate = 0;

struct {
	struct timespec start_time;
	struct timespec last_pulse;
	unsigned int watt;
	unsigned int pulse_cnt;
} state;

void terminate_cb(int signum)
{
	terminate = 1;
}

unsigned int calculate_watt(const struct timespec *last_pulse, const struct timespec *now)
{
	unsigned int delta;
	unsigned int watt;
	delta = (now->tv_sec - last_pulse->tv_sec) * 1000000
		+ (now->tv_nsec / 1000)
		- (last_pulse->tv_nsec / 1000);
//	rot/h = (1 hour / delta)
//	kWatt = rot/h / c_value
//	Watt = 1000 * rot/h / c_value
	watt = (3600000000u / delta) * 1000 / CVALUE;

	return watt;
}

void gpio_cb()
{
	struct timespec now;
	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
		perror("clock_gettime()");
	} else {
		if (state.last_pulse.tv_sec != 0) {
			state.watt = calculate_watt(&state.last_pulse, &now);
		}
		state.pulse_cnt++;
		state.last_pulse = now;
	}
}

#ifdef ENABLE_RPI_PULLUP
int enable_pullup()
{
	int status;

	// Call the 'gpio' utlitity from WiringPI, since sysfs interface
	// doesn't support changing this.
	status = system("gpio -g mode " GPIO_PINNUM " up");
	if (! WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		return -1;
	}

	return 0;
}
#endif // ENABLE_RPI_PULLUP

int main()
{
	struct epoll_event ev[EPOLL_MAX_EVENTS_PER_RUN];
	int gpio_fd = -1;
	int epfd = -1;
	int sock_fd = -1;
	struct sockaddr_un local;
	int retval = EXIT_SUCCESS;
	char rdbuf[15];
	int i;

	// setup signal handlers
	signal(SIGHUP, &terminate_cb);
	signal(SIGINT, &terminate_cb);
	signal(SIGQUIT, &terminate_cb);
	signal(SIGTERM, &terminate_cb);
	signal(SIGPIPE, SIG_IGN);


	// Configure GPIO pin
	if ((gpio_fd = open("/sys/class/gpio/export", O_WRONLY)) == -1) {
		perror("Failed opening /sys/class/gpio/export");
		exit(EXIT_FAILURE);
	}
	if (write(gpio_fd, GPIO_PINNUM, strlen(GPIO_PINNUM)) != strlen(GPIO_PINNUM)) {
		close(gpio_fd);
		perror("Failed to write pin number to export file");
		exit(EXIT_FAILURE);
	}
	close(gpio_fd);

	sleep(1); // Give udev time to chmod() the files
	if ((gpio_fd = open("/sys/class/gpio/gpio"GPIO_PINNUM"/edge", O_WRONLY)) == -1) {
		perror("Failed opening /sys/class/gpio/gpio"GPIO_PINNUM"/edge");
		retval = EXIT_FAILURE;
		goto cleanup;
	}
	if (write(gpio_fd, TRIGGER_EDGE, strlen(TRIGGER_EDGE)) != strlen(TRIGGER_EDGE)) {
		close(gpio_fd);
		perror("Failed to write edge to trigger file");
		retval = EXIT_FAILURE;
		goto cleanup;
	}
	close(gpio_fd);

#ifdef ENABLE_RPI_PULLUP
	// Enable internal pull-up
	if (enable_pullup() != 0) {
		fprintf(stderr, "Failed to enable internal pull-up\n");
		retval = EXIT_FAILURE;
		goto cleanup;
	}
#endif // ENABLE_RPI_PULLUP

	// open gpio value file
	if ((gpio_fd = open("/sys/class/gpio/gpio"GPIO_PINNUM"/value", O_RDONLY)) == -1) {
		perror("Failed opening /sys/class/gpio/gpio"GPIO_PINNUM"/value");
		retval = EXIT_FAILURE;
		goto cleanup;
	}
	
	// setup socket
	if ((sock_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		retval = EXIT_FAILURE;
		goto cleanup2;
	}

	local.sun_family = AF_UNIX;
	assert(strlen(SOCK_PATH) < sizeof(local.sun_path)+1);
	strcpy(local.sun_path, SOCK_PATH);
	unlink(local.sun_path);
	if (bind(sock_fd, (struct sockaddr *)&local, strlen(local.sun_path) + sizeof(local.sun_family)) == -1) {
		perror("bind");
		retval = EXIT_FAILURE;
		goto cleanup2;
	}

	if (listen(sock_fd, 5) == -1) {
		perror("listen");
		retval = EXIT_FAILURE;
		goto cleanup2;
	}

	if (chmod(SOCK_PATH, 0777) != 0) {
		perror("chmod");
		retval = EXIT_FAILURE;
		goto cleanup2;
	}

	// setup epoll
	epfd = epoll_create(EPOLL_QUEUE_LEN);
	if (epfd == -1) {	
		perror("epoll_create");
		retval = EXIT_FAILURE;
		goto cleanup2;
	}

	ev[0].events = EPOLLPRI | EPOLLERR | EPOLLHUP;
	ev[0].data.fd = gpio_fd;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, gpio_fd, &ev[0]) != 0) {
		perror("epoll_ctl gpio");
		retval = EXIT_FAILURE;
		goto cleanup2;
	}
	ev[0].events = EPOLLIN | EPOLLPRI | EPOLLERR | EPOLLHUP;
	ev[0].data.fd = sock_fd;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, sock_fd, &ev[0]) != 0) {
		perror("epoll_ctl socket");
		retval = EXIT_FAILURE;
		goto cleanup2;
	}


	// clear GPIO readable status
	if (read(gpio_fd, rdbuf, sizeof(rdbuf)) < 0) {
		perror("read()");
		retval = EXIT_FAILURE;
		terminate = 1;
	}

	// Main loop
	while (!terminate) {
		int nfds = epoll_wait(epfd, ev, EPOLL_MAX_EVENTS_PER_RUN, EPOLL_RUN_TIMEOUT);
		if (nfds < 0) {
			if (errno == EINTR) {
				continue;
			}
			perror("Error in epoll_wait");
			retval = EXIT_FAILURE;
			terminate = 1;
			continue;
		}

		// for each ready socket
		for (i = 0; i < nfds; i++) {
			if (ev[i].data.fd == gpio_fd) {
				// clear readable status
				if (lseek(gpio_fd, 0, SEEK_SET) == (off_t) -1) {
					perror("lseek()");
					retval = EXIT_FAILURE;
					terminate = 1;
					continue;
				}
				if (read(gpio_fd, rdbuf, sizeof(rdbuf)) < 0) {
					perror("read()");
					retval = EXIT_FAILURE;
					terminate = 1;
					continue;
				}

				gpio_cb();
			} else if (ev[i].data.fd == sock_fd) {
				int client_fd = -1;
				struct sockaddr_un remote;
				unsigned int cur_watt;
				char buf[1024];
				socklen_t t;
				t = sizeof(remote);
				if ((client_fd = accept(sock_fd, (struct sockaddr *)&remote, &t)) == -1) {
					perror("accept");
					retval = EXIT_FAILURE;
					terminate = 1;
					continue;
				}
				cur_watt = state.watt;
				if (state.last_pulse.tv_sec != 0) {
					struct timespec now;
					if (clock_gettime(CLOCK_MONOTONIC, &now) == 0) {
						cur_watt = calculate_watt(&state.last_pulse, &now);
					}
				}

				if (cur_watt < state.watt) {
					snprintf(buf, sizeof(buf)-1, "%u;%s%u\n", state.pulse_cnt, "<", cur_watt);
				} else {
					snprintf(buf, sizeof(buf)-1, "%u;%s%u\n", state.pulse_cnt, "", state.watt);
				}
				buf[sizeof(buf)-1] = '\0';
				write(client_fd, buf, strlen(buf));
				close(client_fd);
			}
		}
		
	}

cleanup2:
	if (epfd != -1) {
		close(epfd);
	}
	if (sock_fd != -1) {
		close(sock_fd);
		unlink(local.sun_path);
	}
	close(gpio_fd);

	// Unconfigure GPIO
cleanup:
	if ((gpio_fd = open("/sys/class/gpio/unexport", O_WRONLY)) == -1) {
		perror("Failed opening /sys/class/gpio/unexport");
		exit(EXIT_FAILURE);
	}
	if (write(gpio_fd, GPIO_PINNUM, strlen(GPIO_PINNUM)) != strlen(GPIO_PINNUM)) {
		close(gpio_fd);
		perror("Failed to write pin number to unexport file");
		exit(EXIT_FAILURE);
	}
	close(gpio_fd);

	return retval;
}
