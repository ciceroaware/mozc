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

#ifndef MOZC_WIN32_BASE_TEXT_ICON_H_
#define MOZC_WIN32_BASE_TEXT_ICON_H_

#include <stddef.h>
#include <stdint.h>
#include <windows.h>

#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace mozc {
namespace win32 {

class TextIcon {
 public:
  TextIcon() = delete;
  ~TextIcon() = delete;

  // Specifies how the size of the text is determined.
  enum class Scaling {
    // The em height of the font is the same as the icon height. The font gets
    // smaller only when the ink box of the text does not fit into the icon.
    // This is suitable for symbol fonts, where each glyph is designed to be
    // placed in the em box.
    kEmHeight,
    // The font size is adjusted so that the ink box of the text fits into the
    // icon as large as possible.
    kFitInkBox,
  };

  // Returns a single-color icon with rendering |text| by using given |fontname|
  // and |text_color|. The text is anti-aliased with 32-bpp alpha channel, and
  // centered in the icon based on its ink box. |opacity| is the alpha value of
  // the pixels that are fully covered by the text, where 255 means opaque.
  // The returned icon consists of color bitmap (a.k.a. XOR bitmap) and mask
  // bitmap (a.k.a. AND bitmap). This is mainly because ITfLangBarMgr causes
  // GDI handle leak when ITfLangBarItemButton::GetIcon returns an icon which
  // consists only of mask bitmap (AND bitmap).
  static HICON CreateMonochromeIcon(size_t width, size_t height,
                                    absl::string_view text,
                                    absl::string_view fontname,
                                    COLORREF text_color, uint8_t opacity,
                                    Scaling scaling);

  // Does the same thing as above with the first font in |fontnames| that is
  // available and has all the glyphs to render |text|. Returns nullptr when
  // there is no such font.
  static HICON CreateMonochromeIcon(
      size_t width, size_t height, absl::string_view text,
      absl::Span<const absl::string_view> fontnames, COLORREF text_color,
      uint8_t opacity, Scaling scaling);
};

}  // namespace win32
}  // namespace mozc

#endif  // MOZC_WIN32_BASE_TEXT_ICON_H_
