/*
 * libusb synchronization using POSIX Threads
 *
 * Copyright © 2011 Vitali Lovich <vlovich@aliph.com>
 * Copyright © 2011 Peter Stuge <peter@stuge.se>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libusbi.h"

#if defined(__ANDROID__)
# include <unistd.h>
#elif defined(__HAIKU__)
# include <os/kernel/OS.h>
#elif defined(__linux__)
# include <sys/syscall.h>
# include <unistd.h>
#elif defined(__NetBSD__)
# include <lwp.h>
#elif defined(__OpenBSD__)
# define _BSD_SOURCE
# include <sys/syscall.h>
# include <unistd.h>
#elif defined(__sun__)
# include <sys/lwp.h>
#endif

int usbi_cond_timedwait(pthread_cond_t *cond,
	pthread_mutex_t *mutex, const struct timeval *tv)
{
	struct timespec timeout;
	int r;

	r = usbi_clock_gettime(USBI_CLOCK_REALTIME, &timeout);
	if (r < 0)
		return r;

	timeout.tv_sec += tv->tv_sec;
	timeout.tv_nsec += tv->tv_usec * 1000;
	while (timeout.tv_nsec >= 1000000000L) {
		timeout.tv_nsec -= 1000000000L;
		timeout.tv_sec++;
	}

	return pthread_cond_timedwait(cond, mutex, &timeout);
}

unsigned int usbi_get_tid(void)
{
#if !(defined(_WIN32) || defined(__CYGWIN__))
	static _Thread_local unsigned int tid;

	if (tid)
		return tid;
#else
	unsigned int tid;
#endif

#if defined(__ANDROID__)
	tid = (unsigned int)gettid();
#elif defined(__APPLE__)
#ifdef HAVE_PTHREAD_THREADID_NP
	uint64_t thread_id;

	if (pthread_threadid_np(NULL, &thread_id) == 0)
		tid = (unsigned int)thread_id;
	else
		tid = UINT_MAX;
#else
	tid = (unsigned int)pthread_mach_thread_np(pthread_self());
#endif
#elif defined(__HAIKU__)
	tid = (unsigned int)get_pthread_thread_id(pthread_self());
#elif defined(__linux__)
	tid = (unsigned int)syscall(SYS_gettid);
#elif defined(__NetBSD__) || defined(__sun__)
	tid = (unsigned int)_lwp_self();
#elif defined(__OpenBSD__)
	/* The following only works with OpenBSD > 5.1 as it requires
	 * real thread support. For 5.1 and earlier, -1 is returned. */
	tid = (unsigned int)syscall(SYS_getthrid);
#elif defined(_WIN32) || defined(__CYGWIN__)
	tid = (unsigned int)GetCurrentThreadId();
#else
	tid = UINT_MAX;
#endif

	if (tid == UINT_MAX) {
		/* If we don't have a thread ID, at least return a unique
		 * value that can be used to distinguish individual
		 * threads. */
		tid = (unsigned int)(uintptr_t)pthread_self();
	}

	return tid;
}
