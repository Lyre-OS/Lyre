#include <sys/sched.h>
#include <lib/print.h>
#include <dev/lapic.h>
#include <sys/idt.h>
#include <sys/cpu.h>

static uint8_t sched_vec;

static void sched_handler(uint8_t vector, struct cpu_ctx *ctx) {
    print("sched: Hello world\n");
    lapic_eoi();
}

void sched_init(void) {
    sti();

    unsigned int freq = lapic_timer_calibrate();
    print("sched: Asking LAPIC to start interrupting freq=%u\n", freq);

    sched_vec = idt_allocate_vector();
    isr[sched_vec] = &sched_handler;

    lapic_send_ipi(0, sched_vec | (1 << 18));
    print("sched: Initialized\n");
}

// Simple round robin scheduler for threads
struct thread *sched_thread(struct proc *proc) {
    size_t initial_tid = proc->curr_thread;
    struct thread *next_thread;
    for (;;) {
        proc->curr_thread++;
        if (proc->curr_thread >= proc->threads.length) {
            proc->curr_thread = 0;
        }
        next_thread = &VECTOR_ITEM(proc->threads, proc->curr_thread);
        if (next_thread->flags == THREAD_FLAGS_RUN) {
            break;
        }

        if (initial_tid == proc->curr_thread) {
            print("No active threads\n");
        }
    }
    return next_thread;
}
