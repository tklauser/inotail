/*
 * simpletail.c
 * A fast implementation of tail which uses the inotify-API present in
 * recent Linux Kernels.
 *
 * Copyright (C) 2005-2006, Tobias Klauser <tklauser@distanz.ch>
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

#define _GNU_SOURCE

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

#define VERSION "0.0"

#define BUFFER_SIZE 4096
#define DEFAULT_N_LINES 10

/* Print header with filename before tailing the file? */
static short verbose = 0;

static void usage(void)
{
	fprintf(stderr, "usage: simpletail [-f] [-n <nr-lines>] <file>\n");
	exit(EXIT_FAILURE);
}

static void write_header(const char *filename)
{
	static unsigned short first_file = 1;

	fprintf (stdout, "%s==> %s <==\n", (first_file ? "" : "\n"), filename);
	first_file = 0;
}

static off_t lines(int fd, int file_size, unsigned int n_lines)
{
	int i;
	char buf[BUFFER_SIZE];
	off_t offset = file_size;

	/* Negative offsets don't make sense here */
	if (offset < 0)
		offset = 0;

	n_lines += 1;	/* We also count the last \n */

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

				if (n_lines == 0) {
					/* We don't want the first \n */
					offset += i + 1;
					break;
				}
			}
		}
	}

	return offset;
}

static int tail_file(struct file_struct *f, int n_lines)
{
	int fd;
	off_t offset = 0;
	char buf[BUFFER_SIZE];
	struct stat finfo;

	fd = open(f->name, O_RDONLY);

	if (fd < 0) {
		perror("open()");
		return -1;
	}

	if (fstat(fd, &finfo) < 0) {
		perror("fstat()");
		return -1;
	}

	f->st_size = finfo.st_size;

	offset = lines(fd, f->st_size, n_lines);
	dprintf("  offset: %lu.\n", offset);

	if (verbose)
		write_header(f->name);

	lseek(fd, offset, SEEK_SET);
	while (read(fd, &buf, BUFFER_SIZE) != 0) {
		write(STDOUT_FILENO, buf, f->st_size - offset);
	}

	close(fd);

	return 0;
}

static int watch_file(struct file_struct *f)
{
	int ifd, watch;
	off_t offset;
	struct inotify_event *inev;
	char buf[BUFFER_SIZE];

	dprintf(">> Watching %s\n", filename);

	ifd = inotify_init();
	if (ifd < 0) {
		perror("inotify_init()");
		exit(-2);
	}

	watch = inotify_add_watch(ifd, f->name, IN_MODIFY|IN_DELETE_SELF|IN_MOVE_SELF|IN_UNMOUNT);

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

				offset = f->st_size;

				dprintf("  File '%s' modified.\n", filename);
				dprintf("  offset: %lu.\n", offset);

				ffd = open(f->name, O_RDONLY);
				if (fstat(ffd, &finfo) < 0) {
					perror("fstat()");
					return -1;
				}

				f->st_size = finfo.st_size;
				block_size = f->st_size - offset;

				if (block_size < 0)
					block_size = 0;

				/* XXX: Dirty hack for now to make sure
				 * block_size doesn't get bigger than
				 * BUFFER_SIZE
				 */
				if (block_size > BUFFER_SIZE)
					block_size = BUFFER_SIZE;

				lseek(ffd, offset, SEEK_SET);
				while (read(ffd, &fbuf, block_size) != 0) {
					write(STDOUT_FILENO, fbuf, block_size);
				}


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
	int i, opt, ret = 0;
	int n_files = 0;
	int n_lines = DEFAULT_N_LINES;
	short forever = 0;
	char **filenames;
	struct file_struct *files;

	if (argc < 2)
		usage();

	for (opt = 1; (opt < argc) && (argv[opt][0] == '-'); opt++) {
		switch (argv[opt][1]) {
                case 'f':
			forever = 1;
			break;
		case 'n':
			n_lines = strtoul(argv[++opt], NULL, 0);
			if (n_lines < 0)
				n_lines = 0;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'V':
			fprintf(stderr, "simpletail %s\n", VERSION);
			return 0;
		case 'h':
                default:
			usage();
			break;
                }
        }

	/* Do we have some files to read from? */
	if (opt < argc) {
		n_files = argc - opt;
		filenames = argv + opt;
	} else {
		usage();
		return -1;
	}

	files = malloc(n_files * sizeof(struct file_struct));
	for (i = 0; i < n_files; i++) {
		files[i].name = filenames[i];
		ret &= tail_file(&files[i], n_lines);
	}

	if (forever)
		ret = watch_file(&files[0]);

	free(files);

	return ret;
}
