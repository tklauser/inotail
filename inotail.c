/*
 * inotail.c
 * A fast implementation of tail which uses the inotify API present in
 * recent versions of the Linux kernel.
 *
 * Copyright (C) 2005-2009, Tobias Klauser <tklauser@distanz.ch>
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
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/inotify.h>

#include "inotail.h"

#define PROGRAM_NAME "inotail"

/* Print header with filename before tailing the file? */
static char verbose = 0;
/* Tailing relative to begin or end of file? */
static char from_begin = 0;
/* Retry reading the file if it is inaccessible? */
static char retry = 0;
/* Number of ignored files */
static int n_ignored = 0;

/* Pseudo-characters for long options that have no equivalent short option */
enum {
	RETRY_OPTION = CHAR_MAX + 1,
	MAX_UNCHANGED_STATS_OPTION,
	PID_OPTION
};

/* Command line options
 * The ones marked with 'X' are here just for compatibility reasons and have no
 * effect on inotail */
static const struct option const long_opts[] = {
	{ "bytes", required_argument, NULL, 'c' },
	{ "follow", optional_argument, NULL, 'f' },
	{ "help", no_argument, NULL, 'h' },
	{ "lines", required_argument, NULL, 'n' },
	/* X */ { "max-unchanged-stats", required_argument, NULL, MAX_UNCHANGED_STATS_OPTION },
	/* X */ { "pid", required_argument, NULL, PID_OPTION },
	{ "quiet", no_argument, NULL, 'q' },
	{ "retry", no_argument, NULL, RETRY_OPTION },
	{ "silent", no_argument, NULL, 'q' },
	/* X */ { "sleep-interval", required_argument, NULL, 's' },
	{ "verbose", no_argument, NULL, 'v' },
	{ "version", no_argument, NULL, 'V' },
	{ NULL, 0, NULL, 0 }
};

static void *emalloc(const size_t size)
{
	void *ret = malloc(size);

	if (unlikely(!ret)) {
		fprintf(stderr, "Error: Failed to allocate %zu bytes of memory (%s)\n", size, strerror(errno));
		exit(EXIT_FAILURE);
	}

	return ret;
}

static void usage(const int status)
{
	fprintf(stdout, "Usage: %s [OPTION]... [FILE]...\n\n"
			"        --retry      keep trying to open a file even if it is not\n"
			"                     accessible at start or becomes inaccessible\n"
			"                     later; useful when following by name\n"
			"  -c N, --bytes=N    output the last N bytes\n"
			"  -f,   --follow     output as the file grows\n"
			"  -n N, --lines=N    output the last N lines (default: %d)\n"
			"  -q,   --quiet, --slient\n"
			"                     never print headers with file names\n"
			"  -v,   --verbose    always print headers with file names\n"
			"  -h,   --help       show this help and exit\n"
			"  -V,   --version    show version and exit\n\n"
			"If the first character of N (the number of bytes or lines) is a `+',\n"
			"begin printing with the Nth item from the start of each file, otherwise,\n"
			"print the last N items in the file.\n", PROGRAM_NAME, DEFAULT_N_LINES);

	exit(status);
}

static inline void setup_file(struct file_struct *f)
{
	f->fd = f->i_watch = -1;
	f->size = 0;
	f->blksize = BUFSIZ;
	f->ignore = 0;
}

static void ignore_file(struct file_struct *f)
{
	if (f->fd != -1) {
		close(f->fd);
		f->fd = -1;
	}
	if (!f->ignore) {
		f->ignore = 1;
		++n_ignored;
	}
}

static inline char *pretty_name(char *filename)
{
	return (strcmp(filename, "-") == 0) ? "standard input" : filename;
}

static void write_header(char *filename)
{
	static unsigned short first_file = 1;
	static char *last = NULL;

	if (last != filename) {
		fprintf(stdout, "%s==> %s <==\n", (first_file ? "" : "\n"), pretty_name(filename));
		fflush(stdout);		/* Make sure the header is printed before the content */
	}

	first_file = 0;
	last = filename;
}

static off_t lines_to_offset_from_end(struct file_struct *f, unsigned long n_lines)
{
	off_t offset = f->size;
	char *buf = emalloc(f->blksize);

	/* We also count the last \n */
	++n_lines;

	while (offset > 0 && n_lines > 0) {
		int i;
		ssize_t rc, block_size = f->blksize;	/* Size of the current block we're reading */

		if (offset < block_size)
			block_size = offset;

		/* Start of current block */
		offset -= block_size;

		if (lseek(f->fd, offset, SEEK_SET) == (off_t) -1) {
			fprintf(stderr, "Error: Could not seek in file '%s' (%s)\n", f->name, strerror(errno));
			free(buf);
			return -1;
		}

		rc = read(f->fd, buf, block_size);
		if (unlikely(rc < 0)) {
			fprintf(stderr, "Error: Could not read from file '%s' (%s)\n", f->name, strerror(errno));
			free(buf);
			return -1;
		}

		for (i = block_size - 1; i > 0; i--) {
			if (buf[i] == '\n') {
				if (--n_lines == 0) {
					free(buf);
					return offset + i + 1; /* We don't want the first \n */
				}
			}
		}
	}

	free(buf);
	return offset;
}

static off_t lines_to_offset_from_begin(struct file_struct *f, unsigned long n_lines)
{
	char *buf;
	off_t offset = 0;

	/* tail everything for 'inotail -n +0' */
	if (n_lines == 0)
		return 0;

	n_lines--;
	buf = emalloc(f->blksize);

	while (offset <= f->size && n_lines > 0) {
		int i;
		ssize_t rc, block_size = f->blksize;

		if (lseek(f->fd, offset, SEEK_SET) == (off_t) -1) {
			fprintf(stderr, "Error: Could not seek in file '%s' (%s)\n", f->name, strerror(errno));
			free(buf);
			return -1;
		}

		rc = read(f->fd, buf, block_size);
		if (unlikely(rc < 0)) {
			fprintf(stderr, "Error: Could not read from file '%s' (%s)\n", f->name, strerror(errno));
			free(buf);
			return -1;
		} else if (rc < block_size)
			block_size = rc;

		for (i = 0; i < block_size; i++) {
			if (buf[i] == '\n') {
				if (--n_lines == 0) {
					free(buf);
					return offset + i + 1;
				}
			}
		}

		offset += block_size;
	}

	free(buf);
	return offset;
}

static off_t lines_to_offset(struct file_struct *f, unsigned long n_lines)
{
	if (from_begin)
		return lines_to_offset_from_begin(f, n_lines);
	else
		return lines_to_offset_from_end(f, n_lines);
}

static off_t bytes_to_offset(struct file_struct *f, unsigned long n_bytes)
{
	off_t offset = 0;

	/* tail everything for 'inotail -c +0' */
	if (from_begin) {
		if (n_bytes > 0)
			offset = (off_t) n_bytes - 1;
	} else if ((off_t) n_bytes < f->size)
		offset = f->size - (off_t) n_bytes;

	/* Otherwise offset stays 0 (begin of file) */

	return offset;
}

static int tail_pipe_from_begin(struct file_struct *f, unsigned long n_units, const char mode)
{
	int bytes_read = 0;
	char buf[BUFSIZ];

	if (n_units)
		n_units--;

	while (n_units > 0) {
		if ((bytes_read = read(f->fd, buf, BUFSIZ)) <= 0) {
			/* Interrupted by a signal, retry reading */
			if (bytes_read < 0 && (errno == EINTR || errno == EAGAIN))
				continue;
			else
				return bytes_read;
		}

		if (mode == M_LINES) {
			int i;
			ssize_t block_size = BUFSIZ;

			if (bytes_read < BUFSIZ)
				block_size = bytes_read;

			for (i = 0; i < block_size; i++) {
				if (buf[i] == '\n') {
					if (--n_units == 0)
						break;
				}
			}

			/* Print remainder of the current block */
			if (++i < block_size)
				write(STDOUT_FILENO, &buf[i], bytes_read - i);
		} else {
			if ((unsigned long) bytes_read > n_units) {
				write(STDOUT_FILENO, &buf[n_units], bytes_read - n_units);
				bytes_read = n_units;
			}

			n_units -= bytes_read;
		}
	}

	while ((bytes_read = read(f->fd, buf, BUFSIZ)) > 0)
		write(STDOUT_FILENO, buf, (size_t) bytes_read);

	return 0;
}

static int tail_pipe_lines(struct file_struct *f, unsigned long n_lines)
{
	struct line_buf {
		char buf[BUFSIZ];
		size_t n_lines;
		size_t n_bytes;
		struct line_buf *next;
	} *first, *last, *tmp;
	int rc;
	unsigned long total_lines = 0;
	const char *p;

	if (from_begin)
		return tail_pipe_from_begin(f, n_lines, M_LINES);

	if (n_lines == 0)
		return 0;	/* No lines to tail */

	first = last = emalloc(sizeof(struct line_buf));
	first->n_bytes = first->n_lines = 0;
	first->next = NULL;
	tmp = emalloc(sizeof(struct line_buf));

	while (1) {
		if ((rc = read(f->fd, tmp->buf, BUFSIZ)) <= 0) {
			if (rc < 0 && (errno == EINTR || errno == EAGAIN))
				continue;
			else
				break;	/* No more data to read */
		}
		tmp->n_bytes = rc;
		tmp->n_lines = 0;
		tmp->next = NULL;
		p = tmp->buf;

		/* Count the lines in the current buffer */
		while ((p = memchr(p, '\n', tmp->buf + rc - p))) {
			++p;
			++tmp->n_lines;
		}
		total_lines += tmp->n_lines;

		/* Try to append to the previous buffer if there's enough free
		 * space
		 */
		if (tmp->n_bytes + last->n_bytes < BUFSIZ) {
			memcpy(&last->buf[last->n_bytes], tmp->buf, tmp->n_bytes);
			last->n_bytes += tmp->n_bytes;
			last->n_lines += tmp->n_lines;
		} else {
			/* Add buffer to the list */
			last->next = tmp;
			last = last->next;
			/* We read more than n_lines lines, reuse the first
			 * buffer.
			 */
			if (total_lines - first->n_lines > n_lines) {
				tmp = first;
				total_lines -= first->n_lines;
				first = first->next;
			} else
				tmp = emalloc(sizeof(struct line_buf));
		}
	}

	free(tmp);

	if (rc < 0) {
		fprintf(stderr, "Error: Could not read from %s\n", pretty_name(f->name));
		goto out;
	}

	if (last->n_bytes == 0)
		goto out;

	/* Count incomplete lines */
	if (last->buf[last->n_bytes - 1] != '\n') {
		++last->n_lines;
		++total_lines;
	}

	/* Skip unneeded buffers */
	for (tmp = first; total_lines - tmp->n_lines > n_lines; tmp = tmp->next)
		total_lines -= tmp->n_lines;

	p = tmp->buf;

	/* Read too many lines, advance */
	if (total_lines > n_lines) {
		unsigned long j;
		for (j = total_lines - n_lines; j; --j) {
			p = memchr(p, '\n', tmp->buf + tmp->n_bytes - p);
			++p;
		}
	}

	if ((rc = write(STDOUT_FILENO, p, tmp->buf + tmp->n_bytes - p)) <= 0) {
		/* e.g. when writing to a pipe which gets closed */
		fprintf(stderr, "Error: Could not write to stdout (%s)\n", strerror(errno));
		goto out;
	}

	for (tmp = tmp->next; tmp; tmp = tmp->next)
		if ((rc = write(STDOUT_FILENO, tmp->buf, tmp->n_bytes)) <= 0) {
			fprintf(stderr, "Error: Could not write to stdout (%s)\n", strerror(errno));
			goto out;
		}

	rc = 0;
out:
	while (first) {
		tmp = first->next;
		free(first);
		first = tmp;
	}

	return rc;
}

/* TODO: Merge some parts (especially buffer handling) with tail_pipe_lines() */
static int tail_pipe_bytes(struct file_struct *f, unsigned long n_bytes)
{
	struct char_buf {
		char buf[BUFSIZ];
		size_t n_bytes;
		struct char_buf *next;
	} *first, *last, *tmp;
	int rc;
	unsigned long total_bytes = 0;
	unsigned long i = 0;		/* Index into buffer */

	if (from_begin)
		return tail_pipe_from_begin(f, n_bytes, M_BYTES);

	/* XXX: Needed? */
	if (n_bytes == 0)
		return 0;

	first = last = emalloc(sizeof(struct char_buf));
	first->n_bytes = 0;
	first->next = NULL;
	tmp = emalloc(sizeof(struct char_buf));

	while(1) {
		if ((rc = read(f->fd, tmp->buf, BUFSIZ)) <= 0) {
			if (rc < 0 && (errno == EINTR || errno == EAGAIN))
				continue;
			else
				break;	/* No more data to read */
		}
		total_bytes += rc;
		tmp->n_bytes = rc;
		tmp->next = NULL;

		/* Try to append to the previous buffer if there's enough free
		 * space
		 */
		if (tmp->n_bytes + last->n_bytes < BUFSIZ) {
			memcpy(&last->buf[last->n_bytes], tmp->buf, tmp->n_bytes);
			last->n_bytes += tmp->n_bytes;
		} else {
			/* Add buffer to the list */
			last->next = tmp;
			last = last->next;
			/* We read more than n_bytess bytes, reuse the first
			 * buffer.
			 */
			if (total_bytes - first->n_bytes > n_bytes) {
				tmp = first;
				total_bytes -= first->n_bytes;
				first = first->next;
			} else
				tmp = emalloc(sizeof(struct char_buf));
		}
	}

	free(tmp);

	if (rc < 0) {
		fprintf(stderr, "Error: Could not read from %s\n", pretty_name(f->name));
		goto out;
	}

	/* Skip unneeded buffers */
	for (tmp = first; total_bytes - tmp->n_bytes > n_bytes; tmp = tmp->next)
		total_bytes -= tmp->n_bytes;

	/* Read too many bytes, advance */
	if (total_bytes > n_bytes)
		i = total_bytes - n_bytes;

	if ((rc = write(STDOUT_FILENO, &tmp->buf[i], tmp->n_bytes - i)) <= 0) {
		/* e.g. when writing to a pipe which gets closed */
		fprintf(stderr, "Error: Could not write to stdout (%s)\n", strerror(errno));
		goto out;
	}

	for (tmp = tmp->next; tmp; tmp = tmp->next)
		if ((rc = write(STDOUT_FILENO, tmp->buf, tmp->n_bytes)) <= 0) {
			fprintf(stderr, "Error: Could not write to stdout (%s)\n", strerror(errno));
			goto out;
		}


	rc = 0;
out:
	while (first) {
		tmp = first->next;
		free(first);
		first = tmp;
	}

	return rc;
}

static int tail_file(struct file_struct *f, unsigned long n_units, mode_t mode, char forever)
{
	ssize_t bytes_read = 0;
	off_t offset = 0;
	char *buf;
	struct stat finfo;

	if (strcmp(f->name, "-") == 0)
		f->fd = STDIN_FILENO;
	else {
		f->fd = open(f->name, O_RDONLY);
		if (unlikely(f->fd < 0)) {
			fprintf(stderr, "Error: Could not open file '%s' (%s)\n", f->name, strerror(errno));
			return -1;
		}
	}

	if (fstat(f->fd, &finfo) < 0) {
		fprintf(stderr, "Error: Could not stat file '%s' (%s)\n", f->name, strerror(errno));
		return -1;
	}

	if (!IS_TAILABLE(finfo.st_mode)) {
		fprintf(stderr, "Error: '%s' of unsupported file type\n", f->name);
		return -1;
	}

	/* Cannot seek on these */
	if (IS_PIPELIKE(finfo.st_mode) || f->fd == STDIN_FILENO) {
		if (verbose)
			write_header(f->name);

		if (mode == M_LINES)
			return tail_pipe_lines(f, n_units);
		else
			return tail_pipe_bytes(f, n_units);
	}

	f->size = finfo.st_size;
	if (likely(finfo.st_blksize > 0))
		f->blksize = finfo.st_blksize;

	if (mode == M_LINES)
		offset = lines_to_offset(f, n_units);
	else
		offset = bytes_to_offset(f, n_units);

	/* We only get negative offsets on errors */
	if (unlikely(offset < 0))
		return -1;

	if (lseek(f->fd, offset, SEEK_SET) == (off_t) -1) {
		fprintf(stderr, "Error: Could not seek in file '%s' (%s)\n", f->name, strerror(errno));
		return -1;
	}

	if (verbose)
		write_header(f->name);

	buf = emalloc(f->blksize);

	while ((bytes_read = read(f->fd, buf, f->blksize)) > 0)
		write(STDOUT_FILENO, buf, (size_t) bytes_read);

	if (!forever) {
		if (close(f->fd) < 0) {
			fprintf(stderr, "Error: Could not close file '%s' (%s)\n", f->name, strerror(errno));
			free(buf);
			return -1;
		}
	}
	/* Let the fd open otherwise, we'll need it */

	free(buf);
	return 0;
}

static int handle_inotify_event(struct inotify_event *inev, struct file_struct *f)
{
	int ret = 0;

	if (inev->mask & IN_MODIFY) {
		char *fbuf;
		ssize_t bytes_read;
		struct stat finfo;

		if (verbose)
			write_header(f->name);

		if ((ret = fstat(f->fd, &finfo)) < 0) {
			fprintf(stderr, "Error: Could not stat file '%s' (%s)\n", f->name, strerror(errno));
			goto ignore;
		}

		/* Regular file got truncated */
		if (S_ISREG(finfo.st_mode) && finfo.st_size < f->size) {
			fprintf(stderr, "File '%s' truncated\n", f->name);
			f->size = finfo.st_size;
		}

		/* Seek to old file size */
		if (!IS_PIPELIKE(finfo.st_mode) && (ret = lseek(f->fd, f->size, SEEK_SET)) == (off_t) -1) {
			fprintf(stderr, "Error: Could not seek in file '%s' (%s)\n", f->name, strerror(errno));
			goto ignore;
		}

		fbuf = emalloc(f->blksize);

		while ((bytes_read = read(f->fd, fbuf, f->blksize)) != 0) {
			write(STDOUT_FILENO, fbuf, (size_t) bytes_read);
			f->size += bytes_read;
		}

		free(fbuf);
		return ret;
	} else if (inev->mask & IN_DELETE_SELF) {
		fprintf(stderr, "File '%s' deleted.\n", f->name);
	} else if (inev->mask & IN_MOVE_SELF) {
		fprintf(stderr, "File '%s' moved.\n", f->name);
		return 0;
	} else if (inev->mask & IN_UNMOUNT) {
		fprintf(stderr, "Device containing file '%s' unmounted.\n", f->name);
	} else if (inev->mask & IN_IGNORED) {
		return 0;
	}

ignore:
	ignore_file(f);
	return ret;
}

static int watch_files(struct file_struct *files, int n_files)
{
	int ifd, i;
	char buf[n_files * INOTIFY_BUFLEN];

	ifd = inotify_init();
	if (errno == ENOSYS) {
		fprintf(stderr, "Error: inotify is not supported by the kernel you're currently running.\n");
		exit(EXIT_FAILURE);
	} else if (unlikely(ifd < 0)) {
		fprintf(stderr, "Error: Could not initialize inotify (%s)\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	for (i = 0; i < n_files; i++) {
		if (!files[i].ignore) {
			files[i].i_watch = inotify_add_watch(ifd, files[i].name,
						IN_MODIFY|IN_DELETE_SELF|IN_MOVE_SELF|IN_UNMOUNT);

			if (files[i].i_watch < 0) {
				fprintf(stderr, "Error: Could not create inotify watch on file '%s' (%s)\n",
						files[i].name, strerror(errno));
				ignore_file(&files[i]);
			}
		}
	}

	while (n_ignored < n_files) {
		ssize_t len;
		int ev_idx = 0;

		len = read(ifd, buf, (n_files * INOTIFY_BUFLEN));
		if (unlikely(len < 0)) {
			/* Some signal, likely ^Z/fg's STOP and CONT interrupted the inotify read, retry */
			if (errno == EINTR || errno == EAGAIN)
				continue;
			else {
				fprintf(stderr, "Error: Could not read inotify events (%s)\n", strerror(errno));
				close(ifd);
				exit(EXIT_FAILURE);
			}
		}

		while (ev_idx < len) {
			struct inotify_event *inev;
			struct file_struct *f = NULL;

			inev = (struct inotify_event *) &buf[ev_idx];

			/* Which file has produced the event? */
			for (i = 0; i < n_files; i++) {
				if (!files[i].ignore
						&& files[i].fd >= 0
						&& files[i].i_watch == inev->wd) {
					f = &files[i];
					break;
				}
			}

			if (unlikely(!f))
				break;

			if (handle_inotify_event(inev, f) < 0)
				break;

			ev_idx += sizeof(struct inotify_event) + inev->len;
		}
	}

	close(ifd);

	return -1;
}

int main(int argc, char **argv)
{
	int i, c, option_idx, ret = 0;
	int n_files;
	unsigned long n_units = DEFAULT_N_LINES;
	char mode = M_LINES;
	char forever = 0;
	char **filenames;
	struct file_struct *files = NULL;

	while ((c = getopt_long(argc, argv, "c:n:fqvVhs:", long_opts, &option_idx)) != -1) {
		switch (c) {
		case 'c':
			mode = M_BYTES;
			/* fall through */
		case 'n':
			if (*optarg == '+') {
				from_begin = 1;
				++optarg;
			} else if (*optarg == '-')
				++optarg;

			/* TODO: Better sanity check */
			if (!is_digit(*optarg)) {
				fprintf(stderr, "Error: Invalid number of %s: %s\n",
						(mode == M_LINES ? "lines" : "bytes"), optarg);
				exit(EXIT_FAILURE);
			}
			n_units = strtoul(optarg, NULL, 0);
			break;
                case 'f':
			forever = 1;
			break;
		case 'q':
			verbose = 0;
			break;
		case 'v':
			verbose = 1;
			break;
		case RETRY_OPTION:
			retry = 1;
			break;
		case 'V':
			fprintf(stdout, "%s %s\n", PROGRAM_NAME, VERSION);
			exit(EXIT_SUCCESS);
		case 'h':
			usage(EXIT_SUCCESS);

		/* Options with no effect in inotail, they just emit a warning */
		case 's':
			/* No sleep interval because we're never sleeping.
			 * That's the whole point of inotail! */
			fprintf(stderr, "Warning: Option '-s' has no effect, ignoring\n");
			break;
		case PID_OPTION:
			/* Watching the PID is not possible because of the
			 * blocking read on the inotify fd */
		case MAX_UNCHANGED_STATS_OPTION:
			/* inotail (will) watch the containing directory for the
			 * file being moved or deleted, so there is no need for
			 * this either */
			fprintf(stderr, "Warning: Option '--%s' has no effect, ignoring\n", long_opts[option_idx].name);
			break;
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
		n_files = 1;
		filenames = &dummy_stdin;

		/* POSIX says that -f is ignored if no file operand is
		   specified and standard input is a pipe. */
		if (forever) {
			struct stat finfo;
			int rc = fstat(STDIN_FILENO, &finfo);

			if (unlikely(rc == -1)) {
				fprintf(stderr, "Error: Could not stat stdin (%s)\n", strerror(errno));
				exit(EXIT_FAILURE);
			}

			if (rc == 0 && IS_PIPELIKE(finfo.st_mode))
				forever = 0;
		}
	}

	files = emalloc(n_files * sizeof(struct file_struct));

	for (i = 0; i < n_files; i++) {
		files[i].name = filenames[i];
		setup_file(&files[i]);
		ret = tail_file(&files[i], n_units, mode, forever);
		if (ret < 0)
			ignore_file(&files[i]);
	}

	if (forever)
		ret = watch_files(files, n_files);

	free(files);

	return ret;
}
