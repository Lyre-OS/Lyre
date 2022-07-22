#ifndef _SCHED__SCHED_H
#define _SCHED__SCHED_H

#include <stdbool.h>
#include <sched/proc.h>

extern struct process *kernel_process;

void sched_init(void);
void sched_await(void);
bool sched_ready(void);

#endif