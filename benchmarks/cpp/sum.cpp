// Same workload as the RBL/Python/ASM versions. A runtime loop bound prevents
// -O2 from replacing the complete accumulation loop with a constant.
#include <cstdint>
#include <cstdlib>

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    const std::int64_t n = std::strtoll(argv[1], nullptr, 10);
    std::int64_t s = 0;
    for (std::int64_t i = 0; i <= n; i++) {
        s += i;
    }
    if (s != 5000000050000000LL) {
        return 1;
    }
    return 0;
}
