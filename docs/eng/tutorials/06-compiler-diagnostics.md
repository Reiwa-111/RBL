# Lesson 6: compiler diagnostics and source positions

RBL 0.8.x diagnostics identify the file, line, column and compiler stage. They include a source excerpt and a caret pointing to the reported location. UTF-8, tabs and LF/CRLF/CR line endings are covered by regression tests.

Use `check` before running a program:

```powershell
python rbl.py check examples/v081/01_formatting_report.rbl
```

To inspect a diagnostic, create a temporary `.rbl` file with a misspelled function or malformed format spec, then run `python rbl.py check path/to/file.rbl`. The command should fail with a non-zero exit code and a location. Fix the source and rerun `check`.

Do not treat every error as a parser error: diagnostics distinguish lexing, parsing, semantic validation and code generation. Runtime errors occur later, when a compiled program executes.

**Exercise:** introduce one syntax error and one unknown identifier in separate files. Compare the stage labels and source locations.