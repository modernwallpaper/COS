#pragma once

#include "inc/kernel/apic/ioapic.hpp"
#include "inc/kernel/vmm/vmm.hpp"
#include <cstdint>

struct acpi_hpet {
    acpi_sdt header;
    uint8_t hardware_rev_id;
    uint8_t info;
    uint16_t pci_vendor_id;
    uint8_t address_space_id; // 0 = system memory
    uint8_t register_bit_width;
    uint8_t register_bit_offset;
    uint8_t reserved;
    uint64_t base_address; // MMIO phys addr
    uint8_t hpet_number;
    uint16_t minimum_tick;
    uint8_t page_protection;
} __attribute__((packed));

class HPET {
private:
    uintptr_t mmio_base;
    uint64_t
        period_fs; // femtoseconds per tick (e.g., 10000000 = 10 ns = 100 MHz)
    uint64_t counter_mask;

public:
    HPET(uintptr_t rsdp_addr, VMM* vmm, uint64_t hddm);
    ~HPET();

    void enable();
    uint64_t read_counter();
    void busy_wait_ns(uint64_t ns);
    uint64_t counter_to_ns(uint64_t ticks);
    uint64_t get_period_fs()
    {
        return period_fs;
    }
};
