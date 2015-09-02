/*
 * Test performance of various locking primitives.
 *
 * Copyright (C) 2007 Anton Blanchard <anton@au.ibm.com>, IBM
 * Copyright (C) 2014 Michael Ellerman, IBM
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define NR_LOOPS 100000000

#define _GNU_SOURCE

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>


#define _str(s) #s
#define str(s) _str(s)


/*
 * For lock we can use sync, isync or lwsync.
 *
 * For unlock we can use sync or lwsync.
 *
 * So the possible combinations are:
 *   sync   sync
 *   sync   lwsync
 *   isync  sync
 *   isync  lwsync
 *   lwsync sync
 *   lwsync lwsync
 *
 */

#define DEF_TRYLOCK(type)				\
static inline unsigned long				\
spin_##type##_trylock(unsigned int *lock)		\
{							\
	unsigned long tmp, token;			\
	token = 1;					\
	asm volatile(					\
"1:	lwarx	%0,0,%2,1\n"				\
"	cmpwi	0,%0,0\n"				\
"	bne-	2f\n"					\
"	stwcx.	%1,0,%2\n"				\
"	bne-	1b\n"					\
	str(type) 					\
"\n2:"	: "=&r" (tmp)					\
	: "r" (token), "r" (lock)			\
	: "cr0", "memory");				\
	return tmp;				\
}

DEF_TRYLOCK(sync)
DEF_TRYLOCK(isync)
DEF_TRYLOCK(lwsync)

struct paca_struct {
	uint8_t io_sync;
};

register struct paca_struct *local_paca asm("r14");

#ifdef DO_SYNC_IO

#define mb()   asm volatile ("sync" : : : "memory")
#define get_paca()	local_paca
#define unlikely(x)	__builtin_expect(!!(x), 0)

#define CLEAR_IO_SYNC	(get_paca()->io_sync = 0)
#define SYNC_IO	\
	do {						\
		if (unlikely(get_paca()->io_sync)) {	\
			mb();				\
			get_paca()->io_sync = 0;	\
		}					\
	} while (0)
#else
#define CLEAR_IO_SYNC
#define SYNC_IO
#endif

#define DEF_LOCK(type)				\
static inline void				\
spin_##type##_lock(unsigned int *lock)		\
{						\
	CLEAR_IO_SYNC;				\
	while (spin_##type##_trylock(lock))	\
		;				\
}

DEF_LOCK(sync)
DEF_LOCK(isync)
DEF_LOCK(lwsync)

#define DEF_UNLOCK(type)			\
static inline void				\
spin_##type##_unlock(unsigned int *lock)	\
{						\
	SYNC_IO;				\
	asm volatile(str(type):::"memory");	\
	*lock = 0;				\
}

DEF_UNLOCK(sync)
DEF_UNLOCK(lwsync)

#define DEF_TEST(ltype, ultype)				\
static inline void					\
test_spin_##ltype##_##ultype(unsigned long nr, unsigned int *lock)	\
{							\
	unsigned long i;				\
							\
	for (i = 0; i < nr; i++) {			\
		spin_##ltype##_lock(lock);		\
		spin_##ultype##_unlock(lock);		\
	}						\
}

DEF_TEST(lwsync, lwsync)
DEF_TEST(isync, lwsync)
DEF_TEST(sync, lwsync)
DEF_TEST(lwsync, sync)
DEF_TEST(sync, sync)
DEF_TEST(isync, sync)

static unsigned int the_lock = 0;

int main()
{
	struct timespec start, end;
	struct paca_struct my_paca;
	unsigned long pvr;
	char hostname[64];

	my_paca.io_sync = 0;
	local_paca = &my_paca;

	asm volatile("mfspr %0, 0x11f" : "=r" (pvr));

	gethostname(hostname, sizeof(hostname));

	/* Get warmed up */
	TEST_NAME(NR_LOOPS, &the_lock);

	clock_gettime(CLOCK_MONOTONIC, &start);

	TEST_NAME(NR_LOOPS, &the_lock);

	clock_gettime(CLOCK_MONOTONIC, &end);

	printf("%s,0x%lx," str(TEST_NAME) ",%1lu\n", hostname, pvr,
		((end.tv_sec  - start.tv_sec) * 1000000000UL + \
		(end.tv_nsec - start.tv_nsec)));

	return 0;
}
