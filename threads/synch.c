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
	{
		list_sort(&sema->waiters, thread_compare_priority, 0);
		thread_unblock(list_entry(list_pop_front(&sema->waiters),
															struct thread, elem));
	}
	sema->value++;
	thread_test_max_priority();
	intr_set_level(old_level);
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
		list_insert_ordered(&sema->waiters, &thread_current()->elem, thread_compare_priority, 0); // 우선순위에 맞춰서 넣어주기
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
LOCK을 초기화합니다. 락은 어느 한 시점에 최대 하나의 스레드만이 보유할 수 있습니다. 우리의 락은 '재귀적'이지 않습니다. 즉, 현재 락을 보유하고 있는 스레드가 동일한 락을 다시 획득하려고 하는 것은 오류입니다.
락은 초기값이 1인 세마포어의 특수한 형태입니다. 락과 이러한 세마포어의 차이점은 두 가지입니다. 첫째, 세마포어는 1보다 큰 값을 가질 수 있지만, 락은 한 번에 단 하나의 스레드만이 소유할 수 있습니다. 둘째, 세마포어는 소유자가 없습니다. 이는 한 스레드가 세마포어를 'down'하고 다른 스레드가 'up'할 수 있다는 것을 의미합니다. 하지만 락의 경우에는 동일한 스레드가 반드시 획득과 해제를 모두 수행해야 합니다. 이러한 제약조건들이 부담스러울 때는 락 대신 세마포어를 사용해야 한다는 좋은 신호입니다.
*/
void lock_init(struct lock *lock)
/*
- 락을 초기화하는 함수
- 단일 스레드만 보유 가능
- 동일 스레드가 획득/해제 담당
- 락은 sema value가 1인 세마포어의 특수한 형태 -> 뮤텍스 락을 구현
*/
{
	ASSERT(lock != NULL);

	lock->holder = NULL;
	sema_init(&lock->semaphore, 1);
}

/*
LOCK을 획득하며, 필요한 경우 락이 사용 가능해질 때까지 대기(sleep)합니다. 이 락은 현재 스레드에 의해 이미 보유되고 있으면 안됩니다.
이 함수는 대기(sleep) 상태가 될 수 있으므로 인터럽트 핸들러 내에서 호출되어서는 안 됩니다. 이 함수는 인터럽트가 비활성화된 상태에서 호출될 수 있지만, 대기(sleep)가 필요한 경우 인터럽트는 다시 활성화됩니다.
*/
void lock_acquire(struct lock *lock)
/*
- 락을 획득하는 함수
- 한 스레드가 같은 락을 중복 획득할 수 없음
- 락 획득 후 현재 스레드를 holder로 설정
- 인터럽트 핸들러 내에서 호출 불가
*/
{
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(!lock_held_by_current_thread(lock));

	struct thread *cur = thread_current();
	if (lock->holder)
	{
		cur->wait_on_lock = lock;
		list_insert_ordered(&lock->holder->donations, &cur->donation_elem,
												thread_compare_priority, 0);
		donate_priority();
	}

	sema_down(&lock->semaphore);

	cur->wait_on_lock = NULL;
	lock->holder = cur;
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

	remove_with_lock(lock);
	refresh_priority();

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

/*
조건 변수 COND를 초기화합니다. 조건 변수는 코드의 한 부분이 특정 조건을 신호로 보내고, 협력하는 코드가 그 신호를 받아서 그에 따라 동작할 수 있게 해줍니다.
*/
void cond_init(struct condition *cond)
/*
- 스레드 간 동기화와 통신을 위한 메커니즘
- 특정 조건의 발생을 다른 스레드에게 알리는 용도
*/
{
	ASSERT(cond != NULL);

	list_init(&cond->waiters);
}

/*
LOCK을 원자적으로 해제하고 다른 코드에 의해 COND가 신호를 받을 때까지 대기합니다. COND에 신호가 오면, 리턴하기 전에 LOCK을 다시 획득합니다. 이 함수를 호출하기 전에 반드시 LOCK을 보유하고 있어야 합니다.

이 함수로 구현된 모니터는 'Hoare' 스타일이 아닌 'Mesa' 스타일입니다. 즉, 신호를 보내고 받는 것이 원자적 연산이 아닙니다. 따라서, 일반적으로 호출자는 대기가 완료된 후 조건을 다시 확인해야 하며, 필요한 경우 다시 대기해야 합니다.

하나의 조건 변수는 단 하나의 락과만 연관됩니다. 하지만 하나의 락은 여러 개의 조건 변수와 연관될 수 있습니다. 즉, 락에서 조건 변수로의 일대다 매핑이 존재합니다.

이 함수는 대기(sleep) 상태가 될 수 있으므로 인터럽트 핸들러 내에서 호출되어서는 안 됩니다. 이 함수는 인터럽트가 비활성화된 상태에서 호출될 수 있지만, 대기가 필요한 경우 인터럽트는 다시 활성화됩니다.
 */
void cond_wait(struct condition *cond, struct lock *lock)
/*
- 조건변수에서 대기하는 함수
- 내부적으로 세마포어를 생성하여 대기
- 락을 해제하고 대기한 후, 시그널을 받으면 다시 락을 획득
- Mesa 스타일 모니터 구현
*/
{
	struct semaphore_elem waiter;

	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));

	sema_init(&waiter.semaphore, 0);
	list_insert_ordered(&cond->waiters, &waiter.elem, sema_compare_priority, 0); // 우선순위에 맞춰서 넣어주기
	lock_release(lock);
	sema_down(&waiter.semaphore);
	lock_acquire(lock);
}

/*
COND에서 대기 중인 스레드들이 있다면(LOCK으로 보호됨), 이 함수는 그 중 하나에게 대기 상태에서 깨어나라는 신호를 보냅니다.
이 함수를 호출하기 전에 반드시 LOCK을 보유하고 있어야 합니다. 인터럽트 핸들러는 락을 획득할 수 없으므로, 인터럽트 핸들러 내에서 조건 변수에 신호를 보내려고 하는 것은 의미가 없습니다.
*/
void cond_signal(struct condition *cond, struct lock *lock UNUSED)
/*
- 대기중인 스레드 하나를 깨우는 함수
- 모든 스레드가 아닌 단 하나의 스레드만 깨움
- waiters 리스트에서 하나의 세마포어를 꺼내서 sema_up 호출
*/
{
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));

	if (!list_empty(&cond->waiters))
	{
		list_sort(&cond->waiters, sema_compare_priority, 0);

		struct semaphore_elem *seam_elem = list_entry(list_pop_front(&cond->waiters), struct semaphore_elem, elem);
		sema_up(&seam_elem->semaphore);
	}
}

/*
COND에서 대기 중인 모든 스레드들을(LOCK으로 보호됨) 깨웁니다. 이 함수를 호출하기 전에 반드시 LOCK을 보유하고 있어야 합니다.
인터럽트 핸들러는 락을 획득할 수 없으므로, 인터럽트 핸들러 내에서 조건 변수에 신호를 보내려고 하는 것은 의미가 없습니다.
*/
void cond_broadcast(struct condition *cond, struct lock *lock)
/*
모든 대기 스레드를 깨우는 함수
내부적으로 cond_signal을 반복 호출
*/
{
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);

	while (!list_empty(&cond->waiters))
		cond_signal(cond, lock);
}

bool sema_compare_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
	const struct semaphore_elem *sa = list_entry(a, struct semaphore_elem, elem);
	const struct semaphore_elem *sb = list_entry(b, struct semaphore_elem, elem);

	if (sa == NULL || sb == NULL)
		return false;

	const struct list *waiter_a = &sa->semaphore.waiters;
	const struct list *waiter_b = &sb->semaphore.waiters;

	if (list_empty(waiter_a) || list_empty(waiter_b))
		return false;

	const struct thread *ta = list_entry(list_front(waiter_a), struct thread, elem);
	const struct thread *tb = list_entry(list_front(waiter_b), struct thread, elem);

	if (ta == NULL || tb == NULL)
		return false;

	return ta->priority > tb->priority;
}