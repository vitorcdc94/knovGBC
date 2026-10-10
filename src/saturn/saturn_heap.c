/*
 * Dedicated user heap for the Saturn port.
 *
 * Yaul's default user pool extends to the physical end of H-WRAM, where this
 * program's Master SH-2 stack also lives. A large allocation can therefore
 * overwrite active stack frames. Keep all public malloc-family allocations
 * in a bounded arena below __end instead. Yaul's own private allocator remains
 * untouched and continues to serve its internal runtime objects.
 */

#include <mm/tlsf.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define SATURN_USER_HEAP_SIZE 0x00050000U

static uint8_t user_heap[SATURN_USER_HEAP_SIZE]
  __attribute__((aligned(16)));
static tlsf_t user_heap_handle;

static tlsf_t
user_heap_get(void)
{
    if (user_heap_handle == NULL) {
        user_heap_handle = tlsf_pool_create(user_heap, sizeof(user_heap));
    }
    return user_heap_handle;
}

void *
malloc(size_t size)
{
    tlsf_t const handle = user_heap_get();
    return (handle == NULL) ? NULL : tlsf_malloc(handle, size);
}

void
free(void *pointer)
{
    if (pointer == NULL) {
        return;
    }

    tlsf_t const handle = user_heap_get();
    if (handle != NULL) {
        tlsf_free(handle, pointer);
    }
}

void *
realloc(void *pointer, size_t size)
{
    tlsf_t const handle = user_heap_get();
    return (handle == NULL) ? NULL : tlsf_realloc(handle, pointer, size);
}

void *
memalign(size_t alignment, size_t size)
{
    tlsf_t const handle = user_heap_get();
    if ((handle == NULL) || (alignment == 0U) ||
        ((alignment & (alignment - 1U)) != 0U)) {
        return NULL;
    }

    const size_t minimum_alignment = tlsf_align_size();
    if (alignment < minimum_alignment) {
        alignment = minimum_alignment;
    }
    return tlsf_memalign(handle, alignment, size);
}

bool
saturn_heap_self_test(void)
{
    const size_t first_size = 0x00020000U;
    uint8_t *first = malloc(first_size);
    uint8_t *aligned = memalign(32U, 64U);

    if ((first == NULL) || (aligned == NULL) ||
        (((uintptr_t)aligned & 31U) != 0U)) {
        free(aligned);
        free(first);
        return false;
    }

    first[0] = 0x4BU;
    first[first_size - 1U] = 0x47U;
    aligned[0] = 0x42U;
    aligned[63] = 0x43U;

    uint8_t *resized = realloc(first, first_size + 0x1000U);
    if ((resized == NULL) || (resized[0] != 0x4BU) ||
        (resized[first_size - 1U] != 0x47U) ||
        (aligned[0] != 0x42U) || (aligned[63] != 0x43U)) {
        free(aligned);
        if (resized != NULL) {
            free(resized);
        }
        else {
            free(first);
        }
        return false;
    }

    free(aligned);
    free(resized);
    return tlsf_check(user_heap_get()) == 0;
}
