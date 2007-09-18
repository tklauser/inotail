/*
 * Copyright (C) 2005-2007, Tobias Klauser <tklauser@distanz.ch>
 *
 * Licensed under the terms of the GNU General Public License; version 2 or later.
 */

#ifndef _INOTAIL_H
#define _INOTAIL_H

#include <sys/types.h>

#define DEFAULT_N_LINES 10	/* Number of items to tail. */

/* tail modes */
enum { M_LINES, M_BYTES };

/* Every tailed file is represented as a file_struct */
struct file_struct {
	char *name;		/* Name of file (or '-' for stdin) */
	int fd;			/* File descriptor (or -1 if file is not open */
	off_t size;		/* File size */
	blksize_t blksize;	/* Blocksize for filesystem I/O */
	unsigned ignore;	/* Whether to ignore the file in further processing */
	int i_watch;		/* Inotify watch associated with file_struct */
};

/* struct for linked list of buffers/lines in tail_pipe_lines */
struct line_buf {
	char buf[BUFSIZ];
	size_t n_lines;
	size_t n_bytes;
	struct line_buf *next;
};

/* struct for linked list of byte buffers in tail_pipe_bytes */
struct char_buf {
	char buf[BUFSIZ];
	size_t n_bytes;
	struct char_buf *next;
};

#define IS_PIPELIKE(mode) \
	(S_ISFIFO(mode) || S_ISSOCK(mode))

/* inotail works on these file types */
#define IS_TAILABLE(mode) \
	(S_ISREG(mode) || IS_PIPELIKE(mode) || S_ISCHR(mode))

#define is_digit(c) ((c) >= '0' && (c) <= '9')

#ifdef DEBUG
# define dprintf(fmt, args...) fprintf(stderr, fmt, ##args)
#else
# define dprintf(fmt, args...)
#endif /* DEBUG */

#ifdef __GNUC__
# define unlikely(x) __builtin_expect(!!(x), 0)
#else
# define unlikely(x) (x)
#endif /* __GNUC__ */

#endif /* _INOTAIL_H */
