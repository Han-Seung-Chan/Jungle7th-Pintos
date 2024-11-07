#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>

/* A counting semaphore. */
struct semaphore {
	unsigned value;             /* Current value. */ /* 자원의 남은 개수 (양수 혹은 0, 0이면 새로운 스레드는 대기 필요) */
	struct list waiters;        /* List of waiting threads. 자원 얻기 위해 대기하는 스레드들이 waiters 리스트에 들어감(연결 리스트) */
								/* 자원이 다시 사용 가능해지면, 대기 중인 스레드 중 하나를 깨워서 자원을 사용할 수 있도록 해야 함 */
};
/*
	semaphore 구조는 스레드들이 공유 자원에 안전하게 접근하도록 동기화 기법을 제공하는 중요한 구조
*/

void sema_init (struct semaphore *, unsigned value);
void sema_down (struct semaphore *);
bool sema_try_down (struct semaphore *);
void sema_up (struct semaphore *);
void sema_self_test (void);

/* Lock. */
struct lock {
	struct thread *holder;      /* Thread holding lock (for debugging). */
	struct semaphore semaphore; /* Binary semaphore controlling access. */
};

void lock_init (struct lock *);
void lock_acquire (struct lock *);
bool lock_try_acquire (struct lock *);
void lock_release (struct lock *);
bool lock_held_by_current_thread (const struct lock *);

/* Condition variable. */
struct condition {
	struct list waiters;        /* List of waiting threads. */
};

void cond_init (struct condition *);
void cond_wait (struct condition *, struct lock *);
void cond_signal (struct condition *, struct lock *);
void cond_broadcast (struct condition *, struct lock *);

/* Optimization barrier.
 *
 * The compiler will not reorder operations across an
 * optimization barrier.  See "Optimization Barriers" in the
 * reference guide for more information.*/
#define barrier() asm volatile ("" : : : "memory")

#endif /* threads/synch.h */
