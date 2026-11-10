from pathlib import Path
import re


source = Path(__file__).resolve().parents[1] / "src" / "main_frame.cpp"
text = source.read_text(encoding="utf-8")
match = re.search(
    r"void MainFrame::OnQuickOpen\(wxCommandEvent&\)\s*\{(?P<body>.*?)\nvoid MainFrame::OnCommandPalette",
    text,
    re.DOTALL,
)
assert match, "Quick Open handler not found"
body = match.group("body")

checks = {
    "background indexing": "std::thread indexer" in body,
    "bounded directory traversal": "max_entries = 50000" in body,
    "bounded file index": "max_files = 20000" in body,
    "bounded traversal depth": "max_depth = 32" in body,
    "ignored generated directories": '"node_modules"' in body and '".git"' in body,
    "ignored temporary and build artifacts": '".analysis-tmp"' in body and '"artifacts"' in body,
    "text-oriented extension filter": '".rbl"' in body and '".rs"' in body and "text_extensions.contains(extension)" in body,
    "Windows reparse-point exclusion": "FILE_ATTRIBUTE_REPARSE_POINT" in body,
    "POSIX symlink exclusion": "std::filesystem::is_symlink(status)" in body,
    "Windows directory-chain locking": "LockWindowsDirectoryChain(directory, directory_guard" in body,
    "POSIX no-follow directory handles": "OpenDirectoryPathNoFollow(root)" in body and "::openat(directory.parent->fd" in body,
    "POSIX no-follow entry metadata": "AT_SYMLINK_NOFOLLOW" in body,
    "live name/path filtering": "filename.StartsWith(query)" in body and "lowered.Find(query)" in body,
    "keyboard navigation": "WXK_DOWN" in body and "WXK_UP" in body and "WXK_RETURN" in body,
    "safe document open path": "OpenDocumentPath(wxString(selected.wstring()))" in body,
    "worker cancellation and join": "state->cancel.store(true" in body and "indexer.join()" in body,
    "no legacy file picker": "wxFileDialog" not in body,
}

for label, passed in checks.items():
    print(f"{'PASS' if passed else 'FAIL'}: {label}")

failed = [label for label, passed in checks.items() if not passed]
if failed:
    raise SystemExit(f"Quick Open static acceptance failed: {', '.join(failed)}")
print(f"Quick Open static acceptance: {len(checks)}/{len(checks)} PASS")
