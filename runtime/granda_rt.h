#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -----------------------------------------------------------------------
 * Reference-counted GC header — must be the FIRST field in every
 * heap-allocated Granda object.
 * --------------------------------------------------------------------- */
typedef struct GC_Header {
    uint32_t ref_count;
    uint8_t  type_tag;
    void (*trace)(struct GC_Header* self);
    void (*free_fn)(struct GC_Header* self);
} GC_Header;

#define GRANDA_TAG_STR   1
#define GRANDA_TAG_ARRAY 2
#define GRANDA_TAG_CLASS 3

static inline void rc_retain(void* ptr) {
    if (ptr) ((GC_Header*)ptr)->ref_count++;
}

static inline void rc_release(void* ptr);

/* -----------------------------------------------------------------------
 * String
 * --------------------------------------------------------------------- */
typedef struct {
    GC_Header _gc;
    size_t    len;
    char*     data;
} GrandaStr;

GrandaStr* granda_str_new(const char* data, size_t len);
GrandaStr* granda_str_literal(const char* s);
GrandaStr* granda_str_concat(GrandaStr* a, GrandaStr* b);
int        granda_str_eq(GrandaStr* a, GrandaStr* b);
int64_t    granda_str_len(GrandaStr* s);
GrandaStr* granda_int_to_str(int64_t n);
GrandaStr* granda_float_to_str(double f);
GrandaStr* granda_bool_to_str(int b);

/* -----------------------------------------------------------------------
 * Array (generic — stores void* items)
 * --------------------------------------------------------------------- */
typedef struct {
    GC_Header _gc;
    size_t    len;
    size_t    cap;
    void**    items;  /* each item is either GC_Header* or a boxed scalar */
    int       elem_is_gc; /* 1 = items are GC pointers, retain/release on write */
} GrandaArray;

GrandaArray* granda_array_new(size_t initial_cap, int elem_is_gc);
void         granda_array_push(GrandaArray* arr, void* item);
void*        granda_array_get(GrandaArray* arr, int64_t idx);
void         granda_array_set(GrandaArray* arr, int64_t idx, void* item);
int64_t      granda_array_len(GrandaArray* arr);

/* Typed integer/float arrays stored as raw data (no boxing) */
GrandaArray* granda_int_array_new(size_t cap);
void         granda_int_array_push(GrandaArray* arr, int64_t val);
int64_t      granda_int_array_get(GrandaArray* arr, int64_t idx);
void         granda_int_array_set(GrandaArray* arr, int64_t idx, int64_t val);

GrandaArray* granda_float_array_new(size_t cap);
void         granda_float_array_push(GrandaArray* arr, double val);
double       granda_float_array_get(GrandaArray* arr, int64_t idx);
void         granda_float_array_set(GrandaArray* arr, int64_t idx, double val);

/* -----------------------------------------------------------------------
 * Built-in I/O
 * --------------------------------------------------------------------- */
void granda_print(GrandaStr* s);
void granda_println(GrandaStr* s);
void granda_print_int(int64_t n);
void granda_print_float(double f);
void granda_print_bool(int b);

/* -----------------------------------------------------------------------
 * Error
 * --------------------------------------------------------------------- */
void granda_panic(const char* msg);
void granda_bounds_check(int64_t idx, int64_t len, int line);

/* -----------------------------------------------------------------------
 * Runtime init / shutdown
 * --------------------------------------------------------------------- */
void gc_init(void);
void gc_shutdown(void);

/* -----------------------------------------------------------------------
 * RC helpers — inline implementation
 * --------------------------------------------------------------------- */
static inline void rc_release(void* ptr) {
    if (!ptr) return;
    GC_Header* h = (GC_Header*)ptr;
    if (h->ref_count == 0) return;
    if (--h->ref_count == 0) {
        if (h->free_fn) h->free_fn(h);
    }
}

/* Assign a new GC value to a slot, managing reference counts. */
#define RC_ASSIGN(slot, val)   \
    do {                       \
        void* _old = (slot);   \
        (slot) = (val);        \
        rc_retain(val);        \
        rc_release(_old);      \
    } while (0)

/* Release all RC slots listed — convenience macro for scope exit. */
#define RC_RELEASE(ptr) rc_release(ptr)
