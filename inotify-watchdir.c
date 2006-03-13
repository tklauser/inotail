/*
 * initofy-watchdir.c
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>

#include "inotify.h"
#include "inotify-syscalls.h"

static void print_event(int mask)
{
	if (mask & IN_ISDIR)
		printf("(dir) ");
	else
		printf("(file) ");

	if (mask & IN_ACCESS)
		printf("ACCESS ");
	if (mask & IN_MODIFY)
		printf("MODIFY ");
	if (mask & IN_ATTRIB)
		printf("ATTRIB ");
	if (mask & IN_CLOSE)
		printf("CLOSE ");
	if (mask & IN_OPEN)
		printf("OPEN ");
	if (mask & IN_MOVED_FROM)
		printf("MOVED_FROM ");
	if (mask & IN_MOVED_TO)
		printf("MOVED_TO ");
	if (mask & IN_MOVE_SELF)
		printf("MOVE_SELF ");
	if (mask & IN_DELETE)
		printf("DELETE ");
	if (mask & IN_CREATE)
		printf("CREATE ");
	if (mask & IN_DELETE_SELF)
		printf("DELETE_SELF ");
	if (mask & IN_UNMOUNT)
		printf("UNMOUNT ");
	if (mask & IN_Q_OVERFLOW)
		printf("Q_OVERFLOW ");
	if (mask & IN_IGNORED)
		printf("IGNORED " );

	printf("(0x%08x), ", mask);
}

int main(int argc, char *argv[])
{
	int fd, watch, len, ret;
	struct inotify_event *inev;
	char buf[1000];

	if (argc != 2) {
		printf("Usage: %s <path>\n", argv[0]);
		exit(-1);
	}

	fd = inotify_init();
	if (fd < 0)
		exit(-2);

	watch = inotify_add_watch(fd, argv[1], IN_ALL_EVENTS|IN_UNMOUNT);

	memset(&buf, 0, sizeof(buf));

	while (1) {
		len = read(fd, buf, sizeof(buf));
		inev = (struct inotify_event *) &buf;
		while (len > 0) {
			printf("wd=%04x, ", inev->wd);
			print_event(inev->mask);
			printf("cookie=%04x, len=%04x, name=\"%s\"\n", inev->cookie, inev->len, inev->name);

			len -= sizeof(struct inotify_event) + inev->len;
			inev = (struct inotify_event *) ((char *) inev + sizeof(struct inotify_event) + inev->len);
		}
	}

	ret = inotify_rm_watch(fd, watch);

	return ret;
}
