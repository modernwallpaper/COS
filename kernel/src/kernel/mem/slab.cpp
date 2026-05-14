#include <cstdint>
#include <inc/kernel/mem/page_meta.hpp>
#include <inc/kernel/mem/slab.hpp>
#include <inc/kernel/mem/buddy.hpp>

static Buddy* slab_buddy;
static uint64_t slab_hhdm;

static kmem_cache caches[] = {
    { "kmalloc-8",   8,   nullptr },
    { "kmalloc-16",  16,  nullptr },
    { "kmalloc-32",  32,  nullptr },
    { "kmalloc-64",  64,  nullptr },
    { "kmalloc-128", 128, nullptr },
    { "kmalloc-256", 256, nullptr },
    { "kmalloc-512", 512, nullptr },
    { "kmalloc-1024", 1024, nullptr },
    { "kmalloc-2048", 2048, nullptr },
};

static const int CACHE_COUNT = sizeof(caches) / sizeof(caches[0]);

void slab_init(Buddy* buddy, uint64_t hhdm_offset) {
    slab_buddy = buddy;
    slab_hhdm = hhdm_offset;
    for (int i = 0; i < CACHE_COUNT; i++)
        caches[i].slabs = nullptr;
}

static int slab_refill(kmem_cache* cache) {
    uintptr_t phys = slab_buddy->alloc(0);
    if (phys == 0)
        return -1;

    slab_header* slab = (slab_header*)(phys + slab_hhdm);
    slab->magic = SLAB_MAGIC;
    slab->cache = cache;
    slab->next = cache->slabs;
    cache->slabs = slab;

    uintptr_t obj_start = (uintptr_t)slab + sizeof(slab_header);
    size_t usable = 0x1000 - sizeof(slab_header);
    uint32_t count = usable / cache->object_size;

    void** prev_free = nullptr;
    for (uint32_t i = 0; i < count; i++) {
        void* obj = (void*)(obj_start + i * cache->object_size);
        if (prev_free)
            *(void**)prev_free = obj;
        prev_free = (void**)obj;
    }
    if (prev_free)
        *(void**)prev_free = nullptr;

    slab->free = (void*)(obj_start);
    slab->free_count = count;
    slab->total = count;

    return 0;
}

void* slab_alloc(size_t size) {
    if (slab_buddy == nullptr)
        return nullptr;

    kmem_cache* cache = nullptr;
    for (int i = 0; i < CACHE_COUNT; i++) {
        if (caches[i].object_size >= size) {
            cache = &caches[i];
            break;
        }
    }
    if (cache == nullptr)
        return nullptr;

    if (cache->slabs == nullptr || cache->slabs->free_count == 0) {
        if (slab_refill(cache) != 0)
            return nullptr;
    }

    slab_header* slab = cache->slabs;
    if (slab->free == nullptr)
        return nullptr;

    void* obj = slab->free;
    slab->free = *(void**)obj;
    slab->free_count--;

    return obj;
}

void slab_free(void* ptr) {
    if (ptr == nullptr || slab_buddy == nullptr)
        return;

    uintptr_t page = (uintptr_t)ptr & ~0xFFFULL;
    slab_header* slab = (slab_header*)page;

    if (slab->magic != SLAB_MAGIC)
        return;

    *(void**)ptr = slab->free;
    slab->free = ptr;
    slab->free_count++;

    if (slab->free_count == slab->total) {
        kmem_cache* cache = slab->cache;
        slab_header** prev = &cache->slabs;
        while (*prev) {
            if (*prev == slab) {
                *prev = slab->next;
                break;
            }
            prev = &(*prev)->next;
        }
        page_meta_set_type((page - slab_hhdm) >> 12, PAGE_FREE);
        slab_buddy->free(page - slab_hhdm, 0);
    }
}
