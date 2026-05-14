#pragma once

#include <cstdint>

enum PageType : uint8_t {
    PAGE_FREE = 0,
    PAGE_SLAB,
    PAGE_BUDDY_DIRECT,
};

class Buddy;

bool page_meta_init(Buddy* buddy, uint64_t hhdm_offset, uint64_t max_pfn);

PageType page_meta_get_type(uint64_t pfn);
void page_meta_set_type(uint64_t pfn, PageType type);
uint8_t page_meta_get_order(uint64_t pfn);
void page_meta_set_order(uint64_t pfn, uint8_t order);
