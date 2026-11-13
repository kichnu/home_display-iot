#include "fonts.h"

#include "theme.h"

extern "C" {
extern const uint8_t font_noto_regular_ttf[];
extern const uint8_t font_noto_regular_ttf_end[];
extern const uint8_t font_noto_bold_ttf[];
extern const uint8_t font_noto_bold_ttf_end[];
}

namespace gui {

namespace {
constexpr size_t TTF_GLYPH_CACHE = 256;
Fonts s_fonts = {};

// TinyTTF logs "cache not allocated" (LV_LOG_ERROR) for every glyph missing in the TTF. LV_SYMBOL_*
// icons (Private Use Area) are never in Noto, so they are skipped here and resolved silently by
// the Montserrat fallback.
using GlyphDscCb = bool (*)(const lv_font_t*, lv_font_glyph_dsc_t*, uint32_t, uint32_t);
GlyphDscCb s_ttfGlyphDsc = nullptr;

bool ttfGlyphDscNoSymbols(const lv_font_t* font, lv_font_glyph_dsc_t* out, uint32_t letter, uint32_t next) {
    if (letter >= 0xE000 && letter <= 0xF8FF) return false;
    return s_ttfGlyphDsc(font, out, letter, next);
}

const lv_font_t* makeFont(const uint8_t* start, const uint8_t* end, int px, const lv_font_t* fallback) {
    lv_font_t* f = lv_tiny_ttf_create_data_ex(start, end - start, px, LV_FONT_KERNING_NORMAL, TTF_GLYPH_CACHE);
    if (!f) {
        LV_LOG_WARN("TinyTTF: font %d px failed, using built-in", px);
        return fallback;
    }
    s_ttfGlyphDsc = f->get_glyph_dsc;  // same TinyTTF callback for every font
    f->get_glyph_dsc = ttfGlyphDscNoSymbols;
    f->fallback = fallback;  // LV_SYMBOL_* icons live only in the built-in Montserrat fonts
    return f;
}
}  // namespace

void initFonts() {
    s_fonts.body = makeFont(font_noto_regular_ttf, font_noto_regular_ttf_end, 22, &lv_font_montserrat_20);
    s_fonts.small = makeFont(font_noto_regular_ttf, font_noto_regular_ttf_end, 18, &lv_font_montserrat_14);
    s_fonts.title = makeFont(font_noto_bold_ttf, font_noto_bold_ttf_end, 28, &lv_font_montserrat_28);
    s_fonts.value = makeFont(font_noto_bold_ttf, font_noto_bold_ttf_end, 44, &lv_font_montserrat_40);
}

const Fonts& fonts() { return s_fonts; }

void applyTheme(lv_display_t* disp) {
    lv_theme_t* th = lv_theme_default_init(disp, color(C_ACCENT), color(C_OK), true, s_fonts.body);
    lv_display_set_theme(disp, th);
    // Top/system layers exist before the theme is set, so their children (modals created with a
    // nullptr parent) would inherit LV_FONT_DEFAULT (Montserrat, no Polish glyphs).
    lv_obj_set_style_text_font(lv_layer_top(), s_fonts.body, 0);
    lv_obj_set_style_text_font(lv_layer_sys(), s_fonts.body, 0);
}

}  // namespace gui
