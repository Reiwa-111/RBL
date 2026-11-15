from pathlib import Path


source = Path(__file__).resolve().parents[1] / "src" / "main_frame.cpp"
text = source.read_text(encoding="utf-8")
app_source = Path(__file__).resolve().parents[1] / "src" / "main.cpp"
app_text = app_source.read_text(encoding="utf-8")
icon_script = Path(__file__).resolve().parents[2] / ".." / "tools" / "generate_app_icon.ps1"
icon_script_text = icon_script.read_text(encoding="utf-8")

checks = {
    "compact tab height survives DPI changes": "SetTabCtrlHeight(FromDIP(compact_mode_ ? 26 : 30))" in text,
    "editor tabs always reserve and show a close button": "wxAUI_NB_CLOSE_ON_ALL_TABS" in text and "wxAUI_NB_MIDDLE_CLICK_CLOSE" in text and "closeButtonState & ~wxAUI_BUTTON_STATE_HIDDEN" in text and "buttonState &= ~wxAUI_BUTTON_STATE_HIDDEN" in text and "Render the close glyph here" in text and "outButtonRect->IsEmpty()" in text,
    "tab measurement reserves close-button width for every page": "wxSize GetTabSize(" in text and "showCloseButton ? closeButtonState & ~wxAUI_BUTTON_STATE_HIDDEN" in text,
    "tab close hit target avoids the following tab overlap": "outButtonRect->Offset(-(wnd ? wnd->FromDIP(10) : 10), 0)" in text,
    "editor and output notebooks use flat borderless frames": text.count("wxBORDER_NONE") >= 5,
    "tab art paints flat theme surfaces over native bevels": "Paint over the stock bevel/gradient" in text and "dc.DrawRectangle(tabRect)" in text,
    "dock sash and pane borders use flat theme drawing": "class FlatDockArt final" in text and "void DrawSash(" in text and "dc.DrawRectangle(rect)" in text and "SetArtProvider(new FlatDockArt(palette))" in text,
    "close buttons ignore AUI's transient hidden state": "buttonState &= ~wxAUI_BUTTON_STATE_HIDDEN" in text,
    "packaged ICO handlers are initialized before frame creation": "wxInitAllImageHandlers();" in app_text and "#include <wx/image.h>" in app_text,
    "application icon generator emits multiple crisp PNG sizes and ICO": "@(16, 24, 32, 48, 64, 128, 256)" in icon_script_text and "rblstudio.ico" in icon_script_text,
    "tab labels use theme foreground rather than system text": "dc.SetTextForeground(page.active ? palette_.text : palette_.muted)" in text,
    "tab close glyph is drawn with theme colors": "dc.DrawLine(left, top, right, bottom)" in text and "palette_.muted" in text,
    "output and settings use the shared flat AUI tab art": "output_tabs_ = new wxAuiNotebook" in text and "new wxAuiNotebook(&dialog" in text and "SetArtProvider(new ThemeTabArt" in text,
    "dock panes disable gradients and 3D pane borders": "wxAUI_DOCKART_GRADIENT_TYPE, wxAUI_GRADIENT_NONE" in text and "wxAUI_DOCKART_PANE_BORDER_SIZE, 0" in text,
    "compact project metrics survive DPI changes": "SetSpacing(FromDIP(compact_mode_ ? 2 : 4))" in text,
    "compact toolbar margins survive DPI changes": "FromDIP(compact_mode_ ? 7 : 10)" in text,
    "compact toolbar icon size survives asset rebuild": "compact_mode_ ? 16 : 18" in text,
    "diagnostic severity uses semantic theme tokens": "diagnostic.is_error ? palette.error : palette.warning" in text,
    "existing diagnostics recolor when theme changes": "problems_->GetItemText(row, 3).CmpNoCase(\"error\")" in text,
    "console font follows compact density": "wxFontInfo(compact_mode_ ? 8 : 9).Family(wxFONTFAMILY_TELETYPE)" in text,
    "settings dialog inherits active theme": "apply_dialog_theme(apply_dialog_theme, child)" in text,
    "non-Windows toolchain page is disabled and labeled": "#ifndef __WXMSW__\n    toolchain_page->Enable(false);\n    pages->SetPageText(3, \"Toolchain (Windows only)\");" in text,
    "result message columns adapt to available width": "fit_result_column" in text and "GetClientSize().GetWidth()" in text,
    "Run menu precedes View menu": text.find('menu_bar->Append(run, "&Run")') < text.find('menu_bar->Append(view, "&View")'),
    "toolbar actions reflect backend and editor availability": "toolbar_->EnableTool(ID_Run, can_compile)" in text and "toolbar_->EnableTool(ID_Stop, task_running_)" in text,
    "toolbar state updates on editor changes and worker polling": "UpdateToolbarState();" in text and "void MainFrame::OnPageChanged" in text,
    "Quick Open dialog inherits active palette": "style_quick_open(style_quick_open, child)" in text,
    "Command Palette dialog inherits active palette": "style_palette_dialog(style_palette_dialog, child)" in text,
    "Quick Open and Benchmarks have distinct toolbar art": "wxART_FOLDER_OPEN" in text and "wxART_TIP" in text,
    "initial frame is clamped to available work area": "ClampFrameToWorkArea(this, GetSize())" in text,
    "wheel scrolling follows the pointer over in-app lists": "int FilterEvent(wxEvent& event) override" in app_text and "wxFindWindowAtPoint(wxGetMousePosition())" in app_text and "WM_MOUSEWHEEL" in app_text and "wxTreeCtrl" in app_text and "wxListCtrl" in app_text and "wxListBox" in app_text,
}

for label, passed in checks.items():
    print(f"{'PASS' if passed else 'FAIL'}: {label}")

failed = [label for label, passed in checks.items() if not passed]
if failed:
    raise SystemExit(f"Visual consistency static acceptance failed: {', '.join(failed)}")
print(f"Visual consistency static acceptance: {len(checks)}/{len(checks)} PASS")
