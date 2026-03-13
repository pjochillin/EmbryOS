## Thread control block design

Each user-level thread is represented by a thread control block stored in `thread.c`. The control block contains:

- A saved **stack pointer** used by `ctx_switch` / `ctx_start` to resume the thread.
- A **state field** indicating one of
  - `RUNNABLE`
  - `SLEEPING`
  - `WAITING_INPUT`
  - `WAITING_SEMA`
- A **next pointer** used to link threads into queues and wait lists.
- A **deadline** field storing the absolute wake-up time (for sleeping threads).
- A field for the **input result** to be returned by `thread_get()` when a blocked input-waiting thread is woken.
- A pointer to the **semaphore** the thread is blocked on, when in the semaphore-wait state.
- A **start routine pointer** and **argument** for the thread’s main function, used by a small trampoline helper when the thread first starts.
- A **heap-allocated stack region** (base pointer and size) for all non-main threads, so another thread can safely free their stacks and control blocks after they exit.
- A **first-run flag** indicating whether the thread has ever been started; the scheduler uses this to choose between `ctx_start` (first run) and `ctx_switch` (subsequent runs).

The **main thread** is created in `thread_init()` as a static control block. It reuses the process’s original stack and is never freed.

---

## Run-queue organization

The scheduler maintains several global lists:

- **Run queue**: `run_head` / `run_tail` — singly linked FIFO list of threads whose `state == RUNNABLE`.
- **Sleep list**: `sleep_head` — singly linked list of threads whose `state == SLEEPING`.
- **Input-wait list**: `input_wait_head` — singly linked list of threads blocked in `thread_get()` (`WAITING_INPUT`).
- **Semaphore wait lists**: each `struct sema` has a `wait_head` list of threads blocked in `sema_dec()` on that semaphore.
- **`run_head` / `run_tail`**: the run queue — threads that are ready to run.
- **`sleep_head`**: the sleep list — threads that are sleeping until a deadline.
- **`input_wait_head`**: the input-wait list — threads blocked in `thread_get()`.
- **Per-semaphore wait lists**: each semaphore has a list of threads blocked in `sema_dec()` on that semaphore.
- **run queue helpers** add and remove threads from the runnable queue.
- A **wake-sleepers helper** consults the current time, finds all sleeping threads whose deadlines have passed, removes them from the sleep list, and enqueues them back on the run queue.
- A **helper to find the earliest deadline** on the sleep list lets the scheduler know how long it can safely sleep in the kernel before it must wake threads again.

Context switching:

- On the **first** time a thread runs, the scheduler uses `ctx_start` to save the old thread’s stack pointer, switch to the new thread’s empty stack, and jump into a small helper (`exec_user` + a trampoline) that calls the thread’s start routine.
- On **later** context switches, the scheduler uses `ctx_switch` to save the current thread’s registers on its stack and restore another thread’s registers from its own stack.

---

## Input multiplexing approach

The kernel provides a **process-level** blocking input API via `user_get(block)`:

- With `block == 0`, it is non-blocking and returns a special code if no event is available.
- With `block == 1`, it blocks the whole process until there is input and then returns a character or a focus event.

We must implement `thread_get()` which behaves like the blocking variant, but **only blocks the calling thread** and allows other threads in the same process to keep running.

Data structure:

- `input_wait_head`: list of threads that are currently blocked in `thread_get()` and waiting for input.
- Each such thread has its eventual return value stored in `input_result` when woken.

Helper:

- A small helper (`serve_one_input_waiter`) looks at the input-wait list and, if anyone is waiting, calls `user_get` either in non-blocking or blocking mode, then wakes exactly one waiting thread and records the input event in that thread’s control block.

Implementation of `thread_get` (high level):

1. First, it calls `user_get` in non-blocking mode. If a character or focus event is ready, it returns that immediately.
2. If no input is ready, it marks the current thread as waiting for input, puts it on the input-wait list, and enters the scheduler loop.

In that loop:

- It always **wakes any sleeping threads** whose deadlines have passed, and **runs any runnable thread** if one exists.
- If there are **sleeping threads but no runnable ones**, it:
  - Tries a **non-blocking** `user_get` to see if input has become available and, if so, wakes exactly one input-waiting thread.
  - Otherwise, calls `user_sleep` until the earliest sleeper’s deadline has expired.
- If there are **no sleeping threads but there are input waiters**, it calls `user_get` in **blocking** mode to wait until an input event arrives, then wakes one waiting thread.
- If there are no runnable threads, no sleepers, and no input-waiting threads, it terminates the process.

This strategy ensures that input is fairly delivered to waiting threads, sleeping threads wake on time, and blocking input in one thread never freezes animations or other work in other threads.

---

## Semaphore blocking and wake-up logic

Semaphores are defined in `thread.c` as a small structure that stores a **count** and a **wait list** of threads blocked on that semaphore.

Operations:

- Creating a semaphore allocates and initializes its count and its empty wait list.
- `sema_dec` (P/“wait”):
  - If the count is greater than zero, it simply decrements the count and returns.
  - If the count is zero, it marks the current thread as waiting on that semaphore, pushes it onto the semaphore’s wait list, and enters the same scheduler loop as used by `thread_get`. The thread will eventually return from `sema_dec` when another thread signals the semaphore.
- `sema_inc` (V/“signal”):
  - If the wait list is non-empty, it removes exactly one waiting thread from that list and puts it back on the run queue.
  - Otherwise, it increments the semaphore’s count.
- `sema_release` frees the semaphore object; it must only be called when no threads remain blocked on it.

Conceptually, this implements a standard **counting semaphore**: `sema_dec` blocks when the count is zero; `sema_inc` either wakes one blocked thread or raises the count for future callers.

---

## Instructions on how to play the game

The game is implemented in `chapter12/apps/game.c` and built as an app named `game`.

### Building and running

From the `chapter12` directory:

1. **Build:**

   ```sh
   make
   ```

2. **Run EmbryOS in QEMU:**

   ```sh
   make qemu
   ```

3. In the EmbryOS shell (`$` prompt), run the game:

   ```text
   game
   ```

   or specify a window (e.g., upper right):

   ```text
   ur game
   ```

The screen is divided into multiple windows (UL, UR, LL, LR). `game` runs in the chosen window.

### Game behavior

- The game shows a **blue rectangular area** with **four colored balls** (red, yellow, green, cyan) bouncing around.
- All balls move at the **same speed**, controlled by a shared delay value (`ball_delay_ms`) that determines how long each ball thread sleeps between position updates.
- A **score** is displayed in the **top row** of the game window:
  - The score is `wall_hits`, the total number of times any ball bounces off any wall.
  - The score is updated by the ball threads and rendered by the render thread.

### Controls

- **TAB**: Change input focus between windows and the shell.
  - You must press TAB until the **game window** has focus; otherwise, key presses go to the shell or another window, not the game.

- **Arrow keys (Up/Down)** while the game window has focus:
  - **Up arrow (↑)**:
    - Speeds up all balls: halves the global `ball_delay_ms` (down to a minimum of about 10 ms).
    - The balls move faster across the blue area.
  - **Down arrow (↓)**:
    - Slows down all balls: doubles `ball_delay_ms` (up to a maximum of about 800 ms).
    - The balls move more slowly.



---

## How We Use AI

We used Cursor as our IDE, and some of the functions were automatically completed by it. Since the auto-inferred code was already quite solid, we only needed to debug it slightly. Cursor is also very good at generating comments, so all of the comments were produced by it. In addition, we used Cursor to help improve the writing quality of the explanation file.