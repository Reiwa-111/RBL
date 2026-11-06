/* Same workload as benchmarks/python/calls.py and benchmarks/rbl_bench_calls.rbl:
   10M calls to a one-line function. Runtime count and noinline preserve the
   calls under -O2 instead of folding the result into x = n. */
#include <stdlib.h>

__attribute__((noinline)) static long long inc(long long x)
{
    return x + 1;
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    long long n = strtoll(argv[1], NULL, 10);
    long long x = 0;
    for (long long i = 0; i < n; i++) {
        x = inc(x);
    }
    if (x != n) {
        return 1;
    }
    return 0;
}
