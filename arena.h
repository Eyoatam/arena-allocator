#ifndef ARENA_H
#define ARENA_H

#include <stdint.h>
#include <stddef.h> 
#include <unistd.h>

typedef struct Arena Arena;

struct Arena {
    uint64_t pos;
    uint64_t base_pos;
    uint64_t capacity;
    uint64_t committed;
    Arena *prev;
    Arena *next;
    uint8_t *base;
    uint8_t *top;
    Arena *curr;
    Arena *free_list;
    uint8_t flags;
};

Arena *_arena_alloc(uint64_t reserve_size, uint64_t commit_size, uint8_t flags);
Arena *arena_alloc();
void arena_release(Arena *arena);
void *arena_push(Arena *arena, uint64_t size, uint64_t align);
void arena_pop_to(Arena *arena, uint64_t pos);
void arena_pop(Arena *arena, uint64_t size);
void arena_clear(Arena *arena);

#endif 
