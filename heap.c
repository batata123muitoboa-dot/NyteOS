#include "heap.h"

extern uint32_t _end;

typedef struct header {
    size_t size;
    int is_free;
    struct header *next;
} header_t;

static header_t *head = NULL;

void init_heap(void) {
    uintptr_t heap_start = (uintptr_t)&_end;
    heap_start = (heap_start + 3) & ~3;

    uintptr_t ram_limit = 0x00300000;

    if (heap_start >= ram_limit) {
        head = NULL;
        return;
    }

    head = (header_t *)heap_start;
    head->size = ram_limit - heap_start - sizeof(header_t);
    head->is_free = 1;
    head->next = NULL;
}

void *kmalloc(size_t size) {
    if (size == 0 || !head) return NULL;

    size = (size + 3) & ~3;

    header_t *curr = head;
    while (curr) {
        if (curr->is_free && curr->size >= size) {
            if (curr->size >= size + sizeof(header_t) + 16) {
                header_t *new_block = (header_t *)((uint8_t *)curr + sizeof(header_t) + size);
                new_block->size = curr->size - size - sizeof(header_t);
                new_block->is_free = 1;
                new_block->next = curr->next;

                curr->size = size;
                curr->next = new_block;
            }

            curr->is_free = 0;
            return (void *)(curr + 1);
        }
        curr = curr->next;
    }

    return NULL;
}

void kfree(void *ptr) {
    if (!ptr || !head) return;

    header_t *header = (header_t *)ptr - 1;
    header->is_free = 1;

    header_t *curr = head;
    while (curr && curr->next) {
        if (curr->is_free && curr->next->is_free) {
            curr->size += sizeof(header_t) + curr->next->size;
            curr->next = curr->next->next;
        } else {
            curr = curr->next;
        }
    }
}