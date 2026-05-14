#pragma once

#include <cstdint>
#include <inc/kernel/kshell/kshell.hpp>

// In 64-bit long mode, segmentation is largely disabled. The GDT exists
// mainly to set the DPL (ring level) for code/data segments and to hold
// the Task State Segment (TSS) for interrupt stack switching.
//
// Layout:
//   0 — Null descriptor (required)
//   1 — Kernel code (ring 0, long mode)
//   2 — Kernel data (ring 0)
//   3 — User code   (ring 3, long mode)
//   4 — User data   (ring 3)
//   5 — TSS low     (IST entries for interrupt stacks)
//   6 — TSS high

// Standard 64-bit GDT entry
struct gdt_entry_struct 
{
    uint16_t limit_low;        // bits 0-15
    uint16_t base_low;         // bits 16-31
    uint8_t  base_middle;      // bits 32-39
    uint8_t  access;           // bits 40-47
    uint8_t  granularity;      // bits 48-55
    uint8_t  base_high;        // bits 56-63
} __attribute__((packed));

// Pointer to GDT (used with lgdt instruction)
struct gdt_ptr_struct 
{
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

// High 64-bit part of TSS descriptor (TSS needs 2 consecutive GDT entries)
struct gdt_entry_tss 
{
    uint32_t base_high;        // bits 63-32 of base
    uint32_t reserved;
} __attribute__((packed));

// 64-bit TSS structure.
// The TSS no longer stores task state for task switching (that's gone in
// long mode). Instead it holds RSP0 (kernel stack pointer for ring 0
// entries) and IST (Interrupt Stack Table) pointers.
struct tss_struct 
{
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base;
} __attribute__((packed));

class GDT 
{
private:
    static constexpr int ENTRIES = 7; // 5 normal + 2 for TSS

    gdt_entry_struct gdt_entries[ENTRIES];
    gdt_ptr_struct gdt_ptr;
    gdt_entry_tss* tss_high_entry; // pointer to second GDT entry for TSS
    tss_struct tss __attribute__((aligned(16)));

    KShell *kshell;
    uint64_t KERNEL_STACK_SIZE;
    uint8_t *kernel_stack;

public:
    GDT(KShell *kshell, uint64_t KERNEL_STACK_SIZE, uint8_t *kernel_stack);
    void set_gdt_gate(uint32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t granularity);
    void set_tss_gate(uint32_t num, uint64_t base, uint32_t limit);
    tss_struct* get_tss() { return &tss; };
    void check_entries();
};
