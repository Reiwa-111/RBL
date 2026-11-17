# RBL Studio Flatpak (Linux x86-64)

This is the portable GUI distribution path. It uses the GNOME 51 Flatpak
runtime, which supplies the GTK platform stack inside the sandbox. Users do not
need to install wxWidgets, GTK development packages, GNU Binutils, or Python on
the host. The RBL payload includes wxWidgets 3.2.4 shared libraries, CPython
3.14.10, and the Clang 23.1.5 integrated assembler + LLD.

The Flatpak runtime itself is installed by Flatpak from the configured remote;
it is not copied into the application bundle. The native shell installer remains
available for distributions that provide GTK 3 as a system runtime dependency.

## Build

First build the Linux payload using the project build script on a Linux system
with GTK 3 development headers, CMake, a C/C++ compiler, Cargo, and the normal
build utilities:

```sh
bash tools/build_wx_ide.sh build/portable-linux
```

Prepare the isolated application root and verify its staged file/checksum manifest. Separate corresponding-source archives are not staged or required as GitHub release deliverables:
The output must be on a case-sensitive Linux filesystem. In WSL, do not place
the prepared payload under `/mnt/c`, `/mnt/q`, or another Windows-mounted drive;
the Python runtime contains case-distinct terminfo entries. The preparer rejects
case-insensitive output filesystems before staging.

```sh
PAYLOAD="$HOME/.cache/rbl-studio/flatpak-payload"
python3 tools/prepare_flatpak_payload.py \
  --build-dir build/portable-linux \
  --output "$PAYLOAD"
MANIFEST="$HOME/.cache/rbl-studio/com.reiwa.rblstudio.yml"
mkdir -p "$(dirname "$MANIFEST")"
cp packaging/flatpak/com.reiwa.rblstudio.yml "$MANIFEST"
sed -i "s|../../build/flatpak-payload|$PAYLOAD|" "$MANIFEST"
```

Install the GNOME 51 SDK/runtime and build the local Flatpak bundle:

```sh
flatpak install flathub org.gnome.Platform//51 org.gnome.Sdk//51
flatpak-builder --force-clean --repo="$HOME/.cache/rbl-studio/flatpak-repo" \
  "$HOME/.cache/rbl-studio/flatpak-build" "$MANIFEST"
flatpak build-bundle "$HOME/.cache/rbl-studio/flatpak-repo" "$HOME/RBLStudio-0.8.1-linux-x86_64.flatpak" \
  com.reiwa.rblstudio
```

Install and run locally:

```sh
flatpak install --user "$HOME/RBLStudio-0.8.1-linux-x86_64.flatpak"
flatpak run com.reiwa.rblstudio
```

The bundle includes the RBL source tree, third-party notices and license texts,
plus toolchain provenance/source URL and checksum inventories. It does not stage
separate wxWidgets, CPython, or Rust corresponding-source archives. This source-tree
release policy does not waive third-party obligations for bundled binaries. The
app-local LLVM binary is distributed under
Apache-2.0 with LLVM Exceptions; its complete license text, source commit,
verified detached GPG signature and provenance checksums are included in
`toolchain/linux-x86_64/`.

This is a local Flatpak build manifest, not yet a Flathub submission. A public
Flathub submission must build from source modules inside the SDK rather than
installing the prebuilt payload directory, and the final app ID must be checked
against the publisher's namespace before submission.
