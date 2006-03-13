/*
 * simpletail.c
 */

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

#define BUFFER_SIZE 4096

off_t lines(int fd, int file_size, unsigned int nr_lines)
{
	int i;
	char buf[BUFFER_SIZE];
	off_t offset = file_size;

	/* Negative offsets don't make sense here */
	if (offset < 0)
		offset = 0;

	while (offset > 0 && nr_lines > 0) {
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
				nr_lines--;

				if (nr_lines == 0)
					break;
			}
		}
	}

	if (nr_lines == 0)
		offset += i + 1; /* We don't want the first \n */

	return offset;
}

int watch_file(const char *filename, off_t offset)
{
	int ifd, watch;
	struct inotify_event *inev;
	char buf[BUFFER_SIZE];

	dprintf(">> Watching %s\n", filename);

	ifd = inotify_init();
	if (ifd < 0) {
		perror("inotify_init()");
		exit(-2);
	}

	watch = inotify_add_watch(ifd, filename, IN_MODIFY|IN_DELETE_SELF|IN_MOVE_SELF|IN_UNMOUNT);

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

				dprintf("  File '%s' modified.\n", filename);
				dprintf("  offset: %lu.\n", offset);

				ffd = open(filename, O_RDONLY);
				if (fstat(ffd, &finfo) < 0) {
					perror("fstat()");
					return -1;
				}

				/* XXX: block_size could be bigger than BUFFER_SIZE */
				block_size = finfo.st_size - offset;
				if (block_size < 0)
					block_size = 0;

				lseek(ffd, offset, SEEK_SET);
				while (read(ffd, &fbuf, BUFFER_SIZE) != 0) {
					write(STDOUT_FILENO, fbuf, block_size);
				}

				offset = finfo.st_size;

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
	int fd;
	int nr_lines = 0;
	int ret = 0;
	struct stat finfo;
	char buf[BUFFER_SIZE];
	off_t offset = 0;

	if (argc < 3) {
		fprintf(stderr, "%s <nr-lines> <file> <forever?>\n", argv[0]);
		return -1;
	}

	nr_lines = strtol(argv[1], NULL, 0) + 1;
	fd = open(argv[2], O_RDONLY);

	if (fd < 0) {
		perror("open()");
		return -1;
	}

	if (fstat(fd, &finfo) < 0) {
		perror("fstat()");
		return -1;
	}

	offset = lines(fd, finfo.st_size, nr_lines);
	dprintf("  offset: %lu.\n", offset);

	lseek(fd, offset, SEEK_SET);
	while (read(fd, &buf, BUFFER_SIZE) != 0) {
		write(STDOUT_FILENO, buf, finfo.st_size - offset);
	}

	close(fd);

	if (argv[3])
		ret = watch_file(argv[2], finfo.st_size);

	return ret;
}
