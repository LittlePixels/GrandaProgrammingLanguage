#include "granda_rt.h"
#include <math.h>
#include <stdarg.h>

/* -----------------------------------------------------------------------
 * Runtime init / shutdown
 * --------------------------------------------------------------------- */
void gc_init(void)   { /* future: heap warmup */ }
void gc_shutdown(void) { /* future: leak report */ }

/* -----------------------------------------------------------------------
 * String implementation
 * --------------------------------------------------------------------- */
static void str_free(GC_Header* h) {
    GrandaStr* s = (GrandaStr*)h;
    free(s->data);
    free(s);
}

GrandaStr* granda_str_new(const char* data, size_t len) {
    GrandaStr* s = (GrandaStr*)malloc(sizeof(GrandaStr));
    if (!s) granda_panic("Out of memory allocating string");
    s->_gc.ref_count = 1;
    s->_gc.type_tag  = GRANDA_TAG_STR;
    s->_gc.trace     = NULL;
    s->_gc.free_fn   = str_free;
    s->len  = len;
    s->data = (char*)malloc(len + 1);
    if (!s->data) granda_panic("Out of memory allocating string data");
    memcpy(s->data, data, len);
    s->data[len] = '\0';
    return s;
}

GrandaStr* granda_str_literal(const char* s) {
    return granda_str_new(s, strlen(s));
}

GrandaStr* granda_str_concat(GrandaStr* a, GrandaStr* b) {
    size_t total = a->len + b->len;
    char* buf = (char*)malloc(total + 1);
    if (!buf) granda_panic("Out of memory in string concat");
    memcpy(buf, a->data, a->len);
    memcpy(buf + a->len, b->data, b->len);
    buf[total] = '\0';
    GrandaStr* result = granda_str_new(buf, total);
    free(buf);
    return result;
}

int granda_str_eq(GrandaStr* a, GrandaStr* b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->len != b->len) return 0;
    return memcmp(a->data, b->data, a->len) == 0;
}

int64_t granda_str_len(GrandaStr* s) {
    return s ? (int64_t)s->len : 0;
}

GrandaStr* granda_int_to_str(int64_t n) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)n);
    return granda_str_literal(buf);
}

GrandaStr* granda_float_to_str(double f) {
    char buf[64];
    /* Drop trailing zeros for clean output */
    snprintf(buf, sizeof(buf), "%g", f);
    return granda_str_literal(buf);
}

GrandaStr* granda_bool_to_str(int b) {
    return granda_str_literal(b ? "true" : "false");
}

/* -----------------------------------------------------------------------
 * Array implementation
 * --------------------------------------------------------------------- */
static void arr_free(GC_Header* h) {
    GrandaArray* arr = (GrandaArray*)h;
    if (arr->elem_is_gc) {
        for (size_t i = 0; i < arr->len; i++) rc_release(arr->items[i]);
    }
    free(arr->items);
    free(arr);
}

GrandaArray* granda_array_new(size_t initial_cap, int elem_is_gc) {
    GrandaArray* arr = (GrandaArray*)malloc(sizeof(GrandaArray));
    if (!arr) granda_panic("Out of memory allocating array");
    arr->_gc.ref_count = 1;
    arr->_gc.type_tag  = GRANDA_TAG_ARRAY;
    arr->_gc.trace     = NULL;
    arr->_gc.free_fn   = arr_free;
    arr->len      = 0;
    arr->cap      = initial_cap < 4 ? 4 : initial_cap;
    arr->items    = (void**)malloc(sizeof(void*) * arr->cap);
    arr->elem_is_gc = elem_is_gc;
    if (!arr->items) granda_panic("Out of memory allocating array items");
    return arr;
}

static void arr_grow(GrandaArray* arr) {
    arr->cap *= 2;
    arr->items = (void**)realloc(arr->items, sizeof(void*) * arr->cap);
    if (!arr->items) granda_panic("Out of memory growing array");
}

void granda_array_push(GrandaArray* arr, void* item) {
    if (arr->len >= arr->cap) arr_grow(arr);
    arr->items[arr->len++] = item;
    if (arr->elem_is_gc) rc_retain(item);
}

void* granda_array_get(GrandaArray* arr, int64_t idx) {
    granda_bounds_check(idx, (int64_t)arr->len, 0);
    return arr->items[idx];
}

void granda_array_set(GrandaArray* arr, int64_t idx, void* item) {
    granda_bounds_check(idx, (int64_t)arr->len, 0);
    if (arr->elem_is_gc) {
        rc_release(arr->items[idx]);
        rc_retain(item);
    }
    arr->items[idx] = item;
}

int64_t granda_array_len(GrandaArray* arr) {
    return arr ? (int64_t)arr->len : 0;
}

/* -----------------------------------------------------------------------
 * Typed int/float arrays — items stored as raw int64_t/double in
 * the items[] buffer (cast to void*). No GC needed for the elements.
 * --------------------------------------------------------------------- */
GrandaArray* granda_int_array_new(size_t cap) {
    return granda_array_new(cap, 0);
}
void granda_int_array_push(GrandaArray* arr, int64_t val) {
    if (arr->len >= arr->cap) arr_grow(arr);
    int64_t* slot = (int64_t*)&arr->items[arr->len++];
    *slot = val;
}
int64_t granda_int_array_get(GrandaArray* arr, int64_t idx) {
    granda_bounds_check(idx, (int64_t)arr->len, 0);
    return *(int64_t*)&arr->items[idx];
}
void granda_int_array_set(GrandaArray* arr, int64_t idx, int64_t val) {
    granda_bounds_check(idx, (int64_t)arr->len, 0);
    *(int64_t*)&arr->items[idx] = val;
}

GrandaArray* granda_float_array_new(size_t cap) {
    return granda_array_new(cap, 0);
}
void granda_float_array_push(GrandaArray* arr, double val) {
    if (arr->len >= arr->cap) arr_grow(arr);
    double* slot = (double*)&arr->items[arr->len++];
    *slot = val;
}
double granda_float_array_get(GrandaArray* arr, int64_t idx) {
    granda_bounds_check(idx, (int64_t)arr->len, 0);
    return *(double*)&arr->items[idx];
}
void granda_float_array_set(GrandaArray* arr, int64_t idx, double val) {
    granda_bounds_check(idx, (int64_t)arr->len, 0);
    *(double*)&arr->items[idx] = val;
}

/* -----------------------------------------------------------------------
 * Built-in I/O
 * --------------------------------------------------------------------- */
void granda_print(GrandaStr* s) {
    if (s) fputs(s->data, stdout);
}

void granda_println(GrandaStr* s) {
    if (s) puts(s->data);
    else   putchar('\n');
}

void granda_print_int(int64_t n) {
    printf("%lld", (long long)n);
}

void granda_print_float(double f) {
    printf("%g", f);
}

void granda_print_bool(int b) {
    fputs(b ? "true" : "false", stdout);
}

/* -----------------------------------------------------------------------
 * Error handling
 * --------------------------------------------------------------------- */
void granda_panic(const char* msg) {
    fprintf(stderr, "Granda runtime panic: %s\n", msg);
    exit(1);
}

void granda_bounds_check(int64_t idx, int64_t len, int line) {
    if (idx < 0 || idx >= len) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "index out of bounds: index %lld, length %lld (line %d)",
                 (long long)idx, (long long)len, line);
        granda_panic(buf);
    }
}
