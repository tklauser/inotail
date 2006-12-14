/*
 * Copyright (C) 2005-2006, Tobias Klauser <tklauser@distanz.ch>
 *
 * Licensed under the terms of the GNU General Public License; version 2 or later.
 */

#ifndef _INOTAIL_H
#define _INOTAIL_H

/* Number of items to tail. */
#define DEFAULT_N_LINES 10

/* tail modes */
enum { M_LINES, M_BYTES };

/* Every tailed file is represented as a file_struct */
struct file_struct {
	char *name;		/* Name of file (or '-' for stdin) */
	int fd;			/* File descriptor (or -1 if file is not open */
	off_t st_size;		/* File size */
	unsigned ignore;	/* Whether to ignore the file in further processing */
	int i_watch;		/* Inotify watch associated with file_struct */
};

#define IS_PIPELIKE(mode) \
	(S_ISFIFO(mode) || S_ISSOCK(mode))

/* inotail works on these file types */
#define IS_TAILABLE(mode) \
	(S_ISREG(mode) || IS_PIPELIKE(mode) || S_ISCHR(mode))

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
