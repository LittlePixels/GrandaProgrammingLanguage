#include "granda_rt.h"
#include <math.h>
#include <stdarg.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#endif

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
 * Math
 * --------------------------------------------------------------------- */
double granda_math_sin(double x)    { return sin(x); }
double granda_math_cos(double x)    { return cos(x); }
double granda_math_tan(double x)    { return tan(x); }
double granda_math_sqrt(double x)   { return sqrt(x); }
double granda_math_pow(double x, double y) { return pow(x, y); }
double granda_math_abs_f(double x)  { return fabs(x); }
int64_t granda_math_abs_i(int64_t x) { return x < 0 ? -x : x; }
int64_t granda_math_floor(double x) { return (int64_t)floor(x); }
int64_t granda_math_ceil(double x)  { return (int64_t)ceil(x); }
int64_t granda_math_round(double x) { return (int64_t)round(x); }
int64_t granda_math_min_i(int64_t a, int64_t b) { return a < b ? a : b; }
double  granda_math_min_f(double a, double b)    { return a < b ? a : b; }
int64_t granda_math_max_i(int64_t a, int64_t b) { return a > b ? a : b; }
double  granda_math_max_f(double a, double b)    { return a > b ? a : b; }
double  granda_math_log(double x)   { return log(x); }
double  granda_math_log2(double x)  { return log2(x); }
double  granda_math_log10(double x) { return log10(x); }

/* -----------------------------------------------------------------------
 * String stdlib
 * --------------------------------------------------------------------- */
GrandaStr* granda_str_substr(GrandaStr* s, int64_t start, int64_t len) {
    if (!s || start < 0 || start >= (int64_t)s->len)
        return granda_str_literal("");
    if (start + len > (int64_t)s->len)
        len = (int64_t)s->len - start;
    if (len < 0) len = 0;
    return granda_str_new(s->data + start, (size_t)len);
}

int64_t granda_str_index_of(GrandaStr* s, GrandaStr* sub) {
    if (!s || !sub || sub->len == 0) return -1;
    for (size_t i = 0; i + sub->len <= s->len; i++) {
        if (memcmp(s->data + i, sub->data, sub->len) == 0)
            return (int64_t)i;
    }
    return -1;
}

int granda_str_contains(GrandaStr* s, GrandaStr* sub) {
    return granda_str_index_of(s, sub) >= 0;
}

GrandaStr* granda_str_to_upper(GrandaStr* s) {
    if (!s) return granda_str_literal("");
    char* buf = (char*)malloc(s->len + 1);
    if (!buf) granda_panic("Out of memory in to_upper");
    for (size_t i = 0; i < s->len; i++)
        buf[i] = (s->data[i] >= 'a' && s->data[i] <= 'z')
            ? s->data[i] - 32 : s->data[i];
    buf[s->len] = '\0';
    GrandaStr* result = granda_str_new(buf, s->len);
    free(buf);
    return result;
}

GrandaStr* granda_str_to_lower(GrandaStr* s) {
    if (!s) return granda_str_literal("");
    char* buf = (char*)malloc(s->len + 1);
    if (!buf) granda_panic("Out of memory in to_lower");
    for (size_t i = 0; i < s->len; i++)
        buf[i] = (s->data[i] >= 'A' && s->data[i] <= 'Z')
            ? s->data[i] + 32 : s->data[i];
    buf[s->len] = '\0';
    GrandaStr* result = granda_str_new(buf, s->len);
    free(buf);
    return result;
}

GrandaStr* granda_str_trim(GrandaStr* s) {
    if (!s || s->len == 0) return granda_str_literal("");
    size_t start = 0, end = s->len;
    while (start < end && (s->data[start] == ' ' || s->data[start] == '\t'
           || s->data[start] == '\n' || s->data[start] == '\r'))
        start++;
    while (end > start && (s->data[end-1] == ' ' || s->data[end-1] == '\t'
           || s->data[end-1] == '\n' || s->data[end-1] == '\r'))
        end--;
    return granda_str_new(s->data + start, end - start);
}

GrandaArray* granda_str_split(GrandaStr* s, GrandaStr* sep) {
    GrandaArray* arr = granda_array_new(4, 1);
    if (!s || !sep || sep->len == 0) {
        if (s) granda_array_push(arr, s);
        return arr;
    }
    size_t pos = 0;
    while (pos <= s->len) {
        size_t next = s->len;
        for (size_t i = pos; i + sep->len <= s->len; i++) {
            if (memcmp(s->data + i, sep->data, sep->len) == 0) {
                next = i;
                break;
            }
        }
        GrandaStr* part = granda_str_new(s->data + pos, next - pos);
        granda_array_push(arr, part);
        if (next == s->len) break;
        pos = next + sep->len;
    }
    return arr;
}

GrandaStr* granda_str_replace(GrandaStr* s, GrandaStr* old_s, GrandaStr* new_s) {
    if (!s || !old_s || old_s->len == 0) return s ? s : granda_str_literal("");
    /* Count occurrences */
    int count = 0;
    size_t pos = 0;
    while (pos <= s->len) {
        int found = 0;
        for (size_t i = pos; i + old_s->len <= s->len; i++) {
            if (memcmp(s->data + i, old_s->data, old_s->len) == 0) {
                count++;
                pos = i + old_s->len;
                found = 1;
                break;
            }
        }
        if (!found) break;
    }
    if (count == 0) return s;
    /* Build result */
    size_t result_len = s->len + (size_t)count * (new_s->len > old_s->len ? new_s->len - old_s->len : 0)
                        + 1;
    char* buf = (char*)malloc(result_len);
    if (!buf) granda_panic("Out of memory in string replace");
    size_t out = 0;
    pos = 0;
    while (pos < s->len) {
        int found = 0;
        for (size_t i = pos; i + old_s->len <= s->len; i++) {
            if (memcmp(s->data + i, old_s->data, old_s->len) == 0) {
                memcpy(buf + out, s->data + pos, i - pos);
                out += i - pos;
                memcpy(buf + out, new_s->data, new_s->len);
                out += new_s->len;
                pos = i + old_s->len;
                found = 1;
                break;
            }
        }
        if (!found) {
            memcpy(buf + out, s->data + pos, s->len - pos);
            out += s->len - pos;
            break;
        }
    }
    buf[out] = '\0';
    GrandaStr* result = granda_str_new(buf, out);
    free(buf);
    return result;
}

int granda_str_starts_with(GrandaStr* s, GrandaStr* prefix) {
    if (!s || !prefix || prefix->len > s->len) return 0;
    return memcmp(s->data, prefix->data, prefix->len) == 0;
}

int granda_str_ends_with(GrandaStr* s, GrandaStr* suffix) {
    if (!s || !suffix || suffix->len > s->len) return 0;
    return memcmp(s->data + s->len - suffix->len, suffix->data, suffix->len) == 0;
}

GrandaStr* granda_str_char_at(GrandaStr* s, int64_t idx) {
    if (!s || idx < 0 || idx >= (int64_t)s->len)
        return granda_str_literal("");
    return granda_str_new(s->data + idx, 1);
}

int64_t granda_str_to_int(GrandaStr* s) {
    if (!s || s->len == 0) return 0;
    return (int64_t)strtoll(s->data, NULL, 10);
}

double granda_str_to_float(GrandaStr* s) {
    if (!s || s->len == 0) return 0.0;
    return strtod(s->data, NULL);
}

/* -----------------------------------------------------------------------
 * Array stdlib
 * --------------------------------------------------------------------- */
void* granda_array_pop(GrandaArray* arr) {
    if (!arr || arr->len == 0) return NULL;
    arr->len--;
    void* item = arr->items[arr->len];
    if (arr->elem_is_gc) rc_release(item);
    return item;
}

int64_t granda_int_array_pop(GrandaArray* arr) {
    if (!arr || arr->len == 0) return 0;
    arr->len--;
    return *(int64_t*)&arr->items[arr->len];
}

double granda_float_array_pop(GrandaArray* arr) {
    if (!arr || arr->len == 0) return 0.0;
    arr->len--;
    return *(double*)&arr->items[arr->len];
}

static int cmp_int(const void* a, const void* b) {
    int64_t va = *(const int64_t*)a;
    int64_t vb = *(const int64_t*)b;
    return (va > vb) - (va < vb);
}

static int cmp_float(const void* a, const void* b) {
    double va = *(const double*)a;
    double vb = *(const double*)b;
    return (va > vb) - (va < vb);
}

void granda_array_sort_i(GrandaArray* arr) {
    if (!arr || arr->len < 2) return;
    qsort(arr->items, arr->len, sizeof(void*), cmp_int);
}

void granda_array_sort_f(GrandaArray* arr) {
    if (!arr || arr->len < 2) return;
    qsort(arr->items, arr->len, sizeof(void*), cmp_float);
}

void granda_array_reverse(GrandaArray* arr) {
    if (!arr || arr->len < 2) return;
    for (size_t i = 0, j = arr->len - 1; i < j; i++, j--) {
        void* tmp = arr->items[i];
        arr->items[i] = arr->items[j];
        arr->items[j] = tmp;
    }
}

/* -----------------------------------------------------------------------
 * File I/O
 * --------------------------------------------------------------------- */
GrandaStr* granda_read_file(GrandaStr* path) {
    if (!path) return granda_str_literal("");
    FILE* f = fopen(path->data, "rb");
    if (!f) {
        char buf[256];
        snprintf(buf, sizeof(buf), "Cannot open file: %s", path->data);
        granda_panic(buf);
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return granda_str_literal(""); }
    char* data = (char*)malloc((size_t)sz + 1);
    if (!data) { fclose(f); granda_panic("Out of memory reading file"); }
    size_t nread = fread(data, 1, (size_t)sz, f);
    fclose(f);
    data[nread] = '\0';
    GrandaStr* result = granda_str_new(data, nread);
    free(data);
    return result;
}

void granda_write_file(GrandaStr* path, GrandaStr* content) {
    if (!path) granda_panic("write_file: null path");
    FILE* f = fopen(path->data, "wb");
    if (!f) {
        char buf[256];
        snprintf(buf, sizeof(buf), "Cannot open file for writing: %s", path->data);
        granda_panic(buf);
    }
    if (content && content->len > 0)
        fwrite(content->data, 1, content->len, f);
    fclose(f);
}

int granda_file_exists(GrandaStr* path) {
    if (!path) return 0;
    FILE* f = fopen(path->data, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

static char _granda_line_buf[4096];
GrandaStr* granda_read_line(void) {
    if (fgets(_granda_line_buf, sizeof(_granda_line_buf), stdin)) {
        size_t len = strlen(_granda_line_buf);
        while (len > 0 && (_granda_line_buf[len-1] == '\n' || _granda_line_buf[len-1] == '\r'))
            len--;
        return granda_str_new(_granda_line_buf, len);
    }
    return granda_str_literal("");
}

static GrandaArray* _granda_argv = NULL;
GrandaArray* granda_args(void) {
    return _granda_argv;
}

void granda_set_args(int argc, char** argv) {
    _granda_argv = granda_array_new((size_t)argc, 1);
    for (int i = 0; i < argc; i++)
        granda_array_push(_granda_argv, granda_str_literal(argv[i]));
}

/* -----------------------------------------------------------------------
 * Random
 * --------------------------------------------------------------------- */
int64_t granda_rand_int(int64_t min_val, int64_t max_val) {
    if (min_val >= max_val) return min_val;
    return min_val + (rand() % (max_val - min_val + 1));
}

double granda_rand_float(void) {
    return (double)rand() / (double)RAND_MAX;
}

void granda_rand_seed(int64_t seed) {
    srand((unsigned int)seed);
}

/* -----------------------------------------------------------------------
 * Time
 * --------------------------------------------------------------------- */
double granda_time_now(void) {
#ifdef _WIN32
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER li;
    li.LowPart = ft.dwLowDateTime;
    li.HighPart = ft.dwHighDateTime;
    /* Windows epoch starts 1601, Unix epoch 1970: difference in 100ns units */
    return (double)(li.QuadPart - 116444736000000000ULL) / 10000000.0;
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
}

void granda_time_sleep(int64_t ms) {
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

/* -----------------------------------------------------------------------
 * OS
 * --------------------------------------------------------------------- */
void granda_os_exit(int64_t code) {
    exit((int)code);
}

GrandaStr* granda_os_env(GrandaStr* name) {
    if (!name) return granda_str_literal("");
    const char* val = getenv(name->data);
    return val ? granda_str_literal(val) : granda_str_literal("");
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
