/* ds_u64.h */

#ifndef DS_U64_H
#define DS_U64_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef U64_VEC_INLINE_CAP
#define U64_VEC_INLINE_CAP 8
#endif

typedef struct u64_vec {
    uint64_t* items;
    size_t length;
    size_t capacity;
    uint64_t inline_storage[U64_VEC_INLINE_CAP];
} u64_vec_t;

static inline void u64_vec_init(u64_vec_t* v) {
    v->items = v->inline_storage;
    v->length = 0;
    v->capacity = U64_VEC_INLINE_CAP;
}

static inline void u64_vec_destroy(u64_vec_t* v) {
    if (v->items && v->items != v->inline_storage) free(v->items);
    v->items = NULL;
    v->length = 0;
    v->capacity = 0;
}

static inline void u64_vec_clear(u64_vec_t* v) { v->length = 0; }

static inline void u64_vec_reserve(u64_vec_t* v, size_t cap) {
    if (cap <= v->capacity) return;

    size_t new_cap = v->capacity ? v->capacity : 1;
    while (new_cap < cap) new_cap *= 2;

    uint64_t* new_items = (uint64_t*)malloc(new_cap * sizeof(uint64_t));
    if (!new_items) abort();

    memcpy(new_items, v->items, v->length * sizeof(uint64_t));
    if (v->items != v->inline_storage) free(v->items);

    v->items = new_items;
    v->capacity = new_cap;
}

static inline void u64_vec_push(u64_vec_t* v, uint64_t x) {
    if (v->length == v->capacity) u64_vec_reserve(v, v->capacity ? (v->capacity * 2) : 2);
    v->items[v->length++] = x;
}

static inline uint64_t u64_vec_get(const u64_vec_t* v, size_t idx) { return v->items[idx]; }

static inline size_t u64_vec_len(const u64_vec_t* v) { return v ? v->length : 0; }

#endif
