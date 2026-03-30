#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>

typedef void (*task_func_t)(void);

typedef struct {
    task_func_t func;
    uint32_t period;
    uint32_t last_run;
} task_t;

void scheduler_init(void);
void scheduler_add_task(task_func_t func, uint32_t period);
void scheduler_run(void);

extern volatile uint32_t sys_tick;

#endif
