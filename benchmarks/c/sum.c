/* Same workload as benchmarks/python/sum.py and benchmarks/rbl_bench_sum.rbl:
   100M additions, with the result checked so nothing can be optimised away. */
#include <stdio.h>

int main(void)
{
    long long n = 100000000LL;
    long long s = 0;
    for (long long i = 0; i <= n; i++) {
        s += i;
    }
    if (s != 5000000050000000LL) {
        return 1;
    }
    return 0;
}
