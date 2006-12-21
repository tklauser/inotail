/*
 * inotail.c
 * A fast implementation of tail which uses the inotify API present in
 * recent versions of the Linux kernel.
 *
 * Copyright (C) 2005-2006, Tobias Klauser <tklauser@distanz.ch>
 *
 * The idea was taken from turbotail.
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
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "inotify.h"
#include "inotify-syscalls.h"

#include "inotail.h"

#define PROGRAM_NAME "inotail"
#define BUFFER_SIZE 4096

/* Print header with filename before tailing the file? */
static char verbose = 0;

/* Tailing relative to begin or end of file */
static char from_begin = 0;

/* Command line options */
static const struct option long_opts[] = {
	{"bytes", required_argument, NULL, 'c'},
	{"follow", optional_argument, NULL, 'f'},
	{"lines", required_argument, NULL, 'n'},
	{"verbose", no_argument, NULL, 'v'},
	{"help", no_argument, NULL, 'h'},
	{"version", no_argument, NULL, 'V'},
	{NULL, 0, NULL, 0}
};

static void usage(const int status)
{
	fprintf(stderr, "Usage: %s [OPTION]... [FILE]...\n\n"
			"  -c N, --bytes=N    output the last N bytes\n"
			"  -f,   --follow     output as the file grows\n"
			"  -n N, --lines=N    output the last N lines (default: %d)\n"
			"  -v,   --verbose    print headers with file names\n"
			"  -h,   --help       show this help and exit\n"
			"  -V,   --version    show version and exit\n\n"
			"If the first character of N (the number of bytes or lines) is a `+',\n"
			"begin printing with the Nth item from the start of each file, otherwise,\n"
			"print the last N items in the file.\n", PROGRAM_NAME, DEFAULT_N_LINES);

	exit(status);
}

static void setup_file(struct file_struct *f)
{
	f->fd = -1;
	f->st_size = 0;
	f->ignore = 0;
	f->i_watch = -1;
}

static inline char *pretty_name(char *filename)
{
	return (strncmp(filename, "-", 1) == 0) ? "standard input" : filename;
}

static void write_header(char *filename)
{
	static unsigned short first_file = 1;

	fprintf(stdout, "%s==> %s <==\n", (first_file ? "" : "\n"), pretty_name(filename));
	first_file = 0;
}

static off_t lines_to_offset_from_end(struct file_struct *f, unsigned int n_lines)
{
	char buf[BUFFER_SIZE];
	off_t offset = f->st_size;

	n_lines++;	/* We also count the last \n */
	memset(&buf, 0, sizeof(buf));

	while (offset > 0 && n_lines > 0) {
		int i, rc;
		int block_size = BUFFER_SIZE;	/* Size of the current block we're reading */

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
				if (--n_lines == 0)
					return offset += i + 1; /* We don't want the first \n */
			}
		}
	}

	return offset;

}

static off_t lines_to_offset_from_begin(struct file_struct *f, unsigned int n_lines)
{
	char buf[BUFFER_SIZE];
	off_t offset = 0;

	/* tail everything for 'inotail -n +0' */
	if (n_lines == 0)
		return 0;

	n_lines--;
	memset(&buf, 0, sizeof(buf));

	while (offset <= f->st_size && n_lines > 0) {
		int i, rc;
		int block_size = BUFFER_SIZE;

		lseek(f->fd, offset, SEEK_SET);

		rc = read(f->fd, &buf, block_size);
		if (rc < 0) {
			fprintf(stderr, "Error: Could not read from file '%s' (%s)\n", f->name, strerror(errno));
			return -1;
		}

		for (i = 0; i < block_size; i++) {
			if (buf[i] == '\n') {
				if (--n_lines == 0)
					return offset + i + 1;
			}
		}

		offset += block_size;
	}

	return offset;
}

static off_t lines_to_offset(struct file_struct *f, unsigned int n_lines)
{
	if (from_begin)
		return lines_to_offset_from_begin(f, n_lines);
	else
		return lines_to_offset_from_end(f, n_lines);
}

static inline off_t bytes_to_offset(struct file_struct *f, unsigned int n_bytes)
{
	/* tail everything for 'inotail -c +0' */
	if (from_begin && n_bytes == 0)
		return 0;
	else
		return (from_begin ? ((off_t) n_bytes - 1) : (f->st_size - (off_t) n_bytes));
}

static int tail_pipe(struct file_struct *f)
{
	int rc;
	char buf[BUFFER_SIZE];

	if (verbose)
		write_header(f->name);

	/* We will just tail everything here */
	while ((rc = read(f->fd, &buf, BUFFER_SIZE)) > 0)
		write(STDOUT_FILENO, buf, (size_t) rc);

	return rc;
}

static int tail_file(struct file_struct *f, unsigned int n_units, char mode, char forever)
{
	int ret = -1;
	ssize_t bytes_read = 0;
	off_t offset = 0;
	char buf[BUFFER_SIZE];
	struct stat finfo;

	if (strncmp(f->name, "-", 1) == 0)
		f->fd = STDIN_FILENO;
	else {
		f->fd = open(f->name, O_RDONLY);
		if (f->fd < 0) {
			fprintf(stderr, "Error: Could not open file '%s' (%s)\n", f->name, strerror(errno));
			return ret;
		}
	}

	if (fstat(f->fd, &finfo) < 0) {
		fprintf(stderr, "Error: Could not stat file '%s' (%s)\n", f->name, strerror(errno));
		goto err;
	}

	if (!IS_TAILABLE(finfo.st_mode)) {
		fprintf(stderr, "Error: '%s' of unsupported file type\n", f->name);
		goto err;
	}

	/* We cannot seek on these */
	if (IS_PIPELIKE(finfo.st_mode) || f->fd == STDIN_FILENO)
		return tail_pipe(f);

	f->st_size = finfo.st_size;

	if (mode == M_LINES)
		offset = lines_to_offset(f, n_units);
	else
		offset = bytes_to_offset(f, n_units);

	/* We only get negative offsets on errors */
	if (offset < 0)
		goto err;

	if (verbose)
		write_header(f->name);

	lseek(f->fd, offset, SEEK_SET);

	while ((bytes_read = read(f->fd, &buf, BUFFER_SIZE)) > 0)
		write(STDOUT_FILENO, buf, (size_t) bytes_read);

	ret = 0;

	/* Let the fd open, we'll need it */
	if (forever)
		return ret;

err:
	if (close(f->fd) < 0) {
		fprintf(stderr, "Error: Could not close file '%s' (%s)\n", f->name, strerror(errno));
		ret = -1;
	}

	return ret;
}

static int handle_inotify_event(struct inotify_event *inev, struct file_struct *fil, int n_ignored)
{
	if (inev->mask & IN_MODIFY) {
		int rc;
		off_t offset;
		char fbuf[BUFFER_SIZE];
		struct stat finfo;

#if 0
		fil->fd = open(fil->name, O_RDONLY);
		if (fil->fd < 0) {
			fprintf(stderr, "Error: Could not open file '%s' (%s)\n", fil->name, strerror(errno));
			goto ignore;
		}
#endif
		if (fstat(fil->fd, &finfo) < 0) {
			fprintf(stderr, "Error: Could not stat file '%s' (%s)\n", fil->name, strerror(errno));
			goto ignore;
		}

		offset = fil->st_size;
		fil->st_size = finfo.st_size;
		memset(&fbuf, 0, sizeof(fbuf));

		if (verbose)
			write_header(fil->name);

		lseek(fil->fd, offset, SEEK_SET);
		while ((rc = read(fil->fd, &fbuf, BUFFER_SIZE)) != 0)
			write(STDOUT_FILENO, fbuf, rc);
#if 0
		close(fil->fd);
#endif
		return n_ignored;
	} else if (inev->mask & IN_DELETE_SELF) {
		fprintf(stderr, "File '%s' deleted.\n", fil->name);
	} else if (inev->mask & IN_MOVE_SELF) {
		/* TODO: Try to follow file/fd */
		fprintf(stderr, "File '%s' moved.\n", fil->name);
	} else if (inev->mask & IN_UNMOUNT) {
		fprintf(stderr, "Device containing file '%s' unmounted.\n", fil->name);
	}

ignore:
	if (close(fil->fd) < 0)
		fprintf(stderr, "Error: Could not close file '%s' (%s)\n", fil->name, strerror(errno));

	fil->ignore = 1;
	n_ignored++;

	return n_ignored;
}

static int watch_files(struct file_struct *f, int n_files)
{
	int ifd, i, n_ignored = 0;
	unsigned buflen = 2 * n_files * sizeof(struct inotify_event);
	struct inotify_event *inev;
	char *buf;

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

	buf = malloc(buflen);
	memset(buf, 0, buflen);

	while (n_ignored < n_files) {
		int len;

		len = read(ifd, buf, buflen);
		inev = (struct inotify_event *) buf;

		while (len > 0) {
			struct file_struct *fil = NULL;

			/* Which file has produced the event? */
			for (i = 0; i < n_files; i++) {
				if (!f[i].ignore && f[i].fd >= 0 && f[i].i_watch == inev->wd) {
					fil = &f[i];
					break;
				}
			}

			if (unlikely(!fil))
				break;

			n_ignored = handle_inotify_event(inev, fil, n_ignored);
			len -= sizeof(struct inotify_event) + inev->len;
			inev = (struct inotify_event *) ((char *) inev + sizeof(struct inotify_event) + inev->len);
		}
	}

	free(buf);
	close(ifd);

	return n_ignored;
}

int main(int argc, char **argv)
{
	int i, c, ret = 0;
	int n_files = 0;
	int n_units = DEFAULT_N_LINES;
	char forever = 0, mode = M_LINES;
	char **filenames;
	struct file_struct *files;

	while ((c = getopt_long(argc, argv, "c:n:fvVh", long_opts, NULL)) != -1) {
		switch (c) {
		case 'c':
			mode = M_BYTES;
			/* fall through */
		case 'n':
			if (*optarg == '+')
				from_begin = 1;
			else if (*optarg == '-')
				optarg++;
			n_units = strtoul(optarg, NULL, 0);
			if (n_units < 0)
				n_units = 0;
			break;
                case 'f':
			forever = 1;
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
	if (optind < argc) {
		n_files = argc - optind;
		filenames = argv + optind;
	} else {
		/* It must be stdin then */
		static char *dummy_stdin = "-";
		n_files++;
		filenames = &dummy_stdin;

		/* POSIX says that -f is ignored if no file operand is
		   specified and standard input is a pipe. */
		if (forever) {
			struct stat finfo;
			if (fstat(STDIN_FILENO, &finfo) == 0
					&& IS_PIPELIKE(finfo.st_mode))
				forever = 0;
		}
	}

	files = malloc(n_files * sizeof(struct file_struct));
	if (unlikely(!files)) {
		fprintf(stderr, "Error: Not enough memory (%s)\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	for (i = 0; i < n_files; i++) {
		files[i].name = filenames[i];
		setup_file(&files[i]);
		ret = tail_file(&files[i], n_units, mode, forever);
		if (ret < 0)
			files[i].ignore = 1;
	}

	if (forever)
		ret = watch_files(files, n_files);

	free(files);

	return ret;
}
