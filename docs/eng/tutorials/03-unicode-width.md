# Lesson 3: Unicode width and alignment

When a width is requested, RBL counts UTF-8 code points, not bytes and not terminal display cells. A wide CJK glyph or a combining sequence can occupy a different number of terminal cells than the formatter's width count.

```rbl
func main() (
    set word = "κόσμος"
    print(f"[{word:*>12s}]")
    set symbol = "λ"
    print(f"[{symbol:·^8s}]")
    print("code points:", len(word))
)
```

The fill character is exactly one Unicode code point. The default fill is a space. For centered output, an odd extra padding code point is placed on the right. Width formatting requires valid UTF-8; without width, string bytes are preserved.

Run `examples/v081/03_unicode_width.rbl` and inspect the brackets. Do not interpret the field width as pixel-perfect terminal alignment for every font.

**Exercise:** align three labels with ASCII, Greek and Cyrillic characters, and compare `len()` with what your terminal visually occupies.