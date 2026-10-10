#pragma once

#include <stdbool.h>

/* Exercise allocation, alignment, reallocation and coalescing once before
 * the CD filesystem and SameBoy begin using the dedicated Saturn heap. */
bool saturn_heap_self_test(void);
