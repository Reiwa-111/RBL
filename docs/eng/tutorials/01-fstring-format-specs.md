# Lesson 1: bounded f-string format specifications

RBL 0.8.0 introduced a deliberately bounded format grammar. In 0.8.1 it remains part of the language contract. A field can specify fill, alignment, minimum width, precision and a conversion type.

```rbl
func main() (
    set name = "compiler"
    set count = 42
    set ratio = 3.1415926
    print(f"{name:<12s} {count:>6d}")
    print(f"ratio={ratio:.3f}")
    print(f"hex={255:04X}")
)
```

The grammar is `[[fill]align][width][.precision][type]`. Alignment is `<`, `>` or `^`. Width is a minimum, not truncation. Float precision is limited to 0–17; width is limited to 1,000,000 code points. Invalid combinations are rejected rather than silently guessed.

Try changing the width and alignment, then run `python rbl.py check examples/v081/01_formatting_report.rbl` and `python rbl.py run ...`.

**Exercise:** produce a three-column report with left-aligned names, right-aligned integer counts and two decimal places.