#ifndef RBL_RUNTIME_ABI_H
#define RBL_RUNTIME_ABI_H

#include <stdint.h>

/* Runtime ABI is versioned independently from the RBL language/compiler. */
#define RBL_RUNTIME_ABI_MAJOR 1u
#define RBL_RUNTIME_ABI_MINOR 2u
#define RBL_RUNTIME_ABI_PATCH 0u
#define RBL_RUNTIME_ABI_DESCRIPTOR_SYMBOL rbl_runtime_abi_v1
#define RBL_RUNTIME_ABI_CHECK_SYMBOL rbl_runtime_abi_check_v1

#define RBL_RUNTIME_ABI_MAGIC UINT64_C(0x52424c4142493031) /* "RBLABI01" */
#define RBL_RUNTIME_ABI_DESCRIPTOR_SIZE 40u
#define RBL_RUNTIME_ABI_BASE_DESCRIPTOR_SIZE 40u
#define RBL_RUNTIME_ABI_VALUE_SIZE 16u
#define RBL_RUNTIME_ABI_POINTER_SIZE 8u
#define RBL_RUNTIME_ABI_CALL_SYSV_AMD64 1u

#define RBL_RUNTIME_ABI_FEATURE_TAGGED_VALUES (UINT64_C(1) << 0)
#define RBL_RUNTIME_ABI_FEATURE_SYSV_AMD64 (UINT64_C(1) << 1)
#define RBL_RUNTIME_ABI_FEATURE_STACK_VALUE_ARRAYS (UINT64_C(1) << 2)
#define RBL_RUNTIME_ABI_FEATURE_CONTAINERS (UINT64_C(1) << 3)
#define RBL_RUNTIME_ABI_FEATURE_GC_MANAGED_STRINGS (UINT64_C(1) << 4)
#define RBL_RUNTIME_ABI_FEATURE_FSTRING_FORMAT (UINT64_C(1) << 5)
#define RBL_RUNTIME_ABI_FEATURES \
    (RBL_RUNTIME_ABI_FEATURE_TAGGED_VALUES | \
     RBL_RUNTIME_ABI_FEATURE_SYSV_AMD64 | \
     RBL_RUNTIME_ABI_FEATURE_STACK_VALUE_ARRAYS | \
     RBL_RUNTIME_ABI_FEATURE_CONTAINERS | \
     RBL_RUNTIME_ABI_FEATURE_GC_MANAGED_STRINGS | \
     RBL_RUNTIME_ABI_FEATURE_FSTRING_FORMAT)

typedef enum {
    RBL_TAG_UNIT = 0,
    RBL_TAG_INT = 1,
    RBL_TAG_FLOAT = 2,
    RBL_TAG_STRING = 3,
    RBL_TAG_BOOL = 4,
    RBL_TAG_LIST = 5,
    RBL_TAG_DICTIONARY = 6,
    RBL_TAG_TUPLE = 7,
    RBL_TAG_NULL = 8
} RBLRuntimeTag;

typedef struct {
    uint64_t tag;
    uint64_t payload;
} RBLRuntimeValue;

typedef struct {
    uint64_t magic;
    uint64_t features;
    uint32_t descriptor_size;
    uint32_t value_size;
    uint32_t pointer_size;
    uint32_t calling_convention;
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
    uint16_t reserved;
} RBLRuntimeAbiDescriptor;

#if defined(__cplusplus)
static_assert(sizeof(RBLRuntimeValue) == RBL_RUNTIME_ABI_VALUE_SIZE,
              "Runtime value ABI layout changed");
#else
_Static_assert(sizeof(RBLRuntimeValue) == RBL_RUNTIME_ABI_VALUE_SIZE,
               "Runtime value ABI layout changed");
#endif

#if defined(_WIN32)
#define RBL_ABI_CALL __attribute__((sysv_abi))
#else
#define RBL_ABI_CALL
#endif

extern const RBLRuntimeAbiDescriptor RBL_RUNTIME_ABI_DESCRIPTOR_SYMBOL;
const char *rbl_runtime_abi_validate(const RBLRuntimeAbiDescriptor *required,
                                     const RBLRuntimeAbiDescriptor *runtime);
RBL_ABI_CALL void RBL_RUNTIME_ABI_CHECK_SYMBOL(const RBLRuntimeAbiDescriptor *required);

/* Runtime-owned string storage and conservative-GC safe points. These helpers
 * are internal Runtime ABI entry points; RBL programs never free strings.
 * Native callers must not treat returned string pointers as independently
 * owned allocations. The collector scans the current thread's stack range,
 * but does not scan native heap/global storage and exposes no pin/root handles.
 * Runtime state is process-global and unsynchronized: calls must stay on the
 * one thread that entered generated RBL main; concurrent use is unsupported. */
RBL_ABI_CALL char *rbl_string_alloc(uint64_t size);
RBL_ABI_CALL void rbl_string_free(char *string);
RBL_ABI_CALL void rbl_fail(const char *message);
RBL_ABI_CALL RBLRuntimeValue rbl_str(uint64_t tag, uint64_t payload);
RBL_ABI_CALL RBLRuntimeValue rbl_eq(uint64_t left_tag, uint64_t left_payload,
                                     uint64_t right_tag, uint64_t right_payload);
RBL_ABI_CALL RBLRuntimeValue rbl_format_value(uint64_t tag, uint64_t payload,
                                               const char *spec);
RBL_ABI_CALL void rbl_gc_init(uint64_t stack_base);
RBL_ABI_CALL void rbl_gc_run(void);
RBL_ABI_CALL void rbl_gc_safepoint(void);

/* Shared container Runtime entry points called by generated RBL assembly. */
RBL_ABI_CALL RBLRuntimeValue rbl_list_new(void);
RBL_ABI_CALL RBLRuntimeValue rbl_list_push(uint64_t tag, uint64_t payload,
                                           uint64_t value_tag, uint64_t value_payload);
RBL_ABI_CALL RBLRuntimeValue rbl_list_len(uint64_t tag, uint64_t payload);
RBL_ABI_CALL RBLRuntimeValue rbl_list_get(uint64_t tag, uint64_t payload, uint64_t index);
RBL_ABI_CALL RBLRuntimeValue rbl_list_set(uint64_t tag, uint64_t payload, uint64_t index,
                                          uint64_t value_tag, uint64_t value_payload);
RBL_ABI_CALL RBLRuntimeValue rbl_list_str(uint64_t tag, uint64_t payload);
RBL_ABI_CALL RBLRuntimeValue rbl_list_eq(uint64_t left_tag, uint64_t left_payload,
                                         uint64_t right_tag, uint64_t right_payload);

RBL_ABI_CALL RBLRuntimeValue rbl_tuple_new(void);
RBL_ABI_CALL RBLRuntimeValue rbl_tuple_push(uint64_t tag, uint64_t payload,
                                            uint64_t value_tag, uint64_t value_payload);
RBL_ABI_CALL RBLRuntimeValue rbl_tuple_str(uint64_t tag, uint64_t payload);
RBL_ABI_CALL RBLRuntimeValue rbl_tuple_eq(uint64_t left_tag, uint64_t left_payload,
                                          uint64_t right_tag, uint64_t right_payload);
RBL_ABI_CALL RBLRuntimeValue rbl_tuple_len(uint64_t tag, uint64_t payload);
RBL_ABI_CALL RBLRuntimeValue rbl_tuple_get(uint64_t tag, uint64_t payload, uint64_t index);

RBL_ABI_CALL RBLRuntimeValue rbl_dict_new(void);
RBL_ABI_CALL RBLRuntimeValue rbl_dict_set(uint64_t tag, uint64_t payload,
                                         uint64_t key_tag, uint64_t key_payload,
                                         uint64_t value_tag, uint64_t value_payload);
RBL_ABI_CALL RBLRuntimeValue rbl_dict_get(uint64_t tag, uint64_t payload,
                                         uint64_t key_tag, uint64_t key_payload);
RBL_ABI_CALL RBLRuntimeValue rbl_dict_has(uint64_t tag, uint64_t payload,
                                         uint64_t key_tag, uint64_t key_payload);
RBL_ABI_CALL RBLRuntimeValue rbl_dict_len(uint64_t tag, uint64_t payload);
RBL_ABI_CALL RBLRuntimeValue rbl_dict_str(uint64_t tag, uint64_t payload);
RBL_ABI_CALL RBLRuntimeValue rbl_dict_eq(uint64_t left_tag, uint64_t left_payload,
                                         uint64_t right_tag, uint64_t right_payload);

RBL_ABI_CALL RBLRuntimeValue rbl_is(uint64_t left_tag, uint64_t left_payload,
                                    uint64_t right_tag, uint64_t right_payload);
RBL_ABI_CALL RBLRuntimeValue rbl_contains(uint64_t tag, uint64_t payload,
                                          uint64_t key_tag, uint64_t key_payload);
RBL_ABI_CALL RBLRuntimeValue rbl_index_get(uint64_t tag, uint64_t payload,
                                           uint64_t key_tag, uint64_t key_payload);
RBL_ABI_CALL RBLRuntimeValue rbl_index_set(uint64_t tag, uint64_t payload,
                                           uint64_t key_tag, uint64_t key_payload,
                                           uint64_t value_tag, uint64_t value_payload);

#ifdef RBL_GC_TESTING
enum {
    RBL_GC_TEST_FAIL_STRING_PAYLOAD = 1,
    RBL_GC_TEST_FAIL_STRING_REGISTRY = 2,
    RBL_GC_TEST_FAIL_CONTAINER_REGISTRY = 3,
    RBL_GC_TEST_FAIL_MARK_STACK = 4,
    RBL_GC_TEST_FAIL_SURVIVORS = 5,
    RBL_GC_TEST_FAIL_REBUILD_REGISTRY = 6
};
RBL_ABI_CALL void rbl_gc_test_fail_site(uint64_t site);
RBL_ABI_CALL uint64_t rbl_gc_test_fail_hits(void);
RBL_ABI_CALL uint64_t rbl_gc_test_string_bytes(void);
RBL_ABI_CALL uint64_t rbl_gc_test_container_bytes(void);
RBL_ABI_CALL uint64_t rbl_gc_test_container_count(void);
RBL_ABI_CALL uint64_t rbl_gc_test_string_count(void);
#endif

#endif
