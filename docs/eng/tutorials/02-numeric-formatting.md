# Lesson 2: integer bases and floating-point formats

The `d`, `b`, `o`, `x` and `X` conversions format integers in decimal, binary, octal and hexadecimal. They do not add `0b`, `0o` or `0x` prefixes. Float conversions include `f`, `e` and `g`, with uppercase variants.

```rbl
func main() (
    set value = 255
    set pi = 3.1415926535
    print(f"{value:d} {value:b} {value:o} {value:X}")
    print(f"fixed={pi:.3f}")
    print(f"scientific={pi:.2e}")
    print(f"general={pi:.5g}")
)
```

Precision applies to floats. Integer precision and precision with `s` are runtime format errors. Use an explicit fill/alignment pair for zero fill, such as `0>8d`; a leading zero alone is just a width digit.

Run `examples/v081/02_numeric_formats.rbl` and compare the representations.

**Exercise:** print 4095 in decimal, binary and uppercase hexadecimal, then display `1/7` with 3, 6 and 12 significant digits.