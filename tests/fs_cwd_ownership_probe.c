#include "../runtime/rbl_abi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern RBL_ABI_CALL RBLRuntimeValue rbl_fs_cwd(void);

int main(void)
{
    RBLRuntimeValue roots[16];
    char *expected = getcwd(NULL, 0);
    if (!expected) {
        perror("getcwd before probe");
        return 1;
    }
    rbl_gc_init((uint64_t)(uintptr_t)__builtin_frame_address(0));
    uint64_t before = rbl_gc_test_string_count();
    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
        roots[i] = rbl_fs_cwd();
        if (roots[i].tag != RBL_TAG_STRING ||
            !roots[i].payload ||
            !((const char *)(uintptr_t)roots[i].payload)[0] ||
            !rbl_gc_test_string_has(roots[i].payload) ||
            strcmp((const char *)(uintptr_t)roots[i].payload, expected) != 0) {
            fputs("FAIL: rbl_fs_cwd returned an invalid string value\n", stderr);
            return 1;
        }
        if (rbl_gc_test_string_count() != before + i + 1) {
            fprintf(stderr,
                    "FAIL: cwd result was not registered as managed storage at call %zu "
                    "(count=%llu, expected=%llu)\n",
                    i + 1,
                    (unsigned long long)rbl_gc_test_string_count(),
                    (unsigned long long)(before + i + 1));
            return 1;
        }
    }
    puts("PASS: rbl.fs.cwd returns Runtime-managed strings (16/16 registered)");
    free(expected);
    return 0;
}
