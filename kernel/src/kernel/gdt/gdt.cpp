#include <cstdint>
#include <inc/kernel/gdt/gdt.hpp>
#include <inc/kernel/kshell/kshell.hpp>

extern "C" void gdt_flush(struct gdt_ptr_struct *gdt_ptr);

GDT::GDT(KShell *kshell, uint64_t KERNEL_STACK_SIZE, uint8_t *kernel_stack) {
    this->kshell = kshell;
    this->KERNEL_STACK_SIZE = KERNEL_STACK_SIZE;
    this->kernel_stack = kernel_stack;

    // --- GDT setup moved to constructor ---
    this->gdt_ptr.limit = sizeof(this->gdt_entries) - 1;
    this->gdt_ptr.base = reinterpret_cast<uint64_t>(&this->gdt_entries);

    set_gdt_gate(0, 0, 0, 0, 0);

    // 0x9A = Present, ring 0, code, non-conforming, readable
    // 0x20 = Long mode (L-bit) — enables 64-bit execution for this segment
    set_gdt_gate(1, 0, 0, 0x9A, 0x20); // Kernel code
    // 0x92 = Present, ring 0, data, writable
    set_gdt_gate(2, 0, 0, 0x92, 0x00); // Kernel data

    // Same as kernel but ring 3 (0xFA = 0x9A | 0x60 for ring 3)
    // 0x20 = Long mode (L-bit)
    set_gdt_gate(3, 0, 0, 0xFA, 0x20); // User code
    // 0xF2 = 0x92 | 0x60 for ring 3
    set_gdt_gate(4, 0, 0, 0xF2, 0x00); // User data

    // TSS segment (uses two GDT entries)
    set_tss_gate(5, reinterpret_cast<uint64_t>(&tss), sizeof(tss) - 1);

    gdt_flush(&this->gdt_ptr);
}

// Fill a regular GDT entry. In 64-bit mode, base and limit are ignored
// (the segment is treated as flat), but we still set them for correctness.
void GDT::set_gdt_gate(uint32_t num, uint32_t base, uint32_t limit,
                       uint8_t access, uint8_t granularity) {
    this->gdt_entries[num].base_low = base & 0xFFFF;
    this->gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    this->gdt_entries[num].base_high = (base >> 24) & 0xFF;
    this->gdt_entries[num].limit_low = limit & 0xFFFF;
    this->gdt_entries[num].granularity =
        ((limit >> 16) & 0x0F) | (granularity & 0xF0);
    this->gdt_entries[num].access = access;
}

// Set up a TSS descriptor spanning two consecutive GDT entries.
// The TSS needs a 64-bit base and 32-bit limit because it can be
// larger than the usual segment descriptor fields allow.
void GDT::set_tss_gate(uint32_t num, uint64_t base, uint32_t limit) {
    // First 8 bytes (low descriptor)
    this->gdt_entries[num].limit_low = limit & 0xFFFF;
    this->gdt_entries[num].base_low = base & 0xFFFF;
    this->gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    this->gdt_entries[num].access =
        0x89; // Present + type 9 (available 64-bit TSS)
    this->gdt_entries[num].granularity = ((limit >> 16) & 0x0F);
    this->gdt_entries[num].base_high = (base >> 24) & 0xFF;

    // Second 8 bytes (high descriptor)
    this->tss_high_entry =
        reinterpret_cast<gdt_entry_tss *>(&gdt_entries[num + 1]);
    this->tss_high_entry->base_high = (base >> 32) & 0xFFFFFFFF;
    this->tss_high_entry->reserved = 0;

    // Clear TSS memory
    for (unsigned long i = 0; i < sizeof(tss_struct); i++)
        reinterpret_cast<uint8_t *>(&tss)[i] = 0;
    this->tss.io_map_base = sizeof(tss_struct);

    // Set RSP0 — the stack pointer the CPU switches to when an interrupt
    // occurs while running in ring 3. This is the kernel stack.
    this->tss.rsp0 = reinterpret_cast<uint64_t>(
        &kernel_stack[KERNEL_STACK_SIZE]);
}

// Print the current segment registers to verify the GDT reload worked.
// Useful for debugging.
void GDT::check_entries() {
    this->kshell->print_kernel_info("Checking GDT entries");

    uint16_t cs, ds, ss, fs, gs;
    asm volatile("mov %%cs, %w0" : "=r"(cs));
    asm volatile("mov %%ds, %w0" : "=r"(ds));
    asm volatile("mov %%ss, %w0" : "=r"(ss));
    asm volatile("mov %%fs, %w0" : "=r"(fs));
    asm volatile("mov %%gs, %w0" : "=r"(gs));

    this->kshell->print("\tCS: 0x%x\n", cs);
    this->kshell->print("\tDS: 0x%x\n", ds);
    this->kshell->print("\tSS: 0x%x\n", ss);
    this->kshell->print("\tFS: 0x%x\n", fs);
    this->kshell->print("\tGS: 0x%x\n", gs);

    this->kshell->print_kernel_info("Checking TSS entry");

    uint16_t tr;
    asm volatile("str %w0" : "=r"(tr));
    this->kshell->print("\tTSS: 0x%x\n", tr);
}
