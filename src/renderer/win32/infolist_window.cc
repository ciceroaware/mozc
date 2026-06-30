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

#include "renderer/win32/infolist_window.h"

#include <atlbase.h>
#include <atltypes.h>
#include <atlwin.h>
#include <wil/resource.h>
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <string>

#include "base/coordinates.h"
#include "base/win32/wide_char.h"
#include "client/client_interface.h"
#include "protocol/candidate_window.pb.h"
#include "protocol/commands.pb.h"
#include "renderer/win32/chrome_renderer.h"
#include "renderer/win32/modern_renderer_style.h"
#include "renderer/win32/text_renderer.h"
#include "renderer/win32/win32_theme_util.h"

namespace mozc {
namespace renderer {
namespace win32 {

using mozc::commands::Information;
using mozc::commands::InformationList;

namespace {

const UINT_PTR kIdDelayShowHideTimer = 100;

// ------------------------------------------------------------------------
// Layout constants (DIP at 96 DPI). Replace the legacy RendererStyle proto.
// ------------------------------------------------------------------------

constexpr int kWindowWidthDip = 420;
constexpr int kWindowPaddingDip = 4;       // outer inset between content edge and rows
constexpr int kFooterHeightDip = 24;
constexpr int kFooterHorizontalPaddingDip = 12;
constexpr int kFooterSeparatorHeightDip = 1;
constexpr int kRowVerticalPaddingDip = 6;
constexpr int kRowHorizontalPaddingDip = 12;
constexpr int kRowDescriptionIndentDip = 12;
constexpr int kRowGapDip = 2;              // visual gap between consecutive rows
constexpr int kSelectedRowCornerRadiusDip = 4;
constexpr int kAccentBarWidthDip = 3;
constexpr int kAccentBarInsetDip = 6;

// Hard-coded footer text. The legacy proto-driven style used the same
// literal as the default caption_string when the caption sat at the top
// of the window; we keep the wording but render it at the bottom now.
constexpr wchar_t kFooterString[] = L"用例";

// ------------------------------------------------------------------------
// Color schemes
// ------------------------------------------------------------------------

// Infolist-specific colors. Footer colors (background, text, separator)
// are intentionally not duplicated here — they are sourced from the
// candidate window's shared ColorScheme so a change there propagates
// automatically.
struct InfolistColors {
  Rgba window_background;
  Rgba title_text;
  Rgba description_text;
  Rgba selected_row_background;
  Rgba selected_accent_bar;
};

constexpr InfolistColors MakeLightColors() {
  InfolistColors c = {};
  // Match candidate window's window_background so the chrome's rounded
  // outer fill blends seamlessly with the inner GDI fill.
  c.window_background        = {0xF9, 0xF9, 0xF9};
  c.title_text               = {0x1A, 0x1A, 0x1A};
  c.description_text         = {0x50, 0x50, 0x50};
  c.selected_row_background  = {0xF3, 0xF3, 0xF3};
  c.selected_accent_bar      = {0x00, 0x67, 0xC0};
  return c;
}

constexpr InfolistColors MakeDarkColors() {
  InfolistColors c = {};
  c.window_background        = {0x2C, 0x2C, 0x2C};
  c.title_text               = {0xE5, 0xE5, 0xE5};
  c.description_text         = {0xC0, 0xC0, 0xC0};
  c.selected_row_background  = {0x3D, 0x3D, 0x3D};
  c.selected_accent_bar      = {0xC5, 0x86, 0xC0};
  return c;
}

InfolistColors ActiveColors() {
  return IsAppsUseLightTheme() ? MakeLightColors() : MakeDarkColors();
}

ChromeTheme ActiveChromeTheme() {
  return IsAppsUseLightTheme() ? MakeLightChromeTheme()
                               : MakeDarkChromeTheme();
}

int Scale(int dip, uint32_t dpi) {
  return ::MulDiv(dip, static_cast<int>(dpi), 96);
}

CRect ToCRect(const Rect& rect) {
  return CRect(rect.Left(), rect.Top(), rect.Right(), rect.Bottom());
}

void FillSolidRect(HDC dc, const RECT* rect, COLORREF color) {
  COLORREF old_color = ::SetBkColor(dc, color);
  if (old_color != CLR_INVALID) {
    ::ExtTextOut(dc, 0, 0, ETO_OPAQUE, rect, nullptr, 0, nullptr);
    ::SetBkColor(dc, old_color);
  }
}

// Software-rasterized AA rounded rectangle. Writes the BGR channels of
// |dst| (a top-down 32 bpp BGRA DIB); leaves alpha untouched, since
// StampChromeOver finalizes alpha later. The interior of the rectangle is
// a fast solid fill; only the four corner squares of side |radius| are
// supersampled.
void FillRoundedRectBgrAa(uint8_t* dst, size_t stride_bytes, int dib_w,
                          int dib_h, const Rect& rect, int radius,
                          COLORREF color) {
  const int x0 = std::max(0, rect.Left());
  const int y0 = std::max(0, rect.Top());
  const int x1 = std::min(dib_w, rect.Right());
  const int y1 = std::min(dib_h, rect.Bottom());
  if (x0 >= x1 || y0 >= y1) {
    return;
  }
  const int r = std::max(
      0, std::min(radius, std::min(rect.Width(), rect.Height()) / 2));
  const float fr = static_cast<float>(r);
  const float fw = static_cast<float>(rect.Width());
  const float fh = static_cast<float>(rect.Height());
  const float r2 = fr * fr;
  const float cb = static_cast<float>(GetBValue(color));
  const float cg = static_cast<float>(GetGValue(color));
  const float cr = static_cast<float>(GetRValue(color));
  const uint8_t cb_u = static_cast<uint8_t>(GetBValue(color));
  const uint8_t cg_u = static_cast<uint8_t>(GetGValue(color));
  const uint8_t cr_u = static_cast<uint8_t>(GetRValue(color));
  constexpr int kSupersample = 4;

  for (int y = y0; y < y1; ++y) {
    const float ly = static_cast<float>(y - rect.Top());
    const bool y_in_strip = (ly >= fr && ly < fh - fr);
    uint8_t* row = dst + static_cast<size_t>(y) * stride_bytes +
                   static_cast<size_t>(x0) * 4;
    for (int x = x0; x < x1; ++x, row += 4) {
      const float lx = static_cast<float>(x - rect.Left());
      const bool x_in_strip = (lx >= fr && lx < fw - fr);
      if (y_in_strip || x_in_strip) {
        row[0] = cb_u;
        row[1] = cg_u;
        row[2] = cr_u;
        continue;
      }
      int hits = 0;
      for (int sy = 0; sy < kSupersample; ++sy) {
        for (int sx = 0; sx < kSupersample; ++sx) {
          const float fx = lx + (sx + 0.5f) / kSupersample;
          const float fy = ly + (sy + 0.5f) / kSupersample;
          const float ax = std::clamp(fx, fr, fw - fr);
          const float ay = std::clamp(fy, fr, fh - fr);
          const float dx = fx - ax;
          const float dy = fy - ay;
          if (dx * dx + dy * dy <= r2) {
            ++hits;
          }
        }
      }
      const float cov = static_cast<float>(hits) /
                        static_cast<float>(kSupersample * kSupersample);
      const float inv = 1.0f - cov;
      row[0] = static_cast<uint8_t>(row[0] * inv + cb * cov + 0.5f);
      row[1] = static_cast<uint8_t>(row[1] * inv + cg * cov + 0.5f);
      row[2] = static_cast<uint8_t>(row[2] * inv + cr * cov + 0.5f);
    }
  }
}

}  // namespace

// ------------------------------------------------------------------------
// InfolistWindow
// ------------------------------------------------------------------------

InfolistWindow::InfolistWindow()
    : send_command_interface_(nullptr),
      candidate_window_(new commands::CandidateWindow),
      dpi_(::GetDpiForSystem()),
      text_renderer_(TextRenderer::Create(dpi_)),
      style_(DefaultModernRendererStyle()),
      chrome_cache_(BuildChromeCache(static_cast<int>(dpi_))),
      chrome_theme_(ActiveChromeTheme()),
      metrics_changed_(false),
      visible_(false) {}

InfolistWindow::~InfolistWindow() { ReleaseDib(); }

void InfolistWindow::RefreshTheme() {
  chrome_theme_ = ActiveChromeTheme();
}

void InfolistWindow::UpdateDpi(uint32_t dpi) {
  if (dpi == dpi_) {
    return;
  }
  dpi_ = dpi;
  text_renderer_->OnDpiChanged(dpi_);
  chrome_cache_ = BuildChromeCache(static_cast<int>(dpi_));
}

void InfolistWindow::OnDestroy() {
  ReleaseDib();
  // PostQuitMessage may stop the message loop even though other
  // windows are not closed. WindowManager should close these windows
  // before process termination.
  ::PostQuitMessage(0);
}

void InfolistWindow::OnGetMinMaxInfo(MINMAXINFO* min_max_info) {
  // Do not restrict the window size in case the candidate window must be
  // very small size.
  min_max_info->ptMinTrackSize.x = 1;
  min_max_info->ptMinTrackSize.y = 1;
  SetMsgHandled(TRUE);
}

void InfolistWindow::OnSettingChange(UINT uFlags, LPCTSTR lpszSection) {
  // Refresh the cached chrome theme when the user toggles light/dark mode.
  if (lpszSection != nullptr) {
    if (::lstrcmpW(lpszSection, L"ImmersiveColorSet") == 0) {
      RefreshTheme();
      Redraw();
    }
  }

  // TextRenderer uses the dialog font; monitor font-related parameters so
  // the next UpdateLayout can refresh the cache.
  switch (uFlags) {
    case 0x1049:  // = SPI_SETCLEARTYPE
    case SPI_SETFONTSMOOTHING:
    case SPI_SETFONTSMOOTHINGCONTRAST:
    case SPI_SETFONTSMOOTHINGORIENTATION:
    case SPI_SETFONTSMOOTHINGTYPE:
    case SPI_SETNONCLIENTMETRICS:
      metrics_changed_ = true;
      break;
    default:
      break;
  }
}

void InfolistWindow::OnTimer(UINT_PTR nIDEvent) {
  if (nIDEvent != kIdDelayShowHideTimer) {
    return;
  }
  if (visible_) {
    DelayShow(0);
  } else {
    DelayHide(0);
  }
}

void InfolistWindow::DelayShow(UINT mseconds) {
  visible_ = true;
  KillTimer(kIdDelayShowHideTimer);
  if (mseconds <= 0) {
    SetWindowPos(HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SendMessageW(WM_NCACTIVATE, FALSE);
  } else {
    SetTimer(kIdDelayShowHideTimer, mseconds, nullptr);
  }
}

void InfolistWindow::DelayHide(UINT mseconds) {
  visible_ = false;
  KillTimer(kIdDelayShowHideTimer);
  if (mseconds <= 0) {
    ShowWindow(SW_HIDE);
  } else {
    SetTimer(kIdDelayShowHideTimer, mseconds, nullptr);
  }
}

void InfolistWindow::UpdateLayout(
    const commands::CandidateWindow& candidate_window) {
  *candidate_window_ = candidate_window;

  RefreshTheme();
  if (metrics_changed_) {
    text_renderer_->OnThemeChanged();
    metrics_changed_ = false;
  }

  // Prime the layered surface so SetWindowPos(SWP_SHOWWINDOW) — which
  // WindowManager calls right after this — has something to display.
  Redraw();
}

void InfolistWindow::SetSendCommandInterface(
    client::SendCommandInterface* send_command_interface) {
  send_command_interface_ = send_command_interface;
}

Size InfolistWindow::ComputeContentSize() const {
  const int content_width_px = Scale(kWindowWidthDip, dpi_);
  const int footer_height_px = Scale(kFooterHeightDip, dpi_);
  const int footer_separator_px = Scale(kFooterSeparatorHeightDip, dpi_);
  const int row_v_pad_px = Scale(kRowVerticalPaddingDip, dpi_);
  const int row_h_pad_px = Scale(kRowHorizontalPaddingDip, dpi_);
  const int desc_indent_px = Scale(kRowDescriptionIndentDip, dpi_);
  const int window_pad_px = Scale(kWindowPaddingDip, dpi_);
  const int row_gap_px = Scale(kRowGapDip, dpi_);

  const int title_text_width_px =
      content_width_px - 2 * row_h_pad_px - 2 * window_pad_px;
  const int desc_text_width_px = title_text_width_px - desc_indent_px;

  int height_px = window_pad_px;

  const InformationList& usages = candidate_window_->usages();
  for (int i = 0; i < usages.information_size(); ++i) {
    const Information& info = usages.information(i);
    const std::wstring title_str = mozc::win32::Utf8ToWide(info.title());
    const std::wstring desc_str = mozc::win32::Utf8ToWide(info.description());
    const Size title_size = text_renderer_->MeasureStringMultiLine(
        TextRenderer::FONTSET_INFOLIST_TITLE, title_str, title_text_width_px);
    const Size desc_size = text_renderer_->MeasureStringMultiLine(
        TextRenderer::FONTSET_INFOLIST_DESCRIPTION, desc_str,
        desc_text_width_px);
    int row_height = title_size.height + desc_size.height + 2 * row_v_pad_px;
    if (i + 1 < usages.information_size()) {
      row_height += row_gap_px;
    }
    height_px += row_height;
  }
  height_px += window_pad_px + footer_separator_px + footer_height_px;

  return Size(content_width_px, height_px);
}

Size InfolistWindow::GetLayoutSize() {
  const Size content = ComputeContentSize();
  const ChromeRenderResult layout =
      ComputeChromeLayout(chrome_cache_, content.width, content.height);
  return Size(layout.out_w, layout.out_h);
}

void InfolistWindow::EnsureDib(int w, int h) {
  if (w <= 0 || h <= 0) {
    return;
  }
  if (!mem_dc_) {
    HDC screen = ::GetDC(nullptr);
    mem_dc_.reset(::CreateCompatibleDC(screen));
    ::ReleaseDC(nullptr, screen);
    if (!mem_dc_) {
      return;
    }
  }
  if (dib_ && dib_w_ == w && dib_h_ == h) {
    return;
  }
  dib_select_.reset();
  dib_.reset();
  dib_bits_ = nullptr;

  BITMAPINFO bmi = {};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = w;
  bmi.bmiHeader.biHeight = -h;  // top-down
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  void* bits = nullptr;
  dib_.reset(::CreateDIBSection(mem_dc_.get(), &bmi, DIB_RGB_COLORS, &bits,
                                nullptr, 0));
  if (!dib_ || bits == nullptr) {
    return;
  }
  dib_select_ = wil::SelectObject(mem_dc_.get(), dib_.get());
  dib_bits_ = bits;
  dib_w_ = w;
  dib_h_ = h;
}

void InfolistWindow::ReleaseDib() {
  dib_select_.reset();
  dib_.reset();
  mem_dc_.reset();
  dib_bits_ = nullptr;
  dib_w_ = 0;
  dib_h_ = 0;
}

void InfolistWindow::Redraw() {
  if (!IsWindow()) {
    return;
  }
  RenderIntoDib();
  if (mem_dc_ == nullptr || dib_ == nullptr) {
    return;
  }
  POINT src_pos = {0, 0};
  SIZE size = {dib_w_, dib_h_};
  BLENDFUNCTION blend = {};
  blend.BlendOp = AC_SRC_OVER;
  blend.SourceConstantAlpha = 255;
  blend.AlphaFormat = AC_SRC_ALPHA;
  ::UpdateLayeredWindow(this->m_hWnd, nullptr, nullptr, &size, mem_dc_.get(),
                        &src_pos, 0, &blend, ULW_ALPHA);
}

void InfolistWindow::RenderIntoDib() {
  const Size content = ComputeContentSize();
  const ChromeRenderResult layout =
      ComputeChromeLayout(chrome_cache_, content.width, content.height);
  content_offset_ = Point(layout.content_x, layout.content_y);

  EnsureDib(layout.out_w, layout.out_h);
  if (mem_dc_ == nullptr || dib_bits_ == nullptr) {
    return;
  }

  const InfolistColors colors = ActiveColors();
  HDC dc = mem_dc_.get();
  ::SetBkMode(dc, TRANSPARENT);

  // 1. Inner content via GDI / Direct2D, mirroring CandidateWindow's
  //    RenderIntoDib pipeline. Pixels outside the content rect are left
  //    untouched (premultiplied black) — StampChromeOver later rewrites
  //    every pixel anyway based on the fill mask.
  const RECT bg_rect = {content_offset_.x, content_offset_.y,
                        content_offset_.x + content.width,
                        content_offset_.y + content.height};
  FillSolidRect(dc, &bg_rect, colors.window_background.ToColorRef());

  const int footer_height_px = Scale(kFooterHeightDip, dpi_);
  const int footer_separator_px = Scale(kFooterSeparatorHeightDip, dpi_);
  const int window_pad_px = Scale(kWindowPaddingDip, dpi_);
  const int row_v_pad_px = Scale(kRowVerticalPaddingDip, dpi_);
  const int row_h_pad_px = Scale(kRowHorizontalPaddingDip, dpi_);
  const int desc_indent_px = Scale(kRowDescriptionIndentDip, dpi_);
  const int row_gap_px = Scale(kRowGapDip, dpi_);

  // 2. Rows. Compute each row's rect from re-measured text sizes; the
  //    cumulative height must match ComputeContentSize() exactly so the
  //    chrome layout aligns with what we draw inside.
  const int title_text_width_px =
      content.width - 2 * row_h_pad_px - 2 * window_pad_px;
  const int desc_text_width_px = title_text_width_px - desc_indent_px;

  int row_y = content_offset_.y + window_pad_px;
  const int row_left = content_offset_.x + window_pad_px;
  const int row_width = content.width - 2 * window_pad_px;

  const InformationList& usages = candidate_window_->usages();
  const bool has_focus = usages.has_focused_index();
  const int focused_row = has_focus ? usages.focused_index() : -1;

  for (int i = 0; i < usages.information_size(); ++i) {
    const Information& info = usages.information(i);
    const std::wstring title_str = mozc::win32::Utf8ToWide(info.title());
    const std::wstring desc_str = mozc::win32::Utf8ToWide(info.description());
    const Size title_size = text_renderer_->MeasureStringMultiLine(
        TextRenderer::FONTSET_INFOLIST_TITLE, title_str, title_text_width_px);
    const Size desc_size = text_renderer_->MeasureStringMultiLine(
        TextRenderer::FONTSET_INFOLIST_DESCRIPTION, desc_str,
        desc_text_width_px);
    const int row_height = title_size.height + desc_size.height +
                           2 * row_v_pad_px;
    const Rect row_rect(row_left, row_y, row_width, row_height);
    DrawRow(dc, i, row_rect, i == focused_row);
    row_y += row_height;
    if (i + 1 < usages.information_size()) {
      row_y += row_gap_px;
    }
  }

  // 3. Footer strip across the bottom of the content area. The separator
  //    sits just above the footer and visually anchors it to the rows.
  const int footer_top_y =
      content_offset_.y + content.height - footer_height_px;
  const ColorScheme& shared_scheme = ActiveColorScheme(style_);
  const RECT separator_rect = {content_offset_.x,
                               footer_top_y - footer_separator_px,
                               content_offset_.x + content.width,
                               footer_top_y};
  FillSolidRect(dc, &separator_rect,
                shared_scheme.footer_separator.ToColorRef());

  const Rect footer_rect(content_offset_.x, footer_top_y, content.width,
                         footer_height_px);
  DrawFooter(dc, footer_rect);

  // 4. Stamp the chrome (rounded fill mask + 1-DIP black stroke + Fluent
  //    depth-8 drop shadow) on top. Flush GDI first so any batched
  //    ExtTextOut / D2D writes from steps 1-3 are visible in the DIB
  //    before StampChromeOver reads them.
  ::GdiFlush();
  StampChromeOver(chrome_cache_, chrome_theme_, content.width, content.height,
                  static_cast<uint8_t*>(dib_bits_),
                  static_cast<size_t>(layout.out_w) * 4);
}

void InfolistWindow::DrawFooter(HDC dc, const Rect& footer_rect) {
  const ColorScheme& scheme = ActiveColorScheme(style_);
  const CRect bg = ToCRect(footer_rect);
  FillSolidRect(dc, &bg, scheme.footer_background.ToColorRef());

  const int footer_h_pad_px = Scale(kFooterHorizontalPaddingDip, dpi_);
  const Rect text_rect(footer_rect.Left() + footer_h_pad_px,
                       footer_rect.Top(),
                       footer_rect.Width() - 2 * footer_h_pad_px,
                       footer_rect.Height());
  text_renderer_->RenderText(dc, kFooterString, text_rect,
                             TextRenderer::FONTSET_INFOLIST_CAPTION,
                             scheme.footer_text.ToColorRef());
}

void InfolistWindow::DrawRow(HDC dc, int row, const Rect& row_rect,
                             bool focused) {
  const InfolistColors colors = ActiveColors();
  const int row_h_pad_px = Scale(kRowHorizontalPaddingDip, dpi_);
  const int row_v_pad_px = Scale(kRowVerticalPaddingDip, dpi_);
  const int desc_indent_px = Scale(kRowDescriptionIndentDip, dpi_);
  const int corner_radius_px = Scale(kSelectedRowCornerRadiusDip, dpi_);
  const int accent_bar_width_px = Scale(kAccentBarWidthDip, dpi_);
  const int accent_bar_inset_px = Scale(kAccentBarInsetDip, dpi_);

  if (focused && dib_bits_ != nullptr) {
    // Direct DIB write for AA rounded corners — flush batched GDI first
    // so the prior background fill is committed before we blend on top.
    ::GdiFlush();
    FillRoundedRectBgrAa(static_cast<uint8_t*>(dib_bits_),
                         static_cast<size_t>(dib_w_) * 4, dib_w_, dib_h_,
                         row_rect, corner_radius_px,
                         colors.selected_row_background.ToColorRef());

    const RECT accent_rect = {
        row_rect.Left(), row_rect.Top() + accent_bar_inset_px,
        row_rect.Left() + accent_bar_width_px,
        row_rect.Bottom() - accent_bar_inset_px};
    FillSolidRect(dc, &accent_rect, colors.selected_accent_bar.ToColorRef());
  }

  // Title spans the full row width minus paddings; description is
  // additionally indented to give a visual hierarchy.
  const Information& info = candidate_window_->usages().information(row);
  const std::wstring title_str = mozc::win32::Utf8ToWide(info.title());
  const std::wstring desc_str = mozc::win32::Utf8ToWide(info.description());
  const int title_text_width_px = row_rect.Width() - 2 * row_h_pad_px;
  const int desc_text_width_px = title_text_width_px - desc_indent_px;
  const Size title_size = text_renderer_->MeasureStringMultiLine(
      TextRenderer::FONTSET_INFOLIST_TITLE, title_str, title_text_width_px);
  const Size desc_size = text_renderer_->MeasureStringMultiLine(
      TextRenderer::FONTSET_INFOLIST_DESCRIPTION, desc_str, desc_text_width_px);

  const Rect title_rect(row_rect.Left() + row_h_pad_px,
                        row_rect.Top() + row_v_pad_px, title_text_width_px,
                        title_size.height);
  const Rect desc_rect(row_rect.Left() + row_h_pad_px + desc_indent_px,
                       title_rect.Bottom(), desc_text_width_px,
                       desc_size.height);

  text_renderer_->RenderText(dc, title_str, title_rect,
                             TextRenderer::FONTSET_INFOLIST_TITLE,
                             colors.title_text.ToColorRef());
  text_renderer_->RenderText(dc, desc_str, desc_rect,
                             TextRenderer::FONTSET_INFOLIST_DESCRIPTION,
                             colors.description_text.ToColorRef());
}

}  // namespace win32
}  // namespace renderer
}  // namespace mozc
