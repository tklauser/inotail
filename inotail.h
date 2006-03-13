#ifndef _INOTAIL_H
#define _INOTAIL_H

#include <limits.h>
#include <getopt.h>
#include <stdlib.h>

/* Number of items to tail. */
#define DEFAULT_N_LINES 10

/* Every tailed file is represented as a file_struct */
struct file_struct {
	char *name;	/* Name of file (or '-' for stdin) */
	int fd;		/* File descriptor (or -1 if file is not open */
	int ignore:1;	/* Ignore file? */

	int i_watch;	/* Inotify watch associated with file_struct */

};

struct option const long_options[] = {
	{"lines", required_argument, NULL, 'n'},
	{"quiet", no_argument, NULL, 'q'},
	{"silent", no_argument, NULL, 'q'},
	{"verbose", no_argument, NULL, 'v'},
	{"help", no_argument, NULL, 'h'},
	{"version", no_argument, NULL, 'V'},
	{NULL, 0, NULL, 0}
};

#ifdef DEBUG
#define dprintf(fmt, args...) fprintf(stderr, fmt, ##args)
#else
#define dprintf(fmt, args...)
#endif /* DEBUG */

#endif /* _INOTAIL_H */
