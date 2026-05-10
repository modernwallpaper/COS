#pragma once
#include <cstdint>
#include <cstdarg>
#include <inc/kernel/kshell/kshell.hpp>

// Standard 64-bit IDT entry
struct IDT_entry_struct 
{
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attributes;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct IDTR 
{
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

// Full interrupt frame pushed by ISR stubs
struct interrupt_frame 
{
    uint64_t r15,r14,r13,r12,r11,r10,r9,r8;
    uint64_t rdi,rsi,rbp,rbx,rdx,rcx,rax;

    uint64_t vector;
    uint64_t error;

    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

class IDT 
{
private:
    KShell* kshell;
    IDT_entry_struct idt[256];
    IDTR idtr;
public:
    IDT(KShell* kshell);
    void load_idt();
    void set_idt_gate(int n, void* handler);
    void print_interrupt_message(const char* msg, ...);
};

extern "C" void isr_handler(interrupt_frame* frame);
extern "C" void irq_handler(interrupt_frame* frame);
