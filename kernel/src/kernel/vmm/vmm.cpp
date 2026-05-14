#include <cstdint>
#include <inc/kernel/vmm/vmm.hpp>
#include <inc/kernel/mem/buddy.hpp>

VMM::VMM(Buddy* buddy, uint64_t hhdm_offset)
{
    this->buddy = buddy;
    this->hhdm_offset = hhdm_offset;

    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    this->pml4_phys = cr3 & 0x000FFFFFFFFFF000;
}

VMM::~VMM()
{
}

uint64_t* VMM::get_table_entry(uint64_t virt, bool allocate)
{
    uint64_t indices[4] = {(virt >> 39) & 0x1FF, (virt >> 30) & 0x1FF,
                           (virt >> 21) & 0x1FF, (virt >> 12) & 0x1FF};

    uint64_t* table = (uint64_t*)(pml4_phys + hhdm_offset);

    for (int level = 0; level < 4; level++)
    {
        uint64_t* entry = &table[indices[level]];

        if (level == 3)
            return entry;

        if (!(*entry & PAGE_PRESENT))
        {
            if (!allocate)
                return nullptr;

            uint64_t new_table_phys = buddy->alloc(0);
            if (!new_table_phys)
                return nullptr;

            uint64_t* new_table = (uint64_t*)(new_table_phys + hhdm_offset);
            for (int i = 0; i < 512; i++)
                new_table[i] = 0;

            *entry = new_table_phys | PAGE_PRESENT | PAGE_RW;
        }

        table = (uint64_t*)((*entry & 0x000FFFFFFFFFF000) + hhdm_offset);
    }

    return nullptr;
}

void VMM::map_page(uint64_t virt, uint64_t phys, uint64_t flags)
{
    uint64_t* entry = get_table_entry(virt, true);
    if (!entry)
        return;

    *entry = (phys & 0x000FFFFFFFFFF000) | (flags & 0xFFF) | PAGE_PRESENT;
    flush_tlb(virt);
}

void VMM::unmap_page(uint64_t virt)
{
    uint64_t* entry = get_table_entry(virt, false);
    if (!entry)
        return;

    *entry = 0;
    flush_tlb(virt);
}

uint64_t VMM::get_phys(uint64_t virt)
{
    uint64_t* entry = get_table_entry(virt, false);
    if (!entry)
        return 0;

    return *entry & 0x000FFFFFFFFFF000;
}

void VMM::map_range(uint64_t virt, uint64_t phys, uint64_t size, uint64_t flags)
{
    uint64_t aligned_virt = virt & ~0xFFFULL;
    uint64_t aligned_phys = phys & ~0xFFFULL;
    uint64_t end = (virt + size + 0xFFF) & ~0xFFFULL;

    for (uint64_t offset = 0; aligned_virt + offset < end; offset += 0x1000)
        map_page(aligned_virt + offset, aligned_phys + offset, flags);
}

void VMM::flush_tlb(uint64_t virt)
{
    asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
}
