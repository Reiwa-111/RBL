#include "../runtime/rbl_abi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern RBL_ABI_CALL void rbl_gc_run(void);
extern RBL_ABI_CALL RBLRuntimeValue rbl_list_new(void);
extern RBL_ABI_CALL RBLRuntimeValue rbl_list_push(uint64_t, uint64_t, uint64_t, uint64_t);
extern RBL_ABI_CALL RBLRuntimeValue rbl_list_get(uint64_t, uint64_t, uint64_t);
extern RBL_ABI_CALL RBLRuntimeValue rbl_list_len(uint64_t, uint64_t);
extern RBL_ABI_CALL RBLRuntimeValue rbl_dict_new(void);
extern RBL_ABI_CALL RBLRuntimeValue rbl_dict_set(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern RBL_ABI_CALL RBLRuntimeValue rbl_dict_get(uint64_t, uint64_t, uint64_t, uint64_t);

typedef struct {
    uint64_t kt, kp, vt, vp, used;
} ProbeDictEntry;

RBL_ABI_CALL void rbl_fail(const char *message)
{
    fprintf(stderr, "runtime probe failure: %s\n", message);
    exit(2);
}

RBL_ABI_CALL RBLRuntimeValue rbl_str(uint64_t tag, uint64_t payload)
{
    RBLRuntimeValue result = {tag, payload};
    return result;
}

RBL_ABI_CALL RBLRuntimeValue rbl_eq(uint64_t t1, uint64_t p1,
                                    uint64_t t2, uint64_t p2)
{
    if (t1 == RBL_TAG_STRING && t2 == RBL_TAG_STRING) {
        RBLRuntimeValue result = {
            RBL_TAG_BOOL,
            strcmp((const char *)(uintptr_t)p1, (const char *)(uintptr_t)p2) == 0 ? 1u : 0u
        };
        return result;
    }
    RBLRuntimeValue result = {RBL_TAG_BOOL, (t1 == t2 && p1 == p2) ? 1u : 0u};
    return result;
}

#if defined(__GNUC__)
__attribute__((noinline))
#endif
static void allocate_discarded_string(void)
{
    char *discarded = rbl_string_alloc(UINT64_C(2) * 1024u * 1024u);
    memset(discarded, 'x', 2u * 1024u * 1024u);
}

#if defined(__GNUC__)
__attribute__((noinline))
#endif
static void scrub_stack(void)
{
    volatile uintptr_t words[8192];
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        words[i] = 0;
    }
}

static void verify_item_string(RBLRuntimeValue list, uint64_t index, const char *expected)
{
    RBLRuntimeValue item = rbl_list_get(list.tag, list.payload, index);
    if (item.tag != RBL_TAG_STRING ||
        strcmp((const char *)(uintptr_t)item.payload, expected) != 0) {
        fputs("FAIL: list string changed across failed collection\n", stderr);
        exit(1);
    }
}

static void verify_nested_item_string(RBLRuntimeValue list, uint64_t index,
                                      const char *expected)
{
    RBLRuntimeValue nested = rbl_list_get(list.tag, list.payload, index);
    if (nested.tag != RBL_TAG_LIST) {
        fputs("FAIL: nested container root was reclaimed or corrupted\n", stderr);
        exit(1);
    }
    verify_item_string(nested, 0, expected);
}

#if defined(__GNUC__)
__attribute__((noinline))
#endif
static void verify_native_stack_borrow(char **caller_root)
{
    char * volatile stack_root = *caller_root;
    *caller_root = NULL;
    rbl_gc_run();
    if (strcmp((const char *)stack_root, "native-stack-borrow") != 0) {
        fputs("FAIL: native stack-borrowed string did not survive collection\n", stderr);
        exit(1);
    }
}

int main(int argc, char **argv)
{
    char * volatile rooted;
    char * volatile child;
    volatile RBLRuntimeValue list;
    volatile RBLRuntimeValue capacity_list;
    volatile RBLRuntimeValue dict;
    RBLRuntimeValue item;
    RBLRuntimeValue fanout;
    const uint64_t expected_live_bytes = (uint64_t)(2u * sizeof(uint64_t) + 32u) +
                                         (uint64_t)(2u * sizeof(uint64_t) + 24u) +
                                         2u * (uint64_t)(2u * sizeof(uint64_t) + 16u);

    rbl_gc_init((uint64_t)(uintptr_t)__builtin_frame_address(0));
    if (argc == 2 && strcmp(argv[1], "--string-alloc-oom") == 0) {
        rbl_gc_test_fail_site(RBL_GC_TEST_FAIL_STRING_PAYLOAD);
        (void)rbl_string_alloc(8);
        fputs("FAIL: string allocation unexpectedly survived injected malloc failure\n", stderr);
        return 1;
    }
    if (argc == 2 && strcmp(argv[1], "--registry-init-oom") == 0) {
        rbl_gc_test_fail_site(RBL_GC_TEST_FAIL_STRING_REGISTRY);
        (void)rbl_string_alloc(8);
        fputs("FAIL: string allocation unexpectedly survived registry initialization failure\n", stderr);
        return 1;
    }
    if (argc == 2 && strcmp(argv[1], "--container-registry-init-oom") == 0) {
        rbl_gc_test_fail_site(RBL_GC_TEST_FAIL_CONTAINER_REGISTRY);
        (void)rbl_list_new();
        fputs("FAIL: container allocation unexpectedly survived registry initialization failure\n", stderr);
        return 1;
    }
    char *native_borrowed = rbl_string_alloc(32);
    strcpy(native_borrowed, "native-stack-borrow");
    verify_native_stack_borrow(&native_borrowed);
    if (native_borrowed != NULL) {
        fputs("FAIL: native borrow probe did not clear the caller's root\n", stderr);
        return 1;
    }

    capacity_list = rbl_list_new();
    uint64_t list_bytes_before_growth = rbl_gc_test_container_bytes();
    for (uint64_t i = 0; i < 5; i++) {
        capacity_list = rbl_list_push(capacity_list.tag, capacity_list.payload, RBL_TAG_INT, i);
    }
    if (rbl_gc_test_container_bytes() - list_bytes_before_growth !=
        4u * 2u * sizeof(uint64_t)) {
        fputs("FAIL: list capacity growth was not reflected in GC heap accounting\n", stderr);
        return 1;
    }

    dict = rbl_dict_new();
    char * volatile dict_key = rbl_string_alloc(16);
    char * volatile dict_value = rbl_string_alloc(16);
    strcpy((char *)dict_key, "dict-key");
    strcpy((char *)dict_value, "dict-value");
    dict = rbl_dict_set(dict.tag, dict.payload, RBL_TAG_STRING,
                        (uint64_t)(uintptr_t)dict_key, RBL_TAG_STRING,
                        (uint64_t)(uintptr_t)dict_value);
    dict_key = NULL;
    dict_value = NULL;
    uint64_t dict_bytes_before_growth = rbl_gc_test_container_bytes();
    for (uint64_t i = 0; i < 5; i++) {
        dict = rbl_dict_set(dict.tag, dict.payload, RBL_TAG_INT, i, RBL_TAG_INT, i);
    }
    if (rbl_gc_test_container_bytes() - dict_bytes_before_growth !=
        8u * sizeof(ProbeDictEntry)) {
        fputs("FAIL: dictionary capacity growth was not reflected in GC heap accounting\n", stderr);
        return 1;
    }

    rooted = rbl_string_alloc(32);
    strcpy((char *)rooted, "rooted-string");

    list = rbl_list_new();
    child = rbl_string_alloc(24);
    strcpy(child, "container-string");
    list = rbl_list_push(list.tag, list.payload, RBL_TAG_STRING,
                         (uint64_t)(uintptr_t)child);
    child = NULL;

    allocate_discarded_string();
    scrub_stack();
    uint64_t container_bytes_before_gc = rbl_gc_test_container_bytes();
    /* Compaction OOM happens after sweep; the old string table must remain valid. */
    rbl_gc_test_fail_site(RBL_GC_TEST_FAIL_STRING_REGISTRY);
    rbl_gc_run();
    if (rbl_gc_test_fail_hits() != 1 ||
        rbl_gc_test_container_bytes() != container_bytes_before_gc) {
        fputs("FAIL: GC recomputed incorrect live container heap accounting\n", stderr);
        return 1;
    }
    if (rbl_list_len(capacity_list.tag, capacity_list.payload).payload != 5) {
        fputs("FAIL: list root was not retained across collection\n", stderr);
        return 1;
    }

    item = rbl_list_get(list.tag, list.payload, 0);
    if (strcmp((const char *)(uintptr_t)rooted, "rooted-string") != 0 ||
        item.tag != RBL_TAG_STRING ||
        strcmp((const char *)(uintptr_t)item.payload, "container-string") != 0) {
        fputs("FAIL: a reachable string was reclaimed or corrupted\n", stderr);
        return 1;
    }
    RBLRuntimeValue dict_item = rbl_dict_get(dict.tag, dict.payload, RBL_TAG_STRING,
                                            (uint64_t)(uintptr_t)"dict-key");
    if (dict_item.tag != RBL_TAG_STRING ||
        strcmp((const char *)(uintptr_t)dict_item.payload, "dict-value") != 0) {
        fputs("FAIL: dictionary key/value strings were not retained by the container graph\n", stderr);
        return 1;
    }
    if (rbl_gc_test_string_bytes() >= UINT64_C(1024) * 1024u) {
        fprintf(stderr, "FAIL: unreachable string storage was not reclaimed (%llu bytes live)\n",
                (unsigned long long)rbl_gc_test_string_bytes());
        return 1;
    }
    if (rbl_gc_test_string_bytes() != expected_live_bytes) {
        fprintf(stderr, "FAIL: unexpected live string byte count: got %llu, expected %llu\n",
                (unsigned long long)rbl_gc_test_string_bytes(),
                (unsigned long long)expected_live_bytes);
        return 1;
    }
    /* A later collection retries compaction without losing strings in the old table. */
    rbl_gc_run();
    if (rbl_gc_test_string_bytes() != expected_live_bytes ||
        rbl_gc_test_string_count() != 4) {
        fputs("FAIL: string registry compaction retry lost live strings\n", stderr);
        return 1;
    }
    if (strcmp((const char *)(uintptr_t)rooted, "rooted-string") != 0) {
        fputs("FAIL: rooted string was lost after string registry compaction retry\n", stderr);
        return 1;
    }

    /* Force mark-stack growth to fail. The failed collection must not sweep. */
    fanout = rbl_list_new();
    for (uint64_t i = 0; i < 300; i++) {
        RBLRuntimeValue leaf = rbl_list_new();
        leaf = rbl_list_push(leaf.tag, leaf.payload, RBL_TAG_STRING,
                             (uint64_t)(uintptr_t)rooted);
        fanout = rbl_list_push(fanout.tag, fanout.payload, leaf.tag, leaf.payload);
    }
    uint64_t containers_before = rbl_gc_test_container_count();
    uint64_t container_bytes_with_garbage = rbl_gc_test_container_bytes();
    allocate_discarded_string();
    scrub_stack();
    uint64_t bytes_with_garbage = rbl_gc_test_string_bytes();
    rbl_gc_test_fail_site(RBL_GC_TEST_FAIL_MARK_STACK);
    rbl_gc_run();
    if (rbl_gc_test_fail_hits() != 1 ||
        rbl_gc_test_string_bytes() != bytes_with_garbage ||
        rbl_gc_test_container_count() != containers_before ||
        rbl_gc_test_container_bytes() != container_bytes_with_garbage) {
        fputs("FAIL: mark-stack OOM partially swept the heap\n", stderr);
        return 1;
    }
    verify_nested_item_string(fanout, 299, "rooted-string");

    rbl_gc_test_fail_site(RBL_GC_TEST_FAIL_SURVIVORS);
    rbl_gc_run();
    if (rbl_gc_test_fail_hits() != 1 ||
        rbl_gc_test_string_bytes() != bytes_with_garbage ||
        rbl_gc_test_container_count() != containers_before ||
        rbl_gc_test_container_bytes() != container_bytes_with_garbage) {
        fputs("FAIL: survivor-vector OOM partially swept the heap\n", stderr);
        return 1;
    }
    verify_nested_item_string(fanout, 0, "rooted-string");

    /* Survivor allocation succeeds, but replacement-registry allocation fails.
       This must also abort before any object or string is swept. */
    rbl_gc_test_fail_site(RBL_GC_TEST_FAIL_REBUILD_REGISTRY);
    rbl_gc_run();
    if (rbl_gc_test_fail_hits() != 1 ||
        rbl_gc_test_string_bytes() != bytes_with_garbage ||
        rbl_gc_test_container_count() != containers_before ||
        rbl_gc_test_container_bytes() != container_bytes_with_garbage) {
        fputs("FAIL: replacement-registry OOM partially swept the heap\n", stderr);
        return 1;
    }
    verify_nested_item_string(fanout, 299, "rooted-string");

    scrub_stack();
    rbl_gc_run();
    uint64_t strings_after_retry = rbl_gc_test_string_bytes();
    if (bytes_with_garbage < strings_after_retry ||
        bytes_with_garbage - strings_after_retry < UINT64_C(2) * 1024u * 1024u) {
        fprintf(stderr, "FAIL: successful retry did not reclaim the discarded allocation (%llu -> %llu bytes)\n",
                (unsigned long long)bytes_with_garbage,
                (unsigned long long)strings_after_retry);
        return 1;
    }
    if (rbl_list_len(fanout.tag, fanout.payload).payload != 300) {
        fputs("FAIL: container graph was damaged by failed collection\n", stderr);
        return 1;
    }

    /* Fill the string registry to its 75% growth boundary and fail rehash once.
       The insertion must remain valid in the old table; the next insertion retries. */
    RBLRuntimeValue registry_roots = rbl_list_new();
    while (rbl_gc_test_string_count() < 767) {
        char *s = rbl_string_alloc(8);
        strcpy(s, "entry");
        registry_roots = rbl_list_push(registry_roots.tag, registry_roots.payload,
                                       RBL_TAG_STRING, (uint64_t)(uintptr_t)s);
    }
    rbl_gc_test_fail_site(RBL_GC_TEST_FAIL_STRING_REGISTRY);
    char *rehash_retry_a = rbl_string_alloc(8);
    strcpy(rehash_retry_a, "retry-a");
    registry_roots = rbl_list_push(registry_roots.tag, registry_roots.payload,
                                   RBL_TAG_STRING, (uint64_t)(uintptr_t)rehash_retry_a);
    char *rehash_retry_b = rbl_string_alloc(8);
    strcpy(rehash_retry_b, "retry-b");
    registry_roots = rbl_list_push(registry_roots.tag, registry_roots.payload,
                                   RBL_TAG_STRING, (uint64_t)(uintptr_t)rehash_retry_b);
    if (rbl_gc_test_fail_hits() != 1) {
        fputs("FAIL: string registry rehash failpoint was not reached exactly once\n", stderr);
        return 1;
    }
    rbl_gc_run();
    verify_item_string(registry_roots, 0, "entry");
    verify_item_string(registry_roots, rbl_list_len(registry_roots.tag, registry_roots.payload).payload - 1,
                       "retry-b");

    /* Repeat the one-shot rehash failure for the container registry. */
    RBLRuntimeValue container_roots = rbl_list_new();
    while (rbl_gc_test_container_count() < 767) {
        RBLRuntimeValue leaf = rbl_list_new();
        leaf = rbl_list_push(leaf.tag, leaf.payload, RBL_TAG_INT, 42);
        container_roots = rbl_list_push(container_roots.tag, container_roots.payload,
                                        leaf.tag, leaf.payload);
    }
    rbl_gc_test_fail_site(RBL_GC_TEST_FAIL_CONTAINER_REGISTRY);
    RBLRuntimeValue retry_leaf_a = rbl_list_new();
    retry_leaf_a = rbl_list_push(retry_leaf_a.tag, retry_leaf_a.payload, RBL_TAG_INT, 43);
    container_roots = rbl_list_push(container_roots.tag, container_roots.payload,
                                    retry_leaf_a.tag, retry_leaf_a.payload);
    RBLRuntimeValue retry_leaf_b = rbl_list_new();
    retry_leaf_b = rbl_list_push(retry_leaf_b.tag, retry_leaf_b.payload, RBL_TAG_INT, 44);
    container_roots = rbl_list_push(container_roots.tag, container_roots.payload,
                                    retry_leaf_b.tag, retry_leaf_b.payload);
    if (rbl_gc_test_fail_hits() != 1) {
        fputs("FAIL: container registry rehash failpoint was not reached exactly once\n", stderr);
        return 1;
    }
    rbl_gc_run();
    RBLRuntimeValue last_leaf = rbl_list_get(container_roots.tag, container_roots.payload,
                                             rbl_list_len(container_roots.tag, container_roots.payload).payload - 1);
    RBLRuntimeValue last_value = rbl_list_get(last_leaf.tag, last_leaf.payload, 0);
    if (last_value.tag != RBL_TAG_INT || last_value.payload != 44) {
        fputs("FAIL: container registry rehash retry lost a reachable object\n", stderr);
        return 1;
    }

    /* Runtime-internal explicit release is exact and idempotent for a managed base pointer. */
    char *released = rbl_string_alloc(16);
    strcpy(released, "release-me");
    uint64_t bytes_before_release = rbl_gc_test_string_bytes();
    rbl_string_free(released);
    rbl_string_free(released);
    if (rbl_gc_test_string_bytes() + sizeof(uint64_t) * 2 + 16 != bytes_before_release) {
        fputs("FAIL: explicit managed-string release accounting is incorrect\n", stderr);
        return 1;
    }

    puts("PASS: string roots, container roots, reclamation, OOM rollback, registry retries and native release");
    return 0;
}
