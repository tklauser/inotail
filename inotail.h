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

	unsigned ignore;	/* Wheter to ignore the file in further processing */

	int i_watch;		/* Inotify watch associated with file_struct */
};

#ifdef DEBUG
# define dprintf(fmt, args...) fprintf(stderr, fmt, ##args)
#else
# define dprintf(fmt, args...)
#endif /* DEBUG */

/* Taken from linux kernel source tree */
#define unlikely(x) __builtin_expect(!!(x), 0)

#endif /* _INOTAIL_H */
