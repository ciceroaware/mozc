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

#include "renderer/win32/candidate_window.h"

#include <atlbase.h>
#include <atltypes.h>
#include <atlwin.h>
#include <wil/resource.h>
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "base/coordinates.h"
#include "base/win32/wide_char.h"
#include "client/client_interface.h"
#include "protocol/candidate_window.pb.h"
#include "protocol/renderer_command.pb.h"
#include "renderer/win32/chrome_renderer.h"
#include "renderer/win32/modern_candidate_layout.h"
#include "renderer/win32/modern_renderer_style.h"
#include "renderer/win32/text_renderer.h"
#include "renderer/win32/win32_theme_util.h"

namespace mozc {
namespace renderer {
namespace win32 {
namespace {

CRect ToCRect(const Rect& rect) {
  return CRect(rect.Left(), rect.Top(), rect.Right(), rect.Bottom());
}

Rect ShiftRect(const Rect& r, const Point& offset) {
  return Rect(r.Left() + offset.x, r.Top() + offset.y, r.Width(), r.Height());
}

ChromeTheme ActiveChromeTheme() {
  return IsAppsUseLightTheme() ? MakeLightChromeTheme()
                               : MakeDarkChromeTheme();
}

// Returns the smallest index of the given candidate list which satisfies
// candidates.candidate(i) == |candidate_index|.
// This function returns the size of the given candidate list when there
// aren't any candidates satisfying the above condition.
int GetCandidateArrayIndexByCandidateIndex(
    const commands::CandidateWindow& candidate_window, int candidate_index) {
  for (size_t i = 0; i < candidate_window.candidate_size(); ++i) {
    if (candidate_window.candidate(i).index() == candidate_index) {
      return i;
    }
  }
  return candidate_window.candidate_size();
}

// Returns the smallest index of the given candidate list which satisfies
// |candidates.focused_index| == |candidates.candidate(i).index()|.
// Returns the size of the given candidate list when there is no focus.
int GetFocusedArrayIndex(const commands::CandidateWindow& candidate_window) {
  if (!candidate_window.has_focused_index()) {
    return candidate_window.candidate_size();
  }
  return GetCandidateArrayIndexByCandidateIndex(
      candidate_window, candidate_window.focused_index());
}

// Returns the displayable shortcut/index text (e.g. "1") for a candidate.
std::wstring GetIndexText(
    const commands::CandidateWindow::Candidate& candidate) {
  if (candidate.has_annotation() && candidate.annotation().has_shortcut()) {
    return mozc::win32::Utf8ToWide(candidate.annotation().shortcut());
  }
  return std::wstring();
}

// Returns the displayable candidate text (value with optional prefix/suffix).
std::wstring GetCandidateText(
    const commands::CandidateWindow::Candidate& candidate) {
  std::wstring out;
  if (candidate.has_annotation() && candidate.annotation().has_prefix()) {
    out += mozc::win32::Utf8ToWide(candidate.annotation().prefix());
  }
  if (candidate.has_value()) {
    out += mozc::win32::Utf8ToWide(candidate.value());
  }
  if (candidate.has_annotation() && candidate.annotation().has_suffix()) {
    out += mozc::win32::Utf8ToWide(candidate.annotation().suffix());
  }
  return out;
}

// Returns the displayable description text for a candidate (e.g. "ひらがな").
std::wstring GetDescriptionText(
    const commands::CandidateWindow::Candidate& candidate) {
  if (candidate.has_annotation() && candidate.annotation().has_description()) {
    return mozc::win32::Utf8ToWide(candidate.annotation().description());
  }
  return std::wstring();
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
        // Inside one of the two interior strips: full coverage.
        row[0] = cb_u;
        row[1] = cg_u;
        row[2] = cr_u;
        continue;
      }
      // Corner zone — supersample coverage against the corner arc.
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

void FillSolidEllipse(HDC dc, const RECT& rect, COLORREF color) {
  wil::unique_hbrush brush(::CreateSolidBrush(color));
  if (!brush.is_valid()) {
    return;
  }
  HGDIOBJ old_brush = ::SelectObject(dc, brush.get());
  HGDIOBJ old_pen = ::SelectObject(dc, ::GetStockObject(NULL_PEN));
  // Ellipse() excludes the right/bottom edge — extend by 1 to fill the rect.
  ::Ellipse(dc, rect.left, rect.top, rect.right + 1, rect.bottom + 1);
  ::SelectObject(dc, old_pen);
  ::SelectObject(dc, old_brush);
}

}  // namespace

// ------------------------------------------------------------------------
// CandidateWindow
// ------------------------------------------------------------------------

CandidateWindow::CandidateWindow()
    : candidate_window_(std::make_unique<commands::CandidateWindow>()),
      send_command_interface_(nullptr),
      layout_(std::make_unique<ModernCandidateLayout>()),
      style_(DefaultModernRendererStyle()),
      active_scheme_(ActiveColorScheme(style_)),
      dpi_(::GetDpiForSystem()),
      text_renderer_(TextRenderer::Create(dpi_)),
      chrome_cache_(BuildChromeCache(static_cast<int>(dpi_))),
      chrome_theme_(ActiveChromeTheme()),
      metrics_changed_(false),
      mouse_moving_(true) {}

CandidateWindow::~CandidateWindow() { ReleaseDib(); }

LRESULT CandidateWindow::OnCreate(LPCREATESTRUCT create_struct) {
  EnableOrDisableWindowForWorkaround();
  return 0;
}

void CandidateWindow::RefreshTheme() {
  active_scheme_ = ActiveColorScheme(style_);
  chrome_theme_ = ActiveChromeTheme();
}

void CandidateWindow::UpdateDpi(uint32_t dpi) {
  if (dpi == dpi_) {
    return;
  }
  dpi_ = dpi;
  text_renderer_->OnDpiChanged(dpi_);
  chrome_cache_ = BuildChromeCache(static_cast<int>(dpi_));
}

void CandidateWindow::EnableOrDisableWindowForWorkaround() {
  // Disable the window if SPI_GETACTIVEWINDOWTRACKING is enabled.
  // See b/2317702 for details.
  // TODO(yukawa): Support mouse operations before we add a GUI feature which
  //   requires UI interaction by mouse and/or touch. (b/2954874)
  BOOL is_tracking_enabled = FALSE;
  if (::SystemParametersInfo(SPI_GETACTIVEWINDOWTRACKING, 0,
                             &is_tracking_enabled, 0)) {
    EnableWindow(!is_tracking_enabled);
  }
}

void CandidateWindow::OnDestroy() {
  ReleaseDib();
  // PostQuitMessage may stop the message loop even though other
  // windows are not closed. WindowManager should close these windows
  // before process termination.
  ::PostQuitMessage(0);
}

void CandidateWindow::OnGetMinMaxInfo(MINMAXINFO* min_max_info) {
  // Do not restrict the window size in case the candidate window must be
  // very small size.
  min_max_info->ptMinTrackSize.x = 1;
  min_max_info->ptMinTrackSize.y = 1;
  SetMsgHandled(TRUE);
}

void CandidateWindow::HandleMouseEvent(UINT nFlags, const CPoint& point,
                                       bool close_candidatewindow) {
  if (send_command_interface_ == nullptr) {
    LOG(ERROR) << "send_command_interface_ is nullptr";
    return;
  }
  if (!layout_->IsFrozen()) {
    return;
  }
  for (size_t i = 0; i < candidate_window_->candidate_size(); ++i) {
    const CRect rect =
        ToCRect(ShiftRect(layout_->GetRowRect(i), content_offset_));
    if (rect.PtInRect(point)) {
      commands::SessionCommand command;
      if (close_candidatewindow) {
        command.set_type(commands::SessionCommand::SELECT_CANDIDATE);
      } else {
        command.set_type(commands::SessionCommand::HIGHLIGHT_CANDIDATE);
      }
      command.set_id(candidate_window_->candidate(i).id());
      commands::Output output;
      send_command_interface_->SendCommand(command, &output);
      return;
    }
  }
}

void CandidateWindow::OnLButtonDown(UINT nFlags, CPoint point) {
  HandleMouseEvent(nFlags, point, false);
}

void CandidateWindow::OnLButtonUp(UINT nFlags, CPoint point) {
  HandleMouseEvent(nFlags, point, true);
}

void CandidateWindow::OnMouseMove(UINT nFlags, CPoint point) {
  if (!mouse_moving_) {
    return;
  }
  if ((nFlags & MK_LBUTTON) != MK_LBUTTON) {
    return;
  }
  HandleMouseEvent(nFlags, point, false);
}

void CandidateWindow::OnSettingChange(UINT uFlags, LPCTSTR lpszSection) {
  // Refresh the cached color scheme when the user toggles light/dark mode.
  // Windows broadcasts a WM_SETTINGCHANGE with section "ImmersiveColorSet"
  // when this happens.
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
    case SPI_SETACTIVEWINDOWTRACKING:
      EnableOrDisableWindowForWorkaround();
      [[fallthrough]];
    default:
      // We ignore other changes.
      break;
  }
}

void CandidateWindow::UpdateLayout(
    const commands::CandidateWindow& candidates) {
  *candidate_window_ = candidates;

  // Pick up theme/font changes seen since the last layout.
  RefreshTheme();
  if (metrics_changed_) {
    text_renderer_->OnThemeChanged();
    metrics_changed_ = false;
  }

  switch (candidate_window_->category()) {
    case commands::CONVERSION:
    case commands::PREDICTION:
    case commands::TRANSLITERATION:
    case commands::SUGGESTION:
    case commands::USAGE:
      break;
    default:
      LOG(INFO) << "Unknown candidates category: "
                << candidate_window_->category();
      return;
  }

  layout_->Initialize(style_, dpi_);
  const int row_count = candidate_window_->candidate_size();
  layout_->SetRowCount(row_count);

  const bool has_scroll =
      candidate_window_->candidate_size() < candidate_window_->size();
  layout_->SetScrollIndicatorVisible(has_scroll);

  for (int i = 0; i < row_count; ++i) {
    const auto& candidate = candidate_window_->candidate(i);
    const std::wstring index_text = GetIndexText(candidate);
    const std::wstring candidate_text = GetCandidateText(candidate);
    const std::wstring description_text = GetDescriptionText(candidate);

    if (!index_text.empty()) {
      const Size size = text_renderer_->MeasureString(
          TextRenderer::FONTSET_SHORTCUT, index_text);
      layout_->EnsureIndexColumnWidth(size.width);
    }
    if (!candidate_text.empty()) {
      const Size size = text_renderer_->MeasureString(
          TextRenderer::FONTSET_CANDIDATE, candidate_text);
      layout_->EnsureCandidateColumnWidth(size.width);
    }
    if (!description_text.empty()) {
      const Size size = text_renderer_->MeasureString(
          TextRenderer::FONTSET_DESCRIPTION, description_text);
      layout_->EnsureDescriptionColumnWidth(size.width);
    }
  }

  bool footer_visible = false;
  std::wstring footer_label;
  if (candidate_window_->has_footer() &&
      candidate_window_->footer().has_label()) {
    footer_label =
        mozc::win32::Utf8ToWide(candidate_window_->footer().label());
    if (!footer_label.empty()) {
      footer_visible = true;
      const Size size = text_renderer_->MeasureString(
          TextRenderer::FONTSET_FOOTER_LABEL, footer_label);
      layout_->EnsureFooterWidth(size.width);
    }
  }
  layout_->SetFooterVisible(footer_visible);

  layout_->Freeze();

  // Prime the layered surface so SetWindowPos(SWP_SHOWWINDOW) — which
  // WindowManager calls right after this — has something to display.
  Redraw();
}

void CandidateWindow::SetSendCommandInterface(
    client::SendCommandInterface* send_command_interface) {
  send_command_interface_ = send_command_interface;
}

Size CandidateWindow::GetLayoutSize() const {
  DCHECK(layout_->IsFrozen()) << "Modern layout is not frozen.";
  const Size content = layout_->GetTotalSize();
  const ChromeRenderResult layout =
      ComputeChromeLayout(chrome_cache_, content.width, content.height);
  return Size(layout.out_w, layout.out_h);
}

Rect CandidateWindow::GetSelectionRectInScreenCord() const {
  const int focused_array_index = GetFocusedArrayIndex(*candidate_window_);
  if (0 <= focused_array_index &&
      focused_array_index < candidate_window_->candidate_size()) {
    CRect rect = ToCRect(
        ShiftRect(layout_->GetRowRect(focused_array_index), content_offset_));
    ClientToScreen(&rect);
    return Rect(rect.left, rect.top, rect.Width(), rect.Height());
  }
  return Rect();
}

Rect CandidateWindow::GetCandidateColumnInClientCord() const {
  DCHECK(layout_->IsFrozen()) << "Modern layout is not frozen.";
  if (layout_->GetRowCount() == 0) {
    return Rect();
  }
  return ShiftRect(layout_->GetCandidateCellRect(0), content_offset_);
}

Rect CandidateWindow::GetFirstRowInClientCord() const {
  DCHECK(layout_->IsFrozen()) << "Modern layout is not frozen.";
  DCHECK_GT(layout_->GetRowCount(), 0)
      << "number of rows should be positive";
  return ShiftRect(layout_->GetRowRect(0), content_offset_);
}

void CandidateWindow::EnsureDib(int w, int h) {
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
  // Drop any previous selection before destroying the bitmap.
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

void CandidateWindow::ReleaseDib() {
  dib_select_.reset();
  dib_.reset();
  mem_dc_.reset();
  dib_bits_ = nullptr;
  dib_w_ = 0;
  dib_h_ = 0;
}

void CandidateWindow::Redraw() {
  if (!IsWindow()) {
    return;
  }
  if (!layout_->IsFrozen()) {
    return;
  }
  switch (candidate_window_->category()) {
    case commands::CONVERSION:
    case commands::PREDICTION:
    case commands::TRANSLITERATION:
    case commands::SUGGESTION:
    case commands::USAGE:
      break;
    default:
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
  // pptDst = nullptr keeps the current screen position; psize resizes the
  // window to match the new bitmap.
  ::UpdateLayeredWindow(this->m_hWnd, nullptr, nullptr, &size, mem_dc_.get(),
                        &src_pos, 0, &blend, ULW_ALPHA);
}

void CandidateWindow::RenderIntoDib() {
  const Size content = layout_->GetTotalSize();
  const ChromeRenderResult layout =
      ComputeChromeLayout(chrome_cache_, content.width, content.height);
  content_offset_ = Point(layout.content_x, layout.content_y);

  EnsureDib(layout.out_w, layout.out_h);
  if (mem_dc_ == nullptr || dib_bits_ == nullptr) {
    return;
  }

  // 1. Inner content via GDI / Direct2D. The content rect is filled
  //    rectangularly with the theme background (covering the entire rect
  //    including the rounded-corner notches — those will be masked out by
  //    chrome in step 2), then row backgrounds, accent bar, text, scroll
  //    dots, and footer are layered on top. GDI/D2D leave alpha at 0; we
  //    don't bother to fix it here because chrome rewrites alpha in step
  //    2 from scratch.
  //
  //    Pixels outside the content rect are left untouched. They are
  //    guaranteed to hold premultiplied black (BGR=0) because (a) a fresh
  //    DIB is zero-initialized and (b) StampChromeOver never writes a
  //    non-zero BGR outside the fill mask, so re-renders preserve the
  //    invariant.
  HDC dc = mem_dc_.get();
  ::SetBkMode(dc, TRANSPARENT);
  DrawContentBackground(dc);
  DrawSelectedRow(dc);
  DrawAccentBar(dc);
  DrawCells(dc);
  DrawScrollDots(dc);
  DrawFooter(dc);

  // 2. Stamp the chrome (rounded fill mask + 1-DIP black stroke + Fluent
  //    depth-8 drop shadow) on top. StampChromeOver multiplies each
  //    pixel's BGR by the fill mask f and computes the premultiplied
  //    alpha from f, the stroke mask, and the blurred shadow coverage:
  //      - Body interior (f=1): inner content shows through opaquely.
  //      - Rounded-corner AA fade (f<1): inner content fades smoothly
  //        into the shadow tint, giving the visible rounded curve.
  //      - Notch / stroke / shadow margin (f=0): BGR collapses to 0,
  //        alpha carries the shadow / stroke contribution.
  //
  //    Flush GDI so any batched ExtTextOut / Ellipse / D2D writes from
  //    step 1 are visible in the DIB pixels before we read them.
  ::GdiFlush();
  StampChromeOver(chrome_cache_, chrome_theme_, content.width,
                  content.height, static_cast<uint8_t*>(dib_bits_),
                  static_cast<size_t>(layout.out_w) * 4);
}

void CandidateWindow::DrawContentBackground(HDC dc) {
  // Fill the entire content rectangle with the theme background. The
  // rounded-corner notches inside this rect get the same fill color, but
  // StampChromeOver later multiplies their BGR by f=0 — so the notches
  // collapse to premultiplied black and only the chrome's shadow tint
  // shows there.
  const Size content = layout_->GetTotalSize();
  const RECT rect = {content_offset_.x, content_offset_.y,
                     content_offset_.x + content.width,
                     content_offset_.y + content.height};
  FillSolidRect(dc, &rect, active_scheme_.window_background.ToColorRef());
}

void CandidateWindow::DrawSelectedRow(HDC dc) {
  const int focused_array_index = GetFocusedArrayIndex(*candidate_window_);
  if (focused_array_index < 0 ||
      focused_array_index >= candidate_window_->candidate_size()) {
    return;
  }
  if (dib_bits_ == nullptr) {
    return;
  }
  // We're about to bypass GDI and write the DIB pixels directly. Flush any
  // batched GDI work (e.g. the DrawContentBackground fill) first so the
  // DIB has up-to-date pixels for our AA blend, and the next GDI op picks
  // up the post-blend state.
  ::GdiFlush();
  const Rect row_rect =
      ShiftRect(layout_->GetRowRect(focused_array_index), content_offset_);
  FillRoundedRectBgrAa(static_cast<uint8_t*>(dib_bits_),
                       static_cast<size_t>(dib_w_) * 4, dib_w_, dib_h_,
                       row_rect, layout_->GetSelectedRowCornerRadiusPx(),
                       active_scheme_.selected_row_background.ToColorRef());
}

void CandidateWindow::DrawAccentBar(HDC dc) {
  const int focused_array_index = GetFocusedArrayIndex(*candidate_window_);
  if (focused_array_index < 0 ||
      focused_array_index >= candidate_window_->candidate_size()) {
    return;
  }
  const CRect bar_rect = ToCRect(ShiftRect(
      layout_->GetAccentBarRect(focused_array_index), content_offset_));
  FillSolidRect(dc, &bar_rect,
                active_scheme_.selected_accent_bar.ToColorRef());
}

void CandidateWindow::DrawCells(HDC dc) {
  const int row_count = layout_->GetRowCount();
  std::vector<TextRenderingInfo> index_list;
  std::vector<TextRenderingInfo> candidate_list;
  std::vector<TextRenderingInfo> description_list;
  index_list.reserve(row_count);
  candidate_list.reserve(row_count);
  description_list.reserve(row_count);

  for (int i = 0; i < row_count; ++i) {
    const auto& candidate = candidate_window_->candidate(i);
    const std::wstring index_text = GetIndexText(candidate);
    const std::wstring candidate_text = GetCandidateText(candidate);
    const std::wstring description_text = GetDescriptionText(candidate);
    if (!index_text.empty()) {
      index_list.emplace_back(
          index_text,
          ShiftRect(layout_->GetIndexCellRect(i), content_offset_));
    }
    if (!candidate_text.empty()) {
      candidate_list.emplace_back(
          candidate_text,
          ShiftRect(layout_->GetCandidateCellRect(i), content_offset_));
    }
    if (!description_text.empty()) {
      description_list.emplace_back(
          description_text,
          ShiftRect(layout_->GetDescriptionCellRect(i), content_offset_));
    }
  }

  text_renderer_->RenderTextList(dc, index_list, TextRenderer::FONTSET_SHORTCUT,
                                 active_scheme_.row_text_dim.ToColorRef());
  text_renderer_->RenderTextList(dc, candidate_list,
                                 TextRenderer::FONTSET_CANDIDATE,
                                 active_scheme_.row_text.ToColorRef());
  text_renderer_->RenderTextList(
      dc, description_list, TextRenderer::FONTSET_DESCRIPTION,
      active_scheme_.row_text_description.ToColorRef());
}

void CandidateWindow::DrawScrollDots(HDC dc) {
  if (!layout_->HasScrollIndicator()) {
    return;
  }
  const Rect dots_rect =
      ShiftRect(layout_->GetScrollIndicatorRect(), content_offset_);
  const int diameter = layout_->GetScrollDotDiameterPx();
  const int gap = layout_->GetScrollDotGapPx();
  const COLORREF color = active_scheme_.scroll_dot.ToColorRef();
  const int x_left = dots_rect.Left();
  for (int i = 0; i < 3; ++i) {
    const int top = dots_rect.Top() + i * (diameter + gap);
    const RECT dot = {x_left, top, x_left + diameter, top + diameter};
    FillSolidEllipse(dc, dot, color);
  }
}

void CandidateWindow::DrawFooter(HDC dc) {
  if (!layout_->HasFooter()) {
    return;
  }
  const CRect footer_rect =
      ToCRect(ShiftRect(layout_->GetFooterRect(), content_offset_));
  FillSolidRect(dc, &footer_rect,
                active_scheme_.footer_background.ToColorRef());

  const CRect separator_rect = ToCRect(
      ShiftRect(layout_->GetFooterSeparatorRect(), content_offset_));
  FillSolidRect(dc, &separator_rect,
                active_scheme_.footer_separator.ToColorRef());

  if (candidate_window_->has_footer() &&
      candidate_window_->footer().has_label()) {
    const std::wstring label =
        mozc::win32::Utf8ToWide(candidate_window_->footer().label());
    if (!label.empty()) {
      const Rect content_rect =
          ShiftRect(layout_->GetFooterContentRect(), content_offset_);
      text_renderer_->RenderText(dc, label, content_rect,
                                 TextRenderer::FONTSET_FOOTER_LABEL,
                                 active_scheme_.footer_text.ToColorRef());
    }
  }
}

void CandidateWindow::set_mouse_moving(bool moving) { mouse_moving_ = moving; }

}  // namespace win32
}  // namespace renderer
}  // namespace mozc
