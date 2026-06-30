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

#ifndef MOZC_RENDERER_WIN32_INFOLIST_WINDOW_H_
#define MOZC_RENDERER_WIN32_INFOLIST_WINDOW_H_

#include <atlbase.h>
#include <atltypes.h>
#include <atlwin.h>
#include <wil/resource.h>
#include <windows.h>

#include <cstdint>
#include <memory>

#include "base/const.h"
#include "base/coordinates.h"
#include "client/client_interface.h"
#include "protocol/candidate_window.pb.h"
#include "renderer/win32/chrome_renderer.h"
#include "renderer/win32/modern_renderer_style.h"
#include "renderer/win32/text_renderer.h"

namespace mozc {
namespace renderer {
namespace win32 {

// WS_EX_LAYERED is required for UpdateLayeredWindow-based presentation: the
// chrome (rounded fill + 1-DIP black stroke + Fluent depth-8 drop shadow)
// is rendered into a 32 bpp premultiplied-BGRA DIB and pushed via
// UpdateLayeredWindow with AC_SRC_ALPHA, mirroring CandidateWindow.
typedef ATL::CWinTraits<WS_POPUP | WS_DISABLED, WS_EX_TOOLWINDOW |
                                                    WS_EX_TOPMOST |
                                                    WS_EX_NOACTIVATE |
                                                    WS_EX_LAYERED>
    InfolistWindowTraits;

// a class which implements an IME infolist window for Windows. This class
// is derived from an ATL CWindowImpl<T> class, which provides methods for
// creating a window and handling windows messages.
class InfolistWindow : public ATL::CWindowImpl<InfolistWindow, ATL::CWindow,
                                               InfolistWindowTraits> {
 public:
  // The infolist window owns its own rounded fill, 1-DIP black stroke, and
  // Fluent depth-8 drop shadow (rendered into the layered DIB by
  // chrome_renderer). No CS_DROPSHADOW: the chrome already provides one.
  DECLARE_WND_CLASS_EX(kInfolistWindowClassName, CS_SAVEBITS, COLOR_WINDOW);

  BEGIN_MSG_MAP(InfolistWindow)
  MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
  MESSAGE_HANDLER(WM_GETMINMAXINFO, OnGetMinMaxInfo)
  MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
  MESSAGE_HANDLER(WM_TIMER, OnTimer)
  END_MSG_MAP()

  InfolistWindow();
  InfolistWindow(const InfolistWindow&) = delete;
  InfolistWindow& operator=(const InfolistWindow&) = delete;
  ~InfolistWindow();
  void OnDestroy();
  void OnGetMinMaxInfo(MINMAXINFO* min_max_info);
  void OnSettingChange(UINT uFlags, LPCTSTR lpszSection);
  void OnTimer(UINT_PTR nIDEvent);

  // If |dpi| differs from the cached DPI, updates the cached DPI, rebuilds
  // the chrome cache for the new DPI, and flags the text renderer for a
  // font-cache refresh on the next UpdateLayout. Idempotent.
  void UpdateDpi(uint32_t dpi);

  void UpdateLayout(const commands::CandidateWindow& candidates);
  void SetSendCommandInterface(
      client::SendCommandInterface* send_command_interface);

  // Layout information for the WindowManager class. Returns the outer
  // bitmap size including the chrome's drop-shadow padding.
  Size GetLayoutSize();

  // Pushes the current layout to the layered window via UpdateLayeredWindow.
  // Replaces the legacy WM_PAINT path. Safe to call before the layout has
  // been computed — returns early in that case.
  void Redraw();

  // Offset (in window-client coords) of the inner content area within the
  // layered window. Equals (chrome left pad + stroke, chrome top pad +
  // stroke). WindowManager uses these to translate between content-edge
  // and outer-bitmap-edge positions when placing the infolist next to the
  // candidate window.
  Point GetContentOriginInClientCord() const { return content_offset_; }

  void DelayShow(UINT mseconds);
  void DelayHide(UINT mseconds);

 private:
  // Computes the content size from |candidate_window_|'s usages. Used both
  // by GetLayoutSize and by RenderIntoDib so the two paths agree.
  Size ComputeContentSize() const;

  void RenderIntoDib();
  void DrawFooter(HDC dc, const Rect& footer_rect);
  void DrawRow(HDC dc, int row, const Rect& row_rect, bool focused);

  // Refreshes the cached chrome theme based on the current OS theme.
  void RefreshTheme();

  // (Re)creates the DIB section that backs the layered presentation if its
  // size doesn't already match (w, h).
  void EnsureDib(int w, int h);

  // Releases mem_dc_ / dib_ / dib_bits_.
  void ReleaseDib();

  inline LRESULT OnDestroy(UINT msg_id, WPARAM wparam, LPARAM lparam,
                           BOOL& handled) {
    OnDestroy();
    return 0;
  }
  inline LRESULT OnGetMinMaxInfo(UINT msg_id, WPARAM wparam, LPARAM lparam,
                                 BOOL& handled) {
    OnGetMinMaxInfo(reinterpret_cast<MINMAXINFO*>(lparam));
    return 0;
  }
  inline LRESULT OnSettingChange(UINT msg_id, WPARAM wparam, LPARAM lparam,
                                 BOOL& handled) {
    OnSettingChange(static_cast<UINT>(wparam),
                    reinterpret_cast<LPCTSTR>(lparam));
    return 0;
  }
  inline LRESULT OnTimer(UINT msg_id, WPARAM wparam, LPARAM lparam,
                         BOOL& handled) {
    OnTimer(static_cast<UINT_PTR>(wparam));
    return 0;
  }

  client::SendCommandInterface* send_command_interface_;
  std::unique_ptr<commands::CandidateWindow> candidate_window_;
  uint32_t dpi_;
  std::unique_ptr<TextRenderer> text_renderer_;

  // Shared with the candidate window so the infolist's footer (background,
  // text, separator) always matches the candidate window's footer.
  // ActiveColorScheme(style_) returns a reference into this struct, so it
  // must outlive every call site.
  ModernRendererStyle style_;

  // Fluent depth-8 drop-shadow chrome. The cache is theme-independent and
  // depends only on DPI; the theme picks shadow / stroke alphas at render
  // time.
  ChromeCache chrome_cache_;
  ChromeTheme chrome_theme_;

  // Offset (in client coords) at which the inner content rect starts
  // within the layered window. Equals (chrome left padding + stroke,
  // chrome top padding + stroke). Cached after each Redraw.
  Point content_offset_;

  // Layered-window backing DIB and its memory DC. Lazily resized.
  wil::unique_hdc mem_dc_;
  wil::unique_hbitmap dib_;
  wil::unique_select_object dib_select_;
  void* dib_bits_ = nullptr;
  int dib_w_ = 0;
  int dib_h_ = 0;

  bool metrics_changed_;
  bool visible_;
};

}  // namespace win32
}  // namespace renderer
}  // namespace mozc

#endif  // MOZC_RENDERER_WIN32_INFOLIST_WINDOW_H_
