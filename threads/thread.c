#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;
static struct list sleep_list;


/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
/*
	스케줄링 알고리즘을 결정합니다. thread_mlfqs가 true일 경우, 멀티 레벨 피드백 큐 스케줄러(Multi-Level Feedback Queue Scheduler, MLFQ)를 사용하고, 
	false일 경우 기본 라운드 로빈 스케줄러를 사용합니다.
*/
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) {
	//interrupt 레벨 확인
	ASSERT (intr_get_level () == INTR_OFF);  //중단(interrupt)이 비활성화된 상태에서만 초기화가 이루어져야 한다(오류 발생 방지 및 안전성위한 검증 절차)

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* Init the globla thread context */
	lock_init (&tid_lock);
	list_init (&ready_list);
	list_init (&sleep_list);
	list_init (&destruction_req);

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT); /* initial_thread를 초기화하여 main이라는 이름과 PRI_DEFAULT(기본 우선순위)로 설정 */
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
}
/*
	thread_init()
	Add the code to initialize the sleep queue data structure.
*/


/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) {
	/* Create the idle thread. */
	struct semaphore idle_started;  /* 여러 스레드가 공유 자원이나 특정 실행 순서를 안전하게 지키도록 제어하는 기법 */
	sema_init (&idle_started, 0);  /* 세마포어의 초기 값을 0으로 설정 */
	thread_create ("idle", PRI_MIN, idle, &idle_started); /* idle 스레드 생성 후, 그 스레드에 &idle_started 세마포어를 인자로 전달 */

	/* Start preemptive thread scheduling. */
	intr_enable ();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down (&idle_started);
}
/*
	thread_start()
	선점형 스레드 스케줄링을 시작하고, idle 스레드 생성하는 역할
	idle 스레드는 다른 모든 스레드가 CPU를 사용하지 않는 상황에서 실행되는 스레드
*/

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
/*
	CPU 스케줄링과 관련된 작업 수행 (시스템의 타이머 인터럽트가 발생할 때마다 주기적으로 호출됨)
	타이머 인터럽트가 발생할 때마다 스레드와 CPU의 상태를 업데이트하는 데 사용됨
*/
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption. 선점(Preemption)을 강제한다 */
	/* 선점 : CPU를 특정 스레드가 계속 점유하지 못하도록 강제로 중단시키고 다른 스레드가 CPU를 사용할 수 있게 만드는 과정 */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return (); //인터럽트가 끝난 후, 즉시 스케줄러에 의해 다른 스레드가 선택될 수 있게 문맥 전환 유도
}

/* Prints thread statistics. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;  /* 새로운 스레드를 위한 struct thread 포인터 */
	tid_t tid;  /* 스레드 식별자를 저장할 변수 */
  
	ASSERT (function != NULL);

	/* Allocate thread. */
	t = palloc_get_page (PAL_ZERO);  /* t에 스레드 메모리 할당 (PAL_ZERO 플래그로 인해 할당된 페이지의 모든 바이트를 0으로 채움) */
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread (t, name, priority);
	tid = t->tid = allocate_tid (); /* 오른쪽에서 왼쪽으로 순서대로 연산 진행 (allocate_tid() 함수 실행 결과가 t->tid 에 대입, 다시 tid 변수에 대입됨) */
									/* 위와 같이 쓰는 이유는 한 번의 연산으로 스레드 구조체 내의 tid 필드와 로컬 변수 tid 모두 초기화하기 위함 */

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	/* 스레드의 tf (intr_frame 구조체)를 통해 CPU 레지스터 상태 설정 => 커널의 문맥을 새 스레드에 맞게 준비하는 작업  */
	/* 새로 생성된 스레드가 처음 실행될 때 정확한 시작 지점과 초기 상태를 가지도록 하기 위해서 */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* Add to run queue. */
	thread_unblock (t);  /* 스레드를 ready queue에 추가하여 실행 가능한 상태로 만든다 */
	/* thread_unblock() 함수는 스레드의 상태를 THREAD_READY로 설정하고 스케줄러가 스레드를 선택할 수 있도록 ready 큐에 넣는다 */
	
	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
/*
	특정 스레드가 BLOCKED 상태에서 READY 상태로 전환되도록 한다.
	이는 스레드가 대기 상태에서 실행 준비 상태로 돌아오는 데 필요한 과정을 수행한다.
	
	- 상태 전환과 스레드 리스트 관리에 집중
	- 특정 자원을 기다리고 있던 스레드를 대기에서 준비 상태로 전환하는 함수이며,
	  이 과정에서 원자성을 유지해 다른 스레드와의 충돌을 방지함
*/
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;  //인터럽트 상태 임시 저장 (인터럽트 끄는 것 안정성 유지 위해)

	ASSERT (is_thread (t));

	old_level = intr_disable (); /* 인터럽트 비활성화 (old_level 변수에 기존 인터럽트 상태 저장하여 나중에 복구 가능하도록)*/
	ASSERT (t->status == THREAD_BLOCKED);
	list_push_back (&ready_list, &t->elem); /* 스레드를 ready_list에 추가 */
	t->status = THREAD_READY; /* 상태 정보 업데이트 (스레드의 상태를 THREAD_READY로 변경 )*/
	intr_set_level (old_level); /* 인터럽트 다시 이전 상태로 복원하여 다른 작업이 다시 개입할 수 있게 함 */
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
/*
	현재 실행 중인 스레드의 주소를 안정적으로 반환하도록 여러 검사를 수행하여,
	코드에서 안전하게 현재 스레드를 사용할 수 있게 한다.
*/
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable ();
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable ();
	if (curr != idle_thread)
		list_push_back (&ready_list, &curr->elem);
	do_schedule (THREAD_READY);
	intr_set_level (old_level);
}

void
thread_sleep(int64_t ticks) {  //ticks : 현재 쓰레드가 자야 하는 시간
	struct thread *cur;
	enum intr_level old_level;  //인터럽트를 켜고 끄기를 쉽게 하기 위한 변수

	old_level = intr_disable();  //스레드 리스트 조작 예정이므로 인터럽트 OFF
	cur = thread_current();
	ASSERT(cur != idle_thread);

	cur->wakeup_tick = ticks;  //일어날 시간 저장
	list_push_back(&sleep_list, &cur->elem);
	thread_block();

	intr_set_level (old_lovel); //인터럽트 ON
}

void
thread_awake(int64_t ticks) {
	struct list_elem *elem = list_begin(&sleep_list);

	while (elem != list_end(&sleep_list)) {
		struct thread *cur = list_entry(elem, struct thread, elem);

		if (cur->wakeup_tick <= ticks) {
			elem = list_remove(elem);
			thread_unblock(cur);
		} else elem = list_next(elem);
	}
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) {
	thread_current ()->priority = new_priority;
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) {
	/* TODO: Your implementation goes here */
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started); /* idle 스레드가 첫 번째 스케줄링을 통해 실행되었다는 신호를 다른 스레드에게 보냄 */

	for (;;) {
		/* Let someone else run. */
		intr_disable ();
		thread_block ();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
/* 커널 스레드 생성하고, 특정 함수를 실행한 뒤 스레드를 종료하는 역할 수행 */
/* 인터럽트 제어를 통해 멀티태스킹 환경에서 스레드 전환이 가능하도록 하는 함수 */
static void
kernel_thread (thread_func *function, void *aux) {  //thread_func 는 실행할 스레드 함수의 포인터이며 aux는 이 함수에 전달할 인자
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. 인터럽트 활성화 */
	function (aux);       /* Execute the thread function. funcion 포인터가 가리키는 함수를 호출함. 이를 통해 새로운 스레드가 특정 작업 수행 가능하게 함 */
	thread_exit ();       /* If function() returns, kill the thread. 현재 스레드 종료 */
	                    /* thread_exit()을 함으로 해당 스레드의 모든 자원을 해제하고, 스케줄러가 다른 스레드를 실행할 수 있도록 준비함 */
}
/* void *aux는 보조 파라미터로 synchronization을 위한 세마포 등이 들어옴 */


/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);  //메모리 초기화 (스레드 구조체 t의 메모리를 0으로 초기화)
	t->status = THREAD_BLOCKED; //스레드의 초기 상태를 THREAD_BLOCKED로 설정 (스레드가 생성되었지만, 아직 실행 가능하지 않은 상태로 설정)
	strlcpy (t->name, name, sizeof t->name);  //name 문자열을 스레드 구조체의 name 필드에 복사
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *); //스택 포인터(rsp) 설정하여 스레드의 시작 위치 지정 (페이지 크기만큼 더하고, void * 만큼 빼서 정확한 스택의 끝 부분 가리키도록 함 => 스레드가 독립된 스택 공간 사용할 수 있도록 함)
	t->priority = priority;  //우선순위 값
	t->magic = THREAD_MAGIC; //스택 오버플로우 감지 (스레드 구조체가 메모리에서 손상되었는지 확인 가능)
}
/*
	개별 스레드를 초기화하는 함수
	struct thread 구조체 하나에 대해 기본 설정
*/

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile (
			/* Store registers that will be used. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* Fetch input once */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
/* 현재 스레드의 상태를 업데이트하고 새로운 스레드를 스케줄링한다 */
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING); /*현재 스레드가 실행 중인지 확인 (스케줄링 호출 전에는 항상 THREAD_RUNNING 상태여야 함)*/
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem); /* 삭제할 스레드를 리스트에서 꺼내어 victim에 저장 */
		palloc_free_page(victim); /* 해당 스레드의 메모리 해제하여 할당된 페이지 반환함 */
	}
	thread_current ()->status = status; /* 현재 스레드의 상태를 인자로 받은 status로 변경 (보통 THREAD_READY나 THREAD_BLOCKED 로 설정됨)*/
	schedule (); /* 스케줄링을 수행하여 다른 스레드로 전환 */
}

/* 현재 스레드를 중단하고 다음 실행할 스레드를 선택하여 실행하는 역할 수행 */
static void
schedule (void) {
	struct thread *curr = running_thread (); /* 현재 실행 중인 스레드 포인터를 가져와 curr에 저장 */
	struct thread *next = next_thread_to_run (); /* 다음에 실행할 스레드 선택하여 next에 저장*/

	ASSERT (intr_get_level () == INTR_OFF); /* 인터럽트가 꺼져 있는지 확인 */
	ASSERT (curr->status != THREAD_RUNNING); /* 현재 스레드는 실행 중이 아니어야 함 */
	ASSERT (is_thread (next));/* next가 올바른 스레드인지 확인. 스레드인지 여부 확인하여 안정성 확보 */
	/* Mark us as running. */
	next->status = THREAD_RUNNING; /* next 스레드를 실행 상태로 설정  => 이제 next 스레드가 CPU를 점유할 준비가 되었다 */

	/* Start new time slice. */
	thread_ticks = 0; /* 새로운 타임 슬라이스 시작하기 위해 thread_ticks를 0으로 초기화 */
					  /* 이는 타임 슬라이스 기반 스케줄링을 위해 사용된다 */
#ifdef USERPROG
	/* Activate the new address space. */
	process_activate (next);
#endif

	if (curr != next) {
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch (next);
	}
}

/* Returns a tid to use for a new thread. */
/* 새로운 스레드에 고유한 TID(Thread ID) 할당해주는 함수 */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;  /* 정적 변수로 선언. 함수 여러 번 호출되어도 값 유지됨 */
	tid_t tid;  /* 함수 내에서 생성되는 각 스레드에 할당할 ID 값을 임시로 저장 */

	lock_acquire (&tid_lock); /* sema_down을 호출하여 세마포어 값 감소시킴 */
	tid = next_tid++;  /* 새로운 스레드가 생성될 때마다 증가하여 고유한 ID 증가 */
	lock_release (&tid_lock); /* sema_up을 호출하여 값 증가시켜 다른 스레드가 자원 사용할 수 있도록 함 */

	return tid;
}
/*
	semaphore는 lock_acquire와 lock_release를 통해 락을 얻거나 해제하는 데 사용된다
*/