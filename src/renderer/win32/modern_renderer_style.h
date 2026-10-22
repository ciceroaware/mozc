// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef MOZC_RENDERER_WIN32_MODERN_RENDERER_STYLE_H_
#define MOZC_RENDERER_WIN32_MODERN_RENDERER_STYLE_H_

#include <windows.h>

#include <cstdint>

namespace mozc {
namespace renderer {
namespace win32 {

struct Rgba {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t a = 255;

  constexpr COLORREF ToColorRef() const { return RGB(r, g, b); }
};

// Per-theme color palette. All colors are opaque unless explicitly noted.
//
// |window_background| should match the chrome fill color (chrome_renderer's
// ChromeTheme.fill_*). The chrome paints the rounded fill outside the
// content rect and a rectangular fill inside it; an inner GDI fill of
// |window_background| then overpaints the inner rect at exactly the same
// color, leaving the rounded AA corners untouched.
struct ColorScheme {
  Rgba window_background;
  Rgba row_text;
  Rgba row_text_dim;            // index numbers ("1", "2", ...)
  Rgba row_text_description;
  Rgba selected_row_background;
  Rgba selected_accent_bar;     // vertical stripe on the focused row
  Rgba footer_background;
  Rgba footer_text;
  Rgba footer_separator;
  Rgba scroll_dot;
};

// Modernized candidate window style. Pure C++ data; no proto.
//
// All `*_dip` fields are in DPI-independent pixels (96 DPI = 1.0). Callers
// scale them for the active monitor DPI before laying out / painting.
struct ModernRendererStyle {
  ColorScheme light;
  ColorScheme dark;

  // Window chrome.
  int corner_radius_dip = 8;
  int window_padding_dip = 4;     // inner inset between border and content

  // Row metrics.
  int row_height_dip = 32;
  int row_horizontal_padding_dip = 12;
  int index_to_candidate_gap_dip = 8;
  int candidate_to_description_gap_dip = 16;

  // Selection accent bar (vertical stripe at the row's leading edge).
  int accent_bar_width_dip = 3;
  int accent_bar_inset_dip = 6;   // top/bottom inset within the row

  // Corner radius for the focused row's highlight rectangle.
  int selected_row_corner_radius_dip = 4;

  // Right-side scroll indicator (three vertical dots).
  int scroll_column_width_dip = 16;
  int scroll_dot_diameter_dip = 3;
  int scroll_dot_gap_dip = 4;

  // Footer.
  int footer_height_dip = 28;
  int footer_separator_height_dip = 1;
  int footer_horizontal_padding_dip = 12;
};

// Returns a ModernRendererStyle with MS-IME-flavored defaults for both
// light and dark color schemes.
ModernRendererStyle DefaultModernRendererStyle();

// Picks the active color scheme based on the current Windows app theme.
const ColorScheme& ActiveColorScheme(const ModernRendererStyle& style);

}  // namespace win32
}  // namespace renderer
}  // namespace mozc

#endif  // MOZC_RENDERER_WIN32_MODERN_RENDERER_STYLE_H_
