# Lesson 9: native IDE workflow and diagnostics

The primary 0.8.1 frontend is C++20 + wxWidgets 3.2 + Scintilla. The Rust worker runs separately over a bounded NDJSON protocol. The editor can open ordinary text files with arbitrary extensions, but compiler actions are deliberately limited to `.rbl` files.

Useful shortcuts: `Ctrl+O` open, `Ctrl+Shift+O` open project folder, `Ctrl+F` find, `Ctrl+H` replace, `Ctrl+Shift+F` find in files, `Ctrl+P` quick open, `Ctrl+Shift+P` command palette, `F7` check, `F5` run, `Ctrl+B` build, `Shift+F5` stop.

The Problems/Console/Run panes are dockable. Clicking a diagnostic opens the source and moves the caret to its location. The project tree and Find in Files have explicit traversal/result limits; these are safety bounds, not arbitrary bugs.

**Exercise:** open a project, run Check on a `.rbl` file, introduce an error and navigate to its diagnostic. Open a `.md` file too and verify that it can be edited but not passed to Run/Build.