#include "thread.h"
#include "syslib.h"
#include "malloc.h"
#include "../shared/ctx.h"
#include <stddef.h>
#include <stdint.h>

#ifndef USER_GET_NO_INPUT
#define USER_GET_NO_INPUT 0
#endif
enum thread_state
{
  RUNNABLE,
  SLEEPING,
  WAITING_INPUT,
  WAITING_SEMA,
  EXITED,
};

struct thread
{
  void *sp;
  enum thread_state state;
  struct thread *next;
  uint64_t deadline;      /* for SLEEPING */
  int input_result;       /* for WAITING_INPUT, filled when woken */
  struct sema *wait_sema; /* for WAITING_SEMA */
  /* For thread_create / trampoline */
  void (*start_routine)(void *);
  void *arg;
  void *stack_base; /* malloc'd; NULL for main */
  unsigned int stack_size;
  int first_run; /* 1 = never switched to yet; use ctx_start */
};

struct sema
{
  unsigned int count;
  struct thread *wait_head;
};

static struct thread *current;

static struct thread *run_head;
static struct thread *run_tail;

static struct thread *sleep_head;
static struct thread *input_wait_head;

static unsigned int thread_count;

/* Set in thread_exit before ctx_switch; cleared by new thread after switch */
static struct thread *thread_to_free;

static void run_enqueue(struct thread *t)
{
  t->state = RUNNABLE;
  t->next = NULL;
  if (run_tail)
  {
    run_tail->next = t;
    run_tail = t;
  }
  else
  {
    run_head = run_tail = t;
  }
}

static struct thread *run_dequeue(void)
{
  struct thread *t = run_head;
  if (!t)
    return NULL;
  run_head = t->next;
  if (run_head == NULL)
    run_tail = NULL;
  t->next = NULL;
  return t;
}

/* Move threads whose deadline has passed from sleep list to run queue. */
static void wake_sleepers(void)
{
  uint64_t now = user_gettime();
  struct thread **p = &sleep_head;
  while (*p)
  {
    struct thread *t = *p;
    if (now >= t->deadline)
    {
      *p = t->next;
      t->next = NULL;
      run_enqueue(t);
    }
    else
    {
      p = &t->next;
    }
  }
}

/* Get earliest deadline in sleep list, or 0 if empty. */
static uint64_t sleep_earliest_deadline(void)
{
  uint64_t min = 0;
  for (struct thread *t = sleep_head; t; t = t->next)
  {
    if (min == 0 || t->deadline < min)
      min = t->deadline;
  }
  return min;
}

/* If any thread is waiting for input, get one event and wake one waiter.
 * If block != 0, use user_get(1) (blocking); else use user_get(0) (non-blocking).
 * Returns 1 if an event was delivered, 0 if there was no input (or no waiters).
 */
static int serve_one_input_waiter(int block)
{
  if (!input_wait_head)
    return 0;

  int c = user_get(block ? 1 : 0);
  if (c == USER_GET_NO_INPUT)
    return 0;

  struct thread *t = input_wait_head;
  input_wait_head = t->next;
  t->next = NULL;
  t->input_result = c;
  run_enqueue(t);
  return 1;
}

void thread_init(void)
{
  static struct thread main_thread;

  current = &main_thread;
  current->sp = NULL;
  current->state = RUNNABLE;
  current->next = NULL;
  current->deadline = 0;
  current->input_result = 0;
  current->wait_sema = NULL;
  current->start_routine = NULL;
  current->arg = NULL;
  current->stack_base = NULL;
  current->stack_size = 0;
  current->first_run = 0;

  run_head = NULL;
  run_tail = NULL;
  sleep_head = NULL;
  input_wait_head = NULL;
  thread_to_free = NULL;

  thread_count = 1;
}

void thread_create(void (*f)(void *), void *arg, unsigned int stack_size)
{
  void *stack_base = malloc(stack_size);
  if (!stack_base)
    return;
  struct thread *t = malloc(sizeof(*t));
  if (!t)
  {
    free(stack_base);
    return;
  }
  /* Stack grows down; sp = top (high address).  Keep 16-byte aligned. */
  t->sp = (char *)stack_base + stack_size;
  t->sp = (void *)((unsigned long)t->sp & ~15UL);
  t->state = RUNNABLE;
  t->next = NULL;
  t->deadline = 0;
  t->input_result = 0;
  t->wait_sema = NULL;
  t->start_routine = f;
  t->arg = arg;
  t->stack_base = stack_base;
  t->stack_size = stack_size;
  t->first_run = 1;

  run_enqueue(t);
  thread_count++;
}

void thread_yield(void)
{
  wake_sleepers();
  struct thread *next = run_dequeue();
  if (!next)
    return;
  run_enqueue(current);
  struct thread *prev = current;
  current = next;
  if (next->first_run)
  {
    next->first_run = 0;
    ctx_start(&prev->sp, next->sp);
  }
  else
  {
    ctx_switch(&prev->sp, next->sp);
  }
}

void thread_sleep(uint64_t deadline)
{
  current->state = SLEEPING;
  current->deadline = deadline;
  current->next = sleep_head;
  sleep_head = current;

  for (;;)
  {
    wake_sleepers();
    struct thread *next = run_dequeue();
    if (next)
    {
      struct thread *prev = current;
      if (next == prev)
        return;
      current = next;
      if (next->first_run)
      {
        next->first_run = 0;
        ctx_start(&prev->sp, next->sp);
      }
      else
      {
        ctx_switch(&prev->sp, next->sp);
      }
      return; /* back from scheduler; our deadline passed and we were woken */
    }
    uint64_t min = sleep_earliest_deadline();

    /* First, if there are sleepers, try to consume any ready input non-blocking. */
    if (min != 0)
    {
      if (serve_one_input_waiter(0))
        continue;      // we woke an input waiter; next loop will run it
      user_sleep(min); // no input ready: sleep until next deadline
      continue;
    }

    /* No sleepers: if there are input waiters, block for input. */
    if (serve_one_input_waiter(1))
      continue;

    /* No sleepers, no input waiters: nothing left runnable → exit. */
    user_exit();
  }
}

int thread_get(void)
{
  int c = user_get(0);
  if (c != USER_GET_NO_INPUT)
    return c;

  current->state = WAITING_INPUT;
  current->next = input_wait_head;
  input_wait_head = current;

  for (;;)
  {
    wake_sleepers();
    struct thread *next = run_dequeue();
    if (next)
    {
      struct thread *prev = current;
      if (next == prev)
        return current->input_result;
      current = next;
      if (next->first_run)
      {
        next->first_run = 0;
        ctx_start(&prev->sp, next->sp);
      }
      else
      {
        ctx_switch(&prev->sp, next->sp);
      }
      return current->input_result;
    }
    uint64_t min = sleep_earliest_deadline();

    /* First, if there are sleepers, try to consume any ready input non-blocking. */
    if (min != 0)
    {
      if (serve_one_input_waiter(0))
        continue;      // we woke an input waiter; next loop will run it
      user_sleep(min); // no input ready: sleep until next deadline
      continue;
    }

    /* No sleepers: if there are input waiters, block for input. */
    if (serve_one_input_waiter(1))
      continue;

    /* No sleepers, no input waiters: nothing left runnable → exit. */
    user_exit();
  }
}

void thread_exit(void)
{
  thread_count--;
  struct thread *prev = current;
  for (;;)
  {
    wake_sleepers();
    struct thread *next = run_dequeue();
    if (next)
    {
      current = next;
      thread_to_free = prev;
      if (next->first_run)
      {
        next->first_run = 0;
        ctx_start(&prev->sp, next->sp);
      }
      else
      {
        ctx_switch(&prev->sp, next->sp);
      }
      /* Land here in the new thread after switch */
      if (thread_to_free)
      {
        if (thread_to_free->stack_base)
        {
          free(thread_to_free->stack_base);
          free(thread_to_free);
        }
        thread_to_free = NULL;
      }
      return;
    }
    uint64_t min = sleep_earliest_deadline();

    /* First, if there are sleepers, try to consume any ready input non-blocking. */
    if (min != 0)
    {
      if (serve_one_input_waiter(0))
        continue;      // we woke an input waiter; next loop will run it
      user_sleep(min); // no input ready: sleep until next deadline
      continue;
    }

    /* No sleepers: if there are input waiters, block for input. */
    if (serve_one_input_waiter(1))
      continue;

    /* No sleepers, no input waiters: nothing left runnable → exit. */
    user_exit();
  }
}

struct sema *sema_create(unsigned int count)
{
  struct sema *s = malloc(sizeof(*s));
  if (!s)
    return NULL;
  s->count = count;
  s->wait_head = NULL;
  return s;
}

void sema_inc(struct sema *sema)
{
  if (sema->wait_head)
  {
    struct thread *t = sema->wait_head;
    sema->wait_head = t->next;
    t->next = NULL;
    t->wait_sema = NULL;
    run_enqueue(t);
  }
  else
  {
    sema->count++;
  }
}

void sema_dec(struct sema *sema)
{
  if (sema->count > 0)
  {
    sema->count--;
    return;
  }

  current->state = WAITING_SEMA;
  current->wait_sema = sema;
  current->next = sema->wait_head;
  sema->wait_head = current;

  for (;;)
  {
    wake_sleepers();
    struct thread *next = run_dequeue();
    if (next)
    {
      struct thread *prev = current;
      if (next == prev)
        return;
      current = next;
      if (next->first_run)
      {
        next->first_run = 0;
        ctx_start(&prev->sp, next->sp);
      }
      else
      {
        ctx_switch(&prev->sp, next->sp);
      }
      return;
    }
    uint64_t min = sleep_earliest_deadline();

    /* First, if there are sleepers, try to consume any ready input non-blocking. */
    if (min != 0)
    {
      if (serve_one_input_waiter(0))
        continue;      // we woke an input waiter; next loop will run it
      user_sleep(min); // no input ready: sleep until next deadline
      continue;
    }

    /* No sleepers: if there are input waiters, block for input. */
    if (serve_one_input_waiter(1))
      continue;

    /* No sleepers, no input waiters: nothing left runnable → exit. */
    user_exit();
  }
}
void sema_release(struct sema *sema)
{
  free(sema);
}

static void thread_trampoline(void);

/* Called by ctx_start() on a new thread's stack; never returns. */
void exec_user(void)
{
  thread_trampoline();
}

static void thread_trampoline(void)
{
  current->start_routine(current->arg);
  thread_exit();
}
