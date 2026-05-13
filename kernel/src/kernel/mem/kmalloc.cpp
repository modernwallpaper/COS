#include <cstdint>
#include <inc/kernel/mem/kmalloc.hpp>
#include <inc/kernel/mem/slab.hpp>
#include <inc/kernel/mem/buddy.hpp>

#define KMALLOC_MAGIC 0x4B4D4C4F43000000ULL

struct kmalloc_hdr {
    uint64_t magic;
    int order;
};

static Buddy* kmalloc_buddy;
static uint64_t kmalloc_hhdm;

void kmalloc_init(void* buddy_ptr, uint64_t hhdm_offset) {
    kmalloc_buddy = (Buddy*)buddy_ptr;
    kmalloc_hhdm = hhdm_offset;
    slab_init(kmalloc_buddy, kmalloc_hhdm);
}

static int size_to_order(size_t size) {
    int order = 0;
    size_t block = 0x1000;
    while (block < size) {
        block <<= 1;
        order++;
    }
    return order;
}

void* kmalloc(size_t size) {
    if (size == 0 || kmalloc_buddy == nullptr)
        return nullptr;

    if (size <= 2048)
        return slab_alloc(size);

    size_t total = size + sizeof(kmalloc_hdr);
    int order = size_to_order(total);
    uintptr_t phys = kmalloc_buddy->alloc(order);
    if (phys == 0)
        return nullptr;

    kmalloc_hdr* hdr = (kmalloc_hdr*)(phys + kmalloc_hhdm);
    hdr->magic = KMALLOC_MAGIC;
    hdr->order = order;

    return (void*)((uintptr_t)hdr + sizeof(kmalloc_hdr));
}

void* kcalloc(size_t num, size_t size) {
    size_t total = num * size;
    void* ptr = kmalloc(total);
    if (ptr) {
        uint8_t* p = (uint8_t*)ptr;
        for (size_t i = 0; i < total; i++)
            p[i] = 0;
    }
    return ptr;
}

void* krealloc(void* ptr, size_t size) {
    if (ptr == nullptr)
        return kmalloc(size);
    if (size == 0) {
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

void kfree(void* ptr) {
    if (ptr == nullptr || kmalloc_buddy == nullptr)
        return;

    uintptr_t page = (uintptr_t)ptr & ~0xFFFULL;

    slab_header* slab = (slab_header*)page;
    if (slab->magic == SLAB_MAGIC) {
        slab_free(ptr);
        return;
    }

    kmalloc_hdr* hdr = (kmalloc_hdr*)((uintptr_t)ptr - sizeof(kmalloc_hdr));
    if (hdr->magic == KMALLOC_MAGIC) {
        uintptr_t hdr_phys = (uintptr_t)hdr - kmalloc_hhdm;
        kmalloc_buddy->free(hdr_phys, hdr->order);
    }
}
