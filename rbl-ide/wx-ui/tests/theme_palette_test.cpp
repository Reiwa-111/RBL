#include "theme.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <string>

namespace {
double LinearChannel(unsigned char value) {
    const double channel = static_cast<double>(value) / 255.0;
    return channel <= 0.04045 ? channel / 12.92
                              : std::pow((channel + 0.055) / 1.055, 2.4);
}

double Luminance(const wxColour& colour) {
    return 0.2126 * LinearChannel(colour.Red()) +
           0.7152 * LinearChannel(colour.Green()) +
           0.0722 * LinearChannel(colour.Blue());
}

double Contrast(const wxColour& foreground, const wxColour& background) {
    const double a = Luminance(foreground);
    const double b = Luminance(background);
    return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

bool Check(const std::string& theme, const std::string& role,
           const wxColour& foreground, const wxColour& background) {
    const double ratio = Contrast(foreground, background);
    if (ratio >= 4.5) return true;
    std::cerr << theme << ": " << role << " contrast is " << ratio << ":1 (minimum 4.5:1)\n";
    return false;
}
}

int main() {
    constexpr std::array<const char*, 5> names = {
        "RBL Dark", "RBL Light", "Nord", "Monokai", "High Contrast"
    };
    bool passed = true;
    for (const char* name : names) {
        const wxString key = wxString::FromUTF8(name);
        const auto ui = rbl::wxui::PaletteForTheme(key);
        const auto editor = rbl::wxui::ColorSchemeForName(key);
        passed &= Check(name, "text/surface", ui.text, ui.surface);
        passed &= Check(name, "text/window", ui.text, ui.window);
        passed &= Check(name, "text/surface_alt", ui.text, ui.surface_alt);
        passed &= Check(name, "text/selected", ui.text, ui.selected);
        passed &= Check(name, "text/editor", ui.text, ui.editor);
        passed &= Check(name, "text/output", ui.text, ui.output);
        passed &= Check(name, "muted/surface", ui.muted, ui.surface);
        passed &= Check(name, "muted/window", ui.muted, ui.window);
        passed &= Check(name, "muted/surface_alt", ui.muted, ui.surface_alt);
        passed &= Check(name, "muted/selected", ui.muted, ui.selected);
        passed &= Check(name, "accent text/accent", ui.accent_text, ui.accent);
        passed &= Check(name, "selection text/selection", ui.selection_text, ui.selection);
        passed &= Check(name, "error/output", ui.error, ui.output);
        passed &= Check(name, "warning/output", ui.warning, ui.output);
        passed &= Check(name, "syntax keyword", editor.keyword, ui.editor);
        passed &= Check(name, "syntax string", editor.string, ui.editor);
        passed &= Check(name, "syntax comment", editor.comment, ui.editor);
        passed &= Check(name, "syntax number", editor.number, ui.editor);
        passed &= Check(name, "syntax function", editor.function, ui.editor);
        passed &= Check(name, "syntax boolean", editor.boolean, ui.editor);
        passed &= Check(name, "syntax operator", editor.op, ui.editor);
    }
    if (!passed) return 1;
    std::cout << "5 UI themes and editor schemes pass WCAG AA text contrast checks.\n";
    return 0;
}
