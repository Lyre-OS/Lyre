#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limine.h>
#include <lib/bitmap.h>
#include <lib/libc.h>
#include <lib/lock.h>
#include <lib/misc.h>
#include <lib/print.h>
#include <mm/pmm.h>
#include <mm/vmm.h>

volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

static spinlock_t lock = SPINLOCK_INIT;
static uint8_t *bitmap = NULL;
static uint64_t total_page_count = 0;
static uint64_t last_used_index = 0;
static uint64_t free_pages = 0;

void pmm_init(void) {
    // TODO: Check if memmap and hhdm responses are null and panic
    struct limine_memmap_response *memmap = memmap_request.response;
    struct limine_hhdm_response *hhdm = hhdm_request.response;
    struct limine_memmap_entry **entries = memmap->entries;

    uint64_t highest_addr = 0;

    // Calculate how big the memory map needs to be.
    for (size_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = entries[i];

        print("pmm: Memory map entry: base=%lx, length=%lx, type=%lx\n",
            entry->base, entry->length, entry->type);

        if (entry->type != LIMINE_MEMMAP_USABLE && entry->type != LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE) {
            continue;
        }

        uint64_t top = entry->base + entry->length;
        if (top > highest_addr) {
            highest_addr = top;
        }
    }

    // Calculate the needed size for the bitmap in bytes and align it to page size.
    total_page_count = highest_addr / PAGE_SIZE;
    size_t bitmap_size = ALIGN_UP(total_page_count / 8, PAGE_SIZE);

    print("pmm: Highest address: %lx\n", highest_addr);
    print("pmm: Bitmap size: %lu bytes\n", bitmap_size);

    // Find a hole for the bitmap in the memory map.
    for (size_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = entries[i];

        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }

        if (entry->length >= bitmap_size) {
            bitmap = (uint8_t *)(entry->base + hhdm->offset);

            // Initialise entire bitmap to 1 (non-free)
            memset(bitmap, 0xff, bitmap_size);

            entry->length -= bitmap_size;
            entry->base += bitmap_size;

            break;
        }
    }

    // Populate free bitmap entries according to the memory map.
    for (size_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = entries[i];

        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }

        for (uint64_t j = 0; j < entry->length; j += PAGE_SIZE) {
            bitmap_reset(bitmap, (entry->base + j) / PAGE_SIZE);
            free_pages++;
        }
    }

    print("pmm: Free pages: %lu\n", free_pages);
}

static void *inner_alloc(size_t pages, uint64_t limit) {
    size_t p = 0;

    while (last_used_index < limit) {
        if (!bitmap_test(bitmap, last_used_index++)) {
            if (++p == pages) {
                size_t page = last_used_index - pages;
                for (size_t i = page; i < last_used_index; i++) {
                    bitmap_set(bitmap, i);
                }
                return (void *)(page * PAGE_SIZE);
            }
        } else {
            p = 0;
        }
    }

    return NULL;
}

void *pmm_alloc(size_t pages) {
    void *ret = pmm_alloc_nozero(pages);
    if (ret != NULL) {
        memset(ret + VMM_HIGHER_HALF, 0, PAGE_SIZE);
    }

    return ret;
}

void *pmm_alloc_nozero(size_t pages) {
    spinlock_acquire(&lock);

    size_t last = last_used_index;
    void *ret = inner_alloc(pages, total_page_count);

    if (ret == NULL) {
        last_used_index = 0;
        ret = inner_alloc(pages, last);
    }

    // TODO: Check if ret is null and panic
    free_pages -= pages;

    spinlock_release(&lock);
    return ret;
}

void pmm_free(void *addr, size_t pages) {
    spinlock_acquire(&lock);

    size_t page = (uint64_t)addr / PAGE_SIZE;
    for (size_t i = page; i < page + pages; i++) {
        bitmap_reset(bitmap, i);
    }

    spinlock_release(&lock);
}
