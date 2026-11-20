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

#include "renderer/win32/qt_text_renderer.h"

#include <wil/resource.h>
#include <windows.h>

#include <QColor>
#include <QCoreApplication>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPointF>
#include <QString>
#include <QTextLayout>
#include <QTextLine>
#include <QTextOption>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/types/span.h"
#include "base/coordinates.h"
#include "protocol/renderer_style.pb.h"
#include "renderer/win32/text_renderer.h"
#include "renderer/win32/win32_dpi_util.h"
#include "renderer/win32/win32_font_util.h"

namespace mozc {
namespace renderer {
namespace win32 {
namespace {

using ::mozc::renderer::RendererStyle;

// The line width that is used when no wrapping is expected. This is the same
// value as what the DirectWrite-based implementation uses.
constexpr qreal kLayoutLimit = 100000.0;

QColor ToQColor(const RendererStyle::RGBAColor& color) {
  return QColor(color.r(), color.g(), color.b());
}

// TODO(yukawa): Share the following three functions with text_renderer.cc.
// They are intentionally the same as the ones in text_renderer.cc.
QColor GetTextColor(TextRenderer::FONT_TYPE type, uint32_t dpi) {
  const RendererStyle style = GetScaledRendererStyle(dpi);
  const RendererStyle::InfolistStyle& infostyle = style.infolist_style();

  switch (type) {
    case TextRenderer::FONTSET_SHORTCUT:
      return ToQColor(style.shortcut_style().foreground_color());
    case TextRenderer::FONTSET_CANDIDATE:
      return ToQColor(style.candidate_style().foreground_color());
    case TextRenderer::FONTSET_DESCRIPTION:
      return ToQColor(style.description_style().foreground_color());
    case TextRenderer::FONTSET_FOOTER_INDEX:
    case TextRenderer::FONTSET_FOOTER_LABEL:
      return ToQColor(style.footer_style().foreground_color());
    case TextRenderer::FONTSET_FOOTER_SUBLABEL:
      return ToQColor(style.footer_sub_label_style().foreground_color());
    case TextRenderer::FONTSET_INFOLIST_CAPTION:
      return ToQColor(infostyle.caption_style().foreground_color());
    case TextRenderer::FONTSET_INFOLIST_TITLE:
      return ToQColor(infostyle.title_style().foreground_color());
    case TextRenderer::FONTSET_INFOLIST_DESCRIPTION:
      return ToQColor(infostyle.description_style().foreground_color());
    default:
      break;
  }

  LOG(DFATAL) << "Unknown type: " << type;
  return QColor(0, 0, 0);
}

LOGFONT GetLogFont(TextRenderer::FONT_TYPE type, uint32_t dpi) {
  LOGFONT font = GetMessageBoxLogFont(dpi);

  switch (type) {
    case TextRenderer::FONTSET_SHORTCUT: {
      font.lfHeight += (font.lfHeight > 0 ? 3 : -3);
      font.lfWeight = FW_BOLD;
      return font;
    }
    case TextRenderer::FONTSET_CANDIDATE: {
      font.lfHeight += (font.lfHeight > 0 ? 3 : -3);
      font.lfWeight = FW_NORMAL;
      return font;
    }
    case TextRenderer::FONTSET_DESCRIPTION:
    case TextRenderer::FONTSET_FOOTER_INDEX:
    case TextRenderer::FONTSET_FOOTER_LABEL:
    case TextRenderer::FONTSET_FOOTER_SUBLABEL: {
      font.lfWeight = FW_NORMAL;
      return font;
    }
    default:
      break;
  }

  const RendererStyle style = GetScaledRendererStyle(dpi);
  const auto& infostyle = style.infolist_style();
  switch (type) {
    case TextRenderer::FONTSET_INFOLIST_CAPTION: {
      font.lfHeight = -infostyle.caption_style().font_size();
      return font;
    }
    case TextRenderer::FONTSET_INFOLIST_TITLE: {
      font.lfHeight = -infostyle.title_style().font_size();
      return font;
    }
    case TextRenderer::FONTSET_INFOLIST_DESCRIPTION: {
      font.lfHeight = -infostyle.description_style().font_size();
      return font;
    }
    default:
      break;
  }

  LOG(DFATAL) << "Unknown type: " << type;
  return font;
}

DWORD GetGdiDrawTextStyle(TextRenderer::FONT_TYPE type) {
  switch (type) {
    case TextRenderer::FONTSET_CANDIDATE:
      return DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
    case TextRenderer::FONTSET_DESCRIPTION:
      return DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
    case TextRenderer::FONTSET_FOOTER_INDEX:
      return DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
    case TextRenderer::FONTSET_FOOTER_LABEL:
      return DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
    case TextRenderer::FONTSET_FOOTER_SUBLABEL:
      return DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
    case TextRenderer::FONTSET_SHORTCUT:
      return DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
    case TextRenderer::FONTSET_INFOLIST_CAPTION:
      return DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
    case TextRenderer::FONTSET_INFOLIST_TITLE:
      return DT_LEFT | DT_SINGLELINE | DT_WORDBREAK | DT_EDITCONTROL |
             DT_NOPREFIX;
    case TextRenderer::FONTSET_INFOLIST_DESCRIPTION:
      return DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX;
    default:
      LOG(DFATAL) << "Unknown type: " << type;
      return 0;
  }
}

QFont ToQFont(const LOGFONT& log_font) {
  QFont font;
  // Unlike DirectWrite, Qt does not rely on the system font fallback. It has
  // its own list of fallback fonts that depends on the user locale, and it
  // prefers "MS UI Gothic" unless the user locale is Japanese. As a result,
  // candidates would look quite different when the message font is, e.g.
  // "Segoe UI", which does not have Japanese glyphs. Explicitly specify the
  // font that the system font fallback uses for Japanese to avoid it.
  font.setFamilies({QString::fromWCharArray(log_font.lfFaceName),
                    QStringLiteral("Yu Gothic UI")});
  // Both DirectWrite and Qt take the em height. A positive |lfHeight|, which
  // means the cell height, is not what SPI_GETNONCLIENTMETRICS returns in
  // practice, so it is just treated in the same way here.
  font.setPixelSize(std::max<LONG>(std::abs(log_font.lfHeight), 1));
  // QFont::Weight uses the same scale as LOGFONT::lfWeight in Qt6.
  font.setWeight(
      static_cast<QFont::Weight>(std::clamp<LONG>(log_font.lfWeight, 1, 1000)));
  font.setItalic(log_font.lfItalic != 0);
  // With the default hinting preference Qt uses GDI-compatible glyph advances,
  // which makes the layout visibly narrower than what the DirectWrite-based
  // implementation produces. Use the ideal (unhinted) metrics instead.
  font.setHintingPreference(QFont::PreferNoHinting);
  return font;
}

QString ToQString(std::wstring_view str) {
  QString result = QString::fromWCharArray(str.data(), str.size());
  // QTextLayout handles only one paragraph. Let it break lines at line feeds
  // like DirectWrite does.
  result.replace(QLatin1Char('\n'), QChar(QChar::LineSeparator));
  return result;
}

// Lays out |layout| and returns its bounding box. |line_width| is ignored
// unless |wrap| is true.
QSizeF DoLayout(QTextLayout* layout, qreal line_width, bool wrap) {
  QTextOption option;
  option.setWrapMode(wrap ? QTextOption::WordWrap : QTextOption::NoWrap);
  // Callers rely on the width of white spaces to make paddings.
  option.setFlags(QTextOption::IncludeTrailingSpaces);
  layout->setTextOption(option);

  qreal width = 0.0;
  qreal height = 0.0;
  layout->beginLayout();
  while (true) {
    QTextLine line = layout->createLine();
    if (!line.isValid()) {
      break;
    }
    line.setLineWidth(wrap ? line_width : kLayoutLimit);
    line.setPosition(QPointF(0.0, height));
    width = std::max(width, line.naturalTextWidth());
    height += line.height();
  }
  layout->endLayout();
  return QSizeF(width, height);
}

// QGuiApplication keeps referencing |argc| and |argv| passed to its
// constructor, so they need to have static storage duration.
std::unique_ptr<QGuiApplication> MaybeCreateApplication() {
  if (QCoreApplication::instance() != nullptr) {
    return nullptr;
  }
  static int argc = 1;
  static char arg0[] = "mozc_renderer";
  static char* argv[] = {arg0, nullptr};
  return std::make_unique<QGuiApplication>(argc, argv);
}

class QtTextRenderer : public TextRenderer {
 public:
  explicit QtTextRenderer(uint32_t dpi)
      : application_(MaybeCreateApplication()), dpi_(dpi) {
    OnThemeChanged();
  }

 private:
  struct RenderInfo {
    QColor color;
    QFont font;
    DWORD style = 0;
  };

  // TextRenderer overrides:
  void OnThemeChanged() override {
    for (size_t i = 0; i < SIZE_OF_FONT_TYPE; ++i) {
      const auto font_type = static_cast<FONT_TYPE>(i);
      render_info_[i].color = GetTextColor(font_type, dpi_);
      render_info_[i].font = ToQFont(GetLogFont(font_type, dpi_));
      render_info_[i].style = GetGdiDrawTextStyle(font_type);
    }
  }

  void OnDpiChanged(uint32_t dpi) override {
    if (dpi == dpi_) {
      return;
    }
    dpi_ = dpi;
    OnThemeChanged();
  }

  Size MeasureString(FONT_TYPE font_type,
                     const std::wstring_view str) const override {
    return MeasureStringImpl(font_type, str, 0, false);
  }

  Size MeasureStringMultiLine(FONT_TYPE font_type, const std::wstring_view str,
                              const int width) const override {
    return MeasureStringImpl(font_type, str, width, true);
  }

  void RenderText(HDC dc, const std::wstring_view text, const Rect& rect,
                  FONT_TYPE font_type) const override {
    std::vector<TextRenderingInfo> infolist;
    infolist.emplace_back(std::wstring(text), rect);
    RenderTextList(dc, infolist, font_type);
  }

  void RenderTextList(HDC dc,
                      const absl::Span<const TextRenderingInfo> display_list,
                      FONT_TYPE font_type) const override {
    // QPainter cannot draw into HDC directly. Instead, copy the affected area
    // into a DIB section, let QPainter draw text over it, then copy it back.
    // Copying the existing pixels is necessary for antialiasing.
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    bool found = false;
    for (const TextRenderingInfo& item : display_list) {
      if (item.text.empty() || item.rect.IsRectEmpty()) {
        continue;
      }
      if (!found) {
        left = item.rect.Left();
        top = item.rect.Top();
        right = item.rect.Right();
        bottom = item.rect.Bottom();
        found = true;
        continue;
      }
      left = std::min(left, item.rect.Left());
      top = std::min(top, item.rect.Top());
      right = std::max(right, item.rect.Right());
      bottom = std::max(bottom, item.rect.Bottom());
    }
    if (!found) {
      return;
    }
    const int width = right - left;
    const int height = bottom - top;
    if (!EnsureBuffer(width, height)) {
      return;
    }
    if (!::BitBlt(buffer_dc_.get(), 0, 0, width, height, dc, left, top,
                  SRCCOPY)) {
      return;
    }
    ::GdiFlush();

    // GDI leaves the alpha channel of the DIB section zero, while Qt expects
    // it to be 0xff for QImage::Format_RGB32. Without fixing it up here, Qt
    // gives up subpixel (ClearType) antialiasing and falls back to grayscale
    // antialiasing because the destination does not look opaque.
    for (int y = 0; y < height; ++y) {
      uint32_t* line = static_cast<uint32_t*>(buffer_bits_) +
                       static_cast<size_t>(y) * buffer_width_;
      for (int x = 0; x < width; ++x) {
        line[x] |= 0xff000000;
      }
    }

    {
      QImage image(static_cast<uchar*>(buffer_bits_), buffer_width_,
                   buffer_height_, buffer_width_ * 4, QImage::Format_RGB32);
      QPainter painter(&image);
      painter.setClipRect(0, 0, width, height);
      painter.translate(-left, -top);
      painter.setPen(render_info_[font_type].color);
      for (const TextRenderingInfo& item : display_list) {
        if (item.text.empty() || item.rect.IsRectEmpty()) {
          continue;
        }
        DrawItem(&painter, item, font_type);
      }
    }

    ::BitBlt(dc, left, top, width, height, buffer_dc_.get(), 0, 0, SRCCOPY);
  }

  void DrawItem(QPainter* painter, const TextRenderingInfo& item,
                FONT_TYPE font_type) const {
    const RenderInfo& info = render_info_[font_type];
    const bool wrap = (info.style & DT_WORDBREAK) == DT_WORDBREAK;

    QTextLayout layout(ToQString(item.text), info.font);
    const QSizeF size = DoLayout(&layout, item.rect.Width(), wrap);

    qreal x = item.rect.Left();
    if ((info.style & DT_CENTER) == DT_CENTER) {
      x += (item.rect.Width() - size.width()) / 2;
    } else if ((info.style & DT_RIGHT) == DT_RIGHT) {
      x += item.rect.Width() - size.width();
    }
    qreal y = item.rect.Top();
    if ((info.style & DT_VCENTER) == DT_VCENTER) {
      // Keep the baseline on the pixel grid like Direct2D does by default.
      y += std::round((item.rect.Height() - size.height()) / 2);
    }
    layout.draw(painter, QPointF(x, y));
  }

  Size MeasureStringImpl(FONT_TYPE font_type, const std::wstring_view str,
                         const int width, bool use_width) const {
    QTextLayout layout(ToQString(str), render_info_[font_type].font);
    const QSizeF size = DoLayout(&layout, width, use_width);
    return Size(std::ceil(size.width()), std::ceil(size.height()));
  }

  // Makes sure that |buffer_dc_| has a 32-bpp top-down DIB section that is not
  // smaller than the given size. The buffer only grows.
  bool EnsureBuffer(int width, int height) const {
    if (width <= 0 || height <= 0) {
      return false;
    }
    if (buffer_dc_.is_valid() && width <= buffer_width_ &&
        height <= buffer_height_) {
      return true;
    }
    const int new_width = std::max(width, buffer_width_);
    const int new_height = std::max(height, buffer_height_);

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = new_width;
    info.bmiHeader.biHeight = -new_height;  // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    wil::unique_hbitmap bitmap(
        ::CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0));
    if (!bitmap.is_valid() || bits == nullptr) {
      return false;
    }
    wil::unique_hdc dc(::CreateCompatibleDC(nullptr));
    if (!dc.is_valid()) {
      return false;
    }
    // Release the old bitmap from the old DC before deleting it.
    buffer_old_bitmap_.reset();
    buffer_dc_ = std::move(dc);
    buffer_bitmap_ = std::move(bitmap);
    buffer_old_bitmap_ =
        wil::SelectObject(buffer_dc_.get(), buffer_bitmap_.get());
    buffer_bits_ = bits;
    buffer_width_ = new_width;
    buffer_height_ = new_height;
    return true;
  }

  // Declared first so that this outlives any other Qt object in this class.
  std::unique_ptr<QGuiApplication> application_;
  std::array<RenderInfo, SIZE_OF_FONT_TYPE> render_info_;
  uint32_t dpi_;

  // Note that the declaration order matters here. |buffer_old_bitmap_| needs
  // to be destructed first to deselect |buffer_bitmap_| from |buffer_dc_|.
  mutable wil::unique_hdc buffer_dc_;
  mutable wil::unique_hbitmap buffer_bitmap_;
  mutable wil::unique_select_object buffer_old_bitmap_;
  mutable void* buffer_bits_ = nullptr;
  mutable int buffer_width_ = 0;
  mutable int buffer_height_ = 0;
};

}  // namespace

std::unique_ptr<TextRenderer> CreateQtTextRenderer(uint32_t dpi) {
  return std::make_unique<QtTextRenderer>(dpi);
}

}  // namespace win32
}  // namespace renderer
}  // namespace mozc
