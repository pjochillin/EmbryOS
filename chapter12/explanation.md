# Sleep Function Implementation - Chapter 12

## How Sleeping Processes Are Represented

Sleeping processes in EmbryOS are represented through a combination of per process state fields in the PCB and a dedicated sleep queue data structure.

### PCB Fields

Each process has two fields in its PCB (`struct pcb` in `process.h`) to track sleep state:

1. **`sleeping` (bit field)**: A flag that indicates whether the process is currently sleeping. Set to 1 when `user_sleep()` is called, cleared when woken up.

2. **`sleep_deadline` (uint64_t)**: Stores the absolute deadline in nanoseconds since boot when the process should be woken up.

### Sleep Queue

The kernel maintains a separate circular linked list called `sleep_queue` (in `sched.c`) to track all sleeping processes. When a process calls `user_sleep()`, it is added to this queue and removed from the run queue via `sched_block()`. When woken up, the process is removed from the sleep queue and added back to the run queue via `sched_resume()`.

### Design Decisions

Using a separate sleep queue (rather than keeping sleeping processes in the run queue) allows the scheduler to skip sleeping processes entirely when selecting the next process to run. The queue is maintained as a circular linked list in insertion order (not sorted by deadline), which is acceptable since `sched_wake_sleepers()` checks all sleeping processes on each invocation.

---

## How Wake-ups Are Triggered

Wake-ups are triggered through `sched_wake_sleepers()`, which is called from `sched_yield()`. Since `sched_yield()` is invoked from `software_trap_handler()` on every trap/interrupt, wake-up checks occur:

- On every timer interrupt (every 50ms)
- On every system call
- On any other trap/interrupt

The `sched_wake_sleepers()` function iterates through the sleep queue, checks if each process's deadline has expired by comparing `current_time >= sleep_deadline`, and if so, removes the process from the sleep queue and adds it back to the run queue. The current time is fetched immediately before checking each process to ensure that when a process is woken up, `user_gettime()` will return a value >= `deadline`.

---

## Scheduler Design Decisions

### Separate Sleep Queue

We use a separate `sleep_queue` rather than keeping sleeping processes in the run queue. This simplifies scheduler logic and makes the distinction between runnable and sleeping processes explicit.

### Unsorted Queue

The sleep queue is maintained in insertion order (FIFO) rather than sorted by deadline. Insertion is O(1), and since we check all processes on each wake-up call, sorting is unnecessary for correctness.

### Wake-up Check Frequency

Wake-ups are checked on every scheduler yield (every trap/interrupt), not just timer interrupts. This ensures maximum responsiveness and guarantees correctness even if timer interrupts are delayed.

### Reusing `next` Pointer

The same `next` pointer field in the PCB is reused for both run queues and the sleep queue. This saves memory since processes are only in one queue at a time.

### Immediate Return for Past Deadlines

If `deadline <= current_time` when `user_sleep()` is called, the function returns immediately without blocking.

---

## How We Use AI

We used Cursor as our IDE, and some of the functions were automatically completed by it. Since the auto-inferred code was already quite solid, we only needed to debug it slightly. Cursor is also very good at generating comments, so all of the comments were produced by it. In addition, we used Cursor to help improve the writing quality of the explanation file.