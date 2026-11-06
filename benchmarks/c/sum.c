/* Same workload as benchmarks/python/sum.py and benchmarks/rbl_bench_sum.rbl:
   100M additions. The iteration count is supplied at runtime so -O2 cannot
   replace the loop with a constant result. */
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    long long n = strtoll(argv[1], NULL, 10);
    long long s = 0;
    for (long long i = 0; i <= n; i++) {
        s += i;
    }
    if (s != n * (n + 1) / 2) {
        return 1;
    }
    return 0;
}
