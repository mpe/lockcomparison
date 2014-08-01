/*
 * Test performance of various locking primitives.
 *
 * Build with:
 * gcc -O2 -o lock_comparison lock_comparison.c -lpthread 
 *
 * Copyright (C) 2007 Anton Blanchard <anton@au.ibm.com>, IBM
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define NR_LOOPS 10000000

#define _XOPEN_SOURCE 600
#define _GNU_SOURCE

#include <semaphore.h>

#include <pthread.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

#include <sys/syscall.h>
#include <unistd.h>

#define gettid() syscall(__NR_gettid);

union semun {
	int val;
	struct semid_ds *buf;
	unsigned short *array;
	struct seminfo *__buf;
};

void do_semop(unsigned long nr)
{
	int semid;
	union semun arg;
	struct sembuf sb;
	unsigned long i;
	key_t key = gettid();

	memset(&sb, 0, sizeof(sb));

	if ((semid = semget(key, 1, 0666 | IPC_CREAT)) == -1) {
		perror("semget");
		exit(1);
	}

	/* initialize semaphore #0 to 1: */
	arg.val = 1;
	if (semctl(semid, 0, SETVAL, arg) == -1) {
		perror("semctl");
		exit(1);
	}

	for (i = 0; i < nr; i++) {
		sb.sem_op = -1; /* allocate */
		if (semop(semid, &sb, 1) == -1) {
			perror("semop");
			exit(1);
		}

		sb.sem_op = 1; /* free resource */
		if (semop(semid, &sb, 1) == -1) {
			perror("semop");
			exit(1);
		}
	}

	semctl(semid, 1, IPC_RMID);
}

void do_sem_wait(unsigned long nr)
{
	sem_t sem;
	unsigned long i;

	sem_init(&sem, 0, 1);

	for (i = 0; i < nr; i++) {
		sem_wait(&sem);
		sem_post(&sem);
	}
}

void do_pthread_mutex(unsigned long nr)
{
	pthread_mutex_t mutex;
	unsigned long i;

	memset(&mutex, 0, sizeof(mutex));

	for (i = 0; i < nr; i++) {
		pthread_mutex_lock(&mutex);
		pthread_mutex_unlock(&mutex);
	}
}

#ifdef __GCC_HAVE_SYNC_COMPARE_AND_SWAP_4

static void inline gcc_spin_lock(unsigned int *lock)
{
	while (__sync_lock_test_and_set(lock, 1) == 1)
		;
}

static void inline gcc_spin_unlock(unsigned int *lock)
{
	__sync_lock_release(lock);
}

void do_gcc_atomic_builtin(unsigned long nr)
{
	unsigned int lock;
	unsigned long i;

	lock = 0;

	for (i = 0; i < nr; i++) {
		gcc_spin_lock(&lock);
		gcc_spin_unlock(&lock);
	}
}

#endif

#ifdef __PPC__
static inline unsigned long spin_trylock(unsigned int *lock)
{
	unsigned long tmp, token;

	token = 1;
	asm volatile(
"1:	lwarx	%0,0,%2\n\
	cmpwi	0,%0,0\n\
	bne-	2f\n\
	stwcx.	%1,0,%2\n\
	bne-	1b\n\
	isync\n\
2:"	: "=&r" (tmp)
	: "r" (token), "r" (lock)
	: "cr0", "memory");

	return tmp;
}

static void inline spin_lock(unsigned int *lock)
{
	while (spin_trylock(lock))
		;
}

static void inline spin_unlock(unsigned int *lock)
{
	asm volatile("lwsync":::"memory");
	*lock = 0;
}

void do_spin_lock(unsigned long nr)
{
	unsigned int lock = 0;
	unsigned long i;

	for (i = 0; i < nr; i++) {
		spin_lock(&lock);
		spin_unlock(&lock);
	}
}

static inline unsigned long spin_lwsync_trylock(unsigned int *lock)
{
	unsigned long tmp, token;

	token = 1;
	asm volatile(
"1:	lwarx	%0,0,%2\n\
	cmpwi	0,%0,0\n\
	bne-	2f\n\
	stwcx.	%1,0,%2\n\
	bne-	1b\n\
	lwsync\n\
2:"	: "=&r" (tmp)
	: "r" (token), "r" (lock)
	: "cr0", "memory");

	return tmp;
}

static void inline spin_lwsync_lock(unsigned int *lock)
{
	while (spin_lwsync_trylock(lock))
		;
}

void do_spin_lwsync_lock(unsigned long nr)
{
	unsigned int lock = 0;
	unsigned long i;

	for (i = 0; i < nr; i++) {
		spin_lwsync_lock(&lock);
		spin_unlock(&lock);
	}
}

static inline unsigned long spin_sync_trylock(unsigned int *lock)
{
	unsigned long tmp, token;

	token = 1;
	asm volatile(
"1:	lwarx	%0,0,%2\n\
	cmpwi	0,%0,0\n\
	bne-	2f\n\
	stwcx.	%1,0,%2\n\
	bne-	1b\n\
	sync\n\
2:"	: "=&r" (tmp)
	: "r" (token), "r" (lock)
	: "cr0", "memory");

	return tmp;
}

static void inline spin_sync_lock(unsigned int *lock)
{
	while (spin_sync_trylock(lock))
		;
}

void do_spin_sync_lock(unsigned long nr)
{
	unsigned int lock = 0;
	unsigned long i;

	for (i = 0; i < nr; i++) {
		spin_sync_lock(&lock);
		spin_unlock(&lock);
	}
}
#else
#warning Implement do_spin_lock, do_spin_lwsync_lock, do_spin_sync_lock
#endif

int thread_started;
void (*thread_func)(unsigned long);
int start;
pthread_t *tids;

void *do_one_thread(void *arg)
{
	unsigned long nr = (unsigned long)arg;

	thread_started = 1;

	while (!start)
		asm volatile("":::"memory");

	thread_func(nr);

	return NULL;
}

void setup_threads(unsigned long nr_threads, unsigned long nr)
{
	int i;

	tids = malloc(nr_threads * sizeof(pthread_t));

	start = 0;

	for (i = 0; i < nr_threads; i++) {
		thread_started = 0;
		pthread_create(&tids[i], NULL, do_one_thread, (void *)nr);
		while (!thread_started)
			asm volatile("":::"memory");
	}
}

void do_threads(unsigned long nr_threads)
{
	int i;

	start = 1;

	for (i = 0; i < nr_threads; i++)
		pthread_join(tids[i], NULL); 

	free(tids);
}


static float timebase_multiplier;

#if defined(__PPC__)
static void get_timebase_multiplier(void)
{
	FILE *f;
	char line[80];
	char *p, *end;
	unsigned long v;
	double d;
	unsigned long timebase_frequency = 0, clock_frequency = 0;

	if ((f = fopen("/proc/cpuinfo", "r")) == NULL)
		return;

	while (fgets(line, sizeof(line), f) != NULL) {
		if (strncmp(line, "timebase", 8) == 0
		    && (p = strchr(line, ':')) != NULL) {
			v = strtoul(p+1, &end, 0);
			if (end != p+1) {
				timebase_frequency = v;
			}
		}

		if (strncmp(line, "clock", 5) == 0
		    && (p = strchr(line, ':')) != NULL) {
			d = strtod(p+1, &end);
			if (end != p+1) {
				clock_frequency = d * 1000000;
			}
		}
	}

	fclose(f);

	timebase_multiplier = (float)clock_frequency / timebase_frequency;
}

#define mftb()	({unsigned long rval;   \
			asm volatile("mftb %0" : "=r" (rval)); rval;})

#elif defined(__i386__)

static void get_timebase_multiplier(void)
{
	timebase_multiplier = 1;
}
#define mftb()	({unsigned long rval;	\
			asm volatile("rdtsc" : "=a" (rval)); rval;})

#else
#error Implement get_timebase_multiplier() and mftb()
#endif

#define TIME(A, STR) \
	before = mftb(); \
	A; \
	time = mftb() - before; \
	if (!baseline) \
		baseline = time; \
	printf("%20s: %10.2f cycles %10.2f%%\n", (STR), (float)time * timebase_multiplier / NR_LOOPS, (float)time * 100 / baseline);

int main()
{
	unsigned long before, time, baseline = 0;
	int nr_threads = sysconf(_SC_NPROCESSORS_ONLN);

	/* Assume SMT is on and we only want one per core */
	nr_threads /= 2;

	get_timebase_multiplier();

	printf("Uncontended\n");
#ifdef __PPC__
	TIME(do_spin_lock(NR_LOOPS), "spin_lock")
	TIME(do_spin_lwsync_lock(NR_LOOPS), "spin_lwsync_lock")
	TIME(do_spin_sync_lock(NR_LOOPS), "spin_sync_lock")
#endif

	printf("\nUncontended threaded\n");

#ifdef __PPC__
	thread_func = do_spin_lock;
	setup_threads(nr_threads, NR_LOOPS);
	TIME(do_threads(nr_threads), "spin_lock")

	thread_func = do_spin_lwsync_lock;
	setup_threads(nr_threads, NR_LOOPS);
	TIME(do_threads(nr_threads), "spin_lwsync_lock")

	thread_func = do_spin_sync_lock;
	setup_threads(nr_threads, NR_LOOPS);
	TIME(do_threads(nr_threads), "spin_sync_lock")
#endif

	return 0;
}
