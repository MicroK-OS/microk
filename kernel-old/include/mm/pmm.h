#pragma once
#include <limine.h>
#include <stdint.h>
#include <stddef.h>
#include <mm/bitmap.h>
#include <mm/memory.h>

class PageFrameAllocator {
public:
        void ReadMemoryMap(limine_memmap_entry **mMap, size_t mMapEntries);
        Bitmap page_bitmap;

        void FreePage(void *address);
        void FreePages(void *address, uint64_t page_count);
        void LockPage(void *address);
        void LockPages(void *address, uint64_t page_count);

        void *RequestPage();
        void *RequestPages(size_t pages);

        uint64_t GetFreeMem();
        uint64_t GetUsedMem();
        uint64_t GetReservedMem();

private:
        void InitBitmap(size_t bitmap_size, void *buffer_address);
        void UnreservePage(void *address);
        void UnreservePages(void *address, uint64_t page_count);
        void ReservePage(void *address);
        void ReservePages(void *address, uint64_t page_count);
};

extern PageFrameAllocator GlobalAllocator;

