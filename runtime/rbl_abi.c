#include "rbl_abi.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

_Static_assert(sizeof(RBLRuntimeAbiDescriptor) == RBL_RUNTIME_ABI_DESCRIPTOR_SIZE,
               "Runtime ABI descriptor layout changed");
_Static_assert(offsetof(RBLRuntimeAbiDescriptor, magic) == 0,
               "Runtime ABI descriptor magic offset changed");
_Static_assert(offsetof(RBLRuntimeAbiDescriptor, features) == 8,
               "Runtime ABI descriptor features offset changed");
_Static_assert(offsetof(RBLRuntimeAbiDescriptor, descriptor_size) == 16,
               "Runtime ABI descriptor size offset changed");
_Static_assert(offsetof(RBLRuntimeAbiDescriptor, value_size) == 20,
               "Runtime ABI value size offset changed");
_Static_assert(offsetof(RBLRuntimeAbiDescriptor, pointer_size) == 24,
               "Runtime ABI pointer size offset changed");
_Static_assert(offsetof(RBLRuntimeAbiDescriptor, calling_convention) == 28,
               "Runtime ABI calling convention offset changed");
_Static_assert(offsetof(RBLRuntimeAbiDescriptor, major) == 32,
               "Runtime ABI major version offset changed");
_Static_assert(offsetof(RBLRuntimeAbiDescriptor, minor) == 34,
               "Runtime ABI minor version offset changed");
_Static_assert(offsetof(RBLRuntimeAbiDescriptor, patch) == 36,
               "Runtime ABI patch version offset changed");
_Static_assert(offsetof(RBLRuntimeAbiDescriptor, reserved) == 38,
               "Runtime ABI reserved field offset changed");
_Static_assert(sizeof(RBLRuntimeValue) == RBL_RUNTIME_ABI_VALUE_SIZE,
               "Runtime value ABI layout changed");
_Static_assert(offsetof(RBLRuntimeValue, tag) == 0,
               "Runtime value tag offset changed");
_Static_assert(offsetof(RBLRuntimeValue, payload) == 8,
               "Runtime value payload offset changed");
_Static_assert(sizeof(void *) == RBL_RUNTIME_ABI_POINTER_SIZE,
               "Runtime ABI requires 64-bit pointers");
_Static_assert(RBL_TAG_UNIT == 0 && RBL_TAG_INT == 1 && RBL_TAG_FLOAT == 2 &&
               RBL_TAG_STRING == 3 && RBL_TAG_BOOL == 4 && RBL_TAG_LIST == 5 &&
               RBL_TAG_DICTIONARY == 6 && RBL_TAG_TUPLE == 7 && RBL_TAG_NULL == 8,
               "Runtime value tag numbers changed");

const RBLRuntimeAbiDescriptor RBL_RUNTIME_ABI_DESCRIPTOR_SYMBOL = {
    RBL_RUNTIME_ABI_MAGIC,
    RBL_RUNTIME_ABI_FEATURES,
    RBL_RUNTIME_ABI_DESCRIPTOR_SIZE,
    RBL_RUNTIME_ABI_VALUE_SIZE,
    RBL_RUNTIME_ABI_POINTER_SIZE,
    RBL_RUNTIME_ABI_CALL_SYSV_AMD64,
    RBL_RUNTIME_ABI_MAJOR,
    RBL_RUNTIME_ABI_MINOR,
    RBL_RUNTIME_ABI_PATCH,
    0
};

const char *rbl_runtime_abi_validate(const RBLRuntimeAbiDescriptor *required,
                                     const RBLRuntimeAbiDescriptor *runtime)
{
    if (!required || !runtime)
        return "missing ABI descriptor";
    if (required->magic != RBL_RUNTIME_ABI_MAGIC ||
        runtime->magic != RBL_RUNTIME_ABI_MAGIC)
        return "invalid ABI descriptor magic";
    if (required->descriptor_size < RBL_RUNTIME_ABI_BASE_DESCRIPTOR_SIZE ||
        runtime->descriptor_size < required->descriptor_size)
        return "ABI descriptor is too small";
    if (runtime->major != required->major)
        return "incompatible ABI major version";
    if (runtime->minor < required->minor)
        return "runtime ABI minor version is older than required";
    if (runtime->value_size != required->value_size)
        return "tagged value layout size differs";
    if (runtime->pointer_size != required->pointer_size)
        return "pointer size differs";
    if (runtime->calling_convention != required->calling_convention)
        return "calling convention differs";
    if ((runtime->features & required->features) != required->features)
        return "runtime is missing required ABI features";
    return NULL;
}

RBL_ABI_CALL void RBL_RUNTIME_ABI_CHECK_SYMBOL(const RBLRuntimeAbiDescriptor *required)
{
    const char *reason = rbl_runtime_abi_validate(required, &RBL_RUNTIME_ABI_DESCRIPTOR_SYMBOL);
    if (reason) {
        fprintf(stderr,
                "RBL Runtime ABI mismatch: compiler requires %u.%u (value=%u, call=%u), "
                "runtime provides %u.%u.%u: %s. Rebuild the Runtime and try again.\n",
                required ? (unsigned)required->major : 0u,
                required ? (unsigned)required->minor : 0u,
                required ? required->value_size : 0u,
                required ? required->calling_convention : 0u,
                (unsigned)RBL_RUNTIME_ABI_DESCRIPTOR_SYMBOL.major,
                (unsigned)RBL_RUNTIME_ABI_DESCRIPTOR_SYMBOL.minor,
                (unsigned)RBL_RUNTIME_ABI_DESCRIPTOR_SYMBOL.patch,
                reason);
        exit(78);
    }
}
