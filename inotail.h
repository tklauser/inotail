/*
 * Copyright (C) 2005-2011, Tobias Klauser <tklauser@distanz.ch>
 *
 * This file is licensed under the terms of the GNU General Public License;
 * version 2 or later.
 */

#ifndef _INOTAIL_H
#define _INOTAIL_H

#include <sys/types.h>
#include <sys/inotify.h>

/* Number of items to tail. */
#define DEFAULT_N_LINES		10
/* inotify event buffer length for one file */
#define INOTIFY_BUFLEN		(4 * sizeof(struct inotify_event))
/* inotify events to watch for on tailed files */
#define INOTAIL_WATCH_MASK	\
	(IN_MODIFY|IN_DELETE_SELF|IN_MOVE_SELF|IN_UNMOUNT|IN_CREATE)

/* tail modes */
enum tail_mode { M_LINES, M_BYTES };
/* follow modes */
enum follow_mode {
	FOLLOW_NONE = 0,	/* Do not follow the file at all */
	FOLLOW_DESCRIPTOR,	/* Follow the file by fd */
	FOLLOW_NAME		/* Follow the file by name */
};

/* Every tailed file is represented as a file_struct */
struct file_struct {
	char *name;		/* Name of file (or '-' for stdin) */
	int fd;			/* File descriptor (or -1 if file is not open) */
	off_t size;		/* File size */
	blksize_t blksize;	/* Blocksize for filesystem I/O */
	unsigned ignore;	/* Whether to ignore the file in further processing */
	int i_watch;		/* Inotify watch associated with file_struct */
};

#define IS_PIPELIKE(mode) \
	(S_ISFIFO(mode) || S_ISSOCK(mode))

/* inotail works on these file types */
#define IS_TAILABLE(mode) \
	(S_ISREG(mode) || IS_PIPELIKE(mode) || S_ISCHR(mode))

#define is_digit(c) ((c) >= '0' && (c) <= '9')

#ifdef __GNUC__
# define __noreturn	__attribute ((noreturn))
# define likely(x)	__builtin_expect(!!(x), 1)
# define unlikely(x)	__builtin_expect(!!(x), 0)
#else
# define __noreturn
# define likely(x)	(x)
# define unlikely(x)	(x)
#endif /* __GNUC__ */

#ifdef DEBUG
# define dprintf(fmt, args...) fprintf(stderr, fmt, ##args)
#else
# define dprintf(fmt, args...)
#endif /* DEBUG */

#endif /* _INOTAIL_H */
