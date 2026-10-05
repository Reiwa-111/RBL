n = 100_000_000
s = 0
for i in range(n + 1):
    s += i
if s != 5_000_000_050_000_000:
    raise SystemExit(1)
