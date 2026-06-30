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

#ifndef MOZC_RENDERER_WIN32_MODERN_CANDIDATE_LAYOUT_H_
#define MOZC_RENDERER_WIN32_MODERN_CANDIDATE_LAYOUT_H_

#include <cstdint>

#include "base/coordinates.h"
#include "renderer/win32/modern_renderer_style.h"

namespace mozc {
namespace renderer {
namespace win32 {

// Modern, MS-IME-flavored candidate window layout.
//
// Visual model (one column of rows, optional footer below):
//
//   +---------------------------------------------+
//   |  pad                                        |
//   |  |A| index | gap | candidate | gap | desc |S|
//   |  |A| index | gap | candidate | gap | desc |S|
//   |  ...                                        |
//   |  pad                                        |
//   |---separator---------------------------------|
//   |  footer (optional)                          |
//   +---------------------------------------------+
//
//   A = vertical accent bar (drawn only on the focused row)
//   S = scroll-dot column (only when the candidate list is paged)
//
// Usage:
//   1. Initialize(style, dpi).
//   2. SetRowCount(n) and EnsureXxxColumnWidth(...) for each text column,
//      based on text widths measured by the caller.
//   3. SetScrollIndicatorVisible / SetFooterVisible / EnsureFooterWidth.
//   4. Freeze().
//   5. Query rect getters.
class ModernCandidateLayout {
 public:
  ModernCandidateLayout() = default;
  ModernCandidateLayout(const ModernCandidateLayout&) = delete;
  ModernCandidateLayout& operator=(const ModernCandidateLayout&) = delete;

  void Initialize(const ModernRendererStyle& style, uint32_t dpi);

  void SetRowCount(int row_count);
  void EnsureIndexColumnWidth(int width_px);
  void EnsureCandidateColumnWidth(int width_px);
  void EnsureDescriptionColumnWidth(int width_px);
  void EnsureFooterWidth(int width_px);
  void SetScrollIndicatorVisible(bool visible);
  void SetFooterVisible(bool visible);

  void Freeze();
  bool IsFrozen() const { return frozen_; }

  // ---------- Accessors valid only after Freeze ----------
  Size GetTotalSize() const { return Size(total_width_px_, total_height_px_); }
  int GetRowCount() const { return row_count_; }
  int GetRowHeightPx() const { return row_height_px_; }

  // The full row band (used both for selected-row background and
  // hit-testing). Spans the full content width minus outer padding.
  Rect GetRowRect(int row) const;

  // The vertical accent bar drawn at the leading edge of the focused row.
  Rect GetAccentBarRect(int row) const;

  // Per-column rects within a row.
  Rect GetIndexCellRect(int row) const;
  Rect GetCandidateCellRect(int row) const;
  Rect GetDescriptionCellRect(int row) const;

  bool HasScrollIndicator() const { return has_scroll_; }
  // Bounding box that contains the three vertical scroll dots (centered
  // vertically within the rows area).
  Rect GetScrollIndicatorRect() const;

  bool HasFooter() const { return has_footer_; }
  Rect GetFooterSeparatorRect() const;
  Rect GetFooterRect() const;
  Rect GetFooterContentRect() const;

  // Convenience accessors for paint code.
  int GetCornerRadiusPx() const { return corner_radius_px_; }
  int GetSelectedRowCornerRadiusPx() const {
    return selected_row_corner_radius_px_;
  }
  int GetScrollDotDiameterPx() const { return scroll_dot_diameter_px_; }
  int GetScrollDotGapPx() const { return scroll_dot_gap_px_; }

 private:
  static int Scale(int dip, uint32_t dpi);

  ModernRendererStyle style_;
  uint32_t dpi_ = 96;

  // DPI-scaled metrics (cached at Initialize).
  int corner_radius_px_ = 0;
  int window_padding_px_ = 0;
  int row_height_px_ = 0;
  int row_horizontal_padding_px_ = 0;
  int index_to_candidate_gap_px_ = 0;
  int candidate_to_description_gap_px_ = 0;
  int accent_bar_width_px_ = 0;
  int accent_bar_inset_px_ = 0;
  int selected_row_corner_radius_px_ = 0;
  int scroll_column_width_px_ = 0;
  int scroll_dot_diameter_px_ = 0;
  int scroll_dot_gap_px_ = 0;
  int footer_height_px_ = 0;
  int footer_separator_height_px_ = 0;
  int footer_horizontal_padding_px_ = 0;

  // Configurable inputs.
  int row_count_ = 0;
  int index_col_width_px_ = 0;
  int candidate_col_width_px_ = 0;
  int description_col_width_px_ = 0;
  int footer_min_width_px_ = 0;
  bool has_scroll_ = false;
  bool has_footer_ = false;

  // Computed at Freeze.
  bool frozen_ = false;
  int total_width_px_ = 0;
  int total_height_px_ = 0;
  int rows_area_top_px_ = 0;
  int rows_area_height_px_ = 0;
  int footer_top_px_ = 0;          // valid only when has_footer_
};

}  // namespace win32
}  // namespace renderer
}  // namespace mozc

#endif  // MOZC_RENDERER_WIN32_MODERN_CANDIDATE_LAYOUT_H_
