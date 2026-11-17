# last verified: v0.8.1 | 2026.11.17 | DOCUMENTATION LANDING PAGE

## RBL documentation

Choose your documentation language:

- **[English](eng/README.md)**
- **[Русский](ru/README.md)**

Both language editions describe the **RBL 0.8.1 language, compiler and runtime contract** and follow the Diataxis structure: tutorial, how-to guides, reference and explanation. Their core page trees use matching names and organization. Internal planning material is not part of the published documentation index.

The repository root [`README.md`](../README.md) provides the project overview and a short CLI quick start. The Linux packaging guide documents the app-local LLVM/wxWidgets toolchain and the Flatpak route for a sandboxed GTK runtime. Release notes and [`RELEASE_MANIFEST.txt`](../RELEASE_MANIFEST.txt) distinguish the published RBL 0.8.0-r2 source snapshot from the 0.8.1 release-preparation tree and record current verification scope.

The bundled Windows MinGW-w64 toolchain, Linux LLVM toolchain, wxWidgets/Scintilla desktop frontend, CPython runtime and Rust worker have separate third-party license/source inventories. See [`toolchain/windows-x86_64/THIRD_PARTY_NOTICES.md`](../toolchain/windows-x86_64/THIRD_PARTY_NOTICES.md), [`rbl-ide/wx-ui/THIRD_PARTY_NOTICES.md`](../rbl-ide/wx-ui/THIRD_PARTY_NOTICES.md), the [Linux packaging/legal inventory](eng/how-to/linux-packaging.md) and the [Rust worker dependency inventory](../rbl-ide/wx-ui/RUST_DEPENDENCY_NOTICES.md); the repository Apache-2.0 license does not relicense these components or third-party crates. Component-specific legal sign-off is still required before public redistribution.
