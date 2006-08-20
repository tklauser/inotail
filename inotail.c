/*
 * inotail.c
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "inotify.h"
#include "inotify-syscalls.h"

#include "inotail.h"

#define PROGRAM_NAME	"inotail"
#ifndef VERSION
# define VERSION 	"undef"
#endif

#define BUFFER_SIZE 4096

/* Print header with filename before tailing the file? */
static char verbose = 0;

static void usage(int status)
{
	fprintf(stderr, "Usage: %s [OPTION]... [FILE]...\n\n", PROGRAM_NAME);
	fprintf(stderr, "  -c N    output the last N bytes\n");
	fprintf(stderr, "  -f      output as the file grows (that's were %s differs from pure tail)\n", PROGRAM_NAME);
	fprintf(stderr, "  -n N    output the last N lines (default: %d)\n", DEFAULT_N_LINES);
	fprintf(stderr, "  -v      Output headers with file names\n");
	fprintf(stderr, "  -h      Show this help and exit\n");
	fprintf(stderr, "  -V      Show %s version and exit\n", PROGRAM_NAME);

	exit(status);
}

static void setup_file(struct file_struct *f)
{
	f->fd = -1;
	f->st_size = 0;
	f->ignore = 0;
	f->i_watch = -1;
}

static void write_header(const char *filename)
{
	static unsigned short first_file = 1;

	fprintf(stdout, "%s==> %s <==\n", (first_file ? "" : "\n"), filename);
	first_file = 0;
}

static off_t lines_to_offset(struct file_struct *f, unsigned int n_lines)
{
	int i;
	char buf[BUFFER_SIZE];
	off_t offset = f->st_size;

	memset(&buf, 0, sizeof(buf));

	/* We also count the last \n */
	n_lines += 1;

	while (offset > 0 && n_lines > 0) {
		int rc;
		int block_size = BUFFER_SIZE; /* Size of the current block we're reading */

		if (offset < BUFFER_SIZE)
			block_size = offset;

		/* Start of current block */
		offset -= block_size;

		lseek(f->fd, offset, SEEK_SET);

		rc = read(f->fd, &buf, block_size);
		if (rc < 0) {
			fprintf(stderr, "Error: Could not read from file '%s' (%s)\n", f->name, strerror(errno));
			return -1;
		}

		for (i = block_size; i > 0; i--) {
			if (buf[i] == '\n') {
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

static int tail_file(struct file_struct *f, int n_lines, char mode)
{
	ssize_t rc = 0;
	off_t offset = 0;
	char buf[BUFFER_SIZE];
	struct stat finfo;

	f->fd = open(f->name, O_RDONLY);
	if (f->fd < 0) {
		fprintf(stderr, "Error: Could not open file '%s' (%s)\n", f->name, strerror(errno));
		return -1;
	}

	if (fstat(f->fd, &finfo) < 0) {
		fprintf(stderr, "Error: Could not stat file '%s' (%s)\n", f->name, strerror(errno));
		close(f->fd);
		return -1;
	}

	f->st_size = finfo.st_size;

	if (mode == M_LINES)
		offset = lines_to_offset(f, n_lines);
	else /* Bytewise tail */
		offset = f->st_size - n_lines;

	if (offset < 0)
		return -1;

	if (verbose)
		write_header(f->name);

	lseek(f->fd, offset, SEEK_SET);

	while ((rc = read(f->fd, &buf, BUFFER_SIZE)) != 0) {
		dprintf("  f->st_size - offset: %lu\n", f->st_size - offset);
		dprintf("  rc:                  %d\n", rc);
		write(STDOUT_FILENO, buf, (size_t) rc);
	}

	close(f->fd);

	return 0;
}

static int watch_files(struct file_struct *f, int n_files)
{
	int ifd, i, n_ignored = 0;
	off_t offset;
	struct inotify_event *inev;
	char buf[sizeof(struct inotify_event) * 32];	/* Let's hope we don't get more than 32 events at a time */

	ifd = inotify_init();
	if (ifd < 0) {
		fprintf(stderr, "Error: Inotify is not supported by the kernel you're currently running.\n");
		exit(EXIT_FAILURE);
	}

	for (i = 0; i < n_files; i++) {
		if (!f[i].ignore)
			f[i].i_watch = inotify_add_watch(ifd, f[i].name,
						IN_MODIFY|IN_DELETE_SELF|IN_MOVE_SELF|IN_UNMOUNT);
		else
			n_ignored++;
	}

	memset(&buf, 0, sizeof(buf));

	while (n_ignored < n_files) {
		int len;

		len = read(ifd, buf, sizeof(buf));
		inev = (struct inotify_event *) &buf;

		while (len > 0) {
			struct file_struct *fil;

			/* Which file has produced the event? */
			for (i = 0; i < n_files; i++) {
				if (!f[i].ignore && f[i].fd >= 0 && f[i].i_watch == inev->wd) {
					fil = &f[i];
					break;
				}
			}

			/* XXX: Is it possible that no file in the list produced the event? */
			if (inev->mask & IN_MODIFY) {
				int block_size;
				char fbuf[BUFFER_SIZE];
				struct stat finfo;

				offset = fil->st_size;

				dprintf("  File '%s' modified, offset: %lu.\n", fil->name, offset);

				fil->fd = open(fil->name, O_RDONLY);
				if (fil->fd < 0) {
					fil->ignore = 1;
					n_ignored++;
					fprintf(stderr, "Error: Could not open file '%s' (%s)\n", f->name, strerror(errno));
					continue;
				}

				if (fstat(fil->fd, &finfo) < 0) {
					fil->ignore = 1;
					n_ignored++;
					fprintf(stderr, "Error: Could not stat file '%s' (%s)\n", f->name, strerror(errno));
					close(fil->fd);
					continue;
				}

				fil->st_size = finfo.st_size;
				block_size = fil->st_size - offset;

				if (block_size < 0)
					block_size = 0;

				/* XXX: Dirty hack for now to make sure
				 * block_size doesn't get bigger than
				 * BUFFER_SIZE
				 */
				if (block_size > BUFFER_SIZE)
					block_size = BUFFER_SIZE;

				if (verbose)
					write_header(fil->name);

				memset(&fbuf, 0, sizeof(fbuf));

				lseek(fil->fd, offset, SEEK_SET);
				while (read(fil->fd, &fbuf, block_size) != 0)
					write(STDOUT_FILENO, fbuf, block_size);

				close(fil->fd);
			} else if (inev->mask & IN_DELETE_SELF) {
				fil->ignore = 1;
				n_ignored++;
				fprintf(stderr, "File '%s' deleted.\n", fil->name);
			} else if (inev->mask & IN_MOVE_SELF) {
				fil->ignore = 1;
				n_ignored++;
				fprintf(stderr, "File '%s' moved.\n", fil->name);
				/* TODO: Try to follow file/fd */
			} else if (inev->mask & IN_UNMOUNT) {
				fil->ignore = 1;
				n_ignored++;
				fprintf(stderr, "Device containing file '%s' unmounted.\n", fil->name);
			}

			len -= sizeof(struct inotify_event) + inev->len;
			inev = (struct inotify_event *) ((char *) inev + sizeof(struct inotify_event) + inev->len);
		}
	}

	return -1;
}

int main(int argc, char **argv)
{
	int i, opt, ret = 0;
	int n_files = 0;
	int n_lines = DEFAULT_N_LINES;
	char forever = 0, mode = M_LINES;
	char **filenames;
	struct file_struct *files;

	for (opt = 1; (opt < argc) && (argv[opt][0] == '-'); opt++) {
		switch (argv[opt][1]) {
		case 'c':
			mode = M_BYTES;
			n_lines = strtoul(argv[++opt], NULL, 0);
			if (n_lines < 0)
				n_lines = 0;
			break;
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
			fprintf(stderr, "%s %s\n", PROGRAM_NAME, VERSION);
			return 0;
		case 'h':
			usage(EXIT_SUCCESS);
		default:
			usage(EXIT_FAILURE);
                }
        }

	/* Do we have some files to read from? */
	if (opt < argc) {
		n_files = argc - opt;
		filenames = argv + opt;
	} else {
		/* For now, reading from stdin will be implemented later (tm) */
		/* XXX: This is not like GNU tail behaves! */
		usage(EXIT_FAILURE);
	}

	files = malloc(n_files * sizeof(struct file_struct));
	for (i = 0; i < n_files; i++) {
		files[i].name = filenames[i];
		setup_file(&files[i]);
		ret = tail_file(&files[i], n_lines, mode);
		if (ret < 0)
			files[i].ignore = 1;
	}

	if (forever)
		ret = watch_files(files, n_files);

	free(files);

	return ret;
}
