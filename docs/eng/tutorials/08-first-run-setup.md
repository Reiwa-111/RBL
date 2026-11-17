# Lesson 8: RBL Studio first-run setup wizard

RBL Studio 0.8.1 adds a native setup wizard. It checks the application root, Rust worker, Python runtime and compiler toolchain before normal work begins. It stores per-user setup/preferences and offers an explicit development/source-checkout path.

When a complete platform installer payload is available, the wizard can start user-scope Install or Repair asynchronously and report captured output and exit status. The wizard can also be reopened from Settings. A source checkout is not the same as a registered installation; choose the development path when working directly from the repository.

**Exercise:** launch the IDE from the repository root, inspect the detected paths, complete the development setup and reopen the wizard from Settings. Do not run an installer against a directory you did not choose.