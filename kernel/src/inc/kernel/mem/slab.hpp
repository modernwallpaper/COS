#pragma once

#include <cstddef>
#include <cstdint>

class Buddy;

#define SLAB_MAGIC 0x5AB12345AB5678ULL

struct slab_header {
    uint64_t magic;
    slab_header* next;
    struct kmem_cache* cache;
    void* free;
    uint32_t free_count;
    uint32_t total;
};

struct kmem_cache {
    const char* name;
    size_t object_size;
    slab_header* slabs;
};

void slab_init(Buddy* buddy, uint64_t hhdm_offset);
size_t slab_max_object_size();
void* slab_alloc(size_t size);
void slab_free(void* ptr);
