#ifndef HANDLE_VEC_H
#define HANDLE_VEC_H

#include "ds_u64.h"
#include "handle.h"

typedef u64_vec_t handle_vec_t;

static inline void handle_vec_init(handle_vec_t* v) { u64_vec_init(v); }
static inline void handle_vec_destroy(handle_vec_t* v) { u64_vec_destroy(v); }
static inline void handle_vec_clear(handle_vec_t* v) { u64_vec_clear(v); }

static inline void handle_vec_push(handle_vec_t* v, handle_t h) { u64_vec_push(v, (uint64_t)h); }
static inline handle_t handle_vec_get(const handle_vec_t* v, size_t i) { return (handle_t)u64_vec_get(v, i); }
static inline size_t handle_vec_len(const handle_vec_t* v) { return u64_vec_len(v); }

#endif
