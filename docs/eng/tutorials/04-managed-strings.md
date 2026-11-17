# Lesson 4: Runtime-managed dynamic strings

RBL 0.8.0 moved dynamic string lifetime under Runtime ownership. Results from `input()`, `read_file()`, conversions and string operations are managed values. RBL source code does not manually free string payloads.

```rbl
func make_message(name string) string (
    return "Hello, " + name
)

func main() (
    set name = "Ada"
    set message = make_message(name)
    print(message)
    print(f"message length: {len(message)}")
)
```

The collector is conservative and non-moving. It traces managed strings and containers at supported safe points. This does not expose a public `gc()` call, pinning API or multithreaded Runtime interface. Native C code has separate borrowing/rooting constraints; do not infer that C callers may retain arbitrary pointers indefinitely.

Read the [execution model](../explanation/execution-model.md#dynamic-string-lifetime) before writing native extensions.

**Exercise:** create strings from several function calls, store them in a list, and read them after the calls return. Do not add a manual free operation.