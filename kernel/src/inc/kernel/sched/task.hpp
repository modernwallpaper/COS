#pragma once

#include <cstdint>

enum TaskState : uint8_t {
    TASK_READY = 0,
    TASK_RUNNING,
    TASK_SLEEPING,
    TASK_DEAD,
};

struct task {
    uint64_t id;
    uint64_t rsp;
    uint64_t stack_base;
    uint64_t wake_tick;
    const char* name;
    TaskState state;
    bool idle;
};

struct interrupt_frame;

class Scheduler {
public:
    static constexpr int MAX_TASKS = 32;
    static constexpr int THREAD_STACK_SIZE = 16 * 1024;
    static constexpr uint64_t TICK_MS = 10;

    Scheduler();
    int add_task(task* t);
    task* create_kthread(void (*entry)(), const char* name, bool idle = false);
    uint64_t switch_if_needed(interrupt_frame* frame);
    void on_tick();
    void yield();
    void sleep_ms(uint64_t ms);

    uint64_t ticks() const
    {
        return tick_count;
    }

    uint64_t switches() const
    {
        return switch_count;
    }

    task* current() const
    {
        return current_task;
    }

private:
    task* tasks[MAX_TASKS];
    int task_count = 0;
    task* current_task = nullptr;
    uint64_t next_task_id = 1;
    uint64_t tick_count = 0;
    uint64_t switch_count = 0;

    task* pick_next(int current_idx);
};

extern Scheduler scheduler;
extern "C" uint64_t scheduler_switch_if_needed(interrupt_frame* frame);
extern "C" void scheduler_on_tick();
void sleep_ms(uint64_t ms);
