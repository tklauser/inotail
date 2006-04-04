/*
 * inotail.c
 * A fast implementation of GNU tail which uses the inotify-API present in
 * recent Linux Kernels.
 *
 * Copyright (C) 2005-2006, Tobias Klauser <tklauser@access.unizh.ch>
 *
 * Parts of this program are based on GNU tail included in the GNU coreutils
 * which is:
 * Copyright (C) 1989, 90, 91, 1995-2005 Free Software Foundation, Inc.
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
#include <unistd.h>
#include <stdarg.h>

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>

#include "inotify.h"
#include "inotify-syscalls.h"

#include "inotail.h"

#define VERSION "0.1"

/* XXX: Move all global variables into a struct and use :1 */

/* If !=0, read from the ends of all specified files until killed. */
static unsigned short forever;

/* Print header with filename before tailing the file? */
static unsigned short print_headers = 0;

/* Say my name! */
static char *program_name = "inotail";

static int dump_remainder(const char *filename, int fd, ssize_t n_bytes)
{
	ssize_t written = 0;

	dprintf("==> dump_remainder()\n");

	if (n_bytes > SSIZE_MAX)
		n_bytes = SSIZE_MAX;

	return written;
}

static char *pretty_name(const struct file_struct *f)
{
	return ((strcmp(f->name, "-") == 0) ? "standard input" : f->name);
}

static void write_header(const struct file_struct *f)
{
	static unsigned short first_file = 1;

	fprintf (stdout, "%s==> %s <==\n", (first_file ? "" : "\n"), pretty_name(f));
	first_file = 0;
}

static int file_lines(struct file_struct *f, uintmax_t n_lines, off_t start_pos, off_t end_pos)
{
	char buffer[BUFSIZ];
	size_t bytes_read;
	off_t pos = end_pos;

	dprintf("==> file_lines()\n");

	if (n_lines == 0)
		return 1;

	dprintf("  start_pos: %lld\n", (unsigned long long) start_pos);
	dprintf("  end_pos: %lld\n", (unsigned long long) end_pos);

	/* Set `bytes_read' to the size of the last, probably partial, buffer;
	 *      0 < `bytes_read' <= `BUFSIZ'.
	 */
	bytes_read = (pos - start_pos) % BUFSIZ;
	if (bytes_read == 0)
		bytes_read = BUFSIZ;

	dprintf("  bytes_read: %zd\n", bytes_read);

	/* Make `pos' a multiple of `BUFSIZ' (0 if the file is short), so that
	 * all
	 *      reads will be on block boundaries, which might increase
	 *      efficiency.  */
	pos -= bytes_read;

	dprintf("  pos: %lld\n", pos);

	lseek(f->fd, pos, SEEK_SET);
	bytes_read = read(f->fd, buffer, bytes_read);

	/* Count the incomplete line on files that don't end with a newline. */
	if (bytes_read && buffer[bytes_read - 1] != '\n')
		--n_lines;

	do {
		size_t n = bytes_read;
		while (n) {
			const char *nl;
			nl = memrchr(buffer, '\n', n);
			if (nl == NULL)
				break;
		}

		/* XXX XXX XXX XXX */

		/* Not enough newlines in that buffer. Just print everything */
		if (pos == start_pos) {
			lseek(f->fd, start_pos, SEEK_SET);
			return 1;
		}
		pos -= BUFSIZ;
	} while (bytes_read > 0);

	return 1;
}

static void check_file(struct file_struct *f)
{
	struct stat new_stats;

	dprintf("==> check_file()\n");

	dprintf(" checking '%s'\n", f->name);
}

static int tail_forever(struct file_struct *f, int n_files)
{
	int i_fd, len;
	unsigned int i;
	struct inotify_event *inev;
	char buf[1000];

	dprintf("==> tail_forever()\n");

	i_fd = inotify_init();
	if (i_fd < 0)
		return -1;

	for (i = 0; i < n_files; i++) {
		f[i].i_watch = inotify_add_watch(i_fd, f[i].name, IN_ALL_EVENTS | IN_UNMOUNT);
		dprintf("  Watch (%d) added to '%s' (%d)\n", f[i].i_watch, f[i].name, i);
	}

	memset(&buf, 0, sizeof(buf));

	while (1) {
		int fd;
		ssize_t bytes_tailed = 0;

		len = read(i_fd, buf, sizeof(buf));
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

			/* We should at least catch the following
			 * events:
			 *  - IN_MODIFY, thats what we hopefully get
			 *  	most of the time
			 *  - IN_ATTRIB, still readable?
			 *  - IN_MOVE, reopen the file at the new
			 *	position?
			 *  - IN_DELETE_SELF, we need to check if the
			 *	file is still there or is really gone
			 *  - IN_MOVE_SELF, ditto
			 *  - IN_UNMOUNT, die gracefully
			 */
			if (inev->mask & IN_MODIFY) {
				dprintf("  File '%s' modified.\n", fil->name);
				check_file(fil);
				/* Dump new content */
			} else if (inev->mask & IN_ATTRIB) {
				dprintf("  File '%s' attributes changed.\n", fil->name);
				check_file(fil);
			} else if (inev->mask & IN_MOVE) {
			} else if (inev->mask & IN_DELETE_SELF) {
				dprintf("  File '%s' possibly deleted.\n", fil->name);
				check_file(fil);
			} else {
				/* Ignore */
			}

			/* Shift one event forward */
			len -= sizeof(struct inotify_event) + inev->len;
			inev = (struct inotify_event *) ((char *) inev + sizeof(struct inotify_event) + inev->len);
		}
	}

	/* XXX: Never reached. Catch SIGINT and handle it there? */
	for (i = 0; i < n_files; i++)
		inotify_rm_watch(i_fd, f[i].i_watch);

	return 0;
}

static int tail_lines(struct file_struct *f, uintmax_t n_lines)
{
	struct stat stats;
	off_t start_pos = -1;
	off_t end_pos;

	dprintf("==> tail_lines()\n");

	if (fstat(f->fd, &stats)) {
		perror("fstat()");
		exit(EXIT_FAILURE);
	}

	start_pos = lseek(f->fd, 0, SEEK_CUR);
	end_pos = lseek(f->fd, 0, SEEK_END);

	/* Use file_lines only if FD refers to a regular file for
	 * which lseek(... SEEK_END) worked.
	 */
	if (S_ISREG(stats.st_mode) && start_pos != -1 && start_pos < end_pos) {
		if (end_pos != 0 && !file_lines(f, n_lines, start_pos, end_pos))
			return -1;
	} else {
	/* Under very unlikely circumstances, it is possible to reach
	this point after positioning the file pointer to end of file
	via the `lseek (...SEEK_END)' above.  In that case, reposition
	the file pointer back to start_pos before calling pipe_lines.  */
/*	if (start_pos != -1)
	xlseek (fd, start_pos, SEEK_SET, pretty_filename);

	return pipe_lines (pretty_filename, fd, n_lines, read_pos);
*/
	}

	return 0;
}

static int tail(struct file_struct *f, uintmax_t n_units)
{
	dprintf("==> tail()\n");

	return tail_lines(f, n_units);
}

static int tail_file(struct file_struct *f, uintmax_t n_units)
{
	int ret = 0;

	dprintf("==> tail_file()\n");

	if (strcmp(f->name, "-") == 0) {
		f->fd = STDIN_FILENO;
	} else {
		f->fd = open(f->name, O_RDONLY);
	}

	if (f->fd == -1) {
		perror("open()");
	} else {
		if (print_headers)
			write_header(f);
		ret = tail(f, n_units);
	}

	return ret;
}

static void usage(void)
{
	dprintf("==> usage()\n");

	fprintf(stderr, "Usage: %s [OPTION]... [FILE]...\n", program_name);
}

static void parse_options(int argc, char *argv[], int *n_lines)
{
	int c;

	dprintf("==> parse_options()\n");

	while ((c = getopt_long(argc, argv, "hfn:qvV", long_options, NULL)) != -1) {
		switch (c) {
		case 'f':
			forever = 1;
			break;
		case 'n':
			*n_lines = strtol(optarg, NULL, 0);
			if (*n_lines < 0)
				*n_lines = 0;
			break;
		case 'q':
			print_headers = 0;
			break;
		case 'v':
			print_headers = 1;
			break;
		case 'V':
			fprintf(stdout, "%s %s by Tobias Klauser <tklauser@access.unizh.ch>\n",
					program_name, VERSION);
			break;
		case 'h':
		default:
			usage();
		}
	}
}

int main(int argc, char *argv[])
{
	int n_files = 0;
	int n_lines = DEFAULT_N_LINES;
	struct file_struct *files;
	char **filenames;
	unsigned int i;

	parse_options(argc, argv, &n_lines);

	/* Do we have some files to read from? */
	if (optind < argc) {
		n_files = argc - optind;
		filenames = argv + optind;
	} else {	/* OK, we read from stdin */
		static char *dummy_stdin = "-";

		n_files = 1;
		filenames = &dummy_stdin;

		/*
		 * POSIX says that -f is ignored if no file operand is specified
		 * and standard input is a pipe.
		 */
		if (forever) {
			struct stat stats;
			/* stdin might be a socket on some systems */
			if ((fstat(STDIN_FILENO, &stats) == 0)
					&& (S_ISFIFO(stats.st_mode) || S_ISSOCK(stats.st_mode)))
				forever = 0;
		}

		fprintf(stderr, "Reading from stdin is currently not supported.\n");
	}

	files = malloc(n_files * sizeof(struct file_struct));
	for (i = 0; i < n_files; i++) {
		files[i].name = filenames[i];
		tail_file(&files[i], n_lines);
	}

	if (forever)
		tail_forever(files, n_files);

	free(files);

	exit(EXIT_SUCCESS);
}
