from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src" / "main_frame.cpp").read_text(encoding="utf-8")
CMAKE = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")


def scale_dock_size_contract(value: str, ratio: float) -> str:
    """Executable fixture for wxAUI's dock_size(dir,layer,row)=size grammar."""
    match = re.fullmatch(r"dock_size\((-?\d+),(-?\d+),(-?\d+)\)=(-?\d+)", value)
    if not match or not 0.25 <= ratio <= 4.0:
        raise ValueError("invalid AUI dock_size field or DPI ratio")
    direction, layer, row, size = (int(part) for part in match.groups())
    scaled_size = size if size <= 0 else round(size * ratio)
    return f"dock_size({direction},{layer},{row})={scaled_size}"


class DpiStaticChecks(unittest.TestCase):
    def test_windows_resource_compiler_and_wx_manifest_are_enabled(self):
        self.assertRegex(CMAKE, r"if\(WIN32\)\s+enable_language\(RC\)")
        self.assertIn("wx/msw/wx.rc", CMAKE)
        self.assertIn("wxUSE_DPI_AWARE_MANIFEST=2", CMAKE)
        self.assertIn("wxUSE_RC_MANIFEST=1", CMAKE)

    def test_gnu_default_manifest_is_removed_when_wx_resource_is_linked(self):
        self.assertIn("-dumpspecs", CMAKE)
        self.assertIn("default-manifest.o%s", CMAKE)
        self.assertIn("string(REPLACE", CMAKE)
        self.assertIn("-specs=${RBL_WX_NO_DEFAULT_MANIFEST_SPECS}", CMAKE)

    def test_frame_size_is_persisted_in_dip_and_clamped(self):
        self.assertIn('"Window/WidthDIP"', SOURCE)
        self.assertIn('"Window/HeightDIP"', SOURCE)
        self.assertIn("ToDIP(GetSize())", SOURCE)
        self.assertIn("FromDIP(wxSize(", SOURCE)
        self.assertIn("ClampFrameToWorkArea(this", SOURCE)
        self.assertIn("display.GetClientArea()", SOURCE)

    def test_known_fixed_metrics_use_dip_conversion(self):
        for expression in (
            "FromDIP(5)", "FromDIP(1)", "FromDIP(compact_mode_ ? 26 : 30)",
            "FromDIP(compact_mode_ ? 24 : 26)", "FromDIP(compact_mode_ ? 26 : 28)",
            "FromDIP(18)", "FromDIP(4)", "FromDIP(10)",
            "FromDIP(220)", "FromDIP(65)", "FromDIP(75)",
            "FromDIP(85)", "FromDIP(420)", "FromDIP(48)",
            "FromDIP(16)",
        ):
            with self.subTest(expression=expression):
                self.assertIn(expression, SOURCE)
        self.assertIn("FromDIP(wxSize(190, 180))", SOURCE)
        self.assertIn("FromDIP(wxSize(260, 500))", SOURCE)
        self.assertIn("FromDIP(wxSize(300, 100))", SOURCE)

    def test_stock_bitmap_bundles_are_requested_at_dip_sizes(self):
        self.assertIn("GetBitmapBundle(art, wxART_TOOLBAR, toolbar_bitmap_size)", SOURCE)
        self.assertGreaterEqual(SOURCE.count("GetBitmapBundle(wxART_"), 5)
        self.assertIn("FromDIP(wxSize(16, 16))", SOURCE)
        self.assertIn("MakeProjectGlyph(0, palette, project_bitmap_size)", SOURCE)

    def test_aui_perspective_persists_scale_and_scales_only_size_fields(self):
        self.assertIn('"AUI/DpiScale"', SOURCE)
        self.assertIn('ReadDouble("AUI/DpiScale", 1.0)', SOURCE)
        self.assertIn("ScaleAuiPerspectiveSizes", SOURCE)
        for key in ("bestw=", "besth=", "minw=", "minh=", "maxw=", "maxh=", "floatw=", "floath="):
            self.assertIn(f'"{key}"', SOURCE)
        self.assertIn('result.find("dock_size("', SOURCE)
        self.assertIn("component < 3", SOURCE)
        self.assertIn("result[cursor + 1] != '='", SOURCE)
        self.assertIn("scale_value(cursor, size_end)", SOURCE)
        self.assertIn("Negative AUI sentinel values", SOURCE)
        # Dock topology and floating screen coordinates must never be scaled.
        self.assertIn("Validate but preserve the three topology integers", SOURCE)
        self.assertIn("Floating x/y coordinates", SOURCE)
        self.assertIn("ClampFloatingPanesToDisplays", SOURCE)
        self.assertIn("wxDisplay::GetFromPoint(pane.floating_pos)", SOURCE)

    def test_dpi_event_defers_aui_relayout_until_after_event_propagation(self):
        self.assertIn("Bind(wxEVT_DPI_CHANGED, &MainFrame::OnDpiChanged, this)", SOURCE)
        self.assertIn("event.Skip();", SOURCE)
        self.assertIn("CallAfter([this, ratio] { ApplyDpiMetrics(ratio); })", SOURCE)
        self.assertIn("manager_.Update();", SOURCE)
        self.assertIn("SetMarginWidth(1, FromDIP(16))", SOURCE)
        self.assertIn("ClampFrameToWorkArea(this, GetSize())", SOURCE)
        self.assertIn('AddLabel(wxID_ANY, "RBL 0.8.0", FromDIP(88))', SOURCE)

    def test_dock_size_scales_pixel_size_and_preserves_topology(self):
        self.assertEqual(
            scale_dock_size_contract("dock_size(1,0,0)=280", 1.5),
            "dock_size(1,0,0)=420",
        )
        self.assertEqual(
            scale_dock_size_contract("dock_size(2,-1,3)=280", 1.5),
            "dock_size(2,-1,3)=420",
        )

    def test_custom_art_rasters_are_generated_at_requested_dip_size(self):
        self.assertIn("wxBitmap MakeProjectGlyph(int kind", SOURCE)
        self.assertIn("wxBitmap bitmap(bitmap_size.GetWidth(), bitmap_size.GetHeight(), 32)", SOURCE)
        self.assertIn("wxIcon MakeApplicationIcon(const wxSize& bitmap_size)", SOURCE)
        self.assertIn("dc.SetUserScale(scale_x, scale_y)", SOURCE)


    def test_windows_uses_multisize_application_icon_bundle_with_fallback(self):
        self.assertIn("#ifdef __WXMSW__", SOURCE)
        self.assertIn("wxIconBundle application_icons;", SOURCE)
        self.assertIn('application_root_ / "assets" / "rblstudio.ico"', SOURCE)
        self.assertIn("application_icons.AddIcon(wxString(icon_path.wstring()), wxBITMAP_TYPE_ICO)", SOURCE)
        self.assertIn("if (!application_icons.IsEmpty()) SetIcons(application_icons)", SOURCE)
        self.assertIn("else SetIcon(MakeApplicationIcon(FromDIP(wxSize(32, 32))))", SOURCE)

    def test_dpi_transition_rebuilds_project_and_problem_image_lists_and_flat_output_tabs(self):
        self.assertIn("RebuildDpiAssets();", SOURCE)
        start = SOURCE.index("void MainFrame::RebuildDpiAssets()")
        end = SOURCE.index("void MainFrame::BuildMenus()", start)
        rebuild = SOURCE[start:end]
        self.assertIn("FromDIP(wxSize(16, 16))", rebuild)
        self.assertIn("project_->AssignImageList(images)", rebuild)
        self.assertIn("problems_->AssignImageList(images, wxIMAGE_LIST_SMALL)", rebuild)
        self.assertNotIn("output_tabs_->AssignImageList(images)", rebuild)
        self.assertIn("output_tabs_ = new wxAuiNotebook", SOURCE)
        self.assertIn("output_tabs_->SetArtProvider(new ThemeTabArt", SOURCE)
        self.assertGreaterEqual(rebuild.count("SetToolBitmap("), 13)
        self.assertIn("toolbar_->SetToolBitmapSize(toolbar_bitmap_size)", rebuild)

    def test_settings_dialog_and_output_sizer_spacing_use_dip(self):
        start = SOURCE.index("void MainFrame::OnSettings(")
        end = SOURCE.index("void MainFrame::OnGoToLine", start)
        dialog = SOURCE[start:end]
        self.assertRegex(dialog, r"const int \w+ = FromDIP\(\d+\);\n    const int \w+ = FromDIP\(\d+\);")
        self.assertIn("wxRIGHT, label_gap", dialog)
        self.assertIn("wxALL, pad", dialog)
        self.assertIn("console_sizer->Add(output_, 1, wxEXPAND | wxALL, FromDIP(4))", SOURCE)
        self.assertIn("input_sizer->Add(input_, 1, wxEXPAND | wxRIGHT, FromDIP(6))", SOURCE)


if __name__ == "__main__":
    unittest.main(verbosity=2)
