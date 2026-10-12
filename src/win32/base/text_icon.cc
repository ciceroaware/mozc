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

#include "win32/base/text_icon.h"

#include <safeint.h>
#include <wil/resource.h>
#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "base/win32/wide_char.h"

namespace mozc {
namespace win32 {
namespace {

using ::msl::utilities::SafeCast;
using ::msl::utilities::SafeMultiply;

// The text is first rendered into a 1-bpp bitmap that is |kSupersamplingScale|
// times larger than the icon, then the coverage of each icon pixel is used as
// its alpha value. This gives us anti-aliased glyphs that do not depend on the
// system font smoothing settings.
constexpr int kSupersamplingScale = 8;

struct MonochromeBitmapInfo {
  BITMAPINFOHEADER header;
  RGBQUAD color_palette[2];
};

// Returns true if the font specified by |fontname| is available and has all
// the glyphs to render |text|. Note that GDI silently picks up another font
// when the specified font is not available.
bool HasAllGlyphs(const std::wstring& fontname, const std::wstring& text) {
  if (fontname.empty() || fontname.size() >= LF_FACESIZE || text.empty()) {
    return false;
  }
  wil::unique_hdc dc(::CreateCompatibleDC(nullptr));
  if (!dc.is_valid()) {
    return false;
  }
  LOGFONT logfont = {};
  logfont.lfWeight = FW_NORMAL;
  logfont.lfCharSet = DEFAULT_CHARSET;
  logfont.lfHeight = -16;
  if (wcscpy_s(logfont.lfFaceName, fontname.c_str()) != 0) {
    return false;
  }
  wil::unique_hfont font_handle(::CreateFontIndirect(&logfont));
  if (!font_handle.is_valid()) {
    return false;
  }
  wil::unique_select_object old_font(
      wil::SelectObject(dc.get(), font_handle.get()));

  wchar_t actual_fontname[LF_FACESIZE] = {};
  if (::GetTextFaceW(dc.get(), LF_FACESIZE, actual_fontname) == 0 ||
      ::_wcsicmp(actual_fontname, fontname.c_str()) != 0) {
    return false;
  }

  auto glyph_indices = std::make_unique<WORD[]>(text.size());
  const DWORD result =
      ::GetGlyphIndicesW(dc.get(), text.c_str(), static_cast<int>(text.size()),
                         glyph_indices.get(), GGI_MARK_NONEXISTING_GLYPHS);
  if (result == GDI_ERROR) {
    return false;
  }
  for (size_t i = 0; i < text.size(); ++i) {
    if (glyph_indices[i] == 0xffff) {
      return false;
    }
  }
  return true;
}

HICON CreateMonochromeIconInternal(int bitmap_width, int bitmap_height,
                                   absl::string_view text,
                                   const std::wstring& wide_fontname,
                                   COLORREF text_color, uint8_t opacity,
                                   TextIcon::Scaling scaling) {
  if (bitmap_width <= 0 || bitmap_height <= 0) {
    return nullptr;
  }

  // Step 1. Create a large black-and-white DIB to render the text.
  //  - This is a top-down DIB.
  //  - pixel bit is 1 if the pixel is a part of the text.
  //  - The canvas is twice as large as the supersampled icon so that the ink
  //    box can be measured without being clipped.
  const int canvas_width = bitmap_width * kSupersamplingScale * 2;
  const int canvas_height = bitmap_height * kSupersamplingScale * 2;
  const size_t canvas_stride = ((canvas_width + 31) / 32) * 4;
  uint8_t* canvas_buffer = nullptr;
  wil::unique_hbitmap canvas_dib;
  {
    MonochromeBitmapInfo info = {};
    info.header.biSize = sizeof(info.header);
    info.header.biWidth = canvas_width;
    info.header.biHeight = -canvas_height;  // negative value for top-down BMP
    info.header.biPlanes = 1;
    info.header.biBitCount = 1;
    info.header.biCompression = BI_RGB;
    info.color_palette[0] = {0x00, 0x00, 0x00, 0x00};
    info.color_palette[1] = {0xff, 0xff, 0xff, 0x00};

    canvas_dib.reset(::CreateDIBSection(
        nullptr, reinterpret_cast<const BITMAPINFO*>(&info), DIB_RGB_COLORS,
        reinterpret_cast<void**>(&canvas_buffer), nullptr, 0));
    if (!canvas_dib.is_valid() || canvas_buffer == nullptr) {
      return nullptr;
    }
  }

  const auto is_ink = [&](int x, int y) -> bool {
    if (x < 0 || y < 0 || x >= canvas_width || y >= canvas_height) {
      return false;
    }
    return ((canvas_buffer[canvas_stride * y + x / 8] >> (7 - x % 8)) & 1) != 0;
  };

  const auto draw_text = [&](int font_height) -> bool {
    wil::unique_hdc dc(::CreateCompatibleDC(nullptr));
    if (!dc.is_valid()) {
      return false;
    }

    wil::unique_select_object old_bitmap(
        wil::SelectObject(dc.get(), canvas_dib.get()));
    LOGFONT logfont = {};
    {
      logfont.lfWeight = FW_NORMAL;
      logfont.lfCharSet = DEFAULT_CHARSET;
      // A negative value means the character height (em height).
      logfont.lfHeight = -font_height;
      logfont.lfQuality = NONANTIALIASED_QUALITY;
      const errno_t error = wcscpy_s(logfont.lfFaceName, wide_fontname.c_str());
      if (error != 0) {
        return false;
      }
    }

    wil::unique_hfont font_handle(::CreateFontIndirect(&logfont));
    if (!font_handle.is_valid()) {
      return false;
    }

    wil::unique_select_object old_font(
        wil::SelectObject(dc.get(), font_handle.get()));
    ::SetBkMode(dc.get(), OPAQUE);
    ::SetBkColor(dc.get(), RGB(0x00, 0x00, 0x00));
    ::SetTextColor(dc.get(), RGB(0xff, 0xff, 0xff));
    const std::wstring wide_text = win32::Utf8ToWide(text);
    RECT rect = {0, 0, canvas_width, canvas_height};
    ::ExtTextOutW(dc.get(), 0, 0, ETO_OPAQUE, &rect, nullptr, 0, nullptr);
    ::DrawTextW(dc.get(), wide_text.c_str(), wide_text.size(), &rect,
                DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_CENTER);
    ::GdiFlush();
    return true;
  };

  // Step 2. Draw the text and measure the ink box so that the text can be
  // centered in the icon regardless of the font metrics. The em height of the
  // font is the same as the icon height at first, then the text is drawn again
  // with a different font size depending on |scaling| and the ink box.
  const int target_width = bitmap_width * kSupersamplingScale;
  const int target_height = bitmap_height * kSupersamplingScale;
  constexpr int kMaxAttempts = 3;
  int font_height = target_height;
  int offset_x = 0;
  int offset_y = 0;
  for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
    if (font_height <= 0 || !draw_text(font_height)) {
      return nullptr;
    }
    int ink_left = canvas_width;
    int ink_top = canvas_height;
    int ink_right = -1;
    int ink_bottom = -1;
    for (int y = 0; y < canvas_height; ++y) {
      for (int x = 0; x < canvas_width; ++x) {
        if (!is_ink(x, y)) {
          continue;
        }
        ink_left = std::min(ink_left, x);
        ink_top = std::min(ink_top, y);
        ink_right = std::max(ink_right, x);
        ink_bottom = std::max(ink_bottom, y);
      }
    }
    if (ink_right < ink_left || ink_bottom < ink_top) {
      // Nothing was drawn.
      break;
    }
    const int ink_width = ink_right - ink_left + 1;
    const int ink_height = ink_bottom - ink_top + 1;
    offset_x = (ink_left + ink_right + 1) / 2 - target_width / 2;
    offset_y = (ink_top + ink_bottom + 1) / 2 - target_height / 2;
    const bool fits = ink_width <= target_width && ink_height <= target_height;
    if (fits && (attempt > 0 || scaling == TextIcon::Scaling::kEmHeight)) {
      break;
    }
    const int new_font_height =
        std::min(font_height * target_width / ink_width,
                 font_height * target_height / ink_height);
    if (new_font_height == font_height) {
      break;
    }
    font_height = new_font_height;
  }

  // Step 3. Create XOR bitmap.
  //  - This is a top-down 32-bpp DIB with non-premultiplied alpha.
  //  - The alpha value is the coverage of the text in the pixel multiplied by
  //    |opacity|.
  uint32_t* xor_dib_buffer = nullptr;
  wil::unique_hbitmap xor_dib;
  {
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = bitmap_width;
    info.bmiHeader.biHeight = -bitmap_height;  // top-down BMP
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    xor_dib.reset(::CreateDIBSection(nullptr, &info, DIB_RGB_COLORS,
                                     reinterpret_cast<void**>(&xor_dib_buffer),
                                     nullptr, 0));
    if (!xor_dib.is_valid() || xor_dib_buffer == nullptr) {
      return nullptr;
    }
  }

  // Step 4. Fill XOR bitmap and AND bitmap.
  //  - AND bitmap is a top-down DDB, each line of which is aligned to 2-byte.
  //  - pixel bit is 1 if the pixel should be transparent.
  const size_t mask_stride = (bitmap_width + 15) / 16 * 2;
  auto mask_buffer = std::make_unique<uint8_t[]>(mask_stride * bitmap_height);
  const uint32_t rgb = (static_cast<uint32_t>(GetRValue(text_color)) << 16) |
                       (static_cast<uint32_t>(GetGValue(text_color)) << 8) |
                       static_cast<uint32_t>(GetBValue(text_color));
  constexpr int kSamplesPerPixel = kSupersamplingScale * kSupersamplingScale;
  for (int y = 0; y < bitmap_height; ++y) {
    for (int x = 0; x < bitmap_width; ++x) {
      int count = 0;
      for (int sub_y = 0; sub_y < kSupersamplingScale; ++sub_y) {
        for (int sub_x = 0; sub_x < kSupersamplingScale; ++sub_x) {
          if (is_ink(offset_x + x * kSupersamplingScale + sub_x,
                     offset_y + y * kSupersamplingScale + sub_y)) {
            ++count;
          }
        }
      }
      const uint32_t alpha = count * opacity / kSamplesPerPixel;
      if (alpha == 0) {
        // Keep the XOR pixel 0 so that the destination is not inverted even
        // when the icon is drawn without using the alpha channel.
        mask_buffer[mask_stride * y + x / 8] |= (0x80 >> (x % 8));
      } else {
        xor_dib_buffer[y * bitmap_width + x] = (alpha << 24) | rgb;
      }
    }
  }

  wil::unique_hbitmap mask_ddb(
      ::CreateBitmap(bitmap_width, bitmap_height, 1, 1, mask_buffer.get()));
  if (!mask_ddb.is_valid()) {
    return nullptr;
  }

  // Step 5. Create a GDI ICON object.
  {
    ICONINFO info = {};
    info.fIcon = TRUE;
    info.hbmColor = xor_dib.get();
    info.hbmMask = mask_ddb.get();
    info.xHotspot = 0;
    info.yHotspot = 0;
    return ::CreateIconIndirect(&info);
  }
}

}  // namespace

// static
HICON TextIcon::CreateMonochromeIcon(size_t width, size_t height,
                                     absl::string_view text,
                                     absl::string_view fontname,
                                     COLORREF text_color, uint8_t opacity,
                                     Scaling scaling) {
  int safe_width = 0;
  int safe_height = 0;
  int safe_num_pixels = 0;
  if (!SafeCast(width, safe_width) || !SafeCast(height, safe_height) ||
      !SafeMultiply(safe_width, safe_height, safe_num_pixels)) {
    LOG(ERROR) << "Requested size is too large." << " width: " << width
               << " height: " << height;
    return nullptr;
  }

  return CreateMonochromeIconInternal(safe_width, safe_height, text,
                                      win32::Utf8ToWide(fontname), text_color,
                                      opacity, scaling);
}

// static
HICON TextIcon::CreateMonochromeIcon(
    size_t width, size_t height, absl::string_view text,
    absl::Span<const absl::string_view> fontnames, COLORREF text_color,
    uint8_t opacity, Scaling scaling) {
  const std::wstring wide_text = win32::Utf8ToWide(text);
  for (const absl::string_view fontname : fontnames) {
    const std::wstring wide_fontname = win32::Utf8ToWide(fontname);
    if (HasAllGlyphs(wide_fontname, wide_text)) {
      return CreateMonochromeIcon(width, height, text, fontname, text_color,
                                  opacity, scaling);
    }
  }
  return nullptr;
}

}  // namespace win32
}  // namespace mozc
