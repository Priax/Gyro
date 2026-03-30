#include "scheduler.h"

#define MAX_TASKS 8

static task_t tasks[MAX_TASKS];
static int task_count = 0;

volatile uint32_t sys_tick = 0;

void scheduler_init(void) {
    task_count = 0;
}

void scheduler_add_task(task_func_t func, uint32_t period) {
    if (task_count < MAX_TASKS) {
        tasks[task_count].func = func;
        tasks[task_count].period = period;
        tasks[task_count].last_run = 0;
        task_count++;
    }
}

void scheduler_run(void) {
    for (int i = 0; i < task_count; i++) {
        if ((sys_tick - tasks[i].last_run) >= tasks[i].period) {
            tasks[i].last_run = sys_tick;
            tasks[i].func();
        }
    }
}
