/* Shared container runtime for RBL 0.8.
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
 * Memory: block 2.3 of the roadmap adds reclamation. Until then lists are not
 * freed, which is a deliberate, documented limitation rather than an oversight.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

enum { TAG_UNIT = 0, TAG_INT = 1, TAG_FLOAT = 2, TAG_STR = 3, TAG_BOOL = 4, TAG_LIST = 5, TAG_DICT = 6, TAG_TUPLE = 7 };

#if defined(_WIN32)
#define RBL_SYSV __attribute__((sysv_abi))
#else
#define RBL_SYSV
#endif

typedef struct {
    uint64_t tag, payload;
} RBLValue;

/* Provided by the platform runtime (asm on Linux, C on Windows). */
RBL_SYSV extern void rbl_fail(const char *msg);
RBL_SYSV extern RBLValue rbl_str(uint64_t tag, uint64_t payload);
RBL_SYSV extern RBLValue rbl_eq(uint64_t t1, uint64_t p1, uint64_t t2, uint64_t p2);

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

static RBLValue value(uint64_t tag, uint64_t payload)
{
    RBLValue v;
    v.tag = tag;
    v.payload = payload;
    return v;
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
        l->cap *= 2;
        uint64_t *grown = (uint64_t *)realloc(l->items, l->cap * 2 * sizeof(uint64_t));
        if (!grown) {
            list_oom();
        }
        l->items = grown;
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
        rbl_fail("expected a list");
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
    RBLList *l = as_list_checked(tag, payload);
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
    return value(TAG_STR, (uint64_t)(uintptr_t)out);
}

/* Deep equality: nested lists compare element-wise, everything else through the
   ordinary ==, so floats and strings behave exactly as they do outside lists. */
RBL_SYSV RBLValue rbl_list_eq(uint64_t t1, uint64_t p1, uint64_t t2, uint64_t p2);
RBL_SYSV RBLValue rbl_tuple_eq(uint64_t t1, uint64_t p1, uint64_t t2, uint64_t p2);

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
    return value(TAG_STR, (uint64_t)(uintptr_t)out);
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
/* Roots are the machine stack (from the current frame up to the        */
/* address captured by rbl_gc_init in main) plus everything reachable   */
/* from it. Any word that equals a live container pointer counts as a   */
/* root, so temporary values held only in registers or on the stack are */
/* safe and the code generator needs no ownership analysis at all.       */
/* ------------------------------------------------------------------ */

static void **g_objs;          /* registry of live objects, open addressing */
static size_t g_objs_cap, g_objs_len, g_objs_tombs;
static char *g_stack_base;
static size_t g_bytes, g_threshold = 1u << 20;
static void **g_mark_stack;
static size_t g_mark_cap, g_mark_len;
static void **g_survivors;
static size_t g_survivors_cap;

#define GC_EMPTY ((void *)0)
#define GC_TOMB  ((void *)1)

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
    void **slots = (void **)calloc(cap, sizeof(void *));
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
        objs_rehash(g_objs_cap ? g_objs_cap * 2 : 1024);
    }
    size_t j = ptr_hash(p) % g_objs_cap;
    while (g_objs[j] && g_objs[j] != GC_TOMB) {
        if (g_objs[j] == p) {
            return;
        }
        j = (j + 1) % g_objs_cap;
    }
    if (g_objs[j] == GC_TOMB) {
        g_objs_tombs--;
    }
    g_objs[j] = p;
    g_objs_len++;
}

static int objs_has(const void *p)
{
    if (!g_objs_cap || !p || p == GC_TOMB) {
        return 0;
    }
    size_t j = ptr_hash(p) % g_objs_cap;
    for (;;) {
        void *s = g_objs[j];
        if (!s) {
            return 0;
        }
        if (s == p) {
            return 1;
        }
        j = (j + 1) % g_objs_cap;
    }
}

static void gc_push(void *o)
{
    if (g_mark_len == g_mark_cap) {
        size_t cap = g_mark_cap ? g_mark_cap * 2 : 256;
        void **grown = (void **)realloc(g_mark_stack, cap * sizeof(void *));
        if (!grown) {
            return;
        }
        g_mark_stack = grown;
        g_mark_cap = cap;
    }
    g_mark_stack[g_mark_len++] = o;
}

static void gc_push_if_container(uint64_t tag, uint64_t payload)
{
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
    char *lo = (char *)&lo;
    char *hi = g_stack_base;
    if (lo > hi) {
        char *t = lo;
        lo = hi;
        hi = t;
    }
    for (char *p = lo; p + (long)sizeof(void *) <= hi; p += sizeof(void *)) {
        void *cand;
        memcpy(&cand, p, sizeof(void *));
        if (objs_has(cand) && !((RBLObj *)cand)->mark) {
            ((RBLObj *)cand)->mark = 1;
            gc_push(cand);
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
    gc_mark_roots();
    while (g_mark_len) {
        gc_scan_children(g_mark_stack[--g_mark_len]);
    }
    size_t live = 0;
    for (size_t i = 0; i < g_objs_cap; i++) {
        void *p = g_objs[i];
        if (!p || p == GC_TOMB) {
            continue;
        }
        if (((RBLObj *)p)->mark) {
            if (live == g_survivors_cap) {
                size_t cap = g_survivors_cap ? g_survivors_cap * 2 : 256;
                void **grown = (void **)realloc(g_survivors, cap * sizeof(void *));
                if (!grown) {
                    break;
                }
                g_survivors = grown;
                g_survivors_cap = cap;
            }
            g_survivors[live++] = p;
            ((RBLObj *)p)->mark = 0;
        } else {
            gc_free_obj(p);
        }
    }
    /* Rebuild the registry from the survivors: rebuilding (instead of deleting
       in place) keeps the probe sequences valid. */
    free(g_objs);
    g_objs = NULL;
    g_objs_cap = g_objs_len = g_objs_tombs = 0;
    g_bytes = 0;
    for (size_t i = 0; i < live; i++) {
        g_bytes += gc_live_bytes(g_survivors[i]);
        objs_add(g_survivors[i]);
    }
    g_threshold = g_bytes * 2 + (1u << 20);
}

static void rbl_gc_register(void *obj, uint64_t tag, size_t bytes)
{
    ((RBLObj *)obj)->tag = tag;
    ((RBLObj *)obj)->mark = 0;
    objs_add(obj);
    g_bytes += bytes;
    if (g_stack_base && g_bytes > g_threshold) {
        gc_collect();
    }
}

/* Called from the prologue of every generated function; only the first call (from
   main) matters, and it must run before any allocation. */
RBL_SYSV void rbl_gc_init(void)
{
    if (!g_stack_base) {
        g_stack_base = (char *)__builtin_frame_address(0);
    }
}

/* Explicit collection, exposed for tests and the runtime's own use. */
RBL_SYSV void rbl_gc_run(void)
{
    if (g_stack_base) {
        gc_collect();
    }
}

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
