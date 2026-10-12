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

#include "win32/tip/tip_lang_bar_menu.h"

#include <atlbase.h>
#include <atltypes.h>
#include <commctrl.h>
#include <ctfutb.h>
#include <ole2.h>
#include <olectl.h>
#include <strsafe.h>
#include <wil/com.h>
#include <wil/resource.h>
#include <windows.h>
#include <winuser.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <utility>

#include "absl/base/casts.h"
#include "absl/base/macros.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "base/win32/com.h"
#include "base/win32/com_implements.h"
#include "win32/base/text_icon.h"
#include "win32/base/tsf_profile.h"
#include "win32/tip/tip_lang_bar_callback.h"
#include "win32/tip/tip_resource.h"

namespace mozc {
namespace win32 {

template <>
bool IsIIDOf<ITfLangBarItemButton>(REFIID riid) {
  return IsIIDOf<ITfLangBarItemButton, ITfLangBarItem>(riid);
}

namespace tsf {
namespace {

// Represents the cookie for the sink to a TipLangBarButton object.
constexpr int kTipLangBarMenuCookie =
    (('M' << 24) | ('o' << 16) | ('z' << 8) | ('c' << 0));

// Symbol fonts that have glyphs for IME modes in the Private Use Area. "Segoe
// Fluent Icons" is available on Windows 11 and later, and "Segoe MDL2 Assets"
// is available on Windows 10 and later.
// https://learn.microsoft.com/en-us/windows/apps/design/style/segoe-fluent-icons-font
// https://learn.microsoft.com/en-us/windows/apps/design/style/segoe-ui-symbol-font
constexpr absl::string_view kSymbolFonts[] = {
    "Segoe Fluent Icons",
    "Segoe MDL2 Assets",
};

// Fonts to render Latin characters. Semilight is preferred because its stroke
// width is close to that of the symbol fonts.
constexpr absl::string_view kLatinFonts[] = {
    "Segoe UI Semilight",
    "Segoe UI",
};

// Fonts to render Kana characters and full-width alphanumeric characters.
constexpr absl::string_view kKanaFonts[] = {
    "Yu Gothic UI Semilight",
    "Yu Gothic UI",
    "Meiryo UI",
    "MS UI Gothic",
};

// The font that is used when none of the above fonts is available. We rely on
// the font linking for the characters that this font does not have.
constexpr absl::string_view kLastResortFont = "Segoe UI";

// Represents how to render an icon for the input mode button.
struct TextIconInfo {
  UINT icon_id;
  // A character in |kSymbolFonts|. Can be empty when there is no symbol that
  // is semantically the same as the input mode.
  absl::string_view symbol;
  // Used when |symbol| is empty or cannot be rendered.
  absl::string_view text;
  absl::Span<const absl::string_view> text_fonts;
};

// TODO(yukawa): Refactor LangBar code so that we can configure following
// settings as a part of initialization.
constexpr TextIconInfo kTextIcons[] = {
    {IDI_DIRECT, "\uE97E" /* HalfAlpha */, "A", kLatinFonts},
    {IDI_HIRAGANA, "\uE986" /* FullHiragana */, "あ", kKanaFonts},
    {IDI_FULL_KATAKANA, "", "ア", kKanaFonts},
    {IDI_HALF_ALPHANUMERIC, "", "_A", kLatinFonts},
    {IDI_FULL_ALPHANUMERIC, "\uE97F" /* FullAlpha */, "Ａ", kKanaFonts},
    {IDI_HALF_KATAKANA, "", "_ｱ", kKanaFonts},
    {IDI_DISABLED, "\uE711" /* Cancel */, "×", kLatinFonts},
};

struct TextIconColor {
  COLORREF color;
  uint8_t opacity;
};

// Returns the color that is appropriate to draw the mode icon on the taskbar.
TextIconColor GetTextIconColor() {
  HIGHCONTRAST high_contrast = {};
  high_contrast.cbSize = sizeof(high_contrast);
  if (::SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(high_contrast),
                              &high_contrast, 0) &&
      (high_contrast.dwFlags & HCF_HIGHCONTRASTON) != 0) {
    return {::GetSysColor(COLOR_WINDOWTEXT), 0xff};
  }

  // The taskbar follows the system theme rather than the app theme. Note that
  // the taskbar is dark when this value does not exist.
  DWORD light_theme = 0;
  DWORD size = sizeof(light_theme);
  const LSTATUS result = ::RegGetValueW(
      HKEY_CURRENT_USER,
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
      L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &light_theme, &size);
  if (result == ERROR_SUCCESS && light_theme != 0) {
    // The same as the primary text color in the light theme (#E4000000), which
    // is what the mode icon of MS-IME looks to be drawn with.
    return {RGB(0x00, 0x00, 0x00), 0xe4};
  }
  return {RGB(0xff, 0xff, 0xff), 0xff};
}

// Returns the size of the mode icon in physical pixels. Note that this code
// runs in the process of the focused application, which is not always
// DPI-aware. If we relied on the DPI awareness of the process, the taskbar
// would need to stretch a small icon, which results in a blurred icon.
int GetTextIconSize() {
  const DPI_AWARENESS_CONTEXT original_context = ::SetThreadDpiAwarenessContext(
      DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  if (original_context == nullptr) {
    return ::GetSystemMetrics(SM_CYSMICON);
  }
  const int icon_size =
      ::GetSystemMetricsForDpi(SM_CYSMICON, ::GetDpiForSystem());
  ::SetThreadDpiAwarenessContext(original_context);
  return icon_size;
}

// Returns an icon rendered on-the-fly for |icon_id|, which is appropriate for
// the current theme. Returns nullptr when |icon_id| is unknown.
HICON CreateTextIcon(UINT icon_id) {
  const auto* info =
      std::find_if(std::begin(kTextIcons), std::end(kTextIcons),
                   [icon_id](const TextIconInfo& info) {
                     return info.icon_id == icon_id;
                   });
  if (info == std::end(kTextIcons)) {
    return nullptr;
  }

  const int icon_size = GetTextIconSize();
  const TextIconColor color = GetTextIconColor();
  if (!info->symbol.empty()) {
    const HICON icon = TextIcon::CreateMonochromeIcon(
        icon_size, icon_size, info->symbol, kSymbolFonts, color.color,
        color.opacity, TextIcon::Scaling::kEmHeight);
    if (icon != nullptr) {
      return icon;
    }
  }
  const HICON icon = TextIcon::CreateMonochromeIcon(
      icon_size, icon_size, info->text, info->text_fonts, color.color,
      color.opacity, TextIcon::Scaling::kFitInkBox);
  if (icon != nullptr) {
    return icon;
  }
  return TextIcon::CreateMonochromeIcon(
      icon_size, icon_size, info->text, kLastResortFont, color.color,
      color.opacity, TextIcon::Scaling::kFitInkBox);
}

}  // namespace

HRESULT TipLangBarMenuDataArray::Init(HINSTANCE instance,
                                      const TipLangBarMenuItem* menu,
                                      int count) {
  HRESULT result = S_OK;

  // Attach menu texts and icons.
  for (int i = 0; i < count; ++i) {
    TipLangBarMenuData data = {};
    int length = 0;
    if ((menu[i].flags_ & TF_LBMENUF_SEPARATOR) == 0) {
      // Retrieve the menu text and button icon.
      length = ::LoadString(instance, menu[i].text_id_, &data.text_[0],
                            std::size(data.text_));
    }
    data.flags_ = menu[i].flags_;
    data.item_id_ = menu[i].item_id_;
    data.text_id_ = menu[i].text_id_;
    data.length_ = length;
    data.icon_id_ = menu[i].icon_id_;
    data_.push_back(data);
  }

  return result;
}

size_t TipLangBarMenuDataArray::size() const { return data_.size(); }

TipLangBarMenuData* TipLangBarMenuDataArray::data(size_t i) {
  if (i >= size()) {
    return nullptr;
  }
  return &data_[i];
}

// Implements the constructor of the TipLangBarButton class.
TipLangBarButton::TipLangBarButton(
    wil::com_ptr_nothrow<TipLangBarCallback> lang_bar_callback,
    const GUID& guid, bool is_menu, bool show_in_tray)
    : lang_bar_callback_(std::move(lang_bar_callback)),
      status_(0),
      context_menu_enabled_(true) {
  // Initialize its TF_LANGBARITEMINFO object, which contains the properties of
  // this item and is copied to the TSF manager in GetInfo() function.
  item_info_.clsidService = TsfProfile::GetTextServiceGuid();
  item_info_.guidItem = guid;
  // The visibility of a langbar-item in the minimized langbar is actually
  // controlled by TF_LBI_STYLE_SHOWNINTRAY flag despite the fact that the
  // document says "This flag is not currently supported".
  // http://msdn.microsoft.com/en-us/library/ms629078.aspx
  // http://b/2275633
  item_info_.dwStyle = 0;
  if (is_menu) {
    item_info_.dwStyle |= TF_LBI_STYLE_BTN_MENU;
  } else {
    item_info_.dwStyle |= TF_LBI_STYLE_BTN_BUTTON;
  }
  if (show_in_tray) {
    item_info_.dwStyle |= TF_LBI_STYLE_SHOWNINTRAY;
  }
  item_info_.ulSort = 0;
  item_info_.szDescription[0] = L'\0';
}

// Implements the ITfLangBarItem::GetInfo() function.
// This function is called by Windows to update this button menu.
STDMETHODIMP TipLangBarButton::GetInfo(TF_LANGBARITEMINFO* item_info) {
  // Just copies the cached TF_LANGBARITEMINFO object.
  *item_info = item_info_;
  return S_OK;
}

// Implements the ITfLangBarItem::GetStatus() function.
// This function is called by Windows to retrieve the current status of this
// button menu.
STDMETHODIMP TipLangBarButton::GetStatus(DWORD* status) {
  *status = status_;
  return S_OK;
}

// Implements the ITfLangBarItem::Show() function.
// This function is called by Windows to notify the display status of this
// button menu has been updated.
STDMETHODIMP TipLangBarButton::Show(BOOL show) {
  // Just return because this button is always shown, i.e. we do not have to
  // manage the display state of this button menu.
  return E_NOTIMPL;
}

// Implements the ITfLangBarItem::GetTooltipString() function.
// This function is called when Windows to retrieve the tool-tip string of
// this button menu.
STDMETHODIMP TipLangBarButton::GetTooltipString(BSTR* tooltip) {
  // Created a COM string from the description and copy it.
  *tooltip = ::SysAllocString(&item_info_.szDescription[0]);
  return (*tooltip ? S_OK : E_OUTOFMEMORY);
}

// Implements the ITfLangBarItemButton::OnClick() function.
// This function is not used for a button menu.
STDMETHODIMP TipLangBarButton::OnClick(TfLBIClick click, POINT point,
                                       const RECT* rect) {
  if (IsMenuButton()) {
    // ITfLangBarItem object is a button menu.
    return S_OK;
  }
  if (click == TF_LBI_CLK_LEFT) {
    return lang_bar_callback_->OnItemClick(item_info_.szDescription);
  }

  // If context menu is disabled, do nothing.
  if (!context_menu_enabled_) {
    return S_OK;
  }

  wil::unique_hmenu menu(::CreatePopupMenu());
  for (size_t i = 0; i < menu_data_size(); ++i) {
    TipLangBarMenuData* data = menu_data(i);
    const UINT id = static_cast<UINT>(data->item_id_);
    MENUITEMINFO info = {};
    info.cbSize = CCSIZEOF_STRUCT(MENUITEMINFO, hbmpItem);
    if (data->flags_ == TF_LBMENUF_SEPARATOR) {
      info.fMask |= MIIM_FTYPE;
      info.fType |= MFT_SEPARATOR;
    } else {
      info.fMask |= MIIM_ID;
      info.wID = id;

      info.fMask |= MIIM_FTYPE;
      info.fType |= MFT_STRING;

      info.fMask |= MIIM_STRING;
      info.dwTypeData = data->text_;

      switch (data->flags_) {
        case TF_LBMENUF_RADIOCHECKED:
          info.fMask |= MIIM_STATE;
          info.fState |= MFS_CHECKED;
          info.fMask |= MIIM_FTYPE;
          info.fType |= MFT_RADIOCHECK;
          break;
        case TF_LBMENUF_CHECKED:
          info.fMask |= MIIM_STATE;
          info.fState |= MFS_CHECKED;
          break;
        case TF_LBMENUF_SUBMENU:
          // TODO(yukawa): Support this.
          break;
        case TF_LBMENUF_GRAYED:
          info.fMask |= MIIM_STATE;
          info.fState |= MFS_GRAYED;
          break;
        default:
          info.fMask |= MIIM_STATE;
          info.fState |= MFS_ENABLED;
          break;
      }
    }
    ::InsertMenuItem(menu.get(), i, TRUE, &info);
  }

  // Caveats: TPM_NONOTIFY is important because the attached window may
  // change the menu state unless this flag is specified. We actually suffered
  // from this issue with Internet Explorer 10 on Windows 8. b/10217103.
  constexpr DWORD kMenuFlags = TPM_NONOTIFY | TPM_RETURNCMD | TPM_LEFTALIGN |
                               TPM_TOPALIGN | TPM_LEFTBUTTON;

  // Make sure that the x-coordinate is inside the work-area.
  const HMONITOR monitor_handle =
      ::MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
  if (monitor_handle != nullptr) {
    MONITORINFO monitor_info = {};
    monitor_info.cbSize = CCSIZEOF_STRUCT(MONITORINFO, dwFlags);
    if (::GetMonitorInfo(monitor_handle, &monitor_info)) {
      point.x = std::clamp(point.x, monitor_info.rcWork.left,
                           monitor_info.rcWork.right);
    }
  }

  const BOOL result = ::TrackPopupMenu(menu.get(), kMenuFlags, point.x, point.y,
                                       0, ::GetFocus(), nullptr);
  if (!result) {
    return E_FAIL;
  }
  return lang_bar_callback_->OnMenuSelect(
      static_cast<TipLangBarCallback::ItemId>(result));
}

// Implements the ITfLangBarItemButton::GetText() function.
// This function is called by Windows to retrieve the text label of this
// button menu.
STDMETHODIMP TipLangBarButton::GetText(BSTR* text) {
  *text = ::SysAllocString(&item_info_.szDescription[0]);
  return (*text ? S_OK : E_OUTOFMEMORY);
}

// Implements the ITfSource::AdviseSink() function.
STDMETHODIMP TipLangBarButton::AdviseSink(REFIID interface_id,
                                          IUnknown* unknown, DWORD* cookie) {
  // Return if the caller tries to start advising any events except the
  // ITfLangBarItemSink events.
  if (!IsEqualIID(IID_ITfLangBarItemSink, interface_id))
    return CONNECT_E_CANNOTCONNECT;

  // Exit if this object has a sink which advising ITfLangBarItemSink events.
  if (item_sink_ != nullptr) return CONNECT_E_ADVISELIMIT;

  // Retrieve the ITfLangBarItemSink interface from the given object and store
  // it into |item_sink_|.
  item_sink_ = ComQuery<ITfLangBarItemSink>(unknown);
  if (item_sink_ == nullptr) {
    return E_INVALIDARG;
  }

  // Return the cookie of this object.
  *cookie = kTipLangBarMenuCookie;
  return S_OK;
}

STDMETHODIMP TipLangBarButton::UnadviseSink(DWORD cookie) {
  if (cookie != kTipLangBarMenuCookie) {
    return E_INVALIDARG;
  }
  if (item_sink_ == nullptr) {
    return CONNECT_E_NOCONNECTION;
  }

  // Release the copy of this event.
  item_sink_.reset();

  return S_OK;
}

// Initializes a TipLangBarButton instance.
// This function is called by a text service to provide the information
// required for creating a menu button. A text service MUST call this function
// before calling the ITfLangBarItemMgr::AddItem() function and adding this
// button menu to a language bar.
HRESULT TipLangBarButton::Init(HINSTANCE instance, int string_id,
                               const TipLangBarMenuItem* menu, int count) {
  // Retrieve the text label from the resource.
  // This string is also used as a tool-tip text.
  ::LoadString(instance, string_id, &item_info_.szDescription[0],
               std::size(item_info_.szDescription));

  // Create a new TipLangBarMenuItem object.
  menu_data_.Init(instance, menu, count);

  return S_OK;
}

HRESULT TipLangBarButton::OnUpdate(DWORD update_flag) {
  // For some reason, this method might be called when the event sink is not
  // available. See b/2977835 for details.
  if (item_sink_ == nullptr) {
    return E_FAIL;
  }
  return item_sink_->OnUpdate(update_flag);
}

HRESULT TipLangBarButton::SetEnabled(bool enabled) {
  HRESULT result = S_OK;

  if (enabled) {
    status_ &= ~TF_LBI_STATUS_DISABLED;
  } else {
    status_ |= TF_LBI_STATUS_DISABLED;
  }
  result = TipLangBarButton::OnUpdate(TF_LBI_STATUS);

  return result;
}

TipLangBarMenuData* TipLangBarButton::menu_data(size_t i) {
  return menu_data_.data(i);
}

size_t TipLangBarButton::menu_data_size() const { return menu_data_.size(); }

const TF_LANGBARITEMINFO* TipLangBarButton::item_info() const {
  return &item_info_;
}

bool TipLangBarButton::IsMenuButton() const {
  return (item_info_.dwStyle & TF_LBI_STYLE_BTN_MENU) == TF_LBI_STYLE_BTN_MENU;
}

void TipLangBarButton::SetContextMenuEnabled(bool enabled) {
  context_menu_enabled_ = enabled;
}

void TipLangBarButton::SetDescription(const std::wstring& description) {
  ::StringCchCopy(item_info_.szDescription, std::size(item_info_.szDescription),
                  description.c_str());
}

TipLangBarToggleButton::TipLangBarToggleButton(
    TipLangBarCallback* langbar_callback, const GUID& guid, bool is_menu,
    bool show_in_tray)
    : TipLangBarButton(langbar_callback, guid, is_menu, show_in_tray),
      menu_selected_(0),
      disabled_(false) {}

// Implements the IUnknown::QueryInterface() function.
// This function is used by Windows to retrieve the interfaces implemented by
// this class.
STDMETHODIMP TipLangBarToggleButton::QueryInterface(REFIID interface_id,
                                                    void** object) {
  if (!object) {
    return E_POINTER;
  }

  // Respond to IMozcLangBarToggleItem in addition to base.
  if (IsIIDOf<IMozcLangBarToggleItem>(interface_id)) {
    *object = absl::implicit_cast<IMozcLangBarToggleItem*>(this);
    AddRef();
    return S_OK;
  }
  return TipLangBarButton::QueryInterface(interface_id, object);
}

// Implements the ITfLangBarItem::GetInfo() function.
// This function is called by Windows to update this button menu.
STDMETHODIMP TipLangBarToggleButton::GetInfo(TF_LANGBARITEMINFO* item_info) {
  if (item_info == nullptr) {
    return E_INVALIDARG;
  }

  // Just copies the cached TF_LANGBARITEMINFO object.
  *item_info = *this->item_info();
  return S_OK;
}

STDMETHODIMP TipLangBarToggleButton::InitMenu(ITfMenu* menu) {
  HRESULT result = S_OK;

  // Do nothing if the langbar item is not menu botton.
  if (!IsMenuButton()) {
    return result;
  }

  // Add the menu items of this object to the given ITfMenu object.
  for (size_t i = 0; i < menu_data_size(); ++i) {
    const TipLangBarMenuData* data = menu_data(i);
    result = menu->AddMenuItem(i, data->flags_, nullptr, nullptr, data->text_,
                               data->length_, nullptr);
    if (result != S_OK) {
      break;
    }
  }

  return result;
}

STDMETHODIMP TipLangBarToggleButton::OnMenuSelect(UINT menu_id) {
  // Call the TipLangBarCallback::OnMenuSelect() function to dispatch the
  // given event.
  TipLangBarMenuData* data = menu_data(menu_id);
  if (data == nullptr) {
    return E_INVALIDARG;
  }
  if (data->item_id_ == TipLangBarCallback::kCancel) {
    return S_OK;
  }
  const HRESULT result = lang_bar_callback_->OnMenuSelect(
      static_cast<TipLangBarCallback::ItemId>(data->item_id_));
  if (result != S_OK) {
    return result;
  }
  TipLangBarMenuData* selected = menu_data(menu_selected_);
  selected->flags_ &= ~TF_LBMENUF_RADIOCHECKED;
  data->flags_ |= TF_LBMENUF_RADIOCHECKED;
  menu_selected_ = menu_id;
  return result;
}

STDMETHODIMP TipLangBarToggleButton::GetIcon(HICON* icon) {
  if (icon == nullptr) {
    return E_INVALIDARG;
  }

  // MSIME 2012 shows special icon when the LangBar item is disabled. Here we
  // adopt this behavior for the consistency.
  // TODO(yukawa): Refactor this design. We should split TipLangBarToggleButton
  //     into two different classes anyway.
  const TipLangBarMenuData& data = !IsMenuButton() && disabled_
                                       ? menu_data_for_disabled_
                                       : *menu_data(menu_selected_);

  //  Excerpt: http://msdn.microsoft.com/en-us/library/ms628718.aspx
  //  The caller must free this icon when it is no longer required by
  //  calling DestroyIcon.
  *icon = CreateTextIcon(data.icon_id_);
  return (*icon ? S_OK : E_FAIL);
}

HRESULT TipLangBarToggleButton::Init(
    HINSTANCE instance, int string_id, const TipLangBarMenuItem* menu,
    int count, const TipLangBarMenuItem& menu_for_disabled) {
  TipLangBarMenuDataArray array;
  if (SUCCEEDED(array.Init(instance, &menu_for_disabled, 1))) {
    menu_data_for_disabled_ = *array.data(0);
  }
  // TODO(yuryu): Change ABSL_ARRAYSIZE to std::size when we upgrade to a  C++17
  // compiler.
  wchar_t buffer[ABSL_ARRAYSIZE(menu_data_for_disabled_.text_)];
  ::LoadString(instance, string_id, buffer, std::size(buffer));
  description_for_enabled_ = buffer;
  return TipLangBarButton::Init(instance, string_id, menu, count);
}

// Implements the IMozcToggleButtonMenu::SelectMenuItem() function.
HRESULT TipLangBarToggleButton::SelectMenuItem(UINT menu_id) {
  // Now SelectMenuItem may be called frequently to update LangbarItem for
  // every key input.  So we call TipLangBarButton::OnUpdate only if any item
  // state is updated.
  bool item_state_changed = false;
  for (size_t i = 0; i < menu_data_size(); ++i) {
    TipLangBarMenuData* data = menu_data(i);
    if (data->item_id_ == menu_id) {
      if ((data->flags_ & TF_LBMENUF_RADIOCHECKED) != 0 ||
          menu_selected_ != static_cast<UINT>(i)) {
        item_state_changed |= true;
      }
      data->flags_ |= TF_LBMENUF_RADIOCHECKED;
      menu_selected_ = static_cast<UINT>(i);
    } else {
      if (data->flags_ != 0) {
        item_state_changed |= true;
      }
      data->flags_ &= ~TF_LBMENUF_RADIOCHECKED;
    }
  }
  if (item_state_changed) {
    TipLangBarButton::OnUpdate(TF_LBI_ICON | TF_LBI_STATUS | TF_LBI_TEXT);
  }
  return S_OK;
}

HRESULT TipLangBarToggleButton::SetEnabled(bool enabled) {
  disabled_ = !enabled;

  // For menu-button, use parent implementation.
  if (IsMenuButton()) {
    return TipLangBarButton::SetEnabled(enabled);
  }

  // For button type element, adopt special behavior to be consistent with
  // MSIME 2012's behavior.
  // TODO(yukawa): Refactor this design. We should split TipLangBarToggleButton
  //     into two different classes anyway.
  SetContextMenuEnabled(enabled);
  if (enabled) {
    const HRESULT result = SelectMenuItem(menu_data(menu_selected_)->item_id_);
    SetDescription(description_for_enabled_);
    TipLangBarButton::OnUpdate(TF_LBI_ICON | TF_LBI_STATUS | TF_LBI_TEXT);
    return result;
  }
  SetDescription(menu_data_for_disabled_.text_);
  TipLangBarButton::OnUpdate(TF_LBI_ICON | TF_LBI_STATUS | TF_LBI_TEXT);
  return S_OK;
}

}  // namespace tsf
}  // namespace win32
}  // namespace mozc
