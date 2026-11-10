/* Shared container runtime for Runtime ABI v1.
 *
 * Written once in C and linked into BOTH targets: the Linux ELF build links it
 * next to the hand-written asm runtime, the Windows PE build next to
 * rbl_runtime_win.c. The ABI is the same as the rest of the runtime, so the code
 * generator calls these entry points exactly like any other runtime function:
 * arguments in rdi, rsi, rdx, rcx, r8 and the result in rax (tag) : rdx (payload).
 *
 * Values are stored as (tag, payload) pairs, 16 bytes each, so a list can hold
 * anything the language has, including nested lists.
 *
 * Memory: containers and dynamic strings are registered with the shared
 * conservative mark-and-sweep collector below. Generated code enters it only
 * at safe points, after live values have been stored in stack slots.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "rbl_abi.h"

enum {
    TAG_UNIT = RBL_TAG_UNIT, TAG_INT = RBL_TAG_INT, TAG_FLOAT = RBL_TAG_FLOAT,
    TAG_STR = RBL_TAG_STRING, TAG_BOOL = RBL_TAG_BOOL, TAG_LIST = RBL_TAG_LIST,
    TAG_DICT = RBL_TAG_DICTIONARY, TAG_TUPLE = RBL_TAG_TUPLE
};

#if defined(_WIN32)
#define RBL_SYSV __attribute__((sysv_abi))
#else
#define RBL_SYSV
#endif

typedef RBLRuntimeValue RBLValue;

/* Every container starts with this header: the collector needs the tag to trace
   an object and a mark bit to keep it. */
typedef struct {
    uint64_t tag;  /* TAG_LIST / TAG_TUPLE / TAG_DICT */
    uint64_t mark; /* set during a collection */
} RBLObj;

typedef struct {
    RBLObj hdr;
    uint64_t len, cap;
    uint64_t *items; /* len * 2 words: tag, payload */
} RBLList;

static void rbl_gc_register(void *obj, uint64_t tag, size_t bytes);
static void gc_collect(void);

typedef struct {
    uint64_t mark;
    size_t size;
} RBLStringHeader;

static char **g_strings;
static size_t g_strings_cap, g_strings_len, g_strings_tombs;
static size_t g_string_bytes;
static size_t g_bytes;

#define GC_EMPTY ((void *)0)
#define GC_TOMB  ((void *)1)

#ifndef RBL_GC_TESTING
enum {
    RBL_GC_TEST_FAIL_STRING_PAYLOAD = 1,
    RBL_GC_TEST_FAIL_STRING_REGISTRY = 2,
    RBL_GC_TEST_FAIL_CONTAINER_REGISTRY = 3,
    RBL_GC_TEST_FAIL_MARK_STACK = 4,
    RBL_GC_TEST_FAIL_SURVIVORS = 5,
    RBL_GC_TEST_FAIL_REBUILD_REGISTRY = 6
};
#endif

#ifdef RBL_GC_TESTING
static uint64_t g_test_fail_site;
static uint64_t g_test_fail_hits;

static int gc_test_should_fail_site(uint64_t site)
{
    if (g_test_fail_site == site) {
        g_test_fail_site = 0;
        g_test_fail_hits++;
        return 1;
    }
    return 0;
}
#else
static int gc_test_should_fail_site(uint64_t site)
{
    (void)site;
    return 0;
}
#endif

static void strings_rehash(size_t cap);
static void strings_add(char *string);
static int strings_remove(char *string);
static int strings_has(const void *string);

RBL_SYSV char *rbl_string_alloc(uint64_t size)
{
    if (size == 0 || size > SIZE_MAX - sizeof(RBLStringHeader)) {
        rbl_fail("out of memory while allocating a string");
    }
    RBLStringHeader *header = gc_test_should_fail_site(RBL_GC_TEST_FAIL_STRING_PAYLOAD)
        ? NULL : (RBLStringHeader *)malloc(sizeof(*header) + (size_t)size);
    if (!header) {
        rbl_fail("out of memory while allocating a string");
    }
    header->mark = 0;
    header->size = (size_t)size;
    char *string = (char *)(header + 1);
    size_t allocation_bytes = sizeof(*header) + (size_t)size;
    if (g_string_bytes > SIZE_MAX - allocation_bytes) {
        free(header);
        rbl_fail("string heap accounting overflow");
        return NULL;
    }
    strings_add(string);
    if (!strings_has(string)) {
        free(header);
        rbl_fail("out of memory while tracking a string");
    }
    g_string_bytes += allocation_bytes;
    return string;
}

RBL_SYSV void rbl_string_free(char *string)
{
    if (!string || !strings_remove(string)) {
        return;
    }
    RBLStringHeader *header = ((RBLStringHeader *)string) - 1;
    size_t bytes = sizeof(*header) + header->size;
    g_string_bytes = g_string_bytes >= bytes ? g_string_bytes - bytes : 0;
    free(header);
}

static RBLValue value(uint64_t tag, uint64_t payload)
{
    RBLValue v;
    v.tag = tag;
    v.payload = payload;
    return v;
}

static RBLValue string_value_from_buffer(char *buffer, size_t len)
{
    char *managed = rbl_string_alloc((uint64_t)(len + 1));
    memcpy(managed, buffer, len + 1);
    free(buffer);
    return value(TAG_STR, (uint64_t)(uintptr_t)managed);
}

static RBLList *as_list(uint64_t payload)
{
    return (RBLList *)(uintptr_t)payload;
}

static void list_oom(void)
{
    rbl_fail("out of memory while growing a list");
}

#define RBL_LIST_CAP0 4

RBL_SYSV RBLValue rbl_list_new(void)
{
    RBLList *l = (RBLList *)malloc(sizeof(RBLList));
    if (!l) {
        list_oom();
    }
    l->len = 0;
    l->cap = RBL_LIST_CAP0;
    l->items = (uint64_t *)malloc(l->cap * 2 * sizeof(uint64_t));
    if (!l->items) {
        list_oom();
    }
    rbl_gc_register(l, TAG_LIST, sizeof(RBLList) + l->cap * 2 * sizeof(uint64_t));
    return value(TAG_LIST, (uint64_t)(uintptr_t)l);
}

RBL_SYSV RBLValue rbl_list_push(uint64_t tag, uint64_t payload, uint64_t vt, uint64_t vp)
{
    (void)tag;
    RBLList *l = as_list(payload);
    if (l->len == l->cap) {
        if (l->cap > SIZE_MAX / (4 * sizeof(uint64_t))) {
            list_oom();
        }
        size_t old_bytes = (size_t)l->cap * 2 * sizeof(uint64_t);
        size_t new_cap = (size_t)l->cap * 2;
        size_t new_bytes = new_cap * 2 * sizeof(uint64_t);
        size_t delta = new_bytes - old_bytes;
        if (g_bytes > SIZE_MAX - delta) {
            rbl_fail("container heap accounting overflow");
        }
        uint64_t *grown = (uint64_t *)realloc(l->items, new_bytes);
        if (!grown) {
            list_oom();
        }
        l->items = grown;
        l->cap = new_cap;
        g_bytes += delta;
    }
    l->items[l->len * 2] = vt;
    l->items[l->len * 2 + 1] = vp;
    l->len++;
    return value(TAG_LIST, payload);
}

static RBLList *as_list_checked(uint64_t tag, uint64_t payload)
{
    /* Tuples share the storage; mutation of a tuple is rejected in rbl_index_set,
       which is the only path the language can reach a store through. */
    if (tag != TAG_LIST && tag != TAG_TUPLE) {
        rbl_fail("expected a list or tuple");
    }
    return as_list(payload);
}

RBL_SYSV RBLValue rbl_list_len(uint64_t tag, uint64_t payload)
{
    return value(TAG_INT, as_list_checked(tag, payload)->len);
}

static void check_index(RBLList *l, uint64_t idx)
{
    if (idx >= l->len) {
        char buf[128];
        snprintf(buf, sizeof buf, "list index %lld is out of range (length %llu)",
                 (long long)idx, (unsigned long long)l->len);
        rbl_fail(buf);
    }
}

RBL_SYSV RBLValue rbl_list_get(uint64_t tag, uint64_t payload, uint64_t idx)
{
    RBLList *l = as_list_checked(tag, payload);
    check_index(l, idx);
    return value(l->items[idx * 2], l->items[idx * 2 + 1]);
}

RBL_SYSV RBLValue rbl_list_set(uint64_t tag, uint64_t payload, uint64_t idx, uint64_t vt, uint64_t vp)
{
    if (tag == TAG_TUPLE) {
        rbl_fail("tuples cannot be modified");
    }
    if (tag != TAG_LIST) {
        rbl_fail("list_set() expects a list");
    }
    RBLList *l = as_list(payload);
    check_index(l, idx);
    l->items[idx * 2] = vt;
    l->items[idx * 2 + 1] = vp;
    return value(TAG_LIST, payload);
}

/* [1, 2, "x", [3]] — elements are rendered with the normal value-to-string path,
   so nested lists print recursively. */
RBL_SYSV RBLValue rbl_list_str(uint64_t tag, uint64_t payload)
{
    RBLList *l = as_list_checked(tag, payload);
    size_t cap = 32, n = 0;
    char *out = (char *)malloc(cap);
    if (!out) {
        list_oom();
    }
    out[n++] = '[';
    for (uint64_t i = 0; i < l->len; i++) {
        RBLValue s = rbl_str(l->items[i * 2], l->items[i * 2 + 1]);
        const char *text = (const char *)(uintptr_t)s.payload;
        size_t len = strlen(text);
        while (n + len + 4 > cap) {
            cap *= 2;
            char *grown = (char *)realloc(out, cap);
            if (!grown) {
                list_oom();
            }
            out = grown;
        }
        if (i) {
            out[n++] = ',';
            out[n++] = ' ';
        }
        memcpy(out + n, text, len);
        n += len;
    }
    while (n + 2 > cap) {
        cap *= 2;
        char *grown = (char *)realloc(out, cap);
        if (!grown) {
            list_oom();
        }
        out = grown;
    }
    out[n++] = ']';
    out[n] = '\0';
    return string_value_from_buffer(out, n);
}

/* Deep equality: nested containers compare element-wise, everything else through
   the ordinary ==, so floats and strings behave exactly as outside containers. */
static int items_equal(RBLList *a, RBLList *b)
{
    if (a == b) {
        return 1;
    }
    if (a->len != b->len) {
        return 0;
    }
    for (uint64_t i = 0; i < a->len; i++) {
        uint64_t at = a->items[i * 2], ap = a->items[i * 2 + 1];
        uint64_t bt = b->items[i * 2], bp = b->items[i * 2 + 1];
        if (at == TAG_LIST || bt == TAG_LIST) {
            if (!rbl_list_eq(at, ap, bt, bp).payload) {
                return 0;
            }
            continue;
        }
        if (at == TAG_TUPLE || bt == TAG_TUPLE) {
            if (!rbl_tuple_eq(at, ap, bt, bp).payload) {
                return 0;
            }
            continue;
        }
        if (!rbl_eq(at, ap, bt, bp).payload) {
            return 0;
        }
    }
    return 1;
}

RBL_SYSV RBLValue rbl_list_eq(uint64_t tag, uint64_t payload, uint64_t tag2, uint64_t payload2)
{
    (void)tag;
    if (tag2 != TAG_LIST) {
        return value(TAG_BOOL, 0);
    }
    return value(TAG_BOOL, items_equal(as_list(payload), as_list(payload2)));
}

/* ------------------------------------------------------------------ */
/* tuples: the same storage as lists, a different tag and parentheses  */
/* ------------------------------------------------------------------ */

RBL_SYSV RBLValue rbl_tuple_new(void)
{
    RBLValue l = rbl_list_new();
    ((RBLObj *)(uintptr_t)l.payload)->tag = TAG_TUPLE;
    return value(TAG_TUPLE, l.payload);
}

RBL_SYSV RBLValue rbl_tuple_push(uint64_t tag, uint64_t payload, uint64_t vt, uint64_t vp)
{
    (void)tag;
    RBLValue r = rbl_list_push(TAG_LIST, payload, vt, vp);
    return value(TAG_TUPLE, r.payload);
}

RBL_SYSV RBLValue rbl_tuple_str(uint64_t tag, uint64_t payload)
{
    (void)tag;
    /* Reuse the list renderer and swap the outer brackets, so nesting prints
       correctly ([1, (2, 3)]) with no second formatting loop. */
    RBLValue s = rbl_list_str(TAG_LIST, payload);
    char *text = (char *)(uintptr_t)s.payload;
    size_t len = strlen(text);
    if (len >= 2) {
        text[0] = '(';
        text[len - 1] = ')';
    }
    return s;
}

RBL_SYSV RBLValue rbl_tuple_eq(uint64_t tag, uint64_t payload, uint64_t tag2, uint64_t payload2)
{
    (void)tag;
    if (tag2 != TAG_TUPLE) {
        return value(TAG_BOOL, 0);
    }
    return value(TAG_BOOL, items_equal(as_list(payload), as_list(payload2)));
}

RBL_SYSV RBLValue rbl_tuple_len(uint64_t tag, uint64_t payload)
{
    (void)tag;
    return value(TAG_INT, as_list(payload)->len);
}

RBL_SYSV RBLValue rbl_tuple_get(uint64_t tag, uint64_t payload, uint64_t idx)
{
    (void)tag;
    return rbl_list_get(TAG_LIST, payload, idx);
}

/* ------------------------------------------------------------------ */
/* dictionaries: open addressing with linear probing                   */
/* ------------------------------------------------------------------ */

typedef struct {
    uint64_t kt, kp; /* key */
    uint64_t vt, vp; /* value */
    uint64_t used;
} RBLDictEntry;

typedef struct {
    RBLObj hdr;
    uint64_t len, cap;
    RBLDictEntry *entries;
} RBLDict;

static RBLDict *as_dict_checked(uint64_t tag, uint64_t payload)
{
    if (tag != TAG_DICT) {
        rbl_fail("expected a dictionary");
    }
    return (RBLDict *)(uintptr_t)payload;
}

static uint64_t dict_hash(uint64_t kt, uint64_t kp)
{
    uint64_t h = 1469598103934665603ULL;
    if (kt == TAG_STR) {
        const unsigned char *s = (const unsigned char *)(uintptr_t)kp;
        while (*s) {
            h ^= (uint64_t)*s++;
            h *= 1099511628211ULL;
        }
        return h;
    }
    h ^= kt;
    h *= 1099511628211ULL;
    for (int i = 0; i < 8; i++) {
        h ^= (kp >> (i * 8)) & 0xFFu;
        h *= 1099511628211ULL;
    }
    return h;
}

static int keys_equal(uint64_t at, uint64_t ap, uint64_t bt, uint64_t bp)
{
    /* Reuse the ordinary ==, so int/float/string/bool keys compare the same way
       they do anywhere else in the language. */
    return rbl_eq(at, ap, bt, bp).payload != 0;
}

#define RBL_DICT_CAP0 8

static void dict_alloc(RBLDict *d, uint64_t cap)
{
    d->cap = cap;
    d->len = 0;
    d->entries = (RBLDictEntry *)calloc(cap, sizeof(RBLDictEntry));
    if (!d->entries) {
        rbl_fail("out of memory while growing a dictionary");
    }
}

static void dict_grow(RBLDict *d)
{
    RBLDictEntry *old = d->entries;
    uint64_t old_cap = d->cap;
    if (old_cap > SIZE_MAX / (2 * sizeof(RBLDictEntry))) {
        rbl_fail("out of memory while growing a dictionary");
    }
    size_t old_bytes = (size_t)old_cap * sizeof(RBLDictEntry);
    size_t new_bytes = old_bytes * 2;
    size_t delta = new_bytes - old_bytes;
    if (g_bytes > SIZE_MAX - delta) {
        rbl_fail("container heap accounting overflow");
    }
    RBLDict fresh;
    dict_alloc(&fresh, old_cap * 2);
    for (uint64_t i = 0; i < old_cap; i++) {
        if (!old[i].used) {
            continue;
        }
        uint64_t j = dict_hash(old[i].kt, old[i].kp) % fresh.cap;
        while (fresh.entries[j].used) {
            j = (j + 1) % fresh.cap;
        }
        fresh.entries[j] = old[i];
        fresh.len++;
    }
    free(old);
    d->entries = fresh.entries;
    d->cap = fresh.cap;
    d->len = fresh.len;
    g_bytes += delta;
}

static RBLDictEntry *dict_slot(RBLDict *d, uint64_t kt, uint64_t kp, int create)
{
    uint64_t j = dict_hash(kt, kp) % d->cap;
    for (;;) {
        RBLDictEntry *e = &d->entries[j];
        if (!e->used) {
            if (!create) {
                return NULL;
            }
            e->used = 1;
            e->kt = kt;
            e->kp = kp;
            e->vt = TAG_UNIT;
            e->vp = 0;
            d->len++;
            return e;
        }
        if (keys_equal(e->kt, e->kp, kt, kp)) {
            return e;
        }
        j = (j + 1) % d->cap;
    }
}

RBL_SYSV RBLValue rbl_dict_new(void)
{
    RBLDict *d = (RBLDict *)malloc(sizeof(RBLDict));
    if (!d) {
        rbl_fail("out of memory while creating a dictionary");
    }
    dict_alloc(d, RBL_DICT_CAP0);
    rbl_gc_register(d, TAG_DICT, sizeof(RBLDict) + RBL_DICT_CAP0 * sizeof(RBLDictEntry));
    return value(TAG_DICT, (uint64_t)(uintptr_t)d);
}

RBL_SYSV RBLValue rbl_dict_set(uint64_t tag, uint64_t payload, uint64_t kt, uint64_t kp, uint64_t vt, uint64_t vp)
{
    RBLDict *d = as_dict_checked(tag, payload);
    if ((d->len + 1) * 4 >= d->cap * 3) {
        dict_grow(d);
    }
    RBLDictEntry *e = dict_slot(d, kt, kp, 1);
    e->vt = vt;
    e->vp = vp;
    return value(TAG_DICT, payload);
}

/* A missing key reads as () — pair it with `in` when the difference matters. */
RBL_SYSV RBLValue rbl_dict_get(uint64_t tag, uint64_t payload, uint64_t kt, uint64_t kp)
{
    RBLDict *d = as_dict_checked(tag, payload);
    RBLDictEntry *e = dict_slot(d, kt, kp, 0);
    if (!e) {
        return value(TAG_UNIT, 0);
    }
    return value(e->vt, e->vp);
}

RBL_SYSV RBLValue rbl_dict_has(uint64_t tag, uint64_t payload, uint64_t kt, uint64_t kp)
{
    RBLDict *d = as_dict_checked(tag, payload);
    return value(TAG_BOOL, dict_slot(d, kt, kp, 0) ? 1 : 0);
}

RBL_SYSV RBLValue rbl_dict_len(uint64_t tag, uint64_t payload)
{
    return value(TAG_INT, as_dict_checked(tag, payload)->len);
}

RBL_SYSV RBLValue rbl_dict_str(uint64_t tag, uint64_t payload)
{
    RBLDict *d = as_dict_checked(tag, payload);
    size_t cap = 32, n = 0;
    char *out = (char *)malloc(cap);
    if (!out) {
        list_oom();
    }
    out[n++] = '{';
    uint64_t written = 0;
    for (uint64_t i = 0; i < d->cap; i++) {
        RBLDictEntry *e = &d->entries[i];
        if (!e->used) {
            continue;
        }
        RBLValue ks = rbl_str(e->kt, e->kp);
        RBLValue vs = rbl_str(e->vt, e->vp);
        const char *k = (const char *)(uintptr_t)ks.payload;
        const char *v = (const char *)(uintptr_t)vs.payload;
        size_t need = strlen(k) + strlen(v) + 8;
        while (n + need > cap) {
            cap *= 2;
            char *grown = (char *)realloc(out, cap);
            if (!grown) {
                list_oom();
            }
            out = grown;
        }
        if (written++) {
            out[n++] = ',';
            out[n++] = ' ';
        }
        size_t kl = strlen(k);
        memcpy(out + n, k, kl);
        n += kl;
        out[n++] = ':';
        out[n++] = ' ';
        size_t vl = strlen(v);
        memcpy(out + n, v, vl);
        n += vl;
    }
    while (n + 2 > cap) {
        cap *= 2;
        char *grown = (char *)realloc(out, cap);
        if (!grown) {
            list_oom();
        }
        out = grown;
    }
    out[n++] = '}';
    out[n] = '\0';
    return string_value_from_buffer(out, n);
}

/* Order-insensitive deep equality. */
RBL_SYSV RBLValue rbl_dict_eq(uint64_t tag, uint64_t payload, uint64_t tag2, uint64_t payload2)
{
    (void)tag;
    if (tag2 != TAG_DICT) {
        return value(TAG_BOOL, 0);
    }
    RBLDict *a = (RBLDict *)(uintptr_t)payload;
    RBLDict *b = (RBLDict *)(uintptr_t)payload2;
    if (a == b) {
        return value(TAG_BOOL, 1);
    }
    if (a->len != b->len) {
        return value(TAG_BOOL, 0);
    }
    for (uint64_t i = 0; i < a->cap; i++) {
        RBLDictEntry *e = &a->entries[i];
        if (!e->used) {
            continue;
        }
        RBLDictEntry *o = dict_slot(b, e->kt, e->kp, 0);
        if (!o) {
            return value(TAG_BOOL, 0);
        }
        if (e->vt == TAG_LIST || o->vt == TAG_LIST) {
            if (!rbl_list_eq(e->vt, e->vp, o->vt, o->vp).payload) {
                return value(TAG_BOOL, 0);
            }
            continue;
        }
        if (e->vt == TAG_DICT || o->vt == TAG_DICT) {
            if (!rbl_dict_eq(e->vt, e->vp, o->vt, o->vp).payload) {
                return value(TAG_BOOL, 0);
            }
            continue;
        }
        if (!rbl_eq(e->vt, e->vp, o->vt, o->vp).payload) {
            return value(TAG_BOOL, 0);
        }
    }
    return value(TAG_BOOL, 1);
}

/* `is`: identity, not structural equality. Two containers compare identical only
   when they are the same object; scalars compare by tag and payload. */
RBL_SYSV RBLValue rbl_is(uint64_t t1, uint64_t p1, uint64_t t2, uint64_t p2)
{
    return value(TAG_BOOL, (t1 == t2 && p1 == p2) ? 1 : 0);
}

/* `in`: membership for dictionaries (keys) and lists (values). */
RBL_SYSV RBLValue rbl_contains(uint64_t tag, uint64_t payload, uint64_t kt, uint64_t kp)
{
    if (tag == TAG_DICT) {
        return rbl_dict_has(tag, payload, kt, kp);
    }
    if (tag == TAG_LIST || tag == TAG_TUPLE) {
        RBLList *l = as_list(payload);
        for (uint64_t i = 0; i < l->len; i++) {
            uint64_t at = l->items[i * 2], ap = l->items[i * 2 + 1];
            if (at == TAG_LIST || at == TAG_DICT || at == TAG_TUPLE) {
                continue;
            }
            if (rbl_eq(at, ap, kt, kp).payload) {
                return value(TAG_BOOL, 1);
            }
        }
        return value(TAG_BOOL, 0);
    }
    if (tag == TAG_STR) {
        const char *hay = (const char *)(uintptr_t)payload;
        const char *needle = (kt == TAG_STR) ? (const char *)(uintptr_t)kp : NULL;
        if (!needle) {
            return value(TAG_BOOL, 0);
        }
        return value(TAG_BOOL, strstr(hay, needle) ? 1 : 0);
    }
    rbl_fail("in expects a list, a dictionary or a string on the right");
    return value(TAG_BOOL, 0);
}

/* ------------------------------------------------------------------ */
/* conservative mark-and-sweep collector                               */
/*                                                                     */
/* Roots are pointer-shaped words in scanned stack memory (including     */
/* payload words in tagged slots, up to the frame captured in main),    */
/* plus values reachable through containers. Generated code calls the   */
/* collector only at safe points after live locals reach stack slots.    */
/* ------------------------------------------------------------------ */

static void **g_objs;          /* registry of live objects, open addressing */
static size_t g_objs_cap, g_objs_len, g_objs_tombs;
static char *g_stack_base;
static size_t g_threshold = 1u << 20;
static void **g_mark_stack;
static size_t g_mark_cap, g_mark_len;
static void **g_survivors;
static size_t g_survivors_cap;
static int g_mark_failed;

static uint64_t ptr_hash(const void *p)
{
    uint64_t x = (uint64_t)(uintptr_t)p;
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    return x;
}

static void objs_add(void *p);

static void objs_rehash(size_t cap)
{
    void **slots = gc_test_should_fail_site(RBL_GC_TEST_FAIL_CONTAINER_REGISTRY)
        ? NULL : (void **)calloc(cap, sizeof(void *));
    if (!slots) {
        return;
    }
    void **old = g_objs;
    size_t old_cap = g_objs_cap;
    g_objs = slots;
    g_objs_cap = cap;
    g_objs_len = 0;
    g_objs_tombs = 0;
    for (size_t i = 0; i < old_cap; i++) {
        if (old[i] && old[i] != GC_TOMB) {
            objs_add(old[i]);
        }
    }
    free(old);
}

static void objs_add(void *p)
{
    if (g_objs_cap == 0 || (g_objs_len + g_objs_tombs + 1) * 4 >= g_objs_cap * 3) {
        if (g_objs_cap > SIZE_MAX / 2 / sizeof(void *)) {
            rbl_fail("container registry capacity overflow");
            return;
        }
        objs_rehash(g_objs_cap ? g_objs_cap * 2 : 1024);
    }
    if (!g_objs_cap) {
        rbl_fail("out of memory while tracking a container");
        return;
    }
    size_t j = ptr_hash(p) % g_objs_cap;
    for (size_t probes = 0; probes < g_objs_cap; probes++) {
        if (!g_objs[j] || g_objs[j] == GC_TOMB) {
            if (g_objs[j] == GC_TOMB) {
                g_objs_tombs--;
            }
            g_objs[j] = p;
            g_objs_len++;
            return;
        }
        if (g_objs[j] == p) {
            return;
        }
        j = (j + 1) % g_objs_cap;
    }
    rbl_fail("container registry is full");
}

static int objs_has(const void *p)
{
    if (!g_objs_cap || !p || p == GC_TOMB) {
        return 0;
    }
    size_t j = ptr_hash(p) % g_objs_cap;
    for (size_t probes = 0; probes < g_objs_cap; probes++) {
        void *s = g_objs[j];
        if (!s) {
            return 0;
        }
        if (s == p) {
            return 1;
        }
        j = (j + 1) % g_objs_cap;
    }
    return 0;
}

static void strings_rehash(size_t cap)
{
    char **slots = gc_test_should_fail_site(RBL_GC_TEST_FAIL_STRING_REGISTRY)
        ? NULL : (char **)calloc(cap, sizeof(char *));
    if (!slots) {
        return;
    }
    char **old = g_strings;
    size_t old_cap = g_strings_cap;
    g_strings = slots;
    g_strings_cap = cap;
    g_strings_len = 0;
    g_strings_tombs = 0;
    for (size_t i = 0; i < old_cap; i++) {
        if (old[i] && old[i] != (char *)GC_TOMB) {
            strings_add(old[i]);
        }
    }
    free(old);
}

static void strings_add(char *string)
{
    if (g_strings_cap == 0 ||
        (g_strings_len + g_strings_tombs + 1) * 4 >= g_strings_cap * 3) {
        if (g_strings_cap > SIZE_MAX / 2 / sizeof(char *)) {
            rbl_fail("string registry capacity overflow");
            return;
        }
        strings_rehash(g_strings_cap ? g_strings_cap * 2 : 1024);
    }
    if (!g_strings_cap) {
        rbl_fail("out of memory while tracking a string");
        return;
    }
    size_t j = ptr_hash(string) % g_strings_cap;
    for (size_t probes = 0; probes < g_strings_cap; probes++) {
        if (!g_strings[j] || g_strings[j] == (char *)GC_TOMB) {
            if (g_strings[j] == (char *)GC_TOMB) {
                g_strings_tombs--;
            }
            g_strings[j] = string;
            g_strings_len++;
            return;
        }
        if (g_strings[j] == string) {
            return;
        }
        j = (j + 1) % g_strings_cap;
    }
    rbl_fail("string registry is full");
}

static int strings_has(const void *string)
{
    if (!g_strings_cap || !string || string == GC_TOMB) {
        return 0;
    }
    size_t j = ptr_hash(string) % g_strings_cap;
    for (size_t probes = 0; probes < g_strings_cap; probes++) {
        char *slot = g_strings[j];
        if (!slot) {
            return 0;
        }
        if (slot == string) {
            return 1;
        }
        j = (j + 1) % g_strings_cap;
    }
    return 0;
}

static int strings_remove(char *string)
{
    if (!g_strings_cap || !string) {
        return 0;
    }
    size_t j = ptr_hash(string) % g_strings_cap;
    for (size_t probes = 0; probes < g_strings_cap; probes++) {
        char *slot = g_strings[j];
        if (!slot) {
            return 0;
        }
        if (slot == string) {
            g_strings[j] = (char *)GC_TOMB;
            g_strings_len--;
            g_strings_tombs++;
            return 1;
        }
        j = (j + 1) % g_strings_cap;
    }
    return 0;
}

static void gc_push(void *o)
{
    if (g_mark_len == g_mark_cap) {
        if (g_mark_cap > SIZE_MAX / 2 / sizeof(void *)) {
            g_mark_failed = 1;
            return;
        }
        size_t cap = g_mark_cap ? g_mark_cap * 2 : 256;
        void **grown = gc_test_should_fail_site(RBL_GC_TEST_FAIL_MARK_STACK)
            ? NULL : (void **)realloc(g_mark_stack, cap * sizeof(void *));
        if (!grown) {
            g_mark_failed = 1;
            return;
        }
        g_mark_stack = grown;
        g_mark_cap = cap;
    }
    g_mark_stack[g_mark_len++] = o;
}

static void gc_push_if_container(uint64_t tag, uint64_t payload)
{
    if (tag == TAG_STR) {
        char *string = (char *)(uintptr_t)payload;
        if (strings_has(string)) {
            RBLStringHeader *header = ((RBLStringHeader *)string) - 1;
            header->mark = 1;
        }
        return;
    }
    if (tag != TAG_LIST && tag != TAG_TUPLE && tag != TAG_DICT) {
        return;
    }
    void *o = (void *)(uintptr_t)payload;
    if (objs_has(o) && !((RBLObj *)o)->mark) {
        ((RBLObj *)o)->mark = 1;
        gc_push(o);
    }
}

static void gc_scan_children(void *obj)
{
    RBLObj *h = (RBLObj *)obj;
    if (h->tag == TAG_LIST || h->tag == TAG_TUPLE) {
        RBLList *l = (RBLList *)obj;
        for (uint64_t i = 0; i < l->len; i++) {
            gc_push_if_container(l->items[i * 2], l->items[i * 2 + 1]);
        }
        return;
    }
    if (h->tag == TAG_DICT) {
        RBLDict *d = (RBLDict *)obj;
        for (uint64_t i = 0; i < d->cap; i++) {
            if (!d->entries[i].used) {
                continue;
            }
            gc_push_if_container(d->entries[i].kt, d->entries[i].kp);
            gc_push_if_container(d->entries[i].vt, d->entries[i].vp);
        }
    }
}

static void gc_mark_roots(void)
{
    if (!g_stack_base) {
        return;
    }
    uintptr_t lo = (uintptr_t)&lo;
    uintptr_t hi = (uintptr_t)g_stack_base;
    if (lo > hi) {
        uintptr_t t = lo;
        lo = hi;
        hi = t;
    }
    /* Generated locals are pointer-aligned stack slots. Start at the preceding
       aligned word so the address of this C local cannot skew the scan stride. */
    lo &= ~((uintptr_t)sizeof(void *) - 1u);
    for (uintptr_t p = lo; p <= hi && hi - p >= sizeof(void *); p += sizeof(void *)) {
        void *cand;
        memcpy(&cand, (const void *)p, sizeof(void *));
        if (objs_has(cand) && !((RBLObj *)cand)->mark) {
            ((RBLObj *)cand)->mark = 1;
            gc_push(cand);
        } else if (strings_has(cand)) {
            RBLStringHeader *header = ((RBLStringHeader *)cand) - 1;
            header->mark = 1;
        }
    }
}

static size_t gc_live_bytes(void *obj)
{
    RBLObj *h = (RBLObj *)obj;
    if (h->tag == TAG_DICT) {
        RBLDict *d = (RBLDict *)obj;
        return sizeof(RBLDict) + d->cap * sizeof(RBLDictEntry);
    }
    RBLList *l = (RBLList *)obj;
    return sizeof(RBLList) + l->cap * 2 * sizeof(uint64_t);
}

static void gc_free_obj(void *obj)
{
    RBLObj *h = (RBLObj *)obj;
    if (h->tag == TAG_DICT) {
        RBLDict *d = (RBLDict *)obj;
        free(d->entries);
    } else {
        RBLList *l = (RBLList *)obj;
        free(l->items);
    }
    free(obj);
}

static void gc_collect(void)
{
    g_mark_len = 0;
    g_mark_failed = 0;
    gc_mark_roots();
    while (g_mark_len) {
        gc_scan_children(g_mark_stack[--g_mark_len]);
    }
    if (g_mark_failed) {
        for (size_t i = 0; i < g_objs_cap; i++) {
            if (g_objs[i] && g_objs[i] != GC_TOMB) {
                ((RBLObj *)g_objs[i])->mark = 0;
            }
        }
        for (size_t i = 0; i < g_strings_cap; i++) {
            if (g_strings[i] && g_strings[i] != (char *)GC_TOMB) {
                (((RBLStringHeader *)g_strings[i]) - 1)->mark = 0;
            }
        }
        return;
    }

    if (g_objs_len > g_survivors_cap) {
        void **grown = gc_test_should_fail_site(RBL_GC_TEST_FAIL_SURVIVORS)
            ? NULL : (void **)realloc(g_survivors, g_objs_len * sizeof(void *));
        if (!grown) {
            for (size_t i = 0; i < g_objs_cap; i++) {
                if (g_objs[i] && g_objs[i] != GC_TOMB) {
                    ((RBLObj *)g_objs[i])->mark = 0;
                }
            }
            for (size_t i = 0; i < g_strings_cap; i++) {
                if (g_strings[i] && g_strings[i] != (char *)GC_TOMB) {
                    (((RBLStringHeader *)g_strings[i]) - 1)->mark = 0;
                }
            }
            return;
        }
        g_survivors = grown;
        g_survivors_cap = g_objs_len;
    }
    /* Prepare the replacement registry before freeing anything. Rebuilding it
       after the sweep had started would make registry OOM fatal after a partial
       collection. The sweep/rebuild phase below is allocation-free. */
    size_t new_objs_cap = 0;
    void **new_objs = NULL;
    if (g_objs_len) {
        new_objs_cap = 1024;
        while (g_objs_len >= new_objs_cap - new_objs_cap / 4) {
            if (new_objs_cap > SIZE_MAX / 2 / sizeof(void *)) {
                rbl_fail("container registry capacity overflow");
                return;
            }
            new_objs_cap *= 2;
        }
        new_objs = gc_test_should_fail_site(RBL_GC_TEST_FAIL_REBUILD_REGISTRY)
            ? NULL : (void **)calloc(new_objs_cap, sizeof(void *));
        if (!new_objs) {
            for (size_t i = 0; i < g_objs_cap; i++) {
                if (g_objs[i] && g_objs[i] != GC_TOMB) {
                    ((RBLObj *)g_objs[i])->mark = 0;
                }
            }
            for (size_t i = 0; i < g_strings_cap; i++) {
                if (g_strings[i] && g_strings[i] != (char *)GC_TOMB) {
                    (((RBLStringHeader *)g_strings[i]) - 1)->mark = 0;
                }
            }
            return;
        }
    }

    size_t live = 0;
    for (size_t i = 0; i < g_objs_cap; i++) {
        void *p = g_objs[i];
        if (!p || p == GC_TOMB) {
            continue;
        }
        if (((RBLObj *)p)->mark) {
            g_survivors[live++] = p;
            ((RBLObj *)p)->mark = 0;
        } else {
            gc_free_obj(p);
        }
    }
    free(g_objs);
    g_objs = new_objs;
    g_objs_cap = new_objs_cap;
    g_objs_len = 0;
    g_objs_tombs = 0;
    g_bytes = 0;
    for (size_t i = 0; i < live; i++) {
        void *p = g_survivors[i];
        size_t j = ptr_hash(p) % g_objs_cap;
        while (g_objs[j]) {
            j = (j + 1) % g_objs_cap;
        }
        g_objs[j] = p;
        g_objs_len++;
        g_bytes += gc_live_bytes(p);
    }

    size_t string_live_bytes = 0;
    for (size_t i = 0; i < g_strings_cap; i++) {
        char *string = g_strings[i];
        if (!string || string == (char *)GC_TOMB) {
            continue;
        }
        RBLStringHeader *header = ((RBLStringHeader *)string) - 1;
        if (header->mark) {
            header->mark = 0;
            string_live_bytes += sizeof(*header) + header->size;
        } else {
            free(header);
            g_strings[i] = (char *)GC_TOMB;
            g_strings_len--;
            g_strings_tombs++;
        }
    }
    g_string_bytes = string_live_bytes;
    if (g_strings_tombs) {
        strings_rehash(g_strings_cap ? g_strings_cap : 1024);
    }
    size_t total_bytes = g_bytes > SIZE_MAX - g_string_bytes
        ? SIZE_MAX : g_bytes + g_string_bytes;
    g_threshold = total_bytes > (SIZE_MAX - (1u << 20)) / 2
        ? SIZE_MAX : total_bytes * 2 + (1u << 20);
}

static void rbl_gc_register(void *obj, uint64_t tag, size_t bytes)
{
    ((RBLObj *)obj)->tag = tag;
    ((RBLObj *)obj)->mark = 0;
    objs_add(obj);
    if (g_bytes > SIZE_MAX - bytes) {
        rbl_fail("container heap accounting overflow");
        return;
    }
    g_bytes += bytes;
}

/* Called from every generated function with its frame pointer. The first call is
 * from main, so this records the upper boundary of the generated RBL stack. */
RBL_SYSV void rbl_gc_init(uint64_t stack_base)
{
    if (!g_stack_base) {
        g_stack_base = (char *)(uintptr_t)stack_base;
    }
}

/* Explicit collection, exposed for tests and the runtime's own use. */
RBL_SYSV void rbl_gc_run(void)
{
    if (g_stack_base) {
        gc_collect();
    }
}

/* Called by generated code between statements, when all live RBL locals are in
 * their stack slots and no expression temporary is held only in a register. */
RBL_SYSV void rbl_gc_safepoint(void)
{
    if (g_stack_base &&
        (g_bytes > g_threshold || g_string_bytes > g_threshold - g_bytes)) {
        gc_collect();
    }
}

#ifdef RBL_GC_TESTING
RBL_SYSV uint64_t rbl_gc_test_string_bytes(void)
{
    return (uint64_t)g_string_bytes;
}

RBL_SYSV uint64_t rbl_gc_test_container_bytes(void)
{
    return (uint64_t)g_bytes;
}

RBL_SYSV void rbl_gc_test_fail_site(uint64_t site)
{
    g_test_fail_site = site;
    g_test_fail_hits = 0;
}

RBL_SYSV uint64_t rbl_gc_test_fail_hits(void)
{
    return g_test_fail_hits;
}

RBL_SYSV uint64_t rbl_gc_test_container_count(void)
{
    return (uint64_t)g_objs_len;
}

RBL_SYSV uint64_t rbl_gc_test_string_count(void)
{
    return (uint64_t)g_strings_len;
}
#endif

RBL_SYSV RBLValue rbl_index_get(uint64_t tag, uint64_t payload, uint64_t kt, uint64_t kp)
{
    if (tag == TAG_LIST || tag == TAG_TUPLE) {
        if (kt != TAG_INT) {
            rbl_fail("list indices must be integers");
        }
        return rbl_list_get(TAG_LIST, payload, kp);
    }
    if (tag == TAG_DICT) {
        return rbl_dict_get(tag, payload, kt, kp);
    }
    rbl_fail("indexing needs a list, a tuple or a dictionary");
    return value(TAG_UNIT, 0);
}

RBL_SYSV RBLValue rbl_index_set(uint64_t tag, uint64_t payload, uint64_t kt, uint64_t kp, uint64_t vt, uint64_t vp)
{
    if (tag == TAG_TUPLE) {
        rbl_fail("tuples cannot be modified");
    }
    if (tag == TAG_LIST) {
        if (kt != TAG_INT) {
            rbl_fail("list indices must be integers");
        }
        return rbl_list_set(tag, payload, kp, vt, vp);
    }
    if (tag == TAG_DICT) {
        return rbl_dict_set(tag, payload, kt, kp, vt, vp);
    }
    rbl_fail("indexing needs a list or a dictionary");
    return value(TAG_UNIT, 0);
}
