#pragma once
#include <cstdarg>
#include <cstdint>
#include <inc/kernel/kshell/kshell.hpp>

// The IDT (Interrupt Descriptor Table) maps interrupt vectors to handlers.
//
// Vectors 0-31:   CPU exceptions (divide-by-zero, page fault, etc.)
// Vectors 32-47:  Hardware IRQs (remapped from PIC/APIC)
// Vectors 48-255: Available for software interrupts and APIC

// Standard 64-bit IDT entry
struct IDT_entry_struct {
    uint16_t offset_low;
    uint16_t selector;       // GDT code segment selector
    uint8_t ist;             // Interrupt Stack Table offset (0 = no IST switch)
    uint8_t type_attributes; // Gate type, DPL, present
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

// Pointer used by the lidt instruction
struct IDTR {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

// Full interrupt frame pushed by ISR stubs (see idt.asm).
// The CPU pushes rip, cs, rflags, rsp, ss automatically.
// Our stubs push all GPRs plus the vector number and error code.
struct interrupt_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;

    uint64_t vector;
    uint64_t error;

    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

class IDT {
private:
    IDT_entry_struct idt[256];
    IDTR idtr;

public:
    KShell* kshell;
    IDT(KShell* kshell);
    void load_idt();
    void set_idt_gate(int n, void* handler);
    void print_interrupt_message(const char* msg, ...);
};

extern "C" void isr_handler(interrupt_frame* frame);
extern "C" void irq_handler(interrupt_frame* frame);
extern IDT* global_idt;
