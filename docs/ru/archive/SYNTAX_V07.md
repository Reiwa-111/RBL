# last verified: v0.7.2 | 2026.11.06 | DOCS UPDATE

## Historical syntax note: RBL 0.7

This page preserves a short description of the parenthesized-block transition introduced in the RBL 0.7 development line. It is historical context, not the current language reference. Consult [`RBL_QUICK_REFERENCE.md`](RBL_QUICK_REFERENCE.md) for the syntax accepted by the current compiler.

## Canonical block form

```rbl
func add(a int, b int) int (
    return a + b
)

func main() (
    if (add(1, 2) == 3) (
        print("ok")
    )
)
```

The condition/expression parentheses and the block parentheses are separate pairs. Current source uses parentheses for blocks; curly braces are used for dictionary literals and are not a block delimiter.

## Range syntax

```rbl
for (i in 0..5) (
    print(i)
)

for (i in 0..=5) (
    print(i)
)
```

`..` is exclusive and `..=` is inclusive.
