/*
 * simpletail.c
 * A fast implementation of tail which uses the inotify-API present in
 * recent Linux Kernels.
 *
 * Copyright (C) 2005-2006, Tobias Klauser <tklauser@access.unizh.ch>
 *
 * The idea and some code were taken from turbotail.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "inotify.h"
#include "inotify-syscalls.h"

#include "inotail.h"

#define BUFFER_SIZE 4096

void usage(void)
{
	fprintf(stderr, "usage: simpletail [-f] [-n <nr-lines>] <file>\n");
	exit(EXIT_FAILURE);
}

off_t lines(int fd, int file_size, unsigned int n_lines)
{
	int i;
	char buf[BUFFER_SIZE];
	off_t offset = file_size;

	/* Negative offsets don't make sense here */
	if (offset < 0)
		offset = 0;

	while (offset > 0 && n_lines > 0) {
		int rc;
		int block_size = BUFFER_SIZE; /* Size of the current block we're reading */

		if (offset < BUFFER_SIZE)
			block_size = offset;

		/* Start of current block */
		offset -= block_size;

		dprintf("  offset: %lu\n", offset);

		lseek(fd, offset, SEEK_SET);

		rc = read(fd, &buf, block_size);

		for (i = block_size; i > 0; i--) {
			if (buf[i] == '\n') {
				dprintf("  Found \\n at position %d\n", i);
				n_lines--;

				if (n_lines == 0)
					break;
			}
		}
	}

	if (n_lines == 0)
		offset += i + 1; /* We don't want the first \n */

	return offset;
}

int watch_file(const char *filename, off_t offset)
{
	int ifd, watch;
	struct inotify_event *inev;
	char buf[BUFFER_SIZE];

	dprintf(">> Watching %s\n", filename);

	ifd = inotify_init();
	if (ifd < 0) {
		perror("inotify_init()");
		exit(-2);
	}

	watch = inotify_add_watch(ifd, filename, IN_MODIFY|IN_DELETE_SELF|IN_MOVE_SELF|IN_UNMOUNT);

	memset(&buf, 0, sizeof(buf));

	while (1) {
		int len;

		len = read(ifd, buf, sizeof(buf));
		inev = (struct inotify_event *) &buf;

		while (len > 0) {
			if (inev->mask & IN_MODIFY) {
				int ffd, block_size;
				char fbuf[BUFFER_SIZE];
				struct stat finfo;

				dprintf("  File '%s' modified.\n", filename);
				dprintf("  offset: %lu.\n", offset);

				ffd = open(filename, O_RDONLY);
				if (fstat(ffd, &finfo) < 0) {
					perror("fstat()");
					return -1;
				}

				/* XXX: block_size could be bigger than BUFFER_SIZE */
				block_size = finfo.st_size - offset;
				if (block_size < 0)
					block_size = 0;

				lseek(ffd, offset, SEEK_SET);
				while (read(ffd, &fbuf, BUFFER_SIZE) != 0) {
					write(STDOUT_FILENO, fbuf, block_size);
				}

				offset = finfo.st_size;

				close(ffd);
			}

			if (inev->mask & IN_DELETE_SELF) {
				dprintf("  File '%s' deleted.\n", filename);
				return -1;
			}
			if (inev->mask & IN_MOVE_SELF) {
				dprintf("  File '%s' moved.\n", filename);
				return -1;
			}
			if (inev->mask & IN_UNMOUNT) {
				dprintf("  Device containing file '%s' unmounted.\n", filename);
				return -1;
			}

			len -= sizeof(struct inotify_event) + inev->len;
			inev = (struct inotify_event *) ((char *) inev + sizeof(struct inotify_event) + inev->len);
		}
	}
}

int main(int argc, char **argv)
{
	int i, fd;
	int n_lines = 0;
	int ret = 0;
	short forever = 0;
	char buf[BUFFER_SIZE], *filename;
	struct stat finfo;
	off_t offset = 0;

	if (argc < 3)
		usage();

	for (i = 1; (i + 1 < argc) && (argv[i][0] == '-'); i++) {
		switch (argv[i][1]) {
                case 'f':
			forever = 1;
			break;
		case 'n':
			n_lines = strtol(argv[++i], NULL, 0) + 1;
			break;
                default:
			usage();
			break;
                }
        }

	filename = argv[i];
	fd = open(filename, O_RDONLY);

	if (fd < 0) {
		perror("open()");
		return -1;
	}

	if (fstat(fd, &finfo) < 0) {
		perror("fstat()");
		return -1;
	}

	offset = lines(fd, finfo.st_size, n_lines);
	dprintf("  offset: %lu.\n", offset);

	lseek(fd, offset, SEEK_SET);
	while (read(fd, &buf, BUFFER_SIZE) != 0) {
		write(STDOUT_FILENO, buf, finfo.st_size - offset);
	}

	close(fd);

	if (forever)
		ret = watch_file(filename, finfo.st_size);

	return ret;
}
