#pragma once

#include <cstddef>
#include <cstdint>

void kmalloc_init(void* buddy_ptr, uint64_t hhdm_offset);
void* kmalloc(size_t size);
void* kcalloc(size_t num, size_t size);
void* krealloc(void* ptr, size_t size);
void kfree(void* ptr);
