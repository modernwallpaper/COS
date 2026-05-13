#include <cstdint>
#include <cstdarg>
#include <inc/kernel/idt/idt.hpp>
#include <inc/kernel/kshell/kshell.hpp>
#include <inc/kernel/ports/ports.hpp>

IDT* global_idt;

// --------------------
// CPU exception stubs — defined in idt.asm
// --------------------
extern "C" void isr0();  extern "C" void isr1();  extern "C" void isr2();  extern "C" void isr3();
extern "C" void isr4();  extern "C" void isr5();  extern "C" void isr6();  extern "C" void isr7();
extern "C" void isr8();  extern "C" void isr9();  extern "C" void isr10(); extern "C" void isr11();
extern "C" void isr12(); extern "C" void isr13(); extern "C" void isr14(); extern "C" void isr15();
extern "C" void isr16(); extern "C" void isr17(); extern "C" void isr18(); extern "C" void isr19();
extern "C" void isr20(); extern "C" void isr21(); extern "C" void isr22(); extern "C" void isr23();
extern "C" void isr24(); extern "C" void isr25(); extern "C" void isr26(); extern "C" void isr27();
extern "C" void isr28(); extern "C" void isr29(); extern "C" void isr30(); extern "C" void isr31();

// Lookup table mapping vector 0-31 to their stub entry points
extern "C" void (*isr_table[32])() = {
    isr0,isr1,isr2,isr3,isr4,isr5,isr6,isr7,
    isr8,isr9,isr10,isr11,isr12,isr13,isr14,isr15,
    isr16,isr17,isr18,isr19,isr20,isr21,isr22,isr23,
    isr24,isr25,isr26,isr27,isr28,isr29,isr30,isr31
};

// --------------------
// Hardware IRQ stubs — defined in idt.asm
// IRQ 0-15 are mapped to vectors 32-47 by the PIC/APIC
// --------------------
extern "C" void irq0();  extern "C" void irq1();  extern "C" void irq2();  extern "C" void irq3();
extern "C" void irq4();  extern "C" void irq5();  extern "C" void irq6();  extern "C" void irq7();
extern "C" void irq8();  extern "C" void irq9();  extern "C" void irq10(); extern "C" void irq11();
extern "C" void irq12(); extern "C" void irq13(); extern "C" void irq14(); extern "C" void irq15();

extern "C" void (*irq_table[16])() = {
    irq0,irq1,irq2,irq3,irq4,irq5,irq6,irq7,
    irq8,irq9,irq10,irq11,irq12,irq13,irq14,irq15
};

// APIC timer stub
extern "C" void apic_timer_stub();

// --------------------
// ISR Handler — C entry point for ALL interrupts
// --------------------
extern "C" void isr_handler(interrupt_frame* frame) 
{
    if(frame->vector == 0) // divide by zero
    {
        // Skip the offending instruction (2 bytes for DIV/IDIV)
        frame->rip += 2;
    }
    else if(frame->vector == 13) // general protection fault
    {
        serial_print("\n[ERROR] GPF at RIP=");
        serial_print_hex(frame->rip);
        serial_print(", error=");
        serial_print_hex(frame->error);
        serial_print("\n");
        global_idt->kshell->print_kernel_error(
            "GPF at RIP=%x, error=%x (selector=%x) (ext=%s) (idt=%s)",
            frame->rip, frame->error,
            frame->error & 0xFFF8,
            (frame->error & 1) ? "yes" : "no",
            (frame->error & 2) ? "yes" : "no"
        );
        while(true) asm volatile("hlt");
    }
    else if(frame->vector == 14) // page fault
    {
        uint64_t cr2;
        asm volatile("mov %%cr2, %0" : "=r"(cr2));
        global_idt->kshell->print_kernel_error(
            "Page fault at RIP=%x, fault addr=%x, error=%x%s%s%s",
            frame->rip, cr2, frame->error,
            (frame->error & 1) ? " (protection)" : " (not-present)",
            (frame->error & 2) ? " write" : " read",
            (frame->error & 4) ? " user" : " supervisor"
        );
        while(true) asm volatile("hlt");
    }
    else if(frame->vector >= 32 && frame->vector <= 47)
    {
        irq_handler(frame); // forward hardware IRQs
    }
    else if(frame->vector == 48)
    {
        // APIC timer fired — acknowledge by reading the LAPIC EOI register
        // (writing any value to 0x0B0 signals EOI to the local APIC)
        *(volatile uint32_t *)0xFEE000B0 = 0;
    }
    else
    {
        // Unhandled exception — print RIP and halt
        serial_print("\n[ERROR] Unhandled exception vector ");
        serial_print_hex(frame->vector);
        serial_print(" at RIP=");
        serial_print_hex(frame->rip);
        serial_print(", error=");
        serial_print_hex(frame->error);
        serial_print("\n");
        global_idt->kshell->print_kernel_error(
            "Unhandled exception vector %d at RIP=%x, error=%x",
            frame->vector, frame->rip, frame->error
        );
        while(true) asm volatile("hlt");
    }
}

// IRQ handler — called for vectors 32-47 (hardware interrupts).
// Sends EOI to the PIC so it can fire again.
extern "C" void irq_handler(interrupt_frame* frame) 
{
    global_idt->print_interrupt_message("IRQ vector %d fired", frame->vector);
    uint8_t irq = frame->vector - 32;
    pic_send_eoi(irq);
}

// --------------------
// IDT class
// --------------------

IDT::IDT(KShell* kshell) 
{
    this->kshell = kshell;
    global_idt = this;

    // CPU exceptions 0-31
    for(int i=0;i<32;i++)
        set_idt_gate(i, (void*)isr_table[i]);

    // Hardware IRQs 0-15 → vectors 32-47
    for(int i=0;i<16;i++)
        set_idt_gate(32+i, (void*)irq_table[i]);

    // APIC timer → vector 48
    set_idt_gate(48, (void*)apic_timer_stub);

    this->load_idt();
    asm volatile("sti"); // enable interrupts
}

// Populate a single IDT entry with a handler address.
// All entries use ring 0, interrupt gate type.
void IDT::set_idt_gate(int n, void* handler) 
{
    uint64_t addr = (uint64_t)handler;
    idt[n].offset_low = addr & 0xFFFF;
    idt[n].selector = 0x08; // kernel code segment (GDT entry 1)
    idt[n].ist = 0;
    idt[n].type_attributes = 0x8E; // present, ring 0, interrupt gate
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
