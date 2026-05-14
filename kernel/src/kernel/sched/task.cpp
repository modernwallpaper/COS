#include <cstdint>
#include <inc/kernel/idt/idt.hpp>
#include <inc/kernel/mem/kmalloc.hpp>
#include <inc/kernel/ports/ports.hpp>
#include <inc/kernel/sched/task.hpp>

static constexpr int THREAD_STACK_SIZE = 16384;

Scheduler scheduler;

extern "C" uint64_t scheduler_switch_if_needed(interrupt_frame *frame) {
    return scheduler.switch_if_needed(frame);
}

Scheduler::Scheduler() {
    task_count = 0;
    current_task = nullptr;
}

int Scheduler::add_task(task *t) {
    if (task_count >= MAX_TASKS)
        return -1;
    tasks[task_count++] = t;
    if (current_task == nullptr)
        current_task = t;
    return task_count - 1;
}

uint64_t Scheduler::switch_if_needed(interrupt_frame *frame) {
    if (current_task == nullptr || task_count == 0) {
        serial_print("[SCHED] no tasks\n");
        return (uint64_t)frame;
    }

    int current_idx = -1;
    for (int i = 0; i < task_count; i++) {
        if (tasks[i] == current_task) {
            current_idx = i;
            break;
        }
    }
    if (current_idx < 0)
        return (uint64_t)frame;

    current_task->rsp = (uint64_t)frame;
    current_task->state = TASK_READY;

    task *next = nullptr;
    for (int i = 1; i <= task_count; i++) {
        int idx = (current_idx + i) % task_count;
        if (tasks[idx]->state == TASK_READY) {
            next = tasks[idx];
            break;
        }
    }

    if (next) {
        serial_print("[SCHED] switch curr=");
        serial_print_hex((uint64_t)current_task);
        serial_print(" next=");
        serial_print_hex((uint64_t)next);
        serial_print(" rsp=");
        serial_print_hex(next->rsp);
        serial_print(" base=");
        serial_print_hex(next->stack_base);
        if (next->rsp) {
            interrupt_frame *f = (interrupt_frame *)next->rsp;
            serial_print(" rip=");
            serial_print_hex(f->rip);
            serial_print(" cs=");
            serial_print_hex(f->cs);
            serial_print(" rfl=");
            serial_print_hex(f->rflags);
        }
        serial_print("\n");

        next->state = TASK_RUNNING;
        current_task = next;
        return next->rsp;
    }

    serial_print("[SCHED] no switch\n");
    current_task->state = TASK_RUNNING;
    return (uint64_t)frame;
}

static uint8_t kthread_stacks[4][THREAD_STACK_SIZE]
    __attribute__((aligned(4096)));
static int kthread_stack_idx = 0;

task *Scheduler::create_kthread(void (*entry)()) {
    task *t = (task *)kmalloc(sizeof(task));
    if (!t)
        return nullptr;

    if (kthread_stack_idx >= 4)
        return nullptr;
    uint8_t *stack = kthread_stacks[kthread_stack_idx++];

    t->stack_base = (uint64_t)stack;
    t->state = TASK_READY;

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

    uint64_t *top = (uint64_t *)(stack + THREAD_STACK_SIZE);

    // Address of the error field inside the frame — this becomes saved_RSP.
    // After all 22 pushes: top = stack+16384 - 22*8 = stack+16384-176.
    // Error field is at top+128 = stack+16384-48.
    uint64_t error_addr = (uint64_t)(stack + THREAD_STACK_SIZE - 48);

    *--top = 0;                 // ss: null selector (offset +168)
    *--top = error_addr;        // rsp: points at error field (offset +160)
    *--top = 0x202;             // rflags: IF=1, reserved bit 1 (offset +152)
    *--top = 0x08;              // cs: kernel code segment (offset +144)
    *--top = (uint64_t)entry;   // rip: thread entry point (offset +136)

    // Error code + vector (consumed by add rsp,16 in .restore)
    *--top = 0;                 // error code (offset +128)
    *--top = 0;                 // vector number (offset +120)

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
