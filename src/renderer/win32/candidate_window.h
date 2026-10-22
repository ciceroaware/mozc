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

#ifndef MOZC_RENDERER_WIN32_CANDIDATE_WINDOW_H_
#define MOZC_RENDERER_WIN32_CANDIDATE_WINDOW_H_

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
#include "protocol/commands.pb.h"
#include "renderer/win32/chrome_renderer.h"
#include "renderer/win32/modern_candidate_layout.h"
#include "renderer/win32/modern_renderer_style.h"
#include "renderer/win32/text_renderer.h"

namespace mozc {
namespace renderer {
namespace win32 {

// As Discussed in b/2317702, UI windows are disabled by default because it is
// hard for a user to find out what caused the problem than finding that the
// operations seems to be disabled on the UI window when
// SPI_GETACTIVEWINDOWTRACKING is enabled.
// TODO(yukawa): Support mouse operations before we add a GUI feature which
//   requires UI interaction by mouse and/or touch. (b/2954874)
//
// WS_EX_LAYERED is required for UpdateLayeredWindow-based presentation: the
// chrome (rounded fill + 1-DIP black stroke + Fluent depth-8 drop shadow) is
// rendered into a 32 bpp premultiplied-BGRA DIB and pushed via
// UpdateLayeredWindow with AC_SRC_ALPHA.
typedef ATL::CWinTraits<WS_POPUP | WS_DISABLED, WS_EX_TOOLWINDOW |
                                                    WS_EX_TOPMOST |
                                                    WS_EX_NOACTIVATE |
                                                    WS_EX_LAYERED>
    CandidateWindowTraits;

// a class which implements an IME candidate window for Windows. This class
// is derived from an ATL CWindowImpl<T> class, which provides methods for
// creating a window and handling windows messages.
class CandidateWindow : public ATL::CWindowImpl<CandidateWindow, ATL::CWindow,
                                                CandidateWindowTraits> {
 public:
  // The candidate window owns its own rounded fill, 1-DIP black stroke, and
  // Fluent depth-8 drop shadow (rendered into the layered DIB by
  // chrome_renderer). No CS_DROPSHADOW: the chrome already provides one,
  // and stacking the GDI class shadow on top would produce a visible
  // hairline band above the window.
  DECLARE_WND_CLASS_EX(kCandidateWindowClassName, CS_SAVEBITS, COLOR_WINDOW);

  BEGIN_MSG_MAP(CandidateWindow)
  MESSAGE_HANDLER(WM_CREATE, OnCreate)
  MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
  MESSAGE_HANDLER(WM_GETMINMAXINFO, OnGetMinMaxInfo)
  MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
  MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
  MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
  MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
  END_MSG_MAP()

  CandidateWindow();
  CandidateWindow(const CandidateWindow&) = delete;
  CandidateWindow& operator=(const CandidateWindow&) = delete;
  ~CandidateWindow();
  LRESULT OnCreate(LPCREATESTRUCT create_struct);
  void OnDestroy();
  void OnGetMinMaxInfo(MINMAXINFO* min_max_info);
  void OnLButtonDown(UINT nFlags, CPoint point);
  void OnLButtonUp(UINT nFlags, CPoint point);
  void OnMouseMove(UINT nFlags, CPoint point);
  void OnSettingChange(UINT uFlags, LPCTSTR lpszSection);

  void set_mouse_moving(bool moving);

  // If |dpi| differs from the cached DPI, updates the cached DPI, rebuilds
  // the chrome cache for the new DPI, and flags the text renderer for a
  // font-cache refresh on the next UpdateLayout. Idempotent.
  void UpdateDpi(uint32_t dpi);

  void UpdateLayout(const commands::CandidateWindow& candidate_window);
  void SetSendCommandInterface(
      client::SendCommandInterface* send_command_interface);

  // Layout information for the WindowManager class. Sizes and rects include
  // the chrome padding (the drop-shadow margin around the content area), so
  // these are the values WindowManager should pass to SetWindowPos and to
  // WindowUtil for caret-relative placement.
  Size GetLayoutSize() const;
  Rect GetSelectionRectInScreenCord() const;
  Rect GetCandidateColumnInClientCord() const;
  Rect GetFirstRowInClientCord() const;

  // Offset (in window-client coords) of the inner content area within the
  // layered window. Equals (chrome left pad + stroke, chrome top pad +
  // stroke). Use the y component as the WindowUtil zero-point y so the
  // caret-relative placement compensates for the shadow margin above the
  // content.
  Point GetContentOriginInClientCord() const { return content_offset_; }

  // Pushes the current layout to the layered window via UpdateLayeredWindow.
  // Replaces the legacy WM_PAINT path. Safe to call when the layout is not
  // yet frozen — returns early in that case.
  void Redraw();

 private:
  void RenderIntoDib();

  void DrawContentBackground(HDC dc);
  void DrawSelectedRow(HDC dc);
  void DrawAccentBar(HDC dc);
  void DrawCells(HDC dc);
  void DrawScrollDots(HDC dc);
  void DrawFooter(HDC dc);

  // Refreshes the cached active color scheme and chrome theme based on the
  // current OS theme.
  void RefreshTheme();

  // (Re)creates the DIB section that backs the layered presentation if its
  // size doesn't already match (w, h).
  void EnsureDib(int w, int h);

  // Releases mem_dc_ / dib_ / dib_bits_.
  void ReleaseDib();

  // Handles candidate selection by mouse.
  void HandleMouseEvent(UINT nFlags, const CPoint& point,
                        bool close_candidatewindow);

  // Even though the candidate window supports limited mouse operations, we
  // accept them when and only when SPI_GETACTIVEWINDOWTRACKING is disabled
  // to avoid problematic side effect as discussed in b/2317702.
  void EnableOrDisableWindowForWorkaround();

  inline LRESULT OnCreate(UINT msg_id, WPARAM wparam, LPARAM lparam,
                          BOOL& handled) {
    return static_cast<LRESULT>(
        OnCreate(reinterpret_cast<LPCREATESTRUCT>(lparam)));
  }
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
  inline LRESULT OnLButtonDown(UINT msg_id, WPARAM wparam, LPARAM lparam,
                               BOOL& handled) {
    OnLButtonDown(static_cast<UINT>(wparam),
                  CPoint(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)));
    return 0;
  }
  inline LRESULT OnLButtonUp(UINT msg_id, WPARAM wparam, LPARAM lparam,
                             BOOL& handled) {
    OnLButtonUp(static_cast<UINT>(wparam),
                CPoint(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)));
    return 0;
  }
  inline LRESULT OnMouseMove(UINT msg_id, WPARAM wparam, LPARAM lparam,
                             BOOL& handled) {
    OnMouseMove(static_cast<UINT>(wparam),
                CPoint(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)));
    return 0;
  }
  inline LRESULT OnSettingChange(UINT msg_id, WPARAM wparam, LPARAM lparam,
                                 BOOL& handled) {
    OnSettingChange(static_cast<UINT>(wparam),
                    reinterpret_cast<LPCTSTR>(lparam));
    return 0;
  }

  std::unique_ptr<commands::CandidateWindow> candidate_window_;
  client::SendCommandInterface* send_command_interface_;
  std::unique_ptr<ModernCandidateLayout> layout_;
  ModernRendererStyle style_;
  ColorScheme active_scheme_;
  uint32_t dpi_;
  std::unique_ptr<TextRenderer> text_renderer_;

  // Fluent depth-8 drop-shadow chrome. The cache is theme-independent and
  // depends only on DPI; the theme picks shadow / stroke alphas at render
  // time. The chrome bitmap is drawn into the layered window's DIB before
  // any inner content (text, highlights, separators) is composited on top.
  ChromeCache chrome_cache_;
  ChromeTheme chrome_theme_;
  // Offset (in client coords) at which the inner content rect starts within
  // the layered window. Equals (chrome left padding + stroke, chrome top
  // padding + stroke). Cached after each Redraw to translate layout rects
  // for hit-testing and for WindowManager queries.
  Point content_offset_;

  // Layered-window backing DIB and its memory DC. Lazily resized.
  wil::unique_hdc mem_dc_;
  wil::unique_hbitmap dib_;
  wil::unique_select_object dib_select_;
  void* dib_bits_ = nullptr;
  int dib_w_ = 0;
  int dib_h_ = 0;

  bool metrics_changed_;
  bool mouse_moving_;
};

}  // namespace win32
}  // namespace renderer
}  // namespace mozc

#endif  // MOZC_RENDERER_WIN32_CANDIDATE_WINDOW_H_
