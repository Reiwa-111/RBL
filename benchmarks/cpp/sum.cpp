// Same workload as benchmarks/c/sum.cpp counterpart and the RBL/Python/ASM
// versions: 100M additions with a checked result.
#include <cstdint>
#include <cstdio>

int main()
{
    const std::int64_t n = 100000000;
    std::int64_t s = 0;
    for (std::int64_t i = 0; i <= n; i++) {
        s += i;
    }
    if (s != 5000000050000000LL) {
        return 1;
    }
    return 0;
}
