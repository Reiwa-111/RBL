n = 100_000_000
x = 0
for i in range(n):
    if i < 50_000_000:
        x += 1
    else:
        x -= 1
if x != 0:
    raise SystemExit(1)
