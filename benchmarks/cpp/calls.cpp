// Same workload as benchmarks/python/calls.py. Runtime count and noinline keep
// the function calls observable in the optimized executable.
#include <cstdint>
#include <cstdlib>

__attribute__((noinline)) static std::int64_t inc(std::int64_t x)
{
    return x + 1;
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    const std::int64_t n = std::strtoll(argv[1], nullptr, 10);
    std::int64_t x = 0;
    for (std::int64_t i = 0; i < n; i++) {
        x = inc(x);
    }
    if (x != n) {
        return 1;
    }
    return 0;
}
