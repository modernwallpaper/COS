#include <inc/kernel/mem/buddy.hpp>
#include <inc/kernel/mem/page_meta.hpp>
#include <inc/kernel/ports/ports.hpp>

static uint8_t* meta;
static uint8_t* orders;
static uint64_t max_pfn;

bool page_meta_init(Buddy* buddy, uint64_t hhdm_offset, uint64_t max_pfn_val)
{
    max_pfn = max_pfn_val;

    uint64_t bytes = max_pfn * 2;

    int order = 0;
    uint64_t block = 0x1000;
    while (block < bytes && order < Buddy::MAX_ORDER)
    {
        block <<= 1;
        order++;
    }

    if (block < bytes)
    {
        serial_print("page_meta_init: max_pfn too large for MAX_ORDER\n");
        return false;
    }

    uintptr_t phys = buddy->alloc(order);
    if (phys == 0)
    {
        serial_print("page_meta_init: buddy alloc(order=");
        serial_print_hex(order);
        serial_print(") returned 0\n");
        return false;
    }

    uintptr_t virt = phys + hhdm_offset;
    uint64_t alloc_bytes = 0x1000 << order;

    volatile uint8_t* v = (volatile uint8_t*)virt;
    for (uint64_t i = 0; i < alloc_bytes; i++)
        v[i] = 0;

    meta = (uint8_t*)virt;
    orders = meta + max_pfn;
    return true;
}

PageType page_meta_get_type(uint64_t pfn)
{
    if (pfn >= max_pfn)
        return PAGE_FREE;
    return (PageType)meta[pfn];
}

void page_meta_set_type(uint64_t pfn, PageType type)
{
    if (pfn < max_pfn)
        meta[pfn] = (uint8_t)type;
}

uint8_t page_meta_get_order(uint64_t pfn)
{
    if (pfn >= max_pfn)
        return 0;
    return orders[pfn];
}

void page_meta_set_order(uint64_t pfn, uint8_t order)
{
    if (pfn < max_pfn)
        orders[pfn] = order;
}
