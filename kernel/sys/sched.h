#ifndef _SYS__SCHED_H
#define _SYS__SCHED_H

#include <sys/cpu.h>
#include <lib/vector.h>

#define THREAD_FLAGS_IDLE 0
#define THREAD_FLAGS_RUN 1

// Dummy scheduler structs
struct thread {
    struct cpu_ctx ctx;
    int flags;
};
typedef int tid_t;

struct proc {
    VECTOR_TYPE(struct thread) threads;
    size_t curr_thread;
};
typedef int pid_t;

void sched_init(void);
struct thread *sched_thread(struct proc *proc);

#endif
