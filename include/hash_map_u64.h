/* hash_map_u64.h */

#ifndef HASH_MAP_U64_H
#define HASH_MAP_U64_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct hash_map_u64_entry {
    uint64_t key;
    uint64_t value;
    uint32_t hash;
} hash_map_u64_entry_t;

typedef struct hash_map_u64 {
    hash_map_u64_entry_t* entries;
    size_t capacity;
    size_t size;
    size_t max_load;
} hash_map_u64_t;

void hash_map_u64_init(hash_map_u64_t* map);
void hash_map_u64_destroy(hash_map_u64_t* map);

bool hash_map_u64_insert(hash_map_u64_t* map, uint64_t key, uint64_t value);
bool hash_map_u64_get(const hash_map_u64_t* map, uint64_t key, uint64_t* out_value);
bool hash_map_u64_remove(hash_map_u64_t* map, uint64_t key);

static inline size_t hash_map_u64_size(const hash_map_u64_t* map) { return map ? map->size : 0u; }

#endif
