#include <cstdint>
#include <inc/kernel/hpet/hpet.hpp>
#include <inc/kernel/mem/memory.hpp>

#define HPET_GCAP_ID 0x000
#define HPET_GEN_CONF 0x010
#define HPET_MAIN_CNT 0x0F0

HPET::HPET(uintptr_t rsdp_addr, VMM* vmm, uint64_t hddm)
{
    mmio_base = 0;
    period_fs = 0;
    counter_mask = 0;

    if (rsdp_addr == 0 || vmm == nullptr)
        return;

    acpi_rsdp* rsdp = (acpi_rsdp*)rsdp_addr;

    uint32_t sdt_count;
    uint32_t* sdt_ptrs;

    if (rsdp->revision >= 2)
    {
        acpi_rsdp20* rsdp20 = (acpi_rsdp20*)rsdp_addr;
        acpi_sdt* xsdt = (acpi_sdt*)(rsdp20->xsdt_addr + hddm);
        sdt_count = (xsdt->length - sizeof(acpi_sdt)) / 8;
        sdt_ptrs = (uint32_t*)((uintptr_t)xsdt + sizeof(acpi_sdt));
    }
    else
    {
        acpi_sdt* rsdt = (acpi_sdt*)(rsdp->rsdt_addr + hddm);
        sdt_count = (rsdt->length - sizeof(acpi_sdt)) / 4;
        sdt_ptrs = (uint32_t*)((uintptr_t)rsdt + sizeof(acpi_sdt));
    }

    uint64_t hpet_phys = 0;
    for (uint32_t i = 0; i < sdt_count; i++)
    {
        uint64_t sdt_phys;
        if (rsdp->revision >= 2)
            sdt_phys = ((uint64_t*)sdt_ptrs)[i];
        else
            sdt_phys = sdt_ptrs[i];

        acpi_sdt* sdt = (acpi_sdt*)(sdt_phys + hddm);
        if (memcmp(sdt->sig, "HPET", 4) == 0)
        {
            hpet_phys = sdt_phys;
            break;
        }
    }

    if (hpet_phys == 0)
        return;

    acpi_hpet* hpet_table = (acpi_hpet*)(hpet_phys + hddm);

    uintptr_t phys_base = (uintptr_t)hpet_table->base_address;
    if (phys_base == 0)
        return;

    vmm->map_range(phys_base, phys_base, 0x1000,
                   VMM::PAGE_RW | VMM::PAGE_CACHEDIS);
    mmio_base = phys_base;

    uint64_t gcap_id = *(volatile uint64_t*)(mmio_base + HPET_GCAP_ID);
    period_fs = gcap_id >> 32;
    counter_mask = (gcap_id & (1ULL << 13)) ? ~0ULL : 0xFFFFFFFFULL;
}

HPET::~HPET()
{
}

void HPET::enable()
{
    if (mmio_base == 0)
        return;
    uint32_t conf = *(volatile uint32_t*)(mmio_base + HPET_GEN_CONF);
    conf |= 1;
    *(volatile uint32_t*)(mmio_base + HPET_GEN_CONF) = conf;
}

uint64_t HPET::read_counter()
{
    if (mmio_base == 0)
        return 0;
    if (counter_mask == 0xFFFFFFFFULL)
        return *(volatile uint32_t*)(mmio_base + HPET_MAIN_CNT);
    return *(volatile uint64_t*)(mmio_base + HPET_MAIN_CNT);
}

void HPET::busy_wait_ns(uint64_t ns)
{
    if (period_fs == 0)
        return;
    uint64_t target_ticks = (ns * 1000000) / period_fs;
    uint64_t start = read_counter();
    while (((read_counter() - start) & counter_mask) < target_ticks)
        ;
}

uint64_t HPET::counter_to_ns(uint64_t ticks)
{
    return (ticks * period_fs) / 1000000;
}
