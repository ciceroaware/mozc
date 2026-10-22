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

#include "renderer/win32/modern_candidate_layout.h"

#include <algorithm>
#include <cstdint>

#include "absl/log/check.h"
#include "base/coordinates.h"

namespace mozc {
namespace renderer {
namespace win32 {

namespace {
constexpr uint32_t kDefaultDpi = 96;
}  // namespace

// static
int ModernCandidateLayout::Scale(int dip, uint32_t dpi) {
  // Round-half-up.
  return (dip * static_cast<int>(dpi) + static_cast<int>(kDefaultDpi) / 2) /
         static_cast<int>(kDefaultDpi);
}

void ModernCandidateLayout::Initialize(const ModernRendererStyle& style,
                                       uint32_t dpi) {
  style_ = style;
  dpi_ = dpi == 0 ? kDefaultDpi : dpi;

  corner_radius_px_ = Scale(style_.corner_radius_dip, dpi_);
  window_padding_px_ = Scale(style_.window_padding_dip, dpi_);
  row_height_px_ = Scale(style_.row_height_dip, dpi_);
  row_horizontal_padding_px_ = Scale(style_.row_horizontal_padding_dip, dpi_);
  index_to_candidate_gap_px_ = Scale(style_.index_to_candidate_gap_dip, dpi_);
  candidate_to_description_gap_px_ =
      Scale(style_.candidate_to_description_gap_dip, dpi_);
  accent_bar_width_px_ = Scale(style_.accent_bar_width_dip, dpi_);
  accent_bar_inset_px_ = Scale(style_.accent_bar_inset_dip, dpi_);
  selected_row_corner_radius_px_ =
      Scale(style_.selected_row_corner_radius_dip, dpi_);
  scroll_column_width_px_ = Scale(style_.scroll_column_width_dip, dpi_);
  scroll_dot_diameter_px_ = Scale(style_.scroll_dot_diameter_dip, dpi_);
  scroll_dot_gap_px_ = Scale(style_.scroll_dot_gap_dip, dpi_);
  footer_height_px_ = Scale(style_.footer_height_dip, dpi_);
  footer_separator_height_px_ = Scale(style_.footer_separator_height_dip, dpi_);
  footer_horizontal_padding_px_ =
      Scale(style_.footer_horizontal_padding_dip, dpi_);

  row_count_ = 0;
  index_col_width_px_ = 0;
  candidate_col_width_px_ = 0;
  description_col_width_px_ = 0;
  footer_min_width_px_ = 0;
  has_scroll_ = false;
  has_footer_ = false;
  frozen_ = false;
  total_width_px_ = 0;
  total_height_px_ = 0;
  rows_area_top_px_ = 0;
  rows_area_height_px_ = 0;
  footer_top_px_ = 0;
}

void ModernCandidateLayout::SetRowCount(int row_count) {
  DCHECK(!frozen_);
  row_count_ = std::max(0, row_count);
}

void ModernCandidateLayout::EnsureIndexColumnWidth(int width_px) {
  DCHECK(!frozen_);
  index_col_width_px_ = std::max(index_col_width_px_, width_px);
}

void ModernCandidateLayout::EnsureCandidateColumnWidth(int width_px) {
  DCHECK(!frozen_);
  candidate_col_width_px_ = std::max(candidate_col_width_px_, width_px);
}

void ModernCandidateLayout::EnsureDescriptionColumnWidth(int width_px) {
  DCHECK(!frozen_);
  description_col_width_px_ = std::max(description_col_width_px_, width_px);
}

void ModernCandidateLayout::EnsureFooterWidth(int width_px) {
  DCHECK(!frozen_);
  footer_min_width_px_ = std::max(footer_min_width_px_, width_px);
}

void ModernCandidateLayout::SetScrollIndicatorVisible(bool visible) {
  DCHECK(!frozen_);
  has_scroll_ = visible;
}

void ModernCandidateLayout::SetFooterVisible(bool visible) {
  DCHECK(!frozen_);
  has_footer_ = visible;
}

void ModernCandidateLayout::Freeze() {
  // Width: padding + index + gap + candidate + (gap + description)? +
  //        scroll? + padding.
  int rows_inner_width = index_col_width_px_;
  if (candidate_col_width_px_ > 0) {
    if (rows_inner_width > 0) {
      rows_inner_width += index_to_candidate_gap_px_;
    }
    rows_inner_width += candidate_col_width_px_;
  }
  if (description_col_width_px_ > 0) {
    if (candidate_col_width_px_ > 0) {
      rows_inner_width += candidate_to_description_gap_px_;
    }
    rows_inner_width += description_col_width_px_;
  }

  int rows_outer_width = window_padding_px_ + row_horizontal_padding_px_ +
                         rows_inner_width + row_horizontal_padding_px_ +
                         window_padding_px_;
  if (has_scroll_) {
    rows_outer_width += scroll_column_width_px_;
  }

  int footer_outer_width = 0;
  if (has_footer_) {
    footer_outer_width = window_padding_px_ + footer_horizontal_padding_px_ +
                         footer_min_width_px_ +
                         footer_horizontal_padding_px_ + window_padding_px_;
  }

  total_width_px_ = std::max(rows_outer_width, footer_outer_width);

  rows_area_top_px_ = window_padding_px_;
  rows_area_height_px_ = row_count_ * row_height_px_;

  int height = rows_area_top_px_ + rows_area_height_px_ + window_padding_px_;
  if (has_footer_) {
    height += footer_separator_height_px_;
    footer_top_px_ = height;
    height += footer_height_px_;
  } else {
    footer_top_px_ = 0;
  }
  total_height_px_ = height;

  frozen_ = true;
}

Rect ModernCandidateLayout::GetRowRect(int row) const {
  DCHECK(frozen_);
  DCHECK_GE(row, 0);
  DCHECK_LT(row, row_count_);
  const int left = window_padding_px_;
  const int right = total_width_px_ - window_padding_px_;
  const int top = rows_area_top_px_ + row * row_height_px_;
  return Rect(left, top, right - left, row_height_px_);
}

Rect ModernCandidateLayout::GetAccentBarRect(int row) const {
  const Rect row_rect = GetRowRect(row);
  return Rect(row_rect.Left(), row_rect.Top() + accent_bar_inset_px_,
              accent_bar_width_px_,
              row_rect.Height() - 2 * accent_bar_inset_px_);
}

Rect ModernCandidateLayout::GetIndexCellRect(int row) const {
  const Rect row_rect = GetRowRect(row);
  const int left = row_rect.Left() + row_horizontal_padding_px_;
  return Rect(left, row_rect.Top(), index_col_width_px_, row_rect.Height());
}

Rect ModernCandidateLayout::GetCandidateCellRect(int row) const {
  const Rect index_cell = GetIndexCellRect(row);
  int left = index_cell.Right();
  if (index_col_width_px_ > 0) {
    left += index_to_candidate_gap_px_;
  }
  return Rect(left, index_cell.Top(), candidate_col_width_px_,
              index_cell.Height());
}

Rect ModernCandidateLayout::GetDescriptionCellRect(int row) const {
  const Rect candidate_cell = GetCandidateCellRect(row);
  int left = candidate_cell.Right();
  if (candidate_col_width_px_ > 0 && description_col_width_px_ > 0) {
    left += candidate_to_description_gap_px_;
  }
  return Rect(left, candidate_cell.Top(), description_col_width_px_,
              candidate_cell.Height());
}

Rect ModernCandidateLayout::GetScrollIndicatorRect() const {
  DCHECK(frozen_);
  if (!has_scroll_) {
    return Rect();
  }
  constexpr int kDotCount = 3;
  const int dots_height =
      kDotCount * scroll_dot_diameter_px_ + (kDotCount - 1) * scroll_dot_gap_px_;
  const int dots_top =
      rows_area_top_px_ + (rows_area_height_px_ - dots_height) / 2;
  const int column_left =
      total_width_px_ - window_padding_px_ - scroll_column_width_px_;
  const int dots_left = column_left + (scroll_column_width_px_ -
                                       scroll_dot_diameter_px_) /
                                          2;
  return Rect(dots_left, dots_top, scroll_dot_diameter_px_, dots_height);
}

Rect ModernCandidateLayout::GetFooterSeparatorRect() const {
  DCHECK(frozen_);
  if (!has_footer_) {
    return Rect();
  }
  return Rect(0, footer_top_px_ - footer_separator_height_px_, total_width_px_,
              footer_separator_height_px_);
}

Rect ModernCandidateLayout::GetFooterRect() const {
  DCHECK(frozen_);
  if (!has_footer_) {
    return Rect();
  }
  return Rect(0, footer_top_px_, total_width_px_, footer_height_px_);
}

Rect ModernCandidateLayout::GetFooterContentRect() const {
  DCHECK(frozen_);
  if (!has_footer_) {
    return Rect();
  }
  const int left = window_padding_px_ + footer_horizontal_padding_px_;
  const int right =
      total_width_px_ - window_padding_px_ - footer_horizontal_padding_px_;
  return Rect(left, footer_top_px_, right - left, footer_height_px_);
}

}  // namespace win32
}  // namespace renderer
}  // namespace mozc
