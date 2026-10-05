/* Same workload as benchmarks/python/branches.py and rbl_bench_branches.rbl:
   100M iterations split by a data-dependent branch. */
#include <stdio.h>

int main(void)
{
    long long n = 100000000LL;
    long long x = 0;
    for (long long i = 0; i < n; i++) {
        if (i < 50000000LL) {
            x += 1;
        } else {
            x -= 1;
        }
    }
    if (x != 0) {
        return 1;
    }
    return 0;
}
