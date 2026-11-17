# Lesson 7: Runtime ABI version and compatibility

The Runtime ABI is versioned independently from the RBL language/compiler version. The current ABI is 1.2.0. The descriptor records layout and feature information; generated programs and the linked Runtime perform compatibility checks before entering RBL `main`.

Run the environment diagnostic:

```bash
python3 rbl.py doctor
python3 rbl.py version
```

`doctor` reports the selected backend, assembler/linker availability, bundled toolchain selection and Runtime ABI presence. It is an environment check, not a substitute for the ABI regression suite. The ABI tests cover compatible minor/patch versions, rejected incompatible layouts/features and missing versioned symbols.

The ABI descriptor is an implementation contract for compiler/Runtime integration. Ordinary RBL programs do not query or mutate it directly.

**Exercise:** run `doctor` on both Windows and Linux/WSL and compare backend/toolchain information. Do not expect platform-specific paths or clock sources to match.