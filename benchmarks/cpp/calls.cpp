// Same workload as benchmarks/python/calls.py: 10M calls to a one-line function.
#include <cstdint>
#include <cstdio>

static std::int64_t inc(std::int64_t x)
{
    return x + 1;
}

int main()
{
    const std::int64_t n = 10000000;
    std::int64_t x = 0;
    for (std::int64_t i = 0; i < n; i++) {
        x = inc(x);
    }
    if (x != n) {
        return 1;
    }
    return 0;
}
