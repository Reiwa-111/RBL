/* Same workload as benchmarks/python/branches.py and rbl_bench_branches.rbl:
   100M iterations split by a data-dependent branch. */
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    long long n = strtoll(argv[1], NULL, 10);
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
