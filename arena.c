#include "arena.h"
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#define ARENA_DEFAULT_RESERVE_SIZE (1u << 30)
#define ARENA_DEFAULT_COMMIT_SIZE  (1u << 20)
#define PAGE_SIZE                  getpagesize()
#define ARENA_HEADER_SIZE          sizeof(Arena)
// flags
#define ARENA_USE_CHAINING  (1u << 0)
#define ARENA_USE_FREE_LIST (1u << 1)

// b should be a power of 2
#define ALIGN(a, b) (((a) + (b) - 1) & (~((b) - 1)))

static void *reserve(uint64_t size) {
    return mmap(NULL, size, PROT_NONE, MAP_ANON | MAP_PRIVATE, -1, 0);
}

static void commit(void *addr, uint64_t size) {
    mprotect(addr, size, PROT_READ | PROT_WRITE);
}

static void decommit(void *addr, uint64_t size) {
    mprotect(addr, size, PROT_NONE);
}

Arena *_arena_alloc(uint64_t reserve_size, uint64_t commit_size,
                    uint8_t flags) {
    Arena *arena = (Arena *)reserve(ALIGN(reserve_size, PAGE_SIZE));
    if (arena == MAP_FAILED) {
        fprintf(stderr, "Error: Failed to map memory.\n");
        return NULL;
    }
    commit(arena, ALIGN(commit_size, PAGE_SIZE));
    arena->pos       = 0;
    arena->base_pos  = 0;
    arena->capacity  = ALIGN(reserve_size, PAGE_SIZE);
    arena->committed = ALIGN(commit_size, PAGE_SIZE);
    arena->prev      = NULL;
    arena->next      = NULL;
    // arena->base starts after the header
    arena->base      = (uint8_t *)((uint8_t *)arena + ARENA_HEADER_SIZE);
    arena->top       = arena->base;
    arena->curr      = arena;
    arena->free_list = NULL;
    arena->flags     = flags;
    return arena;
}

Arena *arena_alloc() {
    return _arena_alloc(ARENA_DEFAULT_RESERVE_SIZE, ARENA_DEFAULT_COMMIT_SIZE,
                        ARENA_USE_CHAINING | ARENA_USE_FREE_LIST);
}

void arena_release(Arena *arena) {
    // release the free list
    if (arena->flags & ARENA_USE_FREE_LIST) {
        for (Arena *curr = arena->free_list; curr != NULL; curr = curr->next) {
            munmap(curr, curr->capacity);
        }
    }

    if (!(arena->flags & ARENA_USE_CHAINING)) {
        munmap(arena->curr, arena->curr->capacity);
        return;
    }
    // release chained arenas
    for (Arena *curr = arena->curr, *prev = NULL; curr != NULL; curr = prev) {
        prev = curr->prev;
        munmap(curr, curr->capacity);
    }
}

void *arena_push(Arena *arena, uint64_t size, uint64_t align) {
    arena->curr->top = (uint8_t *)ALIGN((uint64_t)arena->curr->top, align);
    uint64_t needed_space =
        arena->curr->top - arena->curr->base + ARENA_HEADER_SIZE + size;
    // chain
    if (needed_space > arena->curr->capacity) {
        if (!(arena->flags & ARENA_USE_CHAINING)) {
            fprintf(stderr,
                    "Error: Exceeded arena capacity (chaining not enabled).\n");
            return NULL;
        }
        Arena *new_block = NULL;
        Arena *prev      = NULL;
        if (arena->flags & ARENA_USE_FREE_LIST) {
            for (Arena *curr = arena->free_list; curr != NULL;
                 curr        = curr->next) {
                if (curr->capacity >= needed_space) {
                    new_block = curr;
                    // unchain from the free_list
                    if (prev) {
                        prev->next = curr->next;
                    } else {
                        arena->free_list = curr->next;
                    }
                    break;
                }
                prev = curr;
            }
        }

        if (new_block == NULL) {
            new_block = _arena_alloc(needed_space, needed_space,
                                     ARENA_USE_CHAINING | ARENA_USE_FREE_LIST);
        }
        // chain the new block
        prev              = arena->curr;
        arena->curr->next = new_block;
        new_block->prev   = arena->curr;
        arena->curr       = new_block;
        arena->curr->top  = (uint8_t *)ALIGN((uint64_t)arena->curr->top, align);
        arena->curr->base_pos = prev->base_pos + prev->pos;
        // recalculate needed space for the new block
        needed_space =
            arena->curr->top - arena->curr->base + ARENA_HEADER_SIZE + size;
    }

    // commit
    if (needed_space > arena->curr->committed) {
        uint8_t *commit_addr = arena->curr->base - ARENA_HEADER_SIZE;
        uint64_t commit_size = needed_space - arena->curr->committed;
        commit(commit_addr + arena->curr->committed, commit_size);
        // mprotect aligns the commit_size to the page boundary
        // and thus, arena->curr->committed should be updated accordingly.
        arena->curr->committed += ALIGN(commit_size, PAGE_SIZE);
    }

    void *result = arena->curr->top;
    arena->curr->top += size;
    arena->curr->pos = arena->curr->top - arena->curr->base;
    return result;
}

void arena_pop_to(Arena *arena, uint64_t pos) {
    Arena *curr = arena->curr;

    if (pos > curr->base_pos + curr->pos) {
        fprintf(stderr,
                "Error: pop must move backwards. Attempted to pop to %llu.\n",
                pos);
        return;
    }
    if (!(arena->flags & ARENA_USE_CHAINING)) {
        arena->pos = pos;
        arena->top = arena->base + pos;
        return;
    }
    while (pos < curr->base_pos) {
        Arena *prev = curr->prev;
        if (arena->flags & ARENA_USE_FREE_LIST) {
            // unchain from the arena chain
            curr->next = NULL;
            curr->prev = NULL;
            // chain to the free_list
            if (arena->free_list) {
                curr->next       = arena->free_list;
                arena->free_list = curr;
            } else {
                arena->free_list = curr;
            }
            curr->pos = 0;
            curr->top = curr->base;
            // the space left for the arena header should be page-aligned
            decommit(curr + ALIGN(ARENA_HEADER_SIZE, PAGE_SIZE),
                     curr->capacity - ARENA_HEADER_SIZE);
            curr->committed = ALIGN(ARENA_HEADER_SIZE, PAGE_SIZE);
        } else {
            munmap(curr, curr->capacity);
        }
        curr = prev;
    }

    curr->pos   = pos - curr->base_pos;
    curr->top   = curr->base + curr->pos;
    arena->curr = curr;
}

void arena_pop(Arena *arena, uint64_t size) {
    uint64_t pop_pos = arena->curr->base_pos + arena->curr->pos - size;
    arena_pop_to(arena, pop_pos);
}

void arena_clear(Arena *arena) {
    arena_pop_to(arena, 0);
}
