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

#include "win32/tip/tip_system_tray_icon.h"

#include <commctrl.h>
#include <ctffunc.h>
#include <ctfutb.h>
#include <oleauto.h>
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
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "base/const.h"
#include "base/process.h"
#include "base/win32/com.h"
#include "base/win32/com_implements.h"
#include "protocol/commands.pb.h"
#include "win32/base/tsf_profile.h"
#include "win32/tip/tip_dll_module.h"
#include "win32/tip/tip_edit_session.h"
#include "win32/tip/tip_input_mode_manager.h"
#include "win32/tip/tip_resource.h"
#include "win32/tip/tip_status.h"
#include "win32/tip/tip_text_service.h"
#include "win32/tip/tip_thread_context.h"

namespace mozc {
namespace win32 {

template <>
bool IsIIDOf<ITfLangBarItemButton>(REFIID riid) {
  return IsIIDOf<ITfLangBarItemButton, ITfLangBarItem>(riid);
}

namespace tsf {
namespace {

#ifdef GOOGLE_JAPANESE_INPUT_BUILD
constexpr char kHelpUrl[] = "http://www.google.com/support/ime/japanese";
#else   // GOOGLE_JAPANESE_INPUT_BUILD
constexpr char kHelpUrl[] = "https://github.com/google/mozc";
#endif  // GOOGLE_JAPANESE_INPUT_BUILD

// Represents the cookie for the sink to a TipLangBarToggleButton object.
constexpr int kTipLangBarMenuCookie =
    (('M' << 24) | ('o' << 16) | ('z' << 8) | ('c' << 0));

// Loads an icon from the resource. An icon ID 0 represents "no icon".
HICON LoadIconFromResource(HINSTANCE instance, UINT icon_id) {
  const auto icon_size = ::GetSystemMetrics(SM_CYSMICON);
  return static_cast<HICON>(::LoadImage(instance, MAKEINTRESOURCE(icon_id),
                                        IMAGE_ICON, icon_size, icon_size,
                                        LR_CREATEDIBSECTION));
}

enum TipLangBarItemFlags {
  kTipLangBarItemTypeDefault = 0,
  kTipLangBarItemTypeChecked = TF_LBMENUF_CHECKED,  // 0x1
  // ImeLangBarItemTypeSubMenu = TF_LBMENUF_SUBMENU  // 0x2 (not supported)
  kTipLangBarItemTypeSeparator = TF_LBMENUF_SEPARATOR,        // 0x4
  kTipLangBarItemTypeRadioChecked = TF_LBMENUF_RADIOCHECKED,  // 0x8
  kTipLangBarItemTypeGrayed = TF_LBMENUF_GRAYED,              // 0x10
};

// Represents a tuple to specify the content of a language bar menu item.
struct TipLangBarMenuItem {
  int flags_;
  UINT item_id_;
  UINT text_id_;
  UINT icon_id_;
};

// Represents the data possessed internally by a language bar menu item.
struct TipLangBarMenuData {
  int flags_;
  UINT item_id_;
  UINT text_id_;
  UINT icon_id_;
  int length_;
  wchar_t text_[TF_LBI_DESC_MAXLEN];
};

// Represents the data possessed by a language bar menu.
class TipLangBarMenuDataArray {
 public:
  HRESULT Init(HINSTANCE instance, const TipLangBarMenuItem* menu, int count);

  size_t size() const;
  TipLangBarMenuData* data(size_t i);

 private:
  std::vector<TipLangBarMenuData> data_;
};

// Represents the input mode button, which is shown in the taskbar as the IME
// mode icon. Left click toggles the IME open/close status and right click
// shows the context menu.
class TipLangBarToggleButton
    : public TipComImplements<ITfLangBarItemButton, ITfSource> {
 public:
  enum ItemId {
    // For input mode selection
    kDirect = 10,
    kHiragana = 11,
    kFullKatakana = 12,
    kHalfAlphanumeric = 13,
    kFullAlphanumeric = 14,
    kHalfKatakana = 15,

    // Tool menu
    kProperty = 20,
    kDictionary = 21,
    kWordRegister = 22,

    // Help Menu
    kHelp = 30,
    kAbout = 31,
  };

  explicit TipLangBarToggleButton(
      wil::com_ptr_nothrow<TipTextService> text_service);

  // The ITfLangBarItem interface methods
  STDMETHODIMP GetInfo(TF_LANGBARITEMINFO* item_info) override;
  STDMETHODIMP GetStatus(DWORD* status) override;
  STDMETHODIMP Show(BOOL show) override;
  STDMETHODIMP GetTooltipString(BSTR* tooltip) override;

  // The ITfLangBarItemButton interface methods
  STDMETHODIMP OnClick(TfLBIClick click, POINT point,
                       const RECT* rect) override;
  STDMETHODIMP InitMenu(ITfMenu* menu) override;
  STDMETHODIMP OnMenuSelect(UINT menu_id) override;
  STDMETHODIMP GetIcon(HICON* icon) override;
  STDMETHODIMP GetText(BSTR* text) override;

  // The ITfSource interface methods
  STDMETHODIMP AdviseSink(REFIID interface_id, IUnknown* unknown,
                          DWORD* cookie) override;
  STDMETHODIMP UnadviseSink(DWORD cookie) override;

  // Initializes an ImeButtonMenu instance.
  // This function allocates resources for an ImeButtonMenu instance.
  HRESULT Init(HINSTANCE instance, int string_id,
               const TipLangBarMenuItem* menu, int count,
               const TipLangBarMenuItem& menu_for_disabled);

  // Selects a menu item which has the given |menu_id|.
  HRESULT SelectMenuItem(UINT menu_id);

  // Sets the status of this language bar menu.
  HRESULT SetEnabled(bool enabled);

 private:
  // Notifies the language bar of a change in a language bar item.
  HRESULT OnUpdate(DWORD update_flag);

  // Update the item description. The caller is also responsible for calling
  // OnUpdate method to notify the change to the system.
  void SetDescription(const std::wstring& description);

  // Returns the i-th data in the language bar menu.
  // Returns nullptr if i is out of bounds.
  TipLangBarMenuData* menu_data(size_t i);

  // Returns the size of the language bar menu.
  size_t menu_data_size() const;

  wil::com_ptr_nothrow<ITfLangBarItemSink> item_sink_;
  // Save the TipTextService object that owns this button to prevent the
  // object from being deleted.
  wil::com_ptr_nothrow<TipTextService> text_service_;

  // Represents the information of an instance copied to the TSF manager.
  // The TSF manager uses this information to identifies an instance as
  // a menu button.
  TF_LANGBARITEMINFO item_info_;

  // Represents the data possessed by the language bar menu.
  TipLangBarMenuDataArray menu_data_;

  // Represents the index of the selected menu item.
  UINT menu_selected_;
  bool disabled_;
  std::wstring description_for_enabled_;
  TipLangBarMenuData menu_data_for_disabled_;
};

HRESULT SpawnTool(const std::string& command) {
  if (!Process::SpawnMozcProcess(kMozcTool, "--mode=" + command)) {
    return E_FAIL;
  }
  return S_OK;
}

commands::CompositionMode GetMozcMode(TipLangBarToggleButton::ItemId menu_id) {
  switch (menu_id) {
    case TipLangBarToggleButton::kDirect:
      return commands::DIRECT;
    case TipLangBarToggleButton::kHiragana:
      return commands::HIRAGANA;
    case TipLangBarToggleButton::kFullKatakana:
      return commands::FULL_KATAKANA;
    case TipLangBarToggleButton::kHalfAlphanumeric:
      return commands::HALF_ASCII;
    case TipLangBarToggleButton::kFullAlphanumeric:
      return commands::FULL_ASCII;
    case TipLangBarToggleButton::kHalfKatakana:
      return commands::HALF_KATAKANA;
    default:
      DLOG(FATAL) << "Unexpected item id: " << menu_id;
      // Fall back to DIRECT in release builds.
      return commands::DIRECT;
  }
}

std::string GetMozcToolCommand(TipLangBarToggleButton::ItemId menu_id) {
  switch (menu_id) {
    case TipLangBarToggleButton::kProperty:
      // Open the config dialog.
      return "config_dialog";
    case TipLangBarToggleButton::kDictionary:
      // Open the dictionary tool.
      return "dictionary_tool";
    case TipLangBarToggleButton::kWordRegister:
      // Open the word register dialog.
      return "word_register_dialog";
    case TipLangBarToggleButton::kAbout:
      // Open the about dialog.
      return "about_dialog";
    default:
      DLOG(FATAL) << "Unexpected item id: " << menu_id;
      return "";
  }
}

HRESULT HandleMenuSelect(TipTextService* text_service,
                         TipLangBarToggleButton::ItemId menu_id) {
  switch (menu_id) {
    case TipLangBarToggleButton::kDirect:
    case TipLangBarToggleButton::kHiragana:
    case TipLangBarToggleButton::kFullKatakana:
    case TipLangBarToggleButton::kHalfAlphanumeric:
    case TipLangBarToggleButton::kFullAlphanumeric:
    case TipLangBarToggleButton::kHalfKatakana: {
      const commands::CompositionMode mozc_mode = GetMozcMode(menu_id);
      return TipEditSession::SwitchInputModeAsync(text_service, mozc_mode)
                 ? S_OK
                 : E_FAIL;
    }
    case TipLangBarToggleButton::kProperty:
    case TipLangBarToggleButton::kDictionary:
    case TipLangBarToggleButton::kWordRegister:
    case TipLangBarToggleButton::kAbout:
      return SpawnTool(GetMozcToolCommand(menu_id));
    case TipLangBarToggleButton::kHelp:
      // Open the about dialog.
      return Process::OpenBrowser(kHelpUrl) ? S_OK : E_FAIL;
    default:
      return S_OK;
  }
}

HRESULT HandleItemClick(TipTextService* text_service) {
  // Change input mode to be consistent with MSIME 2012 on Windows 8.
  const bool open = text_service->GetThreadContext()
                        ->GetInputModeManager()
                        ->GetEffectiveOpenClose();
  if (open) {
    return TipStatus::SetIMEOpen(text_service->GetThreadManager(),
                                 text_service->GetClientID(), false)
               ? S_OK
               : E_FAIL;
  }

  // Like MSIME 2012, switch to Hiragana mode when the LangBar button is
  // clicked.
  return TipEditSession::SwitchInputModeAsync(text_service, commands::HIRAGANA)
             ? S_OK
             : E_FAIL;
}

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

// Implements the constructor of the TipLangBarToggleButton class.
TipLangBarToggleButton::TipLangBarToggleButton(
    wil::com_ptr_nothrow<TipTextService> text_service)
    : text_service_(std::move(text_service)),
      menu_selected_(0),
      disabled_(false) {
  // Initialize its TF_LANGBARITEMINFO object, which contains the properties of
  // this item and is copied to the TSF manager in GetInfo() function.
  item_info_.clsidService = TsfProfile::GetTextServiceGuid();
  item_info_.guidItem = GUID_LBI_INPUTMODE;
  // The visibility of a langbar-item in the minimized langbar is actually
  // controlled by TF_LBI_STYLE_SHOWNINTRAY flag despite the fact that the
  // document says "This flag is not currently supported".
  // http://msdn.microsoft.com/en-us/library/ms629078.aspx
  // http://b/2275633
  item_info_.dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_SHOWNINTRAY;
  item_info_.ulSort = 0;
  item_info_.szDescription[0] = L'\0';
}

// Implements the ITfLangBarItem::GetInfo() function.
// This function is called by Windows to update this button menu.
STDMETHODIMP TipLangBarToggleButton::GetInfo(TF_LANGBARITEMINFO* item_info) {
  if (item_info == nullptr) {
    return E_INVALIDARG;
  }

  // Just copies the cached TF_LANGBARITEMINFO object.
  *item_info = item_info_;
  return S_OK;
}

// Implements the ITfLangBarItem::GetStatus() function.
// This function is called by Windows to retrieve the current status of this
// button menu.
STDMETHODIMP TipLangBarToggleButton::GetStatus(DWORD* status) {
  // TF_LBI_STATUS_DISABLED is never set. The disabled state is represented by
  // a dedicated icon and description instead. See SetEnabled().
  *status = 0;
  return S_OK;
}

// Implements the ITfLangBarItem::Show() function.
// This function is called by Windows to notify the display status of this
// button menu has been updated.
STDMETHODIMP TipLangBarToggleButton::Show(BOOL show) {
  // Just return because this button is always shown, i.e. we do not have to
  // manage the display state of this button menu.
  return E_NOTIMPL;
}

// Implements the ITfLangBarItem::GetTooltipString() function.
// This function is called when Windows to retrieve the tool-tip string of
// this button menu.
STDMETHODIMP TipLangBarToggleButton::GetTooltipString(BSTR* tooltip) {
  // Created a COM string from the description and copy it.
  *tooltip = ::SysAllocString(&item_info_.szDescription[0]);
  return (*tooltip ? S_OK : E_OUTOFMEMORY);
}

// Implements the ITfLangBarItemButton::OnClick() function.
// This function is not used for a button menu.
STDMETHODIMP TipLangBarToggleButton::OnClick(TfLBIClick click, POINT point,
                                             const RECT* rect) {
  if (click == TF_LBI_CLK_LEFT) {
    return HandleItemClick(text_service_.get());
  }

  // If context menu is disabled, do nothing.
  if (disabled_) {
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
  return HandleMenuSelect(text_service_.get(), static_cast<ItemId>(result));
}

// Implements the ITfLangBarItemButton::GetText() function.
// This function is called by Windows to retrieve the text label of this
// button menu.
STDMETHODIMP TipLangBarToggleButton::GetText(BSTR* text) {
  *text = ::SysAllocString(&item_info_.szDescription[0]);
  return (*text ? S_OK : E_OUTOFMEMORY);
}

// Implements the ITfSource::AdviseSink() function.
STDMETHODIMP TipLangBarToggleButton::AdviseSink(REFIID interface_id,
                                                IUnknown* unknown,
                                                DWORD* cookie) {
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

STDMETHODIMP TipLangBarToggleButton::UnadviseSink(DWORD cookie) {
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

HRESULT TipLangBarToggleButton::OnUpdate(DWORD update_flag) {
  // For some reason, this method might be called when the event sink is not
  // available. See b/2977835 for details.
  if (item_sink_ == nullptr) {
    return E_FAIL;
  }
  return item_sink_->OnUpdate(update_flag);
}

TipLangBarMenuData* TipLangBarToggleButton::menu_data(size_t i) {
  return menu_data_.data(i);
}

size_t TipLangBarToggleButton::menu_data_size() const {
  return menu_data_.size();
}

void TipLangBarToggleButton::SetDescription(const std::wstring& description) {
  ::StringCchCopy(item_info_.szDescription, std::size(item_info_.szDescription),
                  description.c_str());
}

// Implements the ITfLangBarItemButton::InitMenu() function.
// This function is not used for a button (as opposed to a button menu).
STDMETHODIMP TipLangBarToggleButton::InitMenu(ITfMenu* menu) { return S_OK; }

// Implements the ITfLangBarItemButton::OnMenuSelect() function.
// This function is not used for a button (as opposed to a button menu). The
// context menu is handled in OnClick() instead.
STDMETHODIMP TipLangBarToggleButton::OnMenuSelect(UINT menu_id) {
  return E_NOTIMPL;
}

STDMETHODIMP TipLangBarToggleButton::GetIcon(HICON* icon) {
  if (icon == nullptr) {
    return E_INVALIDARG;
  }

  // MSIME 2012 shows special icon when the LangBar item is disabled. Here we
  // adopt this behavior for the consistency.
  const TipLangBarMenuData& data =
      disabled_ ? menu_data_for_disabled_ : *menu_data(menu_selected_);

  //  Excerpt: http://msdn.microsoft.com/en-us/library/ms628718.aspx
  //  The caller must free this icon when it is no longer required by
  //  calling DestroyIcon.
  *icon = LoadIconFromResource(TipDllModule::module_handle(), data.icon_id_);
  return (*icon ? S_OK : E_FAIL);
}

// Initializes a TipLangBarToggleButton instance.
// This function is called by a text service to provide the information
// required for creating a menu button. A text service MUST call this function
// before calling the ITfLangBarItemMgr::AddItem() function and adding this
// button menu to a language bar.
HRESULT TipLangBarToggleButton::Init(
    HINSTANCE instance, int string_id, const TipLangBarMenuItem* menu,
    int count, const TipLangBarMenuItem& menu_for_disabled) {
  TipLangBarMenuDataArray array;
  if (SUCCEEDED(array.Init(instance, &menu_for_disabled, 1))) {
    menu_data_for_disabled_ = *array.data(0);
  }
  wchar_t buffer[TF_LBI_DESC_MAXLEN];
  ::LoadString(instance, string_id, buffer, std::size(buffer));
  description_for_enabled_ = buffer;

  // Retrieve the text label from the resource.
  // This string is also used as a tool-tip text.
  ::LoadString(instance, string_id, &item_info_.szDescription[0],
               std::size(item_info_.szDescription));

  // Create a new TipLangBarMenuItem object.
  menu_data_.Init(instance, menu, count);

  return S_OK;
}

HRESULT TipLangBarToggleButton::SelectMenuItem(UINT menu_id) {
  // Now SelectMenuItem may be called frequently to update LangbarItem for
  // every key input.  So we call TipLangBarToggleButton::OnUpdate only if any
  // item state is updated.
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
    OnUpdate(TF_LBI_ICON | TF_LBI_STATUS | TF_LBI_TEXT);
  }
  return S_OK;
}

HRESULT TipLangBarToggleButton::SetEnabled(bool enabled) {
  disabled_ = !enabled;

  // For button type element, adopt special behavior to be consistent with
  // MSIME 2012's behavior.
  if (enabled) {
    const HRESULT result = SelectMenuItem(menu_data(menu_selected_)->item_id_);
    SetDescription(description_for_enabled_);
    OnUpdate(TF_LBI_ICON | TF_LBI_STATUS | TF_LBI_TEXT);
    return result;
  }
  SetDescription(menu_data_for_disabled_.text_);
  OnUpdate(TF_LBI_ICON | TF_LBI_STATUS | TF_LBI_TEXT);
  return S_OK;
}

TipLangBarToggleButton::ItemId GetItemId(DWORD composition_mode) {
  switch (composition_mode) {
    case commands::DIRECT:
      return TipLangBarToggleButton::kDirect;
    case commands::HIRAGANA:
      return TipLangBarToggleButton::kHiragana;
    case commands::FULL_KATAKANA:
      return TipLangBarToggleButton::kFullKatakana;
    case commands::HALF_ASCII:
      return TipLangBarToggleButton::kHalfAlphanumeric;
    case commands::FULL_ASCII:
      return TipLangBarToggleButton::kFullAlphanumeric;
    case commands::HALF_KATAKANA:
      return TipLangBarToggleButton::kHalfKatakana;
    default:
      LOG(ERROR) << "Unknown composition mode: " << composition_mode;
      return TipLangBarToggleButton::kDirect;
  }
}

}  // namespace

// Initializes the input mode button, which is shown in the taskbar.
HRESULT TipSystemTrayIcon::Init(TipTextService* text_service) {
  HRESULT result = S_OK;

  // TODO(yukawa): Optimize this method. We do not need to obtain an instance of
  // ITfLangBarItemMgr unless there remains something to be initialized for
  // LangBar.

  // A workaround to satisfy both b/6106437 and b/6641460.
  // Keep the instance in |lang_bar_item_mgr_|.
  if (!lang_bar_item_mgr_) {
    result = TF_CreateLangBarItemMgr(lang_bar_item_mgr_.put());
    if (FAILED(result)) {
      return result;
    }
    if (!lang_bar_item_mgr_) {
      return E_FAIL;
    }
  }

  const TipLangBarMenuItem kInputMenuDisabled = {kTipLangBarItemTypeDefault, 0,
                                                 IDS_DISABLED, IDI_DISABLED};

  if (!input_mode_button_for_win8_) {
    // Add the "Input Mode" button.
    constexpr TipLangBarMenuItem kInputMenu[] = {
        {kTipLangBarItemTypeDefault, TipLangBarToggleButton::kHiragana,
         IDS_HIRAGANA, IDI_HIRAGANA},
        {kTipLangBarItemTypeDefault, TipLangBarToggleButton::kFullKatakana,
         IDS_FULL_KATAKANA, IDI_FULL_KATAKANA},
        {kTipLangBarItemTypeDefault, TipLangBarToggleButton::kFullAlphanumeric,
         IDS_FULL_ALPHANUMERIC, IDI_FULL_ALPHANUMERIC},
        {kTipLangBarItemTypeDefault, TipLangBarToggleButton::kHalfKatakana,
         IDS_HALF_KATAKANA, IDI_HALF_KATAKANA},
        {kTipLangBarItemTypeDefault, TipLangBarToggleButton::kHalfAlphanumeric,
         IDS_HALF_ALPHANUMERIC, IDI_HALF_ALPHANUMERIC},
        {kTipLangBarItemTypeDefault, TipLangBarToggleButton::kDirect,
         IDS_DIRECT, IDI_DIRECT},
        {kTipLangBarItemTypeSeparator, 0, 0, 0},
        {kTipLangBarItemTypeDefault, TipLangBarToggleButton::kDictionary,
         IDS_DICTIONARY, 0},
        {kTipLangBarItemTypeDefault, TipLangBarToggleButton::kWordRegister,
         IDS_WORD_REGISTER, 0},
        {kTipLangBarItemTypeDefault, TipLangBarToggleButton::kProperty,
         IDS_PROPERTY, 0},
        {kTipLangBarItemTypeSeparator, 0, 0, 0},
        {kTipLangBarItemTypeDefault, TipLangBarToggleButton::kAbout, IDS_ABOUT,
         0},
        {kTipLangBarItemTypeDefault, TipLangBarToggleButton::kHelp, IDS_HELP,
         0},
    };

    auto input_mode_menu = MakeComPtr<TipLangBarToggleButton>(text_service);
    if (input_mode_menu == nullptr) {
      return E_OUTOFMEMORY;
    }

    result = input_mode_menu->Init(TipDllModule::module_handle(),
                                   IDS_WIN8_TRAY_ITEM, kInputMenu,
                                   std::size(kInputMenu), kInputMenuDisabled);
    if (FAILED(result)) {
      return result;
    }
    result = lang_bar_item_mgr_->AddItem(input_mode_menu.get());
    input_mode_button_for_win8_ = std::move(input_mode_menu);
  }

  return result;
}

// IMPORTANT: See b/6106437 and b/6641460 before you change this method.
HRESULT TipSystemTrayIcon::Uninit() {
  HRESULT result = S_OK;

  // A workaround to satisfy both b/6106437 and b/6641460.
  // Retrieve the instance from |lang_bar_item_mgr_|.
  wil::com_ptr_nothrow<ITfLangBarItemMgr> item = std::move(lang_bar_item_mgr_);
  if (!item) {
    return E_FAIL;
  }

  if (input_mode_button_for_win8_) {
    item->RemoveItem(input_mode_button_for_win8_.get());
    input_mode_button_for_win8_.reset();
  }
  return result;
}

HRESULT TipSystemTrayIcon::Update(bool enabled, uint32_t composition_mode) {
  // The downcast is safe because |input_mode_button_for_win8_| only holds a
  // TipLangBarToggleButton created in Init().
  auto* button =
      static_cast<TipLangBarToggleButton*>(input_mode_button_for_win8_.get());
  const UINT menu_id = GetItemId(composition_mode);
  button->SelectMenuItem(menu_id);
  button->SetEnabled(enabled);
  return S_OK;
}

bool TipSystemTrayIcon::IsInitialized() const {
  return input_mode_button_for_win8_ != nullptr;
}

}  // namespace tsf
}  // namespace win32
}  // namespace mozc
