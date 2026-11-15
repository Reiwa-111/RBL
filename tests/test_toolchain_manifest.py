#!/usr/bin/env python3
"""Check that the Windows toolchain manifest covers the complete payload."""
from __future__ import annotations

import hashlib
import importlib.util
import json
import os
import subprocess
import tempfile
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "package_windows_toolchain", ROOT / "tools" / "package_windows_toolchain.py"
)
assert SPEC and SPEC.loader
PACKAGER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGER)


def write_bundle(root: Path) -> None:
    payload = root / "bin" / "gcc.exe"
    payload.parent.mkdir(parents=True)
    payload.write_bytes(b"test-gcc")
    manifest = {
        "schema": 1,
        "packages": [
            {
                "id": name,
                "version": version,
                "source_archive": f"https://mirror.msys2.org/mingw/sources/{archive}",
            }
            for name, version, archive in PACKAGER.PACKAGE_SOURCES
        ],
        "files": {
            "bin/gcc.exe": {
                "size": payload.stat().st_size,
                "sha256": hashlib.sha256(payload.read_bytes()).hexdigest(),
            }
        },
        "file_count": 1,
        "total_bytes": payload.stat().st_size,
    }
    (root / "rbl-toolchain-manifest.json").write_text(
        json.dumps(manifest), encoding="utf-8"
    )


def expect_invalid(root: Path, label: str) -> None:
    try:
        PACKAGER.verify_manifest(root)
    except RuntimeError:
        return
    raise AssertionError(f"manifest verifier accepted {label}")


def test_packager_rejects_overlapping_source_and_destination() -> None:
    with tempfile.TemporaryDirectory(prefix="rbl-packager-overlap-") as temp:
        outer = Path(temp) / "ucrt64"
        root = outer / "nested" / "ucrt64"
        root.mkdir(parents=True)
        marker = root / "source-marker.txt"
        marker.write_text("keep", encoding="utf-8")
        candidates = (root, root / "nested" / "ucrt64", outer)
        for destination in candidates:
            try:
                PACKAGER.build_manifest(root, destination)
            except RuntimeError as error:
                if "disjoint" not in str(error):
                    raise AssertionError(f"overlap was not rejected before packaging: {error}") from error
            else:
                raise AssertionError(f"packager accepted overlapping destination: {destination}")
            if marker.read_text(encoding="utf-8") != "keep":
                raise AssertionError("overlap validation modified the source directory")


def test_packager_preserves_existing_bundle_when_staging_fails() -> None:
    with tempfile.TemporaryDirectory(prefix="rbl-packager-stage-") as temp:
        base = Path(temp)
        root = base / "source" / "ucrt64"
        dest = base / "destination" / "ucrt64"
        root.mkdir(parents=True)
        dest.mkdir(parents=True)
        marker = dest / "previous-bundle-marker.txt"
        marker.write_text("previous", encoding="utf-8")
        with mock.patch.object(PACKAGER, "_build_manifest_into", side_effect=RuntimeError("injected stage failure")):
            try:
                PACKAGER.build_manifest(root, dest)
            except RuntimeError as error:
                if "injected stage failure" not in str(error):
                    raise
            else:
                raise AssertionError("injected staging failure was not propagated")
        if marker.read_text(encoding="utf-8") != "previous":
            raise AssertionError("failed staging removed or changed the previous bundle")


def test_packager_restores_previous_bundle_when_commit_fails() -> None:
    with tempfile.TemporaryDirectory(prefix="rbl-packager-rollback-") as temp:
        base = Path(temp)
        root = base / "source" / "ucrt64"
        dest = base / "destination" / "ucrt64"
        root.mkdir(parents=True)
        dest.mkdir(parents=True)
        marker = dest / "previous-bundle-marker.txt"
        marker.write_text("previous", encoding="utf-8")

        def build_staged(_root: Path, staging: Path) -> dict:
            (staging / "new-payload.txt").write_text("new", encoding="utf-8")
            return {}

        real_replace = PACKAGER.os.replace
        calls = 0

        def fail_staging_install(source: Path, destination: Path) -> None:
            nonlocal calls
            calls += 1
            if calls == 2:
                raise OSError("injected staged-directory rename failure")
            real_replace(source, destination)

        with mock.patch.object(PACKAGER, "_build_manifest_into", side_effect=build_staged), \
             mock.patch.object(PACKAGER, "verify_manifest"), \
             mock.patch.object(PACKAGER.os, "replace", side_effect=fail_staging_install):
            try:
                PACKAGER.build_manifest(root, dest)
            except OSError as error:
                if "injected staged-directory rename failure" not in str(error):
                    raise
            else:
                raise AssertionError("injected staged-directory commit failure was not propagated")

        if marker.read_text(encoding="utf-8") != "previous":
            raise AssertionError("failed staged commit did not restore the previous bundle")
        if list(dest.parent.glob(f".{dest.name}.backup-*")):
            raise AssertionError("rollback left an unnecessary backup directory behind")


def test_manifest_verifier_rejects_symlink_manifest() -> None:
    with tempfile.TemporaryDirectory(prefix="rbl-manifest-link-") as temp:
        base = Path(temp)
        root = base / "bundle"
        root.mkdir()
        write_bundle(root)
        manifest = root / "rbl-toolchain-manifest.json"
        outside = base / "outside-manifest.json"
        manifest.replace(outside)
        try:
            manifest.symlink_to(outside)
        except OSError as error:
            if os.name == "nt":
                print(f"SKIP: Windows file symlink creation unavailable: {error}")
                return
            raise
        expect_invalid(root, "a manifest symlink to a valid external manifest")


def test_manifest_verifier_rejects_symlink_bundle_root() -> None:
    with tempfile.TemporaryDirectory(prefix="rbl-manifest-root-link-") as temp:
        base = Path(temp)
        real_root = base / "real-bundle"
        real_root.mkdir()
        write_bundle(real_root)
        linked_root = base / "linked-bundle"
        if os.name == "nt":
            result = subprocess.run(
                ["cmd.exe", "/d", "/c", "mklink", "/J", str(linked_root), str(real_root)],
                capture_output=True,
                text=True,
                check=False,
            )
            if result.returncode:
                raise AssertionError(f"could not create bundle-root junction fixture: {result.stdout}\\n{result.stderr}")
        else:
            linked_root.symlink_to(real_root, target_is_directory=True)
        expect_invalid(linked_root, "a bundle root that is itself a symlink/junction")


def test_manifest_verifier_rejects_symlink_bundle_ancestor() -> None:
    with tempfile.TemporaryDirectory(prefix="rbl-manifest-ancestor-link-") as temp:
        base = Path(temp)
        real_parent = base / "real-parent"
        real_parent.mkdir()
        real_root = real_parent / "bundle"
        real_root.mkdir()
        write_bundle(real_root)
        linked_parent = base / "linked-parent"
        if os.name == "nt":
            result = subprocess.run(
                ["cmd.exe", "/d", "/c", "mklink", "/J", str(linked_parent), str(real_parent)],
                capture_output=True,
                text=True,
                check=False,
            )
            if result.returncode:
                raise AssertionError(f"could not create bundle-ancestor junction fixture: {result.stdout}\\n{result.stderr}")
        else:
            linked_parent.symlink_to(real_parent, target_is_directory=True)
        expect_invalid(linked_parent / "bundle", "a normal bundle directory beneath a symlink/junction ancestor")


def test_manifest_verifier_rejects_symlink_ancestor_hidden_by_parent_component() -> None:
    with tempfile.TemporaryDirectory(prefix="rbl-manifest-dotdot-link-") as temp:
        base = Path(temp)
        target_parent = base / "target" / "subdir"
        target_bundle = base / "target" / "bundle"
        lexical_parent = base / "lexical"
        target_parent.mkdir(parents=True)
        target_bundle.mkdir()
        lexical_parent.mkdir()
        write_bundle(target_bundle)
        lexical_bundle = lexical_parent / "bundle"
        lexical_bundle.mkdir()
        write_bundle(lexical_bundle)
        link = lexical_parent / "link"
        if os.name == "nt":
            result = subprocess.run(
                ["cmd.exe", "/d", "/c", "mklink", "/J", str(link), str(target_parent)],
                capture_output=True,
                text=True,
                check=False,
            )
            if result.returncode:
                raise AssertionError(f"could not create dot-dot ancestor junction fixture: {result.stdout}\\n{result.stderr}")
        else:
            link.symlink_to(target_parent, target_is_directory=True)
        candidate = link / ".." / "bundle"
        expected_target = lexical_bundle if os.name == "nt" else target_bundle
        if candidate.resolve(strict=True) != expected_target.resolve(strict=True):
            raise AssertionError("dot-dot fixture did not resolve according to the platform's path semantics")
        expect_invalid(candidate, "a symlink/junction ancestor erased by lexical '..' normalization")


def test_manifest_verifier_fails_closed_on_inventory_walk_error() -> None:
    with tempfile.TemporaryDirectory(prefix="rbl-manifest-walk-error-") as temp:
        root = Path(temp) / "bundle"
        root.mkdir()
        write_bundle(root)
        real_walk = PACKAGER.os.walk

        def injected_walk(top, *, topdown=True, followlinks=False, onerror=None):
            yield from real_walk(top, topdown=topdown, followlinks=followlinks, onerror=onerror)
            if onerror is not None:
                onerror(PermissionError("injected inaccessible subtree"))

        with mock.patch.object(PACKAGER.os, "walk", side_effect=injected_walk):
            expect_invalid(root, "an inaccessible subtree during exact inventory")


def test_manifest_verifier_rejects_unmanifested_symlink_and_junction_entries() -> None:
    with tempfile.TemporaryDirectory(prefix="rbl-manifest-entry-link-") as temp:
        base = Path(temp)
        root = base / "bundle"
        root.mkdir()
        write_bundle(root)
        outside = base / "outside-directory"
        outside.mkdir()
        (outside / "escaped.txt").write_text("outside", encoding="utf-8")
        link = root / "unmanifested-directory-link"
        if os.name == "nt":
            result = subprocess.run(
                ["cmd.exe", "/d", "/c", "mklink", "/J", str(link), str(outside)],
                capture_output=True,
                text=True,
                check=False,
            )
            if result.returncode:
                raise AssertionError(f"could not create unprivileged junction fixture: {result.stdout}\n{result.stderr}")
        else:
            link.symlink_to(outside, target_is_directory=True)
        expect_invalid(root, "an unmanifested symlink/junction directory pointing outside the bundle")


def test_manifest_verifier_rejects_unmanifested_symlink_files() -> None:
    with tempfile.TemporaryDirectory(prefix="rbl-manifest-file-link-") as temp:
        base = Path(temp)
        root = base / "bundle"
        root.mkdir()
        write_bundle(root)
        outside = base / "outside.txt"
        outside.write_text("outside", encoding="utf-8")
        link = root / "unmanifested-file-link"
        try:
            link.symlink_to(outside)
        except OSError as error:
            if os.name == "nt":
                print(f"SKIP: Windows file symlink creation unavailable: {error}")
                return
            raise
        expect_invalid(root, "an unmanifested symlink file pointing outside the bundle")


def main() -> int:
    bundled = ROOT / "toolchain" / "windows-x86_64" / "msys64" / "ucrt64"
    manifest = PACKAGER.verify_manifest(bundled)
    if manifest["file_count"] != 43:
        raise AssertionError(f"unexpected bundled payload count: {manifest['file_count']}")

    with tempfile.TemporaryDirectory(prefix="rbl-manifest-test-") as temp:
        root = Path(temp)
        write_bundle(root)
        PACKAGER.verify_manifest(root)

        (root / "extra.dll").write_bytes(b"untracked")
        expect_invalid(root, "an unmanifested extra file")
        (root / "extra.dll").unlink()

        payload = root / "bin" / "gcc.exe"
        payload.write_bytes(b"tampered")
        expect_invalid(root, "a payload with a mismatched hash")
        payload.write_bytes(b"test-gcc")

        manifest_path = root / "rbl-toolchain-manifest.json"
        data = json.loads(manifest_path.read_text(encoding="utf-8"))
        data["files"]["../outside"] = {"size": 0, "sha256": "0" * 64}
        manifest_path.write_text(json.dumps(data), encoding="utf-8")
        expect_invalid(root, "a path-traversal manifest entry")

    test_packager_rejects_overlapping_source_and_destination()
    test_packager_preserves_existing_bundle_when_staging_fails()
    test_packager_restores_previous_bundle_when_commit_fails()
    test_manifest_verifier_rejects_symlink_manifest()
    test_manifest_verifier_rejects_symlink_bundle_root()
    test_manifest_verifier_rejects_symlink_bundle_ancestor()
    test_manifest_verifier_rejects_symlink_ancestor_hidden_by_parent_component()
    test_manifest_verifier_fails_closed_on_inventory_walk_error()
    test_manifest_verifier_rejects_unmanifested_symlink_and_junction_entries()
    test_manifest_verifier_rejects_unmanifested_symlink_files()

    print("PASS: toolchain manifest validates hashes, safe paths, exact inventory, symlink/reparse rejection, disjoint paths and staged replacement")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
