#pragma once

#include <stddef.h>
#include <stdint.h>
#include <vmm.h>

// typedef for physical addresses, so that we don't accidentally dereference them as a pointer
typedef uint32_t physaddr_t;

void *memory_alloc(size_t size);
void memory_free(void *ptr);
void *memory_alloc_phys(size_t size, physaddr_t *physAddr);
void memory_free_phys(void *virt);
void *memory_map_phys_to_virt(physaddr_t physAddr, size_t size);
void memory_unmap_phys(void *virt);
