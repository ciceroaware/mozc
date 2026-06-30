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

// Fluent depth-8 chrome renderer. Direct port of the algorithm validated in
// reproduction/cache9patch.py and reproduction/cpp_demo_layered/main.cpp;
// see reproduction/ALGORITHM.md and reproduction/MOZC_INTEGRATION.md for
// derivation and golden-image baselines.

#include "renderer/win32/chrome_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace mozc {
namespace renderer {
namespace win32 {

namespace {

// Chrome geometry constants in DIP (96 DPI = 1 unit).
constexpr float kCornerRadiusDip = 8.0f;
constexpr float kStrokeDip = 1.0f;
constexpr float kKeyOffsetYDip = 4.0f;
constexpr float kKeySigmaDip = 4.0f;
// The depth-8 spec has key and ambient sharing offset and sigma; we exploit
// that by computing only one Gaussian-blurred coverage channel.
static_assert(kKeyOffsetYDip == 4.0f && kKeySigmaDip == 4.0f,
              "depth-8 collapse assumes shared offset/sigma");

inline uint8_t ToU8(float v) {
  v = std::clamp(v, 0.0f, 1.0f);
  return static_cast<uint8_t>(v * 255.0f + 0.5f);
}

// Supersampled coverage of a rounded rectangle of size (w, h) and corner
// radius r. Returns row-major (h, w) coverage in [0, 1].
std::vector<float> RoundedRectCoverage(int w, int h, float r) {
  constexpr int kSupersample = 4;
  std::vector<float> out(static_cast<size_t>(w) * static_cast<size_t>(h),
                         0.0f);
  const float fw = static_cast<float>(w);
  const float fh = static_cast<float>(h);
  const float r2 = r * r;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      int hits = 0;
      for (int sy = 0; sy < kSupersample; ++sy) {
        for (int sx = 0; sx < kSupersample; ++sx) {
          const float fx = x + (sx + 0.5f) / kSupersample;
          const float fy = y + (sy + 0.5f) / kSupersample;
          const float cx = std::clamp(fx, r, fw - r);
          const float cy = std::clamp(fy, r, fh - r);
          const float dx = fx - cx;
          const float dy = fy - cy;
          if (dx * dx + dy * dy <= r2) {
            ++hits;
          }
        }
      }
      out[static_cast<size_t>(y) * static_cast<size_t>(w) +
          static_cast<size_t>(x)] =
          static_cast<float>(hits) / static_cast<float>(kSupersample *
                                                        kSupersample);
    }
  }
  return out;
}

// Separable Gaussian blur with truncated kernel; mirrors scipy's
// ndimage.gaussian_filter(mode='constant', cval=0).
std::vector<float> GaussianBlur(const std::vector<float>& src, int w, int h,
                                float sigma) {
  const int radius = std::max(1, static_cast<int>(std::ceil(3.0f * sigma)));
  std::vector<float> kernel(static_cast<size_t>(2 * radius + 1));
  float ksum = 0.0f;
  for (int k = -radius; k <= radius; ++k) {
    const float v =
        std::exp(-static_cast<float>(k * k) / (2.0f * sigma * sigma));
    kernel[static_cast<size_t>(k + radius)] = v;
    ksum += v;
  }
  for (auto& v : kernel) {
    v /= ksum;
  }

  std::vector<float> tmp(src.size(), 0.0f);
  for (int y = 0; y < h; ++y) {
    const float* row = &src[static_cast<size_t>(y) * static_cast<size_t>(w)];
    float* out_row = &tmp[static_cast<size_t>(y) * static_cast<size_t>(w)];
    for (int x = 0; x < w; ++x) {
      float acc = 0.0f;
      const int k0 = std::max(-radius, -x);
      const int k1 = std::min(radius, w - 1 - x);
      for (int k = k0; k <= k1; ++k) {
        acc += kernel[static_cast<size_t>(k + radius)] * row[x + k];
      }
      out_row[x] = acc;
    }
  }
  std::vector<float> dst(src.size(), 0.0f);
  for (int x = 0; x < w; ++x) {
    for (int y = 0; y < h; ++y) {
      float acc = 0.0f;
      const int k0 = std::max(-radius, -y);
      const int k1 = std::min(radius, h - 1 - y);
      for (int k = k0; k <= k1; ++k) {
        acc += kernel[static_cast<size_t>(k + radius)] *
               tmp[static_cast<size_t>(y + k) * static_cast<size_t>(w) +
                   static_cast<size_t>(x)];
      }
      dst[static_cast<size_t>(y) * static_cast<size_t>(w) +
          static_cast<size_t>(x)] = acc;
    }
  }
  return dst;
}

}  // namespace

ChromeTheme MakeLightChromeTheme() {
  ChromeTheme t;
  t.key_alpha = 0.14f;
  t.ambient_alpha = 0.14f;
  t.stroke_alpha = 0.114f;
  return t;
}

ChromeTheme MakeDarkChromeTheme() {
  ChromeTheme t;
  t.key_alpha = 0.28f;
  t.ambient_alpha = 0.14f;
  t.stroke_alpha = 0.287f;
  return t;
}

ChromeCache BuildChromeCache(int dpi) {
  ChromeCache c;
  c.dpi = dpi;
  c.scale = static_cast<float>(dpi) / 96.0f;
  c.stroke_px = std::max(1, static_cast<int>(std::round(kStrokeDip * c.scale)));

  const float r_px = kCornerRadiusDip * c.scale;
  const float sigma_px = kKeySigmaDip * c.scale;
  const int key_y_px = static_cast<int>(std::round(kKeyOffsetYDip * c.scale));
  const int margin = static_cast<int>(std::ceil(3.0f * sigma_px));
  const int r_ceil = static_cast<int>(std::ceil(r_px));

  c.pad_left_px = margin + 1;
  c.pad_right_px = c.pad_left_px;
  c.pad_top_px = std::max(margin - key_y_px, 0) + 1;
  c.pad_bottom_px = margin + key_y_px + 1;

  c.cap_w = c.pad_left_px + r_ceil + c.stroke_px + margin;
  c.cap_h_top = c.pad_top_px + r_ceil + c.stroke_px + key_y_px + margin;
  c.cap_h_bot = c.pad_bottom_px + r_ceil + c.stroke_px - key_y_px + margin;

  // Build a small canvas just large enough that a 1D-extruded body band
  // exists between the caps, so we can sample one row/column for the edge
  // profiles and one pixel for the center constant.
  const int body_extent = r_ceil + margin;
  const int build_fw = 2 * body_extent + 4;
  const int build_fh = 2 * body_extent + 4;
  const int build_cw =
      c.pad_left_px + (build_fw + 2 * c.stroke_px) + c.pad_right_px;
  const int build_ch =
      c.pad_top_px + (build_fh + 2 * c.stroke_px) + c.pad_bottom_px;

  const auto fill_inner = RoundedRectCoverage(build_fw, build_fh, r_px);
  const auto out_inner = RoundedRectCoverage(build_fw + 2 * c.stroke_px,
                                             build_fh + 2 * c.stroke_px,
                                             r_px + c.stroke_px);

  std::vector<float> fill_mask(
      static_cast<size_t>(build_cw) * static_cast<size_t>(build_ch), 0.0f);
  std::vector<float> out_mask(fill_mask.size(), 0.0f);
  std::vector<float> stroke_mask(fill_mask.size(), 0.0f);

  for (int y = 0; y < build_fh; ++y) {
    for (int x = 0; x < build_fw; ++x) {
      fill_mask[static_cast<size_t>(c.pad_top_px + c.stroke_px + y) *
                    static_cast<size_t>(build_cw) +
                static_cast<size_t>(c.pad_left_px + c.stroke_px + x)] =
          fill_inner[static_cast<size_t>(y) * static_cast<size_t>(build_fw) +
                     static_cast<size_t>(x)];
    }
  }
  const int ow = build_fw + 2 * c.stroke_px;
  const int oh = build_fh + 2 * c.stroke_px;
  for (int y = 0; y < oh; ++y) {
    for (int x = 0; x < ow; ++x) {
      out_mask[static_cast<size_t>(c.pad_top_px + y) *
                   static_cast<size_t>(build_cw) +
               static_cast<size_t>(c.pad_left_px + x)] =
          out_inner[static_cast<size_t>(y) * static_cast<size_t>(ow) +
                    static_cast<size_t>(x)];
    }
  }
  for (size_t i = 0; i < stroke_mask.size(); ++i) {
    stroke_mask[i] = std::clamp(out_mask[i] - fill_mask[i], 0.0f, 1.0f);
  }

  std::vector<float> shifted(fill_mask.size(), 0.0f);
  for (int y = key_y_px; y < build_ch; ++y) {
    std::memcpy(&shifted[static_cast<size_t>(y) * static_cast<size_t>(build_cw)],
                &out_mask[static_cast<size_t>(y - key_y_px) *
                          static_cast<size_t>(build_cw)],
                sizeof(float) * static_cast<size_t>(build_cw));
  }

  const auto blur_cov = GaussianBlur(shifted, build_cw, build_ch, sigma_px);

  const int body_y0 = c.cap_h_top;
  const int body_y1 = build_ch - c.cap_h_bot;
  const int body_x0 = c.cap_w;
  const int body_x1 = build_cw - c.cap_w;
  const int mid_y = (body_y0 + body_y1) / 2;
  const int mid_x = (body_x0 + body_x1) / 2;

  const auto pack_at = [&](std::vector<uint8_t>& dst, size_t i, size_t src) {
    dst[i + 0] = ToU8(blur_cov[src]);
    dst[i + 1] = ToU8(stroke_mask[src]);
    dst[i + 2] = ToU8(fill_mask[src]);
  };

  c.corner_tl.assign(
      static_cast<size_t>(c.cap_h_top) * static_cast<size_t>(c.cap_w) * 3, 0);
  for (int y = 0; y < c.cap_h_top; ++y) {
    for (int x = 0; x < c.cap_w; ++x) {
      pack_at(c.corner_tl,
              static_cast<size_t>(y) * static_cast<size_t>(c.cap_w) * 3 +
                  static_cast<size_t>(x) * 3,
              static_cast<size_t>(y) * static_cast<size_t>(build_cw) +
                  static_cast<size_t>(x));
    }
  }
  c.corner_bl.assign(
      static_cast<size_t>(c.cap_h_bot) * static_cast<size_t>(c.cap_w) * 3, 0);
  for (int y = 0; y < c.cap_h_bot; ++y) {
    for (int x = 0; x < c.cap_w; ++x) {
      pack_at(c.corner_bl,
              static_cast<size_t>(y) * static_cast<size_t>(c.cap_w) * 3 +
                  static_cast<size_t>(x) * 3,
              static_cast<size_t>(build_ch - c.cap_h_bot + y) *
                      static_cast<size_t>(build_cw) +
                  static_cast<size_t>(x));
    }
  }
  c.edge_top.assign(static_cast<size_t>(c.cap_h_top) * 3, 0);
  for (int y = 0; y < c.cap_h_top; ++y) {
    pack_at(c.edge_top, static_cast<size_t>(y) * 3,
            static_cast<size_t>(y) * static_cast<size_t>(build_cw) +
                static_cast<size_t>(mid_x));
  }
  c.edge_bot.assign(static_cast<size_t>(c.cap_h_bot) * 3, 0);
  for (int y = 0; y < c.cap_h_bot; ++y) {
    pack_at(c.edge_bot, static_cast<size_t>(y) * 3,
            static_cast<size_t>(build_ch - c.cap_h_bot + y) *
                    static_cast<size_t>(build_cw) +
                static_cast<size_t>(mid_x));
  }
  c.edge_left.assign(static_cast<size_t>(c.cap_w) * 3, 0);
  for (int x = 0; x < c.cap_w; ++x) {
    pack_at(c.edge_left, static_cast<size_t>(x) * 3,
            static_cast<size_t>(mid_y) * static_cast<size_t>(build_cw) +
                static_cast<size_t>(x));
  }
  {
    const size_t i = static_cast<size_t>(mid_y) *
                         static_cast<size_t>(build_cw) +
                     static_cast<size_t>(mid_x);
    c.center[0] = ToU8(blur_cov[i]);
    c.center[1] = ToU8(stroke_mask[i]);
    c.center[2] = ToU8(fill_mask[i]);
  }
  return c;
}

namespace {

// Returns the pointer + offset of the cache cell at (x, y) within an output
// canvas of size (out_w, out_h). The 9-patch decomposition: corners come
// from corner_tl/bl, edges from the 1D edge profiles (mirrored on the
// right), the center is a single byte triple.
struct FetchResult {
  const uint8_t* src;
  size_t i;
};
inline FetchResult FetchCacheCell(const ChromeCache& c, int y, int x,
                                  int out_w, int out_h) {
  if (y < c.cap_h_top) {
    if (x < c.cap_w) {
      return {c.corner_tl.data(),
              static_cast<size_t>(y) * static_cast<size_t>(c.cap_w) * 3 +
                  static_cast<size_t>(x) * 3};
    }
    if (x >= out_w - c.cap_w) {
      const int xc = (out_w - 1) - x;
      return {c.corner_tl.data(),
              static_cast<size_t>(y) * static_cast<size_t>(c.cap_w) * 3 +
                  static_cast<size_t>(xc) * 3};
    }
    return {c.edge_top.data(), static_cast<size_t>(y) * 3};
  }
  if (y >= out_h - c.cap_h_bot) {
    const int yb = y - (out_h - c.cap_h_bot);
    if (x < c.cap_w) {
      return {c.corner_bl.data(),
              static_cast<size_t>(yb) * static_cast<size_t>(c.cap_w) * 3 +
                  static_cast<size_t>(x) * 3};
    }
    if (x >= out_w - c.cap_w) {
      const int xc = (out_w - 1) - x;
      return {c.corner_bl.data(),
              static_cast<size_t>(yb) * static_cast<size_t>(c.cap_w) * 3 +
                  static_cast<size_t>(xc) * 3};
    }
    return {c.edge_bot.data(), static_cast<size_t>(yb) * 3};
  }
  if (x < c.cap_w) {
    return {c.edge_left.data(), static_cast<size_t>(x) * 3};
  }
  if (x >= out_w - c.cap_w) {
    const int xc = (out_w - 1) - x;
    return {c.edge_left.data(), static_cast<size_t>(xc) * 3};
  }
  return {c.center, 0};
}

}  // namespace

ChromeRenderResult ComputeChromeLayout(const ChromeCache& cache,
                                       int content_w_px, int content_h_px) {
  const int min_dim = cache.min_content_dim_px();
  if (content_w_px < min_dim) content_w_px = min_dim;
  if (content_h_px < min_dim) content_h_px = min_dim;

  ChromeRenderResult r;
  const int sw = cache.stroke_px;
  r.out_w = cache.pad_left_px + sw + content_w_px + sw + cache.pad_right_px;
  r.out_h = cache.pad_top_px + sw + content_h_px + sw + cache.pad_bottom_px;
  r.content_x = cache.pad_left_px + sw;
  r.content_y = cache.pad_top_px + sw;
  r.content_w = content_w_px;
  r.content_h = content_h_px;
  return r;
}

ChromeRenderResult StampChromeOver(const ChromeCache& cache,
                                   const ChromeTheme& theme,
                                   int content_w_px, int content_h_px,
                                   uint8_t* dst, size_t dst_stride_bytes) {
  const ChromeRenderResult layout =
      ComputeChromeLayout(cache, content_w_px, content_h_px);

  for (int y = 0; y < layout.out_h; ++y) {
    uint8_t* row = dst + static_cast<size_t>(y) * dst_stride_bytes;
    for (int x = 0; x < layout.out_w; ++x, row += 4) {
      const FetchResult fr =
          FetchCacheCell(cache, y, x, layout.out_w, layout.out_h);
      const float b = fr.src[fr.i + 0] * (1.0f / 255.0f);
      const float s = fr.src[fr.i + 1] * (1.0f / 255.0f);
      const float f = fr.src[fr.i + 2] * (1.0f / 255.0f);
      const float out_m = std::min(s + f, 1.0f);
      const float key_a = theme.key_alpha * b;
      const float amb_a = theme.ambient_alpha * b;
      const float shadow =
          (1.0f - (1.0f - key_a) * (1.0f - amb_a)) * (1.0f - out_m);
      const float sa = theme.stroke_alpha * s;
      const float black = sa + shadow * (1.0f - sa);
      const float final_a = f + black * (1.0f - f);

      // Premultiplied BGRA. The fill *color* comes from whatever GDI or
      // D2D wrote into this pixel earlier — premultiply it by the chrome's
      // mask f. The stroke and shadow are pure black, so they contribute 0
      // to BGR; the (1-f) term goes entirely into alpha.
      row[0] = ToU8(row[0] * (1.0f / 255.0f) * f);
      row[1] = ToU8(row[1] * (1.0f / 255.0f) * f);
      row[2] = ToU8(row[2] * (1.0f / 255.0f) * f);
      row[3] = ToU8(final_a);
    }
  }
  return layout;
}

}  // namespace win32
}  // namespace renderer
}  // namespace mozc
