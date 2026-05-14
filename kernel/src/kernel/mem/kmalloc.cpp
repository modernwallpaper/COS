#include <cstdint>
#include <inc/kernel/mem/kmalloc.hpp>
#include <inc/kernel/mem/page_meta.hpp>
#include <inc/kernel/mem/slab.hpp>
#include <inc/kernel/mem/buddy.hpp>

static Buddy* kmalloc_buddy;
static uint64_t kmalloc_hhdm;

static uintptr_t hhdm_to_phys(uintptr_t virt)
{
    return virt - kmalloc_hhdm;
}

void kmalloc_init(void* buddy_ptr, uint64_t hhdm_offset)
{
    kmalloc_buddy = (Buddy*)buddy_ptr;
    kmalloc_hhdm = hhdm_offset;
    slab_init(kmalloc_buddy, kmalloc_hhdm);
}

static int size_to_order(size_t size)
{
    int order = 0;
    size_t block = 0x1000;
    while (block < size)
    {
        block <<= 1;
        order++;
    }
    return order;
}

void* kmalloc(size_t size)
{
    if (size == 0 || kmalloc_buddy == nullptr)
        return nullptr;

    if (size <= slab_max_object_size())
        return slab_alloc(size);

    int order = size_to_order(size);
    uintptr_t phys = kmalloc_buddy->alloc(order);
    if (phys == 0)
        return nullptr;

    uint64_t page_count = 1ULL << order;
    for (uint64_t i = 0; i < page_count; i++)
    {
        uint64_t pfn = (phys >> 12) + i;
        page_meta_set_type(pfn, PAGE_BUDDY_DIRECT);
        page_meta_set_order(pfn, order);
    }

    return (void*)(phys + kmalloc_hhdm);
}

void* kcalloc(size_t num, size_t size)
{
    if (size != 0 && num > SIZE_MAX / size)
        return nullptr;

    size_t total = num * size;
    void* ptr = kmalloc(total);
    if (ptr)
    {
        uint8_t* p = (uint8_t*)ptr;
        for (size_t i = 0; i < total; i++)
            p[i] = 0;
    }
    return ptr;
}

void* krealloc(void* ptr, size_t size)
{
    if (ptr == nullptr)
        return kmalloc(size);
    if (size == 0)
    {
        kfree(ptr);
        return nullptr;
    }

    void* new_ptr = kmalloc(size);
    if (new_ptr == nullptr)
        return nullptr;

    uint8_t* src = (uint8_t*)ptr;
    uint8_t* dst = (uint8_t*)new_ptr;
    for (size_t i = 0; i < size; i++)
        dst[i] = src[i];

    kfree(ptr);
    return new_ptr;
}

void kfree(void* ptr)
{
    if (ptr == nullptr || kmalloc_buddy == nullptr)
        return;

    uintptr_t virt = (uintptr_t)ptr;
    if (virt < kmalloc_hhdm)
        return;

    uint64_t pfn = hhdm_to_phys(virt) >> 12;
    PageType type = page_meta_get_type(pfn);

    if (type == PAGE_SLAB)
    {
        slab_free(ptr);
    }
    else if (type == PAGE_BUDDY_DIRECT)
    {
        uint8_t order = page_meta_get_order(pfn);
        uint64_t base_pfn = pfn & ~((1ULL << order) - 1);
        uintptr_t base_phys = base_pfn << 12;
        kmalloc_buddy->free(base_phys, order);

        uint64_t count = 1ULL << order;
        for (uint64_t i = 0; i < count; i++)
            page_meta_set_type(base_pfn + i, PAGE_FREE);
    }
}
