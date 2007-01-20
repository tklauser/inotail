/*
 * Inotify syscall numbers
 * Taken from the Linux kernel source tree
 *
 * Licensed under the terms of the GNU General Public License Version 2.
 *
 * Copyright (c) 2006-2007 Tobias Klauser <tklauser@distanz.ch>
 */

#ifndef _LINUX_INOTIFY_SYSCALLS_H
#define _LINUX_INOTIFY_SYSCALLS_H

#include <sys/syscall.h>
/* glibc already defines them for some architectures */
#ifndef __NR_inotify_init
#if defined(__i386__)
# define __NR_inotify_init	291
# define __NR_inotify_add_watch	292
# define __NR_inotify_rm_watch	293
#elif defined(__x86_64__)
# define __NR_inotify_init	253
# define __NR_inotify_add_watch	254
# define __NR_inotify_rm_watch	255
#elif defined(__powerpc__) || defined(__powerpc64__)
# define __NR_inotify_init	275
# define __NR_inotify_add_watch	276
# define __NR_inotify_rm_watch	277
#elif defined (__ia64__)
# define __NR_inotify_init	1277
# define __NR_inotify_add_watch	1278
# define __NR_inotify_rm_watch	1279
#elif defined (__s390__)
# define __NR_inotify_init	284
# define __NR_inotify_add_watch	285
# define __NR_inotify_rm_watch	286
#elif defined (__alpha__)
# define __NR_inotify_init	444
# define __NR_inotify_add_watch	445
# define __NR_inotify_rm_watch	446
#elif defined (__sparc__) || defined (__sparc64__)
# define __NR_inotify_init	151
# define __NR_inotify_add_watch	152
# define __NR_inotify_rm_watch	156
#elif defined (__arm__)
# define __NR_OABI_SYSCALL_BASE	0x900000
# if defined(__thumb__) || defined(__ARM_EABI__)
#  define __NR_SYSCALL_BASE	0
# else
#  define __NR_SYSCALL_BASE	__NR_OABI_SYSCALL_BASE
# endif
# define __NR_inotify_init	(__NR_SYSCALL_BASE + 316)
# define __NR_inotify_add_watch	(__NR_SYSCALL_BASE + 317)
# define __NR_inotify_rm_watch	(__NR_SYSCALL_BASE + 318)
#elif defined (__sh__)
# define __NR_inotify_init	290
# define __NR_inotify_add_watch	291
# define __NR_inotify_rm_watch	292
#elif defined (__hppa__)
# define __NR_inotify_init	269
# define __NR_inotify_add_watch	270
# define __NR_inotify_rm_watch	271
#elif defined (__mips__)
# include <sgidefs.h>
# if _MIPS_SIM == _MIPS_SIM_ABI32
#  define __NR_Linux			4000
#  define __NR_inotify_init		(__NR_Linux + 284)
#  define __NR_inotify_add_watch	(__NR_Linux + 285)
#  define __NR_inotify_rm_watch		(__NR_Linux + 286)
# elif _MIPS_SIM == _MIPS_SIM_ABI64
#  define __NR_Linux			5000
#  define __NR_inotify_init		(__NR_Linux + 243)
#  define __NR_inotify_add_watch	(__NR_Linux + 244)
#  define __NR_inotify_rm_watch		(__NR_Linux + 245)
# elif _MIPS_SIM == _MIPS_SIM_NABI32
#  define __NR_Linux			6000
#  define __NR_inotify_init		(__NR_Linux + 247)
#  define __NR_inotify_add_watch	(__NR_Linux + 248)
#  define __NR_inotify_rm_watch		(__NR_Linux + 249)
# endif
#elif defined (__m68k__)
# define __NR_inotify_init	284
# define __NR_inotify_add_watch	285
# define __NR_inotify_rm_watch	286
#else
# error "inotify not supported on this architecture!"
#endif
#endif	/* __NR_inotify_init */

static inline int inotify_init (void)
{
	return syscall (__NR_inotify_init);
}

static inline int inotify_add_watch (int fd, const char *name, __u32 mask)
{
	return syscall (__NR_inotify_add_watch, fd, name, mask);
}

static inline int inotify_rm_watch (int fd, __u32 wd)
{
	return syscall (__NR_inotify_rm_watch, fd, wd);
}

#endif /* _LINUX_INOTIFY_SYSCALLS_H */
