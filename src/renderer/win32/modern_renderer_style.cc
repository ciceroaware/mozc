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

#include "renderer/win32/modern_renderer_style.h"

#include "renderer/win32/win32_theme_util.h"

namespace mozc {
namespace renderer {
namespace win32 {

namespace {

constexpr ColorScheme MakeLightScheme() {
  ColorScheme s = {};
  // Matches ChromeTheme.fill_{r,g,b} for the light theme so the inner GDI
  // fill blends seamlessly into the chrome's rounded outer fill.
  s.window_background        = {0xF9, 0xF9, 0xF9};
  s.row_text                 = {0x1A, 0x1A, 0x1A};
  s.row_text_dim             = {0x76, 0x76, 0x76};
  s.row_text_description     = {0x76, 0x76, 0x76};
  s.selected_row_background  = {0xF3, 0xF3, 0xF3};
  s.selected_accent_bar      = {0x00, 0x67, 0xC0};   // Win11 accent blue
  s.footer_background        = {0xFA, 0xFA, 0xFA};
  s.footer_text              = {0x60, 0x60, 0x60};
  s.footer_separator         = {0xE5, 0xE5, 0xE5};
  s.scroll_dot               = {0xBD, 0xBD, 0xBD};
  return s;
}

constexpr ColorScheme MakeDarkScheme() {
  ColorScheme s = {};
  // Matches ChromeTheme.fill_{r,g,b} for the dark theme.
  s.window_background        = {0x2C, 0x2C, 0x2C};
  s.row_text                 = {0xE5, 0xE5, 0xE5};
  s.row_text_dim             = {0x90, 0x90, 0x90};
  s.row_text_description     = {0x90, 0x90, 0x90};
  s.selected_row_background  = {0x3D, 0x3D, 0x3D};
  s.selected_accent_bar      = {0xC5, 0x86, 0xC0};   // soft magenta
  s.footer_background        = {0x35, 0x35, 0x35};
  s.footer_text              = {0xB0, 0xB0, 0xB0};
  s.footer_separator         = {0x4A, 0x4A, 0x4A};
  s.scroll_dot               = {0x6E, 0x6E, 0x6E};
  return s;
}

}  // namespace

ModernRendererStyle DefaultModernRendererStyle() {
  ModernRendererStyle style;
  style.light = MakeLightScheme();
  style.dark = MakeDarkScheme();
  return style;
}

const ColorScheme& ActiveColorScheme(const ModernRendererStyle& style) {
  return IsAppsUseLightTheme() ? style.light : style.dark;
}

}  // namespace win32
}  // namespace renderer
}  // namespace mozc
