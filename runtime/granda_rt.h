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
 * Math
 * --------------------------------------------------------------------- */
double   granda_math_sin(double x);
double   granda_math_cos(double x);
double   granda_math_tan(double x);
double   granda_math_sqrt(double x);
double   granda_math_pow(double x, double y);
double   granda_math_abs_f(double x);
int64_t  granda_math_abs_i(int64_t x);
int64_t  granda_math_floor(double x);
int64_t  granda_math_ceil(double x);
int64_t  granda_math_round(double x);
int64_t  granda_math_min_i(int64_t a, int64_t b);
double   granda_math_min_f(double a, double b);
int64_t  granda_math_max_i(int64_t a, int64_t b);
double   granda_math_max_f(double a, double b);
double   granda_math_log(double x);
double   granda_math_log2(double x);
double   granda_math_log10(double x);

/* -----------------------------------------------------------------------
 * String stdlib
 * --------------------------------------------------------------------- */
GrandaStr*  granda_str_substr(GrandaStr* s, int64_t start, int64_t len);
int64_t     granda_str_index_of(GrandaStr* s, GrandaStr* sub);
int         granda_str_contains(GrandaStr* s, GrandaStr* sub);
GrandaStr*  granda_str_to_upper(GrandaStr* s);
GrandaStr*  granda_str_to_lower(GrandaStr* s);
GrandaStr*  granda_str_trim(GrandaStr* s);
GrandaArray* granda_str_split(GrandaStr* s, GrandaStr* sep);
GrandaStr*  granda_str_replace(GrandaStr* s, GrandaStr* old, GrandaStr* new_s);
int         granda_str_starts_with(GrandaStr* s, GrandaStr* prefix);
int         granda_str_ends_with(GrandaStr* s, GrandaStr* suffix);
GrandaStr*  granda_str_char_at(GrandaStr* s, int64_t idx);
int64_t     granda_str_to_int(GrandaStr* s);
double      granda_str_to_float(GrandaStr* s);

/* -----------------------------------------------------------------------
 * Array stdlib
 * --------------------------------------------------------------------- */
void*    granda_array_pop(GrandaArray* arr);
int64_t  granda_int_array_pop(GrandaArray* arr);
double   granda_float_array_pop(GrandaArray* arr);
void     granda_array_sort_i(GrandaArray* arr);
void     granda_array_sort_f(GrandaArray* arr);
void     granda_array_reverse(GrandaArray* arr);

/* -----------------------------------------------------------------------
 * File I/O
 * --------------------------------------------------------------------- */
GrandaStr*  granda_read_file(GrandaStr* path);
void        granda_write_file(GrandaStr* path, GrandaStr* content);
int         granda_file_exists(GrandaStr* path);
GrandaStr*  granda_read_line(void);
GrandaArray* granda_args(void);
void         granda_set_args(int argc, char** argv);

/* -----------------------------------------------------------------------
 * Random
 * --------------------------------------------------------------------- */
int64_t  granda_rand_int(int64_t min_val, int64_t max_val);
double   granda_rand_float(void);
void     granda_rand_seed(int64_t seed);

/* -----------------------------------------------------------------------
 * Time
 * --------------------------------------------------------------------- */
double   granda_time_now(void);
void     granda_time_sleep(int64_t ms);

/* -----------------------------------------------------------------------
 * OS
 * --------------------------------------------------------------------- */
void     granda_os_exit(int64_t code);
GrandaStr* granda_os_env(GrandaStr* name);

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
