#pragma once

#include <wx/colour.h>
#include <wx/string.h>

namespace rbl::wxui {

// Application chrome tokens. These never contain syntax-token colors.
struct ThemePalette {
    wxString name;
    wxColour window;
    wxColour surface;
    wxColour surface_alt;
    wxColour editor;
    wxColour output;
    wxColour text;
    wxColour muted;
    wxColour accent;
    wxColour accent_text;
    wxColour border;
    wxColour selected;
    wxColour caret_line;
    wxColour selection;
    wxColour selection_text;
    wxColour error;
    wxColour warning;
};

// Editor syntax schemes are intentionally independent from application UI themes.
struct EditorColorScheme {
    wxString name;
    wxColour keyword;
    wxColour string;
    wxColour comment;
    wxColour number;
    wxColour function;
    wxColour boolean;
    wxColour op;
};

inline ThemePalette PaletteForTheme(const wxString& requested) {
    if (requested == "RBL Light" || requested == "Light") {
        return {"RBL Light", wxColour(242, 244, 247), wxColour(250, 251, 253),
            wxColour(232, 236, 242), wxColour(255, 255, 255), wxColour(247, 249, 252),
            wxColour(35, 41, 50), wxColour(96, 104, 118), wxColour(42, 112, 190),
            wxColour(255, 255, 255), wxColour(210, 216, 225), wxColour(224, 235, 249),
            wxColour(241, 246, 253), wxColour(190, 215, 245), wxColour(25, 38, 55),
            wxColour(190, 55, 55), wxColour(145, 90, 16)};
    }
    if (requested == "Nord") {
        return {"Nord", wxColour(40, 44, 56), wxColour(46, 52, 64),
            wxColour(59, 66, 82), wxColour(46, 52, 64), wxColour(39, 44, 55),
            wxColour(216, 222, 233), wxColour(176, 186, 204), wxColour(136, 192, 208),
            wxColour(24, 32, 42), wxColour(67, 76, 94), wxColour(59, 66, 82),
            wxColour(52, 59, 73), wxColour(76, 92, 119), wxColour(236, 239, 244),
            wxColour(245, 130, 140), wxColour(235, 203, 139)};
    }
    if (requested == "Monokai") {
        return {"Monokai", wxColour(39, 40, 34), wxColour(47, 48, 41),
            wxColour(58, 59, 50), wxColour(39, 40, 34), wxColour(34, 35, 30),
            wxColour(248, 248, 242), wxColour(170, 169, 154), wxColour(166, 226, 46),
            wxColour(39, 40, 34), wxColour(73, 74, 63), wxColour(58, 59, 50),
            wxColour(49, 50, 43), wxColour(73, 72, 62), wxColour(248, 248, 242),
            wxColour(255, 75, 140), wxColour(230, 219, 116)};
    }
    if (requested == "High Contrast") {
        return {"High Contrast", wxColour(0, 0, 0), wxColour(8, 8, 8),
            wxColour(24, 24, 24), wxColour(0, 0, 0), wxColour(0, 0, 0),
            wxColour(255, 255, 255), wxColour(210, 210, 210), wxColour(255, 220, 0),
            wxColour(0, 0, 0), wxColour(190, 190, 190), wxColour(45, 45, 45),
            wxColour(20, 20, 20), wxColour(0, 75, 160), wxColour(255, 255, 255),
            wxColour(255, 100, 100), wxColour(255, 220, 0)};
    }
    return {"RBL Dark", wxColour(28, 30, 36), wxColour(35, 38, 46),
        wxColour(40, 44, 53), wxColour(30, 33, 39), wxColour(24, 27, 33),
        wxColour(218, 223, 232), wxColour(156, 166, 182), wxColour(105, 164, 235),
        wxColour(20, 25, 32), wxColour(55, 61, 72), wxColour(49, 56, 68),
        wxColour(39, 43, 51), wxColour(62, 86, 120), wxColour(245, 245, 245),
        wxColour(244, 128, 128), wxColour(236, 193, 104)};
}

inline EditorColorScheme ColorSchemeForName(const wxString& requested) {
    if (requested == "RBL Light" || requested == "Light")
        return {"RBL Light", wxColour(125, 55, 155), wxColour(45, 125, 75),
            wxColour(107, 114, 128), wxColour(165, 95, 35), wxColour(35, 105, 175),
            wxColour(190, 65, 75), wxColour(70, 80, 95)};
    if (requested == "Nord")
        return {"Nord", wxColour(199, 166, 200), wxColour(163, 190, 140),
            wxColour(129, 161, 193), wxColour(226, 165, 143), wxColour(136, 192, 208),
            wxColour(232, 143, 151), wxColour(216, 222, 233)};
    if (requested == "Monokai")
        return {"Monokai", wxColour(255, 91, 153), wxColour(230, 219, 116),
            wxColour(167, 163, 143), wxColour(174, 129, 255), wxColour(102, 217, 239),
            wxColour(255, 91, 153), wxColour(248, 248, 242)};
    if (requested == "High Contrast")
        return {"High Contrast", wxColour(255, 140, 255), wxColour(130, 255, 130),
            wxColour(190, 190, 190), wxColour(255, 200, 100), wxColour(100, 200, 255),
            wxColour(255, 120, 120), wxColour(255, 255, 255)};
    return {"RBL Dark", wxColour(198, 120, 221), wxColour(152, 195, 121),
        wxColour(157, 165, 177), wxColour(209, 154, 102), wxColour(97, 175, 239),
        wxColour(224, 108, 117), wxColour(171, 178, 191)};
}

} // namespace rbl::wxui
