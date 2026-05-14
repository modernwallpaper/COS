#pragma once

#include <cstdint>
#include <inc/kernel/apic/apic.hpp>
#include <inc/kernel/kshell/kshell.hpp>
#include <limine.h>

struct cpu_info {
    uint32_t lapic_id;
    uint32_t acpi_id;
    bool online;
    bool bsp;
    uint64_t stack_base;
    uint64_t ipi_count;
};

void smp_init(KShell* kshell, Apic* apic, limine_mp_response* mp_response,
              uint64_t hhdm_offset);

extern cpu_info cpu_infos[64];
extern uint64_t cpu_count;
