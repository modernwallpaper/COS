#include "inc/kernel/pmm/pmm.hpp"
#include "limine.h"
#include <cstdint>

PMM::PMM(uintptr_t* stack_buffer, uint64_t buffer_entries,
         uint64_t kernel_phys_base, uint64_t kernel_phys_end,
         uint64_t pmm_stack_phys, limine_memmap_entry** entries,
         uint64_t entry_count)
{
    this->stack = stack_buffer;
    this->stack_size = buffer_entries;
    this->stack_index = 0;

    // The PMM stack buffer occupies this many bytes in physical memory.
    // We must not push those frames onto the stack or we'd overwrite it.
    uint64_t pmm_stack_size_bytes = buffer_entries * sizeof(uintptr_t);
    uint64_t pmm_stack_end = pmm_stack_phys + pmm_stack_size_bytes;

    for (uint64_t i = 0; i < entry_count; i++)
    {
        if (entries[i]->type != LIMINE_MEMMAP_USABLE)
            continue;

        uint64_t aligned_base = (entries[i]->base + 0xFFF) & ~0xFFF;
        uint64_t aligned_end = (entries[i]->base + entries[i]->length) & ~0xFFF;
        if (aligned_base >= aligned_end)
            continue;

        for (uintptr_t frame = aligned_base; frame < aligned_end;
             frame += 0x1000)
        {
            // Skip frames occupied by the kernel binary.
            if (frame >= kernel_phys_base && frame < kernel_phys_end)
                continue;
            // Skip frames occupied by the PMM stack itself.
            if (frame >= pmm_stack_phys && frame < pmm_stack_end)
                continue;

            if (stack_index >= stack_size)
                return;
            stack[stack_index++] = frame;
        }
    }
};

PMM::~PMM() {};

uintptr_t PMM::alloc()
{
    if (this->stack_index == 0)
        return 0;
    return this->stack[--this->stack_index];
};

void PMM::free(uintptr_t addr)
{
    if (this->stack_index < this->stack_size)
        this->stack[this->stack_index++] = addr;
};
