#pragma once

#include "limine.h"
#include <cstdint>
class PMM {
private:
    uintptr_t* stack;
    uint64_t stack_size;
    uint64_t stack_index;

public:
    PMM(uintptr_t* stack_buffer, uint64_t buffer_entries,
        uint64_t kernel_phys_base, uint64_t kernel_phys_end,
        uint64_t pmm_stack_phys, limine_memmap_entry** entries,
        uint64_t entry_count);
    ~PMM();

    uintptr_t alloc();
    void free(uintptr_t addr);
};
