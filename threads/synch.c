/* This file is derived from source code for the Nachos
	instructional operating system.  The Nachos copyright notice
	is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
	All rights reserved.

	Permission to use, copy, modify, and distribute this software
	and its documentation for any purpose, without fee, and
	without written agreement is hereby granted, provided that the
	above copyright notice and the following two paragraphs appear
	in all copies of this software.

	IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
	ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
	CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
	AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
	HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

	THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
	WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
	WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
	PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
	BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
	PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
	MODIFICATIONS.
	 */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/*
세마포어 SEMA를 VALUE로 초기화합니다.  세마포어는 음수가 아닌 정수와 이를 조작하는 두 개의 원자 연산자입니다:
	- down 또는 “P”: 값이 양수가 될 때까지 기다린 다음 값을 감소시킵니다.
	- up 또는 “V”: 값을 증가시킵니다(대기 중인 스레드가 하나 있으면 깨웁니다).
*/
void sema_init(struct semaphore *sema, unsigned value)
/*
- 세마포어를 초기화하는 함수
- value로 초기값을 설정하고 대기자 목록(waiters)을 초기화
- 세마포어는 비음수 정수와 두 개의 atomic 연산자(P,V)로 구성
*/
{
	ASSERT(sema != NULL);

	sema->value = value;
	list_init(&sema->waiters);
}

/*
세마포어에서 "down" 또는 "P" 연산.  SEMA 값이 양수가 될 때까지 기다렸다가 원자적으로 감소시킵니다.
이 함수는 절전 상태가 될 수 있으므로 인터럽트 핸들러 내에서 호출해서는 안 됩니다. 이 함수는 인터럽트가 비활성화된 상태에서 호출할 수 있지만, 잠자기 상태가 되면 다음 예약된 스레드가 인터럽트를 다시 켜게 될 수 있습니다.
*/
void sema_down(struct semaphore *sema)
/*
- P 연산을 수행하는 함수
- 세마포어 값이 0이면 현재 스레드를 waiters 리스트에 추가하고 block
- 세마포어 값이 양수면 값을 감소시키고 진행
- 인터럽트 핸들러 내에서 호출 불가
*/
{
	enum intr_level old_level;

	ASSERT(sema != NULL);
	ASSERT(!intr_context());

	old_level = intr_disable();
	while (sema->value == 0)
	{
		list_push_back(&sema->waiters, &thread_current()->elem);
		thread_block();
	}
	sema->value--;
	intr_set_level(old_level);
}

/*
세마포어의 "down" 또는 "P" 연산을 수행하지만, 세마포어가 아직 0이 아닌 경우에만 가능합니다. 세마포어가 감소하면 참을 반환하고, 그렇지 않으면 거짓을 반환합니다.
이 함수는 인터럽트 핸들러에서 호출할 수 있습니다.
*/
bool sema_try_down(struct semaphore *sema)
/*
- P 연산을 시도하는 함수
- 세마포어 값이 0이면 false 반환
- 값이 양수면 감소시키고 true 반환
- 인터럽트 핸들러에서 호출 가능
*/
{
	enum intr_level old_level;
	bool success;

	ASSERT(sema != NULL);

	old_level = intr_disable();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level(old_level);

	return success;
}

/*
세마포어에서 "up" 또는 "V" 연산.  세마 값을 증가시키고 세마 대기 중인 스레드 중 하나(있는 경우)를 깨웁니다.	 이 함수는 인터럽트 핸들러에서 호출할 수 있습니다.
*/
void sema_up(struct semaphore *sema)
/*
- V 연산을 수행하는 함수
- 세마포어 값을 증가시키고, waiters 리스트에서 대기중인 스레드 하나를 깨움
- 인터럽트 핸들러에서 호출 가능
*/
{
	enum intr_level old_level;

	ASSERT(sema != NULL);

	old_level = intr_disable();
	if (!list_empty(&sema->waiters))
		thread_unblock(list_entry(list_pop_front(&sema->waiters),
															struct thread, elem));
	sema->value++;
	intr_set_level(old_level);
}

static void sema_test_helper(void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
	between a pair of threads.  Insert calls to printf() to see
	 what's going on. */
void sema_self_test(void)
{
	struct semaphore sema[2];
	int i;

	printf("Testing semaphores...");
	sema_init(&sema[0], 0);
	sema_init(&sema[1], 0);
	thread_create("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up(&sema[0]);
		sema_down(&sema[1]);
	}
	printf("done.\n");
}

/* Thread function used by sema_self_test(). */
static void sema_test_helper(void *sema_)
{
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down(&sema[0]);
		sema_up(&sema[1]);
	}
}

/*
LOCK을 초기화합니다. 잠금은 주어진 시간에 최대 하나의 스레드만 보유할 수 있습니다. 즉, 현재 잠금을 보유하고 있는 스레드가 해당 잠금을 획득하려고 시도하는 것은 오류입니다.
잠금은 초기값이 1인 세마포어의 특수화입니다. 잠금과 이러한 세마포어의 차이점은 두 가지입니다.첫째, 세마포어는 1보다 큰 값을 가질 수 있지만, 잠금은 한 번에 하나의 스레드만 소유할 수 있습니다.
둘째, 세마포어는 소유자가 없으므로 한 스레드가 세마포어를 'down'한 다음 다른 스레드가 'up'할 수 있지만, 잠금을 사용하면 동일한 스레드가 모두 잠금을 획득하고 해제해야 합니다. 이러한 제한이 부담스러울 때는 잠금 대신 세마포어를 사용해야 한다는 좋은 신호입니다.
*/
void lock_init(struct lock *lock)
/*
- 락을 초기화하는 함수
- holder는 NULL로, 내부 세마포어는 1로 초기화
- 락은 value가 1인 세마포어의 특수한 형태
*/
{
	ASSERT(lock != NULL);

	lock->holder = NULL;
	sema_init(&lock->semaphore, 1);
}

/*
잠금을 획득하고 필요한 경우 잠금을 사용할 수 있을 때까지 대기합니다. 현재 스레드가 이미 잠금을 잡고 있지 않아야 합니다.
이 함수는 잠자기 상태일 수 있으므로 인터럽트 핸들러 내에서 호출해서는 안 됩니다. 이 함수는 인터럽트가 비활성화된 상태에서 호출할 수 있지만, 잠자기 모드가 필요한 경우 인터럽트가 다시 켜집니다.
*/
void lock_acquire(struct lock *lock)
/*
- 락을 획득하는 함수
- 내부적으로 sema_down을 호출
- 락 획득 후 현재 스레드를 holder로 설정
- 인터럽트 핸들러 내에서 호출 불가
*/
{
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(!lock_held_by_current_thread(lock));

	sema_down(&lock->semaphore);
	lock->holder = thread_current();
}

/* Tries to acquires LOCK and returns true if successful or false
	on failure.  The lock must not already be held by the current
	thread.

	This function will not sleep, so it may be called within an
	 interrupt handler. */
bool lock_try_acquire(struct lock *lock)
{
	bool success;

	ASSERT(lock != NULL);
	ASSERT(!lock_held_by_current_thread(lock));

	success = sema_try_down(&lock->semaphore);
	if (success)
		lock->holder = thread_current();
	return success;
}

/*
현재 스레드가 소유하고 있어야 하는 LOCK을 해제합니다. 이것이 lock_release 함수입니다.
인터럽트 핸들러는 잠금을 획득할 수 없으므로 인터럽트 핸들러 내에서 잠금을 해제하려고 시도하는 것은 의미가 없습니다.
*/
void lock_release(struct lock *lock)
/*
- 락을 해제하는 함수
- holder를 NULL로 설정하고 sema_up 호출
- 현재 스레드가 holder인 경우만 호출 가능
*/
{
	ASSERT(lock != NULL);
	ASSERT(lock_held_by_current_thread(lock));

	lock->holder = NULL;
	sema_up(&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
	otherwise.  (Note that testing whether some other thread holds
	 a lock would be racy.) */
bool lock_held_by_current_thread(const struct lock *lock)
{
	ASSERT(lock != NULL);

	return lock->holder == thread_current();
}

/* One semaphore in a list. */
struct semaphore_elem
{
	struct list_elem elem;			/* List element. */
	struct semaphore semaphore; /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
	allows one piece of code to signal a condition and cooperating
	 code to receive the signal and act upon it. */
void cond_init(struct condition *cond)
{
	ASSERT(cond != NULL);

	list_init(&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
	some other piece of code.  After COND is signaled, LOCK is
	reacquired before returning.  LOCK must be held before calling
	this function.

	The monitor implemented by this function is "Mesa" style, not
	"Hoare" style, that is, sending and receiving a signal are not
	an atomic operation.  Thus, typically the caller must recheck
	the condition after the wait completes and, if necessary, wait
	again.

	A given condition variable is associated with only a single
	lock, but one lock may be associated with any number of
	condition variables.  That is, there is a one-to-many mapping
	from locks to condition variables.

	This function may sleep, so it must not be called within an
	interrupt handler.  This function may be called with
	interrupts disabled, but interrupts will be turned back on if
	 we need to sleep. */
void cond_wait(struct condition *cond, struct lock *lock)
{
	struct semaphore_elem waiter;

	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));

	sema_init(&waiter.semaphore, 0);
	list_push_back(&cond->waiters, &waiter.elem);
	lock_release(lock);
	sema_down(&waiter.semaphore);
	lock_acquire(lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
	this function signals one of them to wake up from its wait.
	LOCK must be held before calling this function.

	An interrupt handler cannot acquire a lock, so it does not
	make sense to try to signal a condition variable within an
	 interrupt handler. */
void cond_signal(struct condition *cond, struct lock *lock UNUSED)
{
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));

	if (!list_empty(&cond->waiters))
		sema_up(&list_entry(list_pop_front(&cond->waiters),
												struct semaphore_elem, elem)
								 ->semaphore);
}

/* Wakes up all threads, if any, waiting on COND (protected by
	LOCK).  LOCK must be held before calling this function.

	An interrupt handler cannot acquire a lock, so it does not
	make sense to try to signal a condition variable within an
	 interrupt handler. */
void cond_broadcast(struct condition *cond, struct lock *lock)
{
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);

	while (!list_empty(&cond->waiters))
		cond_signal(cond, lock);
}
