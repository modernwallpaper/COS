#pragma once

#include <cstdint>

#define IOAPIC_IOREGSEL 0x00
#define IOAPIC_IOWIN 0x10

#define IOAPIC_ID 0x00
#define IOAPIC_VER 0x01
#define IOAPIC_ARB 0x02
#define IOAPIC_REDTBL 0x10

#define IOAPIC_FIXED 0x0
#define IOAPIC_LOWEST 0x1
#define IOAPIC_SMI 0x2
#define IOAPIC_NMI 0x4
#define IOAPIC_INIT 0x5
#define IOAPIC_EXTINT 0x7

#define IOAPIC_DEST_PHYS 0x0
#define IOAPIC_DEST_LOG 0x1
#define IOAPIC_MASKED (1 << 16)

#define MADT_TYPE_IOAPIC 1
#define MADT_TYPE_ISO 2

struct acpi_rsdp {
    char sig[8];
    uint8_t checksum;
    char oemid[6];
    uint8_t revision;
    uint32_t rsdt_addr;
} __attribute__((packed));

struct acpi_rsdp20 {
    acpi_rsdp first;
    uint32_t length;
    uint64_t xsdt_addr;
    uint8_t ext_checksum;
    uint8_t reserved[3];
} __attribute__((packed));

struct acpi_sdt {
    char sig[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oemid[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct acpi_madt {
    acpi_sdt header;
    uint32_t lapic_addr;
    uint32_t flags;
} __attribute__((packed));

struct madt_entry_header {
    uint8_t type;
    uint8_t length;
} __attribute__((packed));

struct madt_ioapic {
    madt_entry_header header;
    uint8_t ioapic_id;
    uint8_t reserved;
    uint32_t ioapic_addr;
    uint32_t gsi_base;
} __attribute__((packed));

struct madt_iso {
    madt_entry_header header;
    uint8_t bus;
    uint8_t source;
    uint32_t gsi;
    uint16_t flags;
} __attribute__((packed));

class KShell;
class Buddy;

class IOAPIC {
  public:
    bool initialized;
    IOAPIC(KShell *kshell, Buddy *buddy, uint64_t hhdm_offset,
           uint64_t lapic_id, uintptr_t rsdp_addr);
    ~IOAPIC();

  private:
    KShell *kshell;
    Buddy *buddy;
    uint64_t hhdm_offset;
    uintptr_t ioapic_phys;
    uint64_t gsi_base;
    uint32_t redir_count;

    uint32_t ioapic_read(uint8_t reg);
    void ioapic_write(uint8_t reg, uint32_t value);
    void set_irq_redirect(uint32_t irq, uint8_t vector, uint64_t dest_apic_id,
                          bool masked);

    uintptr_t find_madt(uintptr_t rsdp_phys);
};
