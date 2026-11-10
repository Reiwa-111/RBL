#include "../runtime/rbl_abi.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    RBLRuntimeAbiDescriptor descriptor;
    uint64_t extension_field;
} RBLRuntimeAbiDescriptorExtension;

_Static_assert(sizeof(RBLRuntimeAbiDescriptorExtension) ==
               RBL_RUNTIME_ABI_DESCRIPTOR_SIZE + sizeof(uint64_t),
               "extended ABI descriptor test layout changed");

static int expect_invalid(const char *name, RBLRuntimeAbiDescriptor required,
                          RBLRuntimeAbiDescriptor runtime)
{
    const char *reason = rbl_runtime_abi_validate(&required, &runtime);
    if (!reason) {
        fprintf(stderr, "FAIL: %s was accepted\n", name);
        return 1;
    }
    printf("PASS: %s (%s)\n", name, reason);
    return 0;
}

int main(int argc, char **argv)
{
    RBLRuntimeAbiDescriptor required = RBL_RUNTIME_ABI_DESCRIPTOR_SYMBOL;
    RBLRuntimeAbiDescriptor runtime = RBL_RUNTIME_ABI_DESCRIPTOR_SYMBOL;
    int failures = 0;

    if (rbl_runtime_abi_validate(NULL, &runtime) == NULL ||
        rbl_runtime_abi_validate(&required, NULL) == NULL) {
        fputs("FAIL: missing ABI descriptors were accepted\n", stderr);
        failures++;
    } else {
        puts("PASS: missing ABI descriptors rejected");
    }

    if (argc == 2 && strcmp(argv[1], "--mismatch") == 0) {
        required.major++;
        RBL_RUNTIME_ABI_CHECK_SYMBOL(&required);
        return 99;
    }

    if (rbl_runtime_abi_validate(&required, &runtime) != NULL) {
        fputs("FAIL: identical ABI descriptors were rejected\n", stderr);
        failures++;
    } else {
        puts("PASS: identical descriptor");
    }

    runtime.minor++;
    runtime.patch++;
    if (rbl_runtime_abi_validate(&required, &runtime) != NULL) {
        fputs("FAIL: newer compatible runtime was rejected\n", stderr);
        failures++;
    } else {
        puts("PASS: newer minor/patch runtime");
    }

    runtime = RBL_RUNTIME_ABI_DESCRIPTOR_SYMBOL;
    runtime.patch++;
    if (rbl_runtime_abi_validate(&required, &runtime) != NULL) {
        fputs("FAIL: newer compatible patch runtime was rejected\n", stderr);
        failures++;
    } else {
        puts("PASS: newer patch runtime");
    }

    runtime = RBL_RUNTIME_ABI_DESCRIPTOR_SYMBOL;
    runtime.features |= (UINT64_C(1) << 63);
    if (rbl_runtime_abi_validate(&required, &runtime) != NULL) {
        fputs("FAIL: additional runtime feature was rejected\n", stderr);
        failures++;
    } else {
        puts("PASS: additional runtime feature bits accepted");
    }

    runtime = RBL_RUNTIME_ABI_DESCRIPTOR_SYMBOL;
    {
        RBLRuntimeAbiDescriptor newer_patch_required = required;
        newer_patch_required.patch++;
        if (rbl_runtime_abi_validate(&newer_patch_required, &runtime) != NULL) {
            fputs("FAIL: older compatible patch runtime was rejected\n", stderr);
            failures++;
        } else {
            puts("PASS: older patch runtime");
        }
    }

    {
        RBLRuntimeAbiDescriptorExtension extended = {
            RBL_RUNTIME_ABI_DESCRIPTOR_SYMBOL, 0
        };
        extended.descriptor.descriptor_size = (uint32_t)sizeof(extended);
        if (rbl_runtime_abi_validate(&required, &extended.descriptor) != NULL) {
            fputs("FAIL: newer descriptor extension was rejected\n", stderr);
            failures++;
        } else {
            puts("PASS: newer compatible descriptor extension");
        }
    }

    runtime = RBL_RUNTIME_ABI_DESCRIPTOR_SYMBOL;
    {
        RBLRuntimeAbiDescriptor newer_required = required;
        newer_required.minor++;
        failures += expect_invalid("older minor", newer_required, runtime);
    }
    {
        RBLRuntimeAbiDescriptor newer_required = required;
        newer_required.descriptor_size += 8;
        failures += expect_invalid("older descriptor layout", newer_required, runtime);
    }
    {
        RBLRuntimeAbiDescriptor truncated_required = required;
        truncated_required.descriptor_size--;
        failures += expect_invalid("truncated compiler descriptor", truncated_required, runtime);
    }
    runtime = RBL_RUNTIME_ABI_DESCRIPTOR_SYMBOL;
    runtime.major++;
    failures += expect_invalid("different major", required, runtime);
    runtime = RBL_RUNTIME_ABI_DESCRIPTOR_SYMBOL;
    runtime.value_size++;
    failures += expect_invalid("different value layout", required, runtime);
    runtime = RBL_RUNTIME_ABI_DESCRIPTOR_SYMBOL;
    runtime.pointer_size++;
    failures += expect_invalid("different pointer size", required, runtime);
    runtime = RBL_RUNTIME_ABI_DESCRIPTOR_SYMBOL;
    runtime.calling_convention++;
    failures += expect_invalid("different calling convention", required, runtime);
    runtime = RBL_RUNTIME_ABI_DESCRIPTOR_SYMBOL;
    runtime.features &= ~RBL_RUNTIME_ABI_FEATURE_CONTAINERS;
    failures += expect_invalid("missing required feature", required, runtime);
    runtime = RBL_RUNTIME_ABI_DESCRIPTOR_SYMBOL;
    runtime.features &= ~RBL_RUNTIME_ABI_FEATURE_GC_MANAGED_STRINGS;
    failures += expect_invalid("missing managed-string ownership feature", required, runtime);
    runtime = RBL_RUNTIME_ABI_DESCRIPTOR_SYMBOL;
    runtime.features &= ~RBL_RUNTIME_ABI_FEATURE_FSTRING_FORMAT;
    failures += expect_invalid("missing f-string formatting feature", required, runtime);
    runtime = RBL_RUNTIME_ABI_DESCRIPTOR_SYMBOL;
    runtime.magic++;
    failures += expect_invalid("invalid descriptor magic", required, runtime);
    {
        RBLRuntimeAbiDescriptor invalid_required = required;
        invalid_required.magic++;
        failures += expect_invalid("invalid compiler descriptor magic", invalid_required,
                                   RBL_RUNTIME_ABI_DESCRIPTOR_SYMBOL);
    }
    runtime = RBL_RUNTIME_ABI_DESCRIPTOR_SYMBOL;
    runtime.descriptor_size--;
    failures += expect_invalid("truncated descriptor", required, runtime);

    return failures ? 1 : 0;
}
