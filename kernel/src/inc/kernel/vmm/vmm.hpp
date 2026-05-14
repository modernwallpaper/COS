#pragma once

#include <cstdint>

class Buddy;

class VMM {
public:
    static constexpr uint64_t PAGE_PRESENT = 1ULL << 0;
    static constexpr uint64_t PAGE_RW = 1ULL << 1;
    static constexpr uint64_t PAGE_USER = 1ULL << 2;
    static constexpr uint64_t PAGE_WRITETHRU = 1ULL << 3;
    static constexpr uint64_t PAGE_CACHEDIS = 1ULL << 4;
    static constexpr uint64_t PAGE_NX = 1ULL << 63;

    VMM(Buddy* buddy, uint64_t hhdm_offset);
    ~VMM();

    void map_page(uint64_t virt, uint64_t phys, uint64_t flags);
    void unmap_page(uint64_t virt);
    uint64_t get_phys(uint64_t virt);
    void map_range(uint64_t virt, uint64_t phys, uint64_t size, uint64_t flags);

private:
    uint64_t pml4_phys;
    Buddy* buddy;
    uint64_t hhdm_offset;

    uint64_t* get_table_entry(uint64_t virt, bool allocate);
    void flush_tlb(uint64_t virt);
};
