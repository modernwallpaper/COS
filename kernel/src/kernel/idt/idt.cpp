#include <cstdint>
#include <cstdarg>
#include <inc/kernel/idt/idt.hpp>
#include <inc/kernel/kshell/kshell.hpp>
#include <inc/kernel/ports/ports.hpp>

IDT* global_idt;

// --------------------
// CPU exception stubs
// --------------------
extern "C" void isr0();  extern "C" void isr1();  extern "C" void isr2();  extern "C" void isr3();
extern "C" void isr4();  extern "C" void isr5();  extern "C" void isr6();  extern "C" void isr7();
extern "C" void isr8();  extern "C" void isr9();  extern "C" void isr10(); extern "C" void isr11();
extern "C" void isr12(); extern "C" void isr13(); extern "C" void isr14(); extern "C" void isr15();
extern "C" void isr16(); extern "C" void isr17(); extern "C" void isr18(); extern "C" void isr19();
extern "C" void isr20(); extern "C" void isr21(); extern "C" void isr22(); extern "C" void isr23();
extern "C" void isr24(); extern "C" void isr25(); extern "C" void isr26(); extern "C" void isr27();
extern "C" void isr28(); extern "C" void isr29(); extern "C" void isr30(); extern "C" void isr31();

extern "C" void (*isr_table[32])() = {
    isr0,isr1,isr2,isr3,isr4,isr5,isr6,isr7,
    isr8,isr9,isr10,isr11,isr12,isr13,isr14,isr15,
    isr16,isr17,isr18,isr19,isr20,isr21,isr22,isr23,
    isr24,isr25,isr26,isr27,isr28,isr29,isr30,isr31
};

// --------------------
// Hardware IRQ stubs
// --------------------
extern "C" void irq0();  extern "C" void irq1();  extern "C" void irq2();  extern "C" void irq3();
extern "C" void irq4();  extern "C" void irq5();  extern "C" void irq6();  extern "C" void irq7();
extern "C" void irq8();  extern "C" void irq9();  extern "C" void irq10(); extern "C" void irq11();
extern "C" void irq12(); extern "C" void irq13(); extern "C" void irq14(); extern "C" void irq15();

extern "C" void (*irq_table[16])() = {
    irq0,irq1,irq2,irq3,irq4,irq5,irq6,irq7,
    irq8,irq9,irq10,irq11,irq12,irq13,irq14,irq15
};

// --------------------
// ISR Handler
// --------------------
extern "C" void isr_handler(interrupt_frame* frame) 
{
    if(frame->vector == 0) // divide by zero
    {
        frame->rip += 2; // skip offending instruction
    } 
    else if(frame->vector >= 32 && frame->vector <= 47)
    {
        irq_handler(frame); // forward IRQ to separate handler
    }
    else
    {
        global_idt->print_interrupt_message(
            "No handler for interrupt vector %d, halting CPU...", frame->vector
        );
        while(true) asm volatile("hlt");
    }
}

// Example IRQ handler
extern "C" void irq_handler(interrupt_frame* frame) 
{
    global_idt->print_interrupt_message("IRQ vector %d fired", frame->vector);
    uint8_t irq = frame->vector - 32;
    pic_send_eoi(irq);
}

// --------------------
// IDT class implementation
// --------------------
IDT::IDT(KShell* kshell) 
{
    this->kshell = kshell;
    global_idt = this;

    // CPU exceptions
    for(int i=0;i<32;i++)
        set_idt_gate(i, (void*)isr_table[i]);

    // Hardware IRQs 0–15 → vectors 32–47
    for(int i=0;i<16;i++)
        set_idt_gate(32+i, (void*)irq_table[i]);

    this->load_idt();
    asm volatile("sti");
}

void IDT::set_idt_gate(int n, void* handler) 
{
    uint64_t addr = (uint64_t)handler;
    idt[n].offset_low = addr & 0xFFFF;
    idt[n].selector = 0x08; // kernel code segment
    idt[n].ist = 0;
    idt[n].type_attributes = 0x8E; // interrupt gate
    idt[n].offset_mid = (addr >> 16) & 0xFFFF;
    idt[n].offset_high = (addr >> 32) & 0xFFFFFFFF;
    idt[n].zero = 0;
}

void IDT::load_idt()
{
    idtr.limit = sizeof(idt)-1;
    idtr.base = (uint64_t)&idt;
    asm volatile("lidt %0" : : "m"(idtr));
}

void IDT::print_interrupt_message(const char* msg, ...) 
{
    va_list args;
    va_start(args, msg);
    this->kshell->vprint_kernel_warning(msg, args);
    va_end(args);
}
