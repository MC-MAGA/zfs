// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _SPL_KMEM_H
#define	_SPL_KMEM_H

#include <sys/debug.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>

extern int kmem_debugging(void);
__attribute__((format(printf, 1, 0)))
extern char *kmem_vasprintf(const char *fmt, va_list ap);
__attribute__((format(printf, 1, 2)))
extern char *kmem_asprintf(const char *fmt, ...);
extern char *kmem_strdup(const char *str);
extern void kmem_strfree(char *str);

#define	kmem_scnprintf	scnprintf

#define	POINTER_IS_VALID(p)	(!((uintptr_t)(p) & 0x3))
#define	POINTER_INVALIDATE(pp)	(*(pp) = (void *)((uintptr_t)(*(pp)) | 0x1))

/*
 * Memory allocation interfaces
 */
#define	KM_SLEEP	0x0000	/* can block for memory; success guaranteed */
#define	KM_NOSLEEP	0x0001	/* cannot block for memory; may fail */
#define	KM_PUSHPAGE	0x0004	/* can block for memory; may use reserve */
#define	KM_ZERO		0x1000	/* zero the allocation */
#define	KM_VMEM		0x2000	/* caller is vmem_* wrapper */

#define	KM_PUBLIC_MASK	(KM_SLEEP | KM_NOSLEEP | KM_PUSHPAGE)

static int spl_fstrans_check(void);
void *spl_kvmalloc(size_t size, gfp_t flags);

/*
 * Convert a KM_* flags mask to its Linux GFP_* counterpart.  The conversion
 * function is context aware which means that KM_SLEEP allocations can be
 * safely used in syncing contexts which have set SPL_FSTRANS.
 */
static inline gfp_t
kmem_flags_convert(int flags)
{
	gfp_t lflags = __GFP_NOWARN | __GFP_COMP;

	if (flags & KM_NOSLEEP) {
		lflags |= GFP_ATOMIC | __GFP_NORETRY;
	} else {
		lflags |= GFP_KERNEL;
		if (spl_fstrans_check())
			lflags &= ~(__GFP_IO|__GFP_FS);
	}

	if (flags & KM_PUSHPAGE)
		lflags |= __GFP_HIGH;

	if (flags & KM_ZERO)
		lflags |= __GFP_ZERO;

	return (lflags);
}

typedef struct {
	struct task_struct *fstrans_thread;
	unsigned int saved_flags;
} fstrans_cookie_t;

/*
 * SPL_FSTRANS is the set of flags that indicate that the task is in a
 * filesystem or IO codepath, and so any allocation must not call back into
 * those codepaths (eg to swap).
 */
#define	SPL_FSTRANS (PF_MEMALLOC_NOIO)

static inline fstrans_cookie_t
spl_fstrans_mark(void)
{
	fstrans_cookie_t cookie;

	BUILD_BUG_ON(SPL_FSTRANS == 0);

	cookie.fstrans_thread = current;
	cookie.saved_flags = current->flags & SPL_FSTRANS;
	current->flags |= SPL_FSTRANS;

	return (cookie);
}

static inline void
spl_fstrans_unmark(fstrans_cookie_t cookie)
{
	ASSERT3P(cookie.fstrans_thread, ==, current);
	ASSERT((current->flags & SPL_FSTRANS) == SPL_FSTRANS);

	current->flags &= ~SPL_FSTRANS;
	current->flags |= cookie.saved_flags;
}

static inline int
spl_fstrans_check(void)
{
	return (current->flags & SPL_FSTRANS);
}

extern atomic64_t kmem_alloc_used;
extern uint64_t kmem_alloc_max;

extern unsigned int spl_kmem_alloc_warn;
extern unsigned int spl_kmem_alloc_max;

#define	kmem_alloc(sz, fl)	spl_kmem_alloc((sz), (fl), __func__, __LINE__)
#define	kmem_zalloc(sz, fl)	spl_kmem_zalloc((sz), (fl), __func__, __LINE__)
#define	kmem_free(ptr, sz)	spl_kmem_free((ptr), (sz))
#define	kmem_cache_reap_active	spl_kmem_cache_reap_active

__attribute__((malloc, alloc_size(1)))
extern void *spl_kmem_alloc(size_t sz, int fl, const char *func, int line);
__attribute__((malloc, alloc_size(1)))
extern void *spl_kmem_zalloc(size_t sz, int fl, const char *func, int line);
extern void spl_kmem_free(const void *ptr, size_t sz);

/*
 * 5.8 API change, pgprot_t argument removed.
 */
#ifdef HAVE_VMALLOC_PAGE_KERNEL
#define	spl_vmalloc(size, flags)	__vmalloc(size, flags, PAGE_KERNEL)
#else
#define	spl_vmalloc(size, flags)	__vmalloc(size, flags)
#endif

/*
 * The following functions are only available for internal use.
 */
extern void *spl_kmem_alloc_impl(size_t size, int flags, int node);
extern void *spl_kmem_alloc_debug(size_t size, int flags, int node);
extern void *spl_kmem_alloc_track(size_t size, int flags,
    const char *func, int line, int node);
extern void spl_kmem_free_impl(const void *buf, size_t size);
extern void spl_kmem_free_debug(const void *buf, size_t size);
extern void spl_kmem_free_track(const void *buf, size_t size);

extern int spl_kmem_init(void);
extern void spl_kmem_fini(void);
extern int spl_kmem_cache_reap_active(void);

#endif	/* _SPL_KMEM_H */
