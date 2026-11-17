# Lesson 10: Linux native build and Flatpak scope

The native Linux wxWidgets GUI + Rust worker build and shell install path were verified on Ubuntu 24.04 with system wxWidgets 3.2.4. This path uses host GTK 3 and the host display stack.

Build the native frontend from the repository root:

```bash
./tools/build_wx_ide.sh
./RBLStudio.sh
```

The Flatpak payload is a separate packaging path. Its local payload/bundle and sandbox CLI commands (`version`, `doctor`, `check`, `run`) were tested. Interactive Flatpak GUI startup, Wayland/X11 display, file dialogs/portals, desktop integration and Flathub publication are not covered by those CLI checks.

Use the [Linux packaging guide](../how-to/linux-packaging.md) for prerequisites and the exact distinction between native host dependencies and the Flatpak runtime.

**Exercise:** build the native Linux GUI on Ubuntu 24.04, run `rbl doctor`, then inspect the Flatpak guide and list which checks are CLI-only versus interactive GUI checks.