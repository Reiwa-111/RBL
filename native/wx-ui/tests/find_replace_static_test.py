from pathlib import Path


source = Path(__file__).resolve().parents[1] / "src" / "main_frame.cpp"
text = source.read_text(encoding="utf-8")

checks = {
    "persistent AUI find pane": 'Name("find-pane")' in text and "find_panel_" in text,
    "find and replace inputs": "find_query_ = new wxTextCtrl" in text and "replace_query_ = new wxTextCtrl" in text,
    "next and previous actions": "ID_FindNext" in text and "ID_FindPrevious" in text,
    "replace one and replace all": "ID_ReplaceOne" in text and "ID_ReplaceAll" in text,
    "case-sensitive search option": "wxSTC_FIND_MATCHCASE" in text and "find_match_case_" in text,
    "forward and backward search": "FindInCurrentEditor(bool backwards)" in text,
    "search wraps around document": "SetTargetStart(backwards ? length : 0)" in text,
    "replace all is one undo action": "BeginUndoAction()" in text and "EndUndoAction()" in text,
    "menu actions open persistent pane": "OnFind(wxCommandEvent&) { ShowFindPanel(false); }" in text
        and "OnReplace(wxCommandEvent&) { ShowFindPanel(true); }" in text,
}

for label, passed in checks.items():
    print(f"{'PASS' if passed else 'FAIL'}: {label}")

failed = [label for label, passed in checks.items() if not passed]
if failed:
    raise SystemExit(f"Find/Replace static acceptance failed: {', '.join(failed)}")
print(f"Find/Replace static acceptance: {len(checks)}/{len(checks)} PASS")
