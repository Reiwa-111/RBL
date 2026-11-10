# Rust worker — third-party dependency notices

Generated inventory snapshot: RBL Studio 0.8.0 `Cargo.lock`, checked 2026-11-10.
This file covers the active normal dependency closure of the explicitly selected `rbl-studio-worker` Cargo binary, using default features and the Windows GNU and Linux x86-64 targets. Cargo.toml gates the retained egui application behind the opt-in `legacy-egui` feature; `eframe` and `rfd` are optional and are not compiled or shipped with the wxWidgets worker.

Direct worker dependencies: `anyhow`, `crossbeam-channel`, `dirs`, `serde`, `serde_json`, `tempfile`; Unix additionally uses `libc`. The legacy GUI-only `eframe` and `rfd` dependencies are optional and excluded from the worker build graph unless `--features legacy-egui` is explicitly requested for the separate `rbl-studio` binary.

License expressions below are copied from each exact crate package manifest. They are package metadata, not a legal interpretation. The package-specific upstream license/notice files are copied verbatim under the matching subdirectory of `licenses/Rust/`.

Windows GNU worker closure: 24 crates. Linux x86-64 worker closure: 26 crates. Union: 28 crates.

| Crate | Version | Cargo license expression | Target | Included license files |
|---|---:|---|---|---|
| `anyhow` | `1.0.104` | `MIT OR Apache-2.0` | Windows GNU / Linux | LICENSE-APACHE, LICENSE-MIT |
| `bitflags` | `2.13.2` | `MIT OR Apache-2.0` | Linux | LICENSE-APACHE, LICENSE-MIT |
| `cfg-if` | `1.0.5` | `MIT OR Apache-2.0` | Windows GNU / Linux | LICENSE-APACHE, LICENSE-MIT |
| `crossbeam-channel` | `0.5.17` | `MIT OR Apache-2.0` | Windows GNU / Linux | LICENSE-APACHE, LICENSE-MIT, LICENSE-THIRD-PARTY |
| `crossbeam-utils` | `0.8.23` | `MIT OR Apache-2.0` | Windows GNU / Linux | LICENSE-APACHE, LICENSE-MIT |
| `dirs` | `6.0.0` | `MIT OR Apache-2.0` | Windows GNU / Linux | LICENSE-APACHE, LICENSE-MIT |
| `dirs-sys` | `0.5.0` | `MIT OR Apache-2.0` | Windows GNU / Linux | LICENSE-APACHE, LICENSE-MIT |
| `fastrand` | `2.5.0` | `Apache-2.0 OR MIT` | Windows GNU / Linux | LICENSE-APACHE, LICENSE-MIT |
| `getrandom` | `0.4.3` | `MIT OR Apache-2.0` | Windows GNU / Linux | LICENSE-APACHE, LICENSE-MIT |
| `itoa` | `1.0.18` | `MIT OR Apache-2.0` | Windows GNU / Linux | LICENSE-APACHE, LICENSE-MIT |
| `libc` | `0.2.191` | `MIT OR Apache-2.0` | Linux | LICENSE-APACHE, LICENSE-MIT |
| `linux-raw-sys` | `0.12.1` | `Apache-2.0 WITH LLVM-exception OR Apache-2.0 OR MIT` | Linux | COPYRIGHT, LICENSE-APACHE, LICENSE-Apache-2.0_WITH_LLVM-exception, LICENSE-MIT |
| `memchr` | `2.8.3` | `Unlicense OR MIT` | Windows GNU / Linux | COPYING, LICENSE-MIT |
| `once_cell` | `1.21.4` | `MIT OR Apache-2.0` | Windows GNU / Linux | LICENSE-APACHE, LICENSE-MIT |
| `option-ext` | `0.2.0` | `MPL-2.0` | Windows GNU / Linux | LICENSE.txt |
| `proc-macro2` | `1.0.107` | `MIT OR Apache-2.0` | Windows GNU / Linux | LICENSE-APACHE, LICENSE-MIT |
| `quote` | `1.0.47` | `MIT OR Apache-2.0` | Windows GNU / Linux | LICENSE-APACHE, LICENSE-MIT |
| `rustix` | `1.1.6` | `Apache-2.0 WITH LLVM-exception OR Apache-2.0 OR MIT` | Linux | COPYRIGHT, LICENSE-APACHE, LICENSE-Apache-2.0_WITH_LLVM-exception, LICENSE-MIT |
| `serde` | `1.0.229` | `MIT OR Apache-2.0` | Windows GNU / Linux | LICENSE-APACHE, LICENSE-MIT |
| `serde_core` | `1.0.229` | `MIT OR Apache-2.0` | Windows GNU / Linux | LICENSE-APACHE, LICENSE-MIT |
| `serde_derive` | `1.0.229` | `MIT OR Apache-2.0` | Windows GNU / Linux | LICENSE-APACHE, LICENSE-MIT |
| `serde_json` | `1.0.154` | `MIT OR Apache-2.0` | Windows GNU / Linux | LICENSE-APACHE, LICENSE-MIT |
| `syn` | `3.0.9` | `MIT OR Apache-2.0` | Windows GNU / Linux | LICENSE-APACHE, LICENSE-MIT |
| `tempfile` | `3.27.0` | `MIT OR Apache-2.0` | Windows GNU / Linux | LICENSE-APACHE, LICENSE-MIT |
| `unicode-ident` | `1.0.27` | `(MIT OR Apache-2.0) AND Unicode-3.0` | Windows GNU / Linux | LICENSE-APACHE, LICENSE-MIT, LICENSE-UNICODE |
| `windows-link` | `0.2.1` | `MIT OR Apache-2.0` | Windows GNU | license-apache-2.0, license-mit |
| `windows-sys` | `0.61.2` | `MIT OR Apache-2.0` | Windows GNU | license-apache-2.0, license-mit |
| `zmij` | `1.0.23` | `MIT` | Windows GNU / Linux | LICENSE-MIT |

## Distribution notes

- `option-ext 0.2.0` declares `MPL-2.0`; its `LICENSE.txt` is included. Its crates.io source archive is https://crates.io/api/v1/crates/option-ext/0.2.0/download (Cargo.lock package checksum for this .crate archive: 04744f49eae99ab78e0d5c0b603ab218f515ea8cfe5a456d7629ad883a3b6e7d). Preserve the corresponding crate source and its MPL terms when redistributing the worker binary.
- `unicode-ident 1.0.27` declares `(MIT OR Apache-2.0) AND Unicode-3.0`; its Unicode license text is included separately.
- `linux-raw-sys` and `rustix` include the Apache-2.0-with-LLVM-exception text as well as the alternative Apache-2.0 and MIT texts.
- `memchr` declares `Unlicense OR MIT`; both `COPYING` and `LICENSE-MIT` are included.
- The verified corresponding-source archive is build/source-material/RBL-0.8.0-Rust-Worker-Source-Materials-20261110-1955.tar.zst (8,099,357 bytes; SHA-256 df5881aa72dc556d9fff6217fd436cec9ad9cb345ac899058a12d23bbdf40da5). It includes worker/shared Rust source files, Cargo.toml, Cargo.lock, repository LICENSE, this inventory snapshot and all 28 exact active crates.io source archives. Every crate archive hash was checked against its Cargo.lock checksum. Both wx build scripts first verify its internal manifest, live Cargo dependency graph, source files and license inventory, then copy it to source/Rust-worker-source.tar.zst; installers verify the SHA-256 manifest and retain the archive in the installed application tree. This is separate from the 20-archive MSYS2 toolchain source bundle.
- This inventory does not replace the separate wxWidgets/Scintilla and MSYS2 MinGW notices, nor does it certify legal compliance. Re-run the dependency inventory whenever `Cargo.lock`, worker imports or target triples change.
