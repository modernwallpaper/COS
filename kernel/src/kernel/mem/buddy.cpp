#include <cstdint>
#include <inc/kernel/mem/buddy.hpp>
#include <inc/kernel/pmm/pmm.hpp>

Buddy::Buddy(PMM* pmm, uint64_t hhdm_offset)
{
    this->pmm = pmm;
    this->hhdm_offset = hhdm_offset;
    this->total_free_pages = 0;
    for (int i = 0; i <= MAX_ORDER; i++)
    {
        free_lists[i] = nullptr;
        free_counts[i] = 0;
    }
}

Buddy::~Buddy()
{
}

uintptr_t Buddy::alloc(int order)
{
    if (order > MAX_ORDER)
        return 0;

    // Try buddy free lists first
    uintptr_t addr = split_alloc(order);
    if (addr != 0)
        return addr;

    // Fallback: direct PMM for single pages
    if (order == 0)
        return pmm->alloc();

    return 0;
}

void Buddy::free(uintptr_t addr, int order)
{
    if (addr == 0 || order > MAX_ORDER)
        return;

    Node* node = (Node*)(addr + hhdm_offset);

    // Try to coalesce with the buddy at this order (if not at max)
    if (order < MAX_ORDER)
    {
        uintptr_t buddy_addr = addr ^ (1ULL << (order + 12));
        Node** prev_ptr = &free_lists[order];
        Node* curr = free_lists[order];

        while (curr)
        {
            if (curr->addr == buddy_addr)
            {
                // Buddy is free! Remove it and coalesce one level up.
                *prev_ptr = curr->next;
                free_counts[order]--;
                uintptr_t merged_addr = (addr < buddy_addr) ? addr : buddy_addr;
                free(merged_addr, order + 1);
                return;
            }
            prev_ptr = &curr->next;
            curr = curr->next;
        }
    }

    // No coalescing possible — add to the free list
    node->addr = addr;
    node->next = free_lists[order];
    free_lists[order] = node;
    free_counts[order]++;
    total_free_pages += (1ULL << order);
}

uintptr_t Buddy::split_alloc(int order)
{
    if (order > MAX_ORDER)
        return 0;

    for (int higher = order; higher <= MAX_ORDER; higher++)
    {
        if (free_lists[higher] == nullptr)
            continue;

        // We have a free block at this order. Allocate it.
        Node* block = free_lists[higher];
        free_lists[higher] = block->next;
        free_counts[higher]--;
        total_free_pages -= (1ULL << higher);

        uintptr_t block_addr = block->addr;

        // Split down to the desired order
        for (int o = higher - 1; o >= order; o--)
        {
            uintptr_t half_size = 1ULL << (o + 12);
            uintptr_t upper_half = block_addr + half_size;

            // Add upper half to the free list at order o
            Node* upper_node = (Node*)(upper_half + hhdm_offset);
            upper_node->addr = upper_half;
            upper_node->next = free_lists[o];
            free_lists[o] = upper_node;
            free_counts[o]++;
            total_free_pages += (1ULL << o);
        }

        // Return lower half at the requested order
        return block_addr;
    }

    return 0;
}
