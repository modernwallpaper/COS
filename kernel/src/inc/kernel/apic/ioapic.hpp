#pragma once

#include <cstdint>

class KShell;
class Buddy;

class IOAPIC {
public:
    IOAPIC(KShell* kshell, Buddy* buddy, uint64_t hhdm_offset);
    ~IOAPIC();

    bool init(uint64_t lapic_id, uintptr_t rsdp_phys);

private:
    KShell* kshell;
    Buddy* buddy;
    uint64_t hhdm_offset;
    uintptr_t ioapic_phys;
    uint64_t gsi_base;
    uint32_t redir_count;

    uint32_t ioapic_read(uint8_t reg);
    void ioapic_write(uint8_t reg, uint32_t value);
    void set_irq_redirect(uint32_t irq, uint8_t vector, uint64_t dest_apic_id, bool masked);

    uintptr_t find_madt(uintptr_t rsdp_phys);
};
