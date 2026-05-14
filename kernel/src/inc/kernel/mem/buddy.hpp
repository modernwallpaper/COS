#pragma once

#include <cstdint>

class PMM;

class Buddy {
public:
    static constexpr int MAX_ORDER = 10;
    static constexpr uint64_t PAGE_SIZE = 0x1000;

    Buddy(PMM* pmm, uint64_t hhdm_offset);
    ~Buddy();

    uintptr_t alloc(int order);
    void free(uintptr_t addr, int order);

    uint64_t total_free() const
    {
        return total_free_pages;
    }

private:
    struct Node {
        uintptr_t addr;
        Node* next;
    };

    PMM* pmm;
    uint64_t hhdm_offset;
    Node* free_lists[MAX_ORDER + 1];
    uint64_t free_counts[MAX_ORDER + 1];
    uint64_t total_free_pages;

    uintptr_t split_alloc(int order);
};
