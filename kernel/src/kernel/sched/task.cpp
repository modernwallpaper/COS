#include <inc/kernel/idt/idt.hpp>
#include <inc/kernel/mem/kmalloc.hpp>
#include <inc/kernel/ports/ports.hpp>
#include <inc/kernel/sched/task.hpp>

Scheduler scheduler;

extern "C" uint64_t scheduler_switch_if_needed(interrupt_frame* frame)
{
    return scheduler.switch_if_needed(frame);
}

extern "C" void scheduler_on_tick()
{
    scheduler.on_tick();
}

void sleep_ms(uint64_t ms)
{
    scheduler.sleep_ms(ms);
}

Scheduler::Scheduler()
{
    task_count = 0;
    current_task = nullptr;
    next_task_id = 1;
    tick_count = 0;
    switch_count = 0;
}

int Scheduler::add_task(task* t)
{
    if (task_count >= MAX_TASKS)
        return -1;
    t->id = next_task_id++;
    tasks[task_count++] = t;
    if (current_task == nullptr)
        current_task = t;
    return task_count - 1;
}

void Scheduler::on_tick()
{
    tick_count++;

    for (int i = 0; i < task_count; i++)
    {
        task* t = tasks[i];
        if (t->state == TASK_SLEEPING && t->wake_tick <= tick_count)
            t->state = TASK_READY;
    }

    if ((tick_count % 100) == 0 && current_task != nullptr)
    {
        serial_print("[SCHED] ticks=");
        serial_print_hex(tick_count);
        serial_print(" switches=");
        serial_print_hex(switch_count);
        serial_print(" current=");
        serial_print(current_task->name ? current_task->name : "<unnamed>");
        serial_print("\n");
    }
}

task* Scheduler::pick_next(int current_idx)
{
    task* idle_task = nullptr;

    for (int i = 1; i <= task_count; i++)
    {
        int idx = (current_idx + i) % task_count;
        task* candidate = tasks[idx];

        if (candidate->state != TASK_READY)
            continue;

        if (candidate->idle)
        {
            if (idle_task == nullptr)
                idle_task = candidate;
            continue;
        }

        return candidate;
    }

    return idle_task;
}

uint64_t Scheduler::switch_if_needed(interrupt_frame* frame)
{
    if (current_task == nullptr || task_count == 0)
    {
        serial_print("[SCHED] no tasks\n");
        return (uint64_t)frame;
    }

    int current_idx = -1;
    for (int i = 0; i < task_count; i++)
    {
        if (tasks[i] == current_task)
        {
            current_idx = i;
            break;
        }
    }
    if (current_idx < 0)
        return (uint64_t)frame;

    current_task->rsp = (uint64_t)frame;
    if (current_task->state == TASK_RUNNING)
        current_task->state = TASK_READY;

    task* next = pick_next(current_idx);

    if (next)
    {
        next->state = TASK_RUNNING;
        if (next != current_task)
            switch_count++;
        current_task = next;
        return next->rsp;
    }

    current_task->state = TASK_RUNNING;
    return (uint64_t)frame;
}

void Scheduler::sleep_ms(uint64_t ms)
{
    if (current_task == nullptr || current_task->idle)
        return;

    uint64_t ticks_to_sleep = (ms + TICK_MS - 1) / TICK_MS;
    if (ticks_to_sleep == 0)
        ticks_to_sleep = 1;

    asm volatile("cli" ::: "memory");
    current_task->wake_tick = tick_count + ticks_to_sleep;
    current_task->state = TASK_SLEEPING;
    asm volatile("sti" ::: "memory");

    while (current_task->state == TASK_SLEEPING)
        asm volatile("hlt" ::: "memory");
}

task* Scheduler::create_kthread(void (*entry)(), const char* name, bool idle)
{
    task* t = (task*)kmalloc(sizeof(task));
    if (!t)
        return nullptr;

    uint8_t* stack = (uint8_t*)kmalloc(THREAD_STACK_SIZE);
    if (!stack)
    {
        kfree(t);
        return nullptr;
    }

    t->stack_base = (uint64_t)stack;
    t->wake_tick = 0;
    t->name = name;
    t->state = TASK_READY;
    t->idle = idle;

    // Build a fake interrupt_frame at the top of the stack that isr_common
    // will restore when this task is first scheduled.
    //
    // In IA-32e 64-bit mode, IRETQ always pops 5 items: RIP, CS, RFLAGS,
    // RSP, SS — even for ring-0 → ring-0 returns (SDM Vol. 2A §IRET,
    // "SS:RSP pops unconditionally").  The CPU also pushes RSP/SS during
    // interrupt delivery due to mandatory 16-byte RSP alignment in this
    // mode, so 5 Pops match 5 Pushes.
    //
    // Stack layout (top = low addr, * = CPU-pushed):
    //
    //   [rsp+  0] r15        <-- saved frame pointer (t->rsp)
    //   [rsp+  8] r14
    //   [rsp+ 16] r13
    //   [rsp+ 24] r12
    //   [rsp+ 32] r11
    //   [rsp+ 40] r10
    //   [rsp+ 48] r9
    //   [rsp+ 56] r8
    //   [rsp+ 64] rdi
    //   [rsp+ 72] rsi
    //   [rsp+ 80] rbp
    //   [rsp+ 88] rbx
    //   [rsp+ 96] rdx
    //   [rsp+104] rcx
    //   [rsp+112] rax
    //   [rsp+120] vector      (add rsp,16 skips these two)
    //   [rsp+128] error       ← saved_RSP points here (value 0, 16‑byte aligned)
    //   [rsp+136] rip         *
    //   [rsp+144] cs          *
    //   [rsp+152] rflags      *
    //   [rsp+160] rsp         * ← saved_RSP = &error = frame+128
    //   [rsp+168] ss          *
    //
    // After IRETQ pops RIP/CS/RFLAGS, it loads RSP from offset 160, then
    // reads SS from [new_RSP].  By pointing saved_RSP at the error field
    // (value 0, 16‑byte aligned), SS = 0 and final RSP = frame+136
    // (≡ 8 mod 16, correct for -fomit-frame-pointer).

    uint64_t* top = (uint64_t*)(stack + THREAD_STACK_SIZE);

    // Address of the error field inside the frame — this becomes saved_RSP.
    // After all 22 pushes: top = stack + THREAD_STACK_SIZE - 22*8.
    // Error field is at top+128 = stack + THREAD_STACK_SIZE - 48.
    uint64_t error_addr = (uint64_t)(stack + THREAD_STACK_SIZE - 48);

    *--top = 0;               // ss: null selector (offset +168)
    *--top = error_addr;      // rsp: points at error field (offset +160)
    *--top = 0x202;           // rflags: IF=1, reserved bit 1 (offset +152)
    *--top = 0x08;            // cs: kernel code segment (offset +144)
    *--top = (uint64_t)entry; // rip: thread entry point (offset +136)

    // Error code + vector (consumed by add rsp,16 in .restore)
    *--top = 0; // error code (offset +128)
    *--top = 0; // vector number (offset +120)

    // 15 saved general-purpose registers (all zero)
    *--top = 0; // rax (offset +112)
    *--top = 0; // rcx (offset +104)
    *--top = 0; // rdx (offset  +96)
    *--top = 0; // rbx (offset  +88)
    *--top = 0; // rbp (offset  +80)
    *--top = 0; // rsi (offset  +72)
    *--top = 0; // rdi (offset  +64)
    *--top = 0; // r8  (offset  +56)
    *--top = 0; // r9  (offset  +48)
    *--top = 0; // r10 (offset  +40)
    *--top = 0; // r11 (offset  +32)
    *--top = 0; // r12 (offset  +24)
    *--top = 0; // r13 (offset  +16)
    *--top = 0; // r14 (offset   +8)
    *--top = 0; // r15 (offset   +0)

    t->rsp = (uint64_t)top;
    return t;
}
