# RBL 0.8.x focused examples

Eight small, runnable programs demonstrate the 0.8.x formatting/runtime contract. They complement the original twenty beginner lessons and larger scientific/algorithmic examples.

| File | Topic |
|---|---|
| `01_formatting_report.rbl` | Aligned text and integer columns |
| `02_numeric_formats.rbl` | Integer bases and float precision |
| `03_unicode_width.rbl` | Unicode code-point width and fill |
| `04_string_pipeline.rbl` | `rbl.string` helpers |
| `05_managed_string_collections.rbl` | Managed strings retained by lists/dictionaries |
| `06_managed_string_workload.rbl` | Repeated dynamic string creation and container updates |
| `07_runtime_math_report.rbl` | Math helpers and formatted numeric output |
| `08_file_roundtrip.rbl` | Text I/O and filesystem helpers under `build/` |

Run from the repository root:

```bash
python3 rbl.py check examples/v081/01_formatting_report.rbl
python3 rbl.py run examples/v081/01_formatting_report.rbl
```

All files are single-source RBL programs. The managed-string workloads rely on automatic Runtime collection; they do not call a manual GC API and are not performance benchmarks.
