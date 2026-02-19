#include "embryos.h"

#define N_PRIORITIES 3

struct pcb *run_queue[N_PRIORITIES];
struct pcb *sleep_queue; // queue for sleeping processes

void sched_resume(struct pcb *pcb) { proc_enqueue(&run_queue[0], pcb); }
void sched_wake_sleepers(void)
{
    if (sleep_queue == 0)
        return; // no sleeping processes

    // Iterate through the circular queue
    // We need to be careful: processes use 'next' for both sleep_queue and run_queue
    struct pcb *start = sleep_queue->next; // head of circular queue
    struct pcb *pcb = start;
    struct pcb *prev = sleep_queue; // tail (element before head)

    if (pcb == 0)
        return;

    // Count iterations to avoid infinite loops
    int iterations = 0;
    int max_iterations = 100; // safety limit

    do
    {
        if (sleep_queue == 0)
            break; // queue became empty

        struct pcb *next = pcb->next;

        // Check if this process's deadline has expired
        // Re-check time right before waking to guarantee deadline has passed
        if (pcb->sleeping)
        {
            // Get current time right before checking this specific process
            // This ensures that when we wake it, user_gettime() will return >= deadline
            uint64_t ticks = mtime_get();
            uint64_t current_time = (ticks * 1000000000ULL) / time_base;

            if (current_time > pcb->sleep_deadline)
            {
                // Wake up this process - clear flag FIRST
                pcb->sleeping = 0;

                // Remove from sleep queue
                if (pcb == next)
                {
                    // Only one process in queue
                    sleep_queue = 0;
                    sched_resume(pcb);
                    break;
                }
                else
                {
                    // Remove from circular queue: prev->next = next
                    prev->next = next;

                    // Update sleep_queue pointer if we removed the tail
                    if (pcb == sleep_queue)
                    {
                        sleep_queue = prev;
                    }

                    // Update start if we removed the head
                    if (pcb == start)
                    {
                        start = next;
                        if (sleep_queue == 0)
                        {
                            break;
                        }
                    }

                    // Add to run queue AFTER removing from sleep queue
                    // This will overwrite the 'next' pointer, which is fine
                    sched_resume(pcb);
                }
            }
            else
            {
                // Deadline hasn't passed yet, advance to next process
                prev = pcb;
            }
        }
        else
        {
            // Process is not sleeping, advance to next
            prev = pcb;
        }

        pcb = next;
        iterations++;

        // Safety check
        if (iterations > max_iterations)
        {
            break;
        }
    } while (pcb != start && sleep_queue != 0);
}

void sched_block(struct pcb *old)
{
    int p = 0; // first find highest priority process
    while (p < N_PRIORITIES && run_queue[p] == 0)
        p++;

    if (p >= N_PRIORITIES)
    {
        return;
    }

    struct pcb *new = proc_dequeue(&run_queue[p]);
    if (new != old)
    { // switch needed
        new->hart = old->hart;
        new->hart->needs_tlb_flush = 1;
        sched_set_self(new);
        L3(L_FREQ, L_CTX_SWITCH, (uintptr_t)old, (uintptr_t)new, new->hart->id);
        ctx_switch(&old->sp, new->sp);
        reap_zombies();
    }
}

void sched_run(int executable, struct rect area, void *args, int size)
{
    struct pcb *old = sched_self();
    proc_enqueue(&run_queue[0], old);
    struct pcb *new = proc_create(old->hart, executable, area, args, size);
    sched_set_self(new);
    L4(L_NORM, L_CTX_START, (uintptr_t)old, (uintptr_t)new, new->hart->id, executable);
    ctx_start(&old->sp, (char *)new + PAGE_SIZE);
    reap_zombies();
}

void sched_yield(void)
{
    sched_wake_sleepers(); // Check and wake expired sleepers
    struct pcb *current = sched_self();
    if (current->executable > 0)
    { // idle loop doesn't yield at interrupts
        proc_enqueue(&run_queue[1], current);
        sched_block(current);
    }
}