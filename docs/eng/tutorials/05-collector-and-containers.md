# Lesson 5: managed strings inside containers

Lists and dictionaries can keep references to dynamic strings alive. The Runtime collector follows supported container references, so a string stored in a reachable list or dictionary remains reachable too.

```rbl
func main() (
    set labels = ["lexer", "parser", "codegen"]
    set details = {"release": "0.8.1", "abi": "1.2.0"}
    set release = details["release"]
    set title = f"RBL {release}"
    let labels[1] = title
    print(labels[0], labels[1], labels[2])
)
```

Collection is automatic. Compiler-emitted safe points are omitted from proven non-allocating fast loops, but retained where allocation or pointer-root mutation can occur. This is an implementation optimization, not a user-controlled memory-management switch.

Run `examples/v081/05_managed_string_collections.rbl` and `06_managed_string_workload.rbl`. They are functional examples, not performance benchmarks.

**Exercise:** keep generated labels in a nested list and verify that later reads return the expected text.