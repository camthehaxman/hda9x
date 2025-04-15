#include <vmm.h>

#include "hdaudio.h"
#include "memory.h"

// Allocates general purpose memory from the heap
void *memory_alloc(size_t size)
{
	return _HeapAllocate(size, 0);
}

// Frees memory allocated with memory_alloc
void memory_free(void *ptr)
{
	if (!_HeapFree(ptr, 0))
		dprintf("Warning: failed to free memory at 0x%08X\n", ptr);
}

// Allocates a physical region of memory.
// Returns the virtual address, and stores the physical address in physAddr
// This buffer is contiguous and page-locked, so it will never be swapped.
// TODO: check if caching is enabled. If PCI snooping is enabled, we don't need
// to care about cache coherency, but if it's not, then this memory NEEDS to be uncached!
void *memory_alloc_phys(size_t size, physaddr_t *physAddr)
{
	const size_t PAGESIZE = 4096;  // size of a page
	unsigned int nPages = (size + PAGESIZE - 1) / PAGESIZE;
	return _PageAllocate(
		nPages,  // number of pages
		PG_SYS,  // type
		0,  // handle to VM
		0, // align mask (multiple of 4K)
		0,  // min phys page number
		0x100000,  // max phys page number
		(PVOID *)physAddr,
		PAGECONTIG | PAGEUSEALIGN | PAGEFIXED);
}

void memory_free_phys(void *virt)
{
	// TODO: implement
}

// Maps the specified physical memory region to a virtual address that the CPU can access
// TODO: We shouldn't use _MapPhysToLinear here because there's no
// way to unmap the address it gave us, should the Plug & Play system
// decide to reassign the device's memory range. There is another way
// to do this that allows unmapping, but I'm too lazy to implement it right now.
void *memory_map_phys_to_virt(physaddr_t physAddr, size_t size)
{
	void *ptr = _MapPhysToLinear(physAddr, size, 0);
	if (ptr == (void *)0xFFFFFFFF)
		return NULL;
	return ptr;
}

void memory_unmap_phys(void *virt)
{
	// TODO: implement
}
