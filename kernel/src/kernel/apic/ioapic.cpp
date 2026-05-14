#include <cstdint>
#include <inc/kernel/apic/ioapic.hpp>
#include <inc/kernel/kshell/kshell.hpp>
#include <inc/kernel/mem/buddy.hpp>
#include <inc/kernel/mem/memory.hpp>
#include <inc/kernel/ports/ports.hpp>

IOAPIC::IOAPIC(KShell *kshell, Buddy *buddy, uint64_t hhdm_offset,
               uint64_t lapic_id, uintptr_t rsdp_addr) {
    this->kshell = kshell;
    this->buddy = buddy;
    this->hhdm_offset = hhdm_offset;
    ioapic_phys = 0;
    gsi_base = 0;
    redir_count = 0;
    initialized = false;

    if (rsdp_addr == 0) {
        kshell->print_kernel_error("ACPI RSDP not found");
        return;
    }
    kshell->print_kernel_info("ACPI RSDP at %x", rsdp_addr);

    uintptr_t madt_phys = find_madt(rsdp_addr);
    if (madt_phys == 0) {
        kshell->print_kernel_error("MADT not found");
        return;
    }
    kshell->print_kernel_info("MADT at %x", madt_phys);

    acpi_madt *madt = (acpi_madt *)(madt_phys + hhdm_offset);
    kshell->print_kernel_info("Local APIC addr from MADT: %x",
                              madt->lapic_addr);

    uintptr_t madt_end = madt_phys + madt->header.length;
    uintptr_t entry_phys = madt_phys + sizeof(acpi_madt);

    uintptr_t ioapic_found_phys = 0;

    while (entry_phys < madt_end) {
        madt_entry_header *hdr =
            (madt_entry_header *)(entry_phys + hhdm_offset);

        if (hdr->type == MADT_TYPE_IOAPIC) {
            madt_ioapic *ioapic_entry = (madt_ioapic *)hdr;
            ioapic_found_phys = ioapic_entry->ioapic_addr;
            gsi_base = ioapic_entry->gsi_base;

            kshell->print_kernel_info(
                "I/O APIC: id=%d addr=%x gsi_base=%d",
                ioapic_entry->ioapic_id, ioapic_entry->ioapic_addr,
                ioapic_entry->gsi_base);
        } else if (hdr->type == MADT_TYPE_ISO) {
            madt_iso *iso = (madt_iso *)hdr;
            kshell->print_kernel_info(
                "  ISO: bus=%d source=%d -> gsi=%d flags=%x", iso->bus,
                iso->source, iso->gsi, iso->flags);
        }

        if (hdr->length == 0)
            break;
        entry_phys += hdr->length;
    }

    if (ioapic_found_phys == 0) {
        kshell->print_kernel_error("No I/O APIC found in MADT");
        return;
    }

    ioapic_phys = ioapic_found_phys;

    uint32_t ver = ioapic_read(IOAPIC_VER);
    redir_count = (ver >> 16) & 0xFF;
    kshell->print_kernel_info("I/O APIC version %d, %d redirection entries",
                              ver & 0xFF, redir_count);

    for (uint32_t i = 0; i < redir_count; i++)
        set_irq_redirect(i, 0, 0, true);

    set_irq_redirect(0, 32, lapic_id, false);
    kshell->print_kernel_success("IRQ0 (PIT) routed through I/O APIC");

    set_irq_redirect(1, 33, lapic_id, false);
    kshell->print_kernel_success("IRQ1 (Keyboard) routed through I/O APIC");

    initialized = true;
}

IOAPIC::~IOAPIC() {}

uint32_t IOAPIC::ioapic_read(uint8_t reg) {
    // The I/O APIC MMIO is identity-mapped as uncacheable by the VMM
    // (see main.cpp). HHDM offset does NOT cover MMIO regions.
    *(volatile uint32_t *)(ioapic_phys + IOAPIC_IOREGSEL) = reg;
    return *(volatile uint32_t *)(ioapic_phys + IOAPIC_IOWIN);
}

void IOAPIC::ioapic_write(uint8_t reg, uint32_t value) {
    *(volatile uint32_t *)(ioapic_phys + IOAPIC_IOREGSEL) = reg;
    *(volatile uint32_t *)(ioapic_phys + IOAPIC_IOWIN) = value;
}

void IOAPIC::set_irq_redirect(uint32_t irq, uint8_t vector,
                              uint64_t dest_apic_id, bool masked) {
    if (irq >= redir_count)
        return;

    uint8_t reg = IOAPIC_REDTBL + irq * 2;
    uint32_t low = vector | IOAPIC_FIXED | IOAPIC_DEST_PHYS;
    if (masked)
        low |= IOAPIC_MASKED;
    uint32_t high = (uint32_t)(dest_apic_id << 24);

    ioapic_write(reg, low);
    ioapic_write(reg + 1, high);
}

uintptr_t IOAPIC::find_madt(uintptr_t rsdp_virt) {
    acpi_rsdp *rsdp = (acpi_rsdp *)rsdp_virt;

    uint32_t sdt_count;
    uint32_t *sdt_ptrs;

    if (rsdp->revision >= 2) {
        acpi_rsdp20 *rsdp20 = (acpi_rsdp20 *)rsdp_virt;
        acpi_sdt *xsdt = (acpi_sdt *)(rsdp20->xsdt_addr + hhdm_offset);
        sdt_count = (xsdt->length - sizeof(acpi_sdt)) / 8;
        sdt_ptrs = (uint32_t *)((uintptr_t)xsdt + sizeof(acpi_sdt));
    } else {
        acpi_sdt *rsdt = (acpi_sdt *)(rsdp->rsdt_addr + hhdm_offset);
        sdt_count = (rsdt->length - sizeof(acpi_sdt)) / 4;
        sdt_ptrs = (uint32_t *)((uintptr_t)rsdt + sizeof(acpi_sdt));
    }

    for (uint32_t i = 0; i < sdt_count; i++) {
        uint64_t sdt_phys;
        if (rsdp->revision >= 2) {
            uint64_t *xsdt_ptrs = (uint64_t *)sdt_ptrs;
            sdt_phys = xsdt_ptrs[i];
        } else {
            sdt_phys = sdt_ptrs[i];
        }

        acpi_sdt *sdt = (acpi_sdt *)(sdt_phys + hhdm_offset);
        if (memcmp(sdt->sig, "APIC", 4) == 0)
            return sdt_phys;
    }

    return 0;
}
