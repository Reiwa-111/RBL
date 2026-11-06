// Same workload as benchmarks/python/branches.py: 100M iterations with a branch.
#include <cstdint>
#include <cstdlib>

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    const std::int64_t n = std::strtoll(argv[1], nullptr, 10);
    std::int64_t x = 0;
    for (std::int64_t i = 0; i < n; i++) {
        if (i < 50000000) {
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
