#pragma once

#include <cstdint>

enum TaskState : uint8_t {
    TASK_READY = 0,
    TASK_RUNNING,
    TASK_BLOCKED,
};

struct task {
    uint64_t rsp;
    uint64_t stack_base;
    TaskState state;
};

struct interrupt_frame;

class Scheduler {
public:
    Scheduler();
    int add_task(task* t);
    task* create_kthread(void (*entry)());
    uint64_t switch_if_needed(interrupt_frame* frame);

private:
    static constexpr int MAX_TASKS = 32;
    static constexpr int THREAD_STACK_SIZE = 16384;

    task* tasks[MAX_TASKS];
    int task_count = 0;
    task* current_task = nullptr;
};

extern Scheduler scheduler;
extern "C" uint64_t scheduler_switch_if_needed(interrupt_frame* frame);
