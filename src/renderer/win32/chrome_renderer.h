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

#ifndef MOZC_RENDERER_WIN32_CHROME_RENDERER_H_
#define MOZC_RENDERER_WIN32_CHROME_RENDERER_H_

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mozc {
namespace renderer {
namespace win32 {

// Theme parameters for the Fluent depth-8 chrome (rounded fill + 1-DIP black
// stroke + key/ambient drop shadow). Values are float in [0, 1]. The fill
// *color* is sourced from the existing DIB BGR (whatever GDI / D2D drew
// inside the content rect) — see StampChromeOver — so the theme only
// supplies the alpha-side parameters.
struct ChromeTheme {
  float key_alpha = 0.14f;
  float ambient_alpha = 0.14f;
  float stroke_alpha = 0.114f;
};

ChromeTheme MakeLightChromeTheme();
ChromeTheme MakeDarkChromeTheme();

// DPI-resolved 9-patch cache for the chrome bitmap. Theme-independent: a
// single cache feeds both light and dark renders.
struct ChromeCache {
  int dpi = 0;
  float scale = 1.0f;
  int stroke_px = 1;
  // Padding around the content rect that holds the drop shadow.
  int pad_left_px = 0;
  int pad_right_px = 0;
  int pad_top_px = 0;
  int pad_bottom_px = 0;
  // Corner / edge sprite dimensions.
  int cap_w = 0;       // width of left & right caps, and length of edge_left
  int cap_h_top = 0;   // height of top caps, and length of edge_top
  int cap_h_bot = 0;   // height of bottom caps, and length of edge_bot
  // Each entry is a packed (blur_cov, stroke_mask, fill_mask) byte triple.
  std::vector<uint8_t> corner_tl;   // (cap_h_top * cap_w * 3) bytes
  std::vector<uint8_t> corner_bl;   // (cap_h_bot * cap_w * 3) bytes
  std::vector<uint8_t> edge_top;    // (cap_h_top * 3) bytes (1D vertical)
  std::vector<uint8_t> edge_bot;    // (cap_h_bot * 3) bytes (1D vertical)
  std::vector<uint8_t> edge_left;   // (cap_w     * 3) bytes (1D horizontal)
  uint8_t center[3] = {0, 0, 0};

  // Minimum content side length below which the 9-patch decomposition is
  // invalid (opposing-edge shadow tails would interact). Render() clamps
  // the requested content size up to this floor.
  int min_content_dim_px() const {
    return 2 * (cap_w - pad_left_px - stroke_px);
  }
};

// Builds the cache for the given DPI. Run once per DPI; cache theme-free.
ChromeCache BuildChromeCache(int dpi);

// Geometry returned by RenderChromeInto. The caller uses (content_x,
// content_y) to translate the inner draw operations (text, highlights) and
// uses (out_w, out_h) as the layered window's bitmap size.
struct ChromeRenderResult {
  int out_w = 0;
  int out_h = 0;
  int content_x = 0;
  int content_y = 0;
  int content_w = 0;
  int content_h = 0;
};

// Computes the output bitmap size for given content dimensions, without
// rendering. Use this to size the DIB before calling RenderChromeInto.
ChromeRenderResult ComputeChromeLayout(const ChromeCache& cache,
                                       int content_w_px, int content_h_px);

// Stamps the chrome (rounded fill mask + stroke + drop shadow) onto the
// existing top-down 32 bpp BGRA buffer in-place. Every pixel is rewritten
// with premultiplied BGRA based on its current BGR (treated as the fill
// color underneath the mask) and the chrome's alpha math:
//
//   final_BGR = inner_BGR * f         (premultiplied by the fill mask)
//   final_a   = f + (sa + shadow*(1-sa)) * (1-f)
//
// where (b, s, f) come from the DPI-only-keyed cache. Consequences:
//   - Body interior (f=1): final_BGR = inner_BGR, final_a = 255. Whatever
//     GDI/D2D drew inside the content rect shows through opaquely.
//   - Rounded-corner AA fade (0<f<1): inner_BGR fades smoothly into the
//     drop-shadow tint while alpha drops toward the shadow value.
//   - Corner notch / shadow margin / stroke ring (f=0): final_BGR = 0,
//     final_a = stroke_alpha*s + shadow*(1-stroke_alpha*s). The chrome's
//     soft black tint shows through the layered window's transparency.
//
// Caller must ensure |dst| has at least (out_h * dst_stride_bytes) bytes
// capacity with dst_stride_bytes >= out_w * 4. The bitmap area outside the
// content rect should hold premultiplied black (BGR = 0); a freshly-
// created DIB satisfies this automatically and a re-stamped DIB does too,
// since this function never produces non-zero BGR outside the fill mask.
ChromeRenderResult StampChromeOver(const ChromeCache& cache,
                                   const ChromeTheme& theme,
                                   int content_w_px, int content_h_px,
                                   uint8_t* dst, size_t dst_stride_bytes);

}  // namespace win32
}  // namespace renderer
}  // namespace mozc

#endif  // MOZC_RENDERER_WIN32_CHROME_RENDERER_H_
