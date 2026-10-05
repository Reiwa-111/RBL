/* Same workload as benchmarks/python/calls.py and benchmarks/rbl_bench_calls.rbl:
   10M calls to a one-line function. */
#include <stdio.h>

static long long inc(long long x)
{
    return x + 1;
}

int main(void)
{
    long long n = 10000000LL;
    long long x = 0;
    for (long long i = 0; i < n; i++) {
        x = inc(x);
    }
    if (x != n) {
        return 1;
    }
    return 0;
}
