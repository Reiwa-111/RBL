n = 10_000_000

def inc(x):
    return x + 1

x = 0
for _ in range(n):
    x = inc(x)
if x != n:
    raise SystemExit(1)
