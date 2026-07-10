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

#include "renderer/qt/qt_server.h"

#if defined(__linux__) && !defined(__ANDROID__)
#include <stdlib.h>
#endif  // __linux__ && !__ANDROID__

#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#include <QMetaType>
#include <QString>
#include <QVariant>
#include <string>

#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "base/system_util.h"
#include "base/vlog.h"
#include "ipc/named_event.h"
#include "protocol/config.pb.h"
#include "protocol/renderer_command.pb.h"
#include "renderer/qt/qt_ipc_thread.h"

#ifndef NDEBUG
#include "config/config_handler.h"
#endif  // NDEBUG

Q_DECLARE_METATYPE(std::string);

namespace mozc {
namespace renderer {

namespace {
constexpr char kServiceName[] = "renderer";

std::string GetServiceName() {
  std::string name = kServiceName;
  const std::string desktop_name = SystemUtil::GetDesktopNameAsString();
  if (!desktop_name.empty()) {
    name += '.';
    name += desktop_name;
  }
  return name;
}

// XDG Desktop Portal `org.freedesktop.portal.Settings` interface, used to read
// and observe the system dark-theme preference.
// https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Settings.html
constexpr char kPortalService[] = "org.freedesktop.portal.Desktop";
constexpr char kPortalPath[] = "/org/freedesktop/portal/desktop";
constexpr char kPortalSettingsInterface[] = "org.freedesktop.portal.Settings";
constexpr char kAppearanceNamespace[] = "org.freedesktop.appearance";
constexpr char kColorSchemeKey[] = "color-scheme";

// color-scheme values per the portal spec: 0 = no preference (treated as
// light), 1 = prefer dark, 2 = prefer light.
bool IsDarkColorScheme(uint color_scheme) { return color_scheme == 1; }
}  // namespace

QtServer::QtServer() {
#ifndef NDEBUG
  mozc::internal::SetConfigVLogLevel(
      config::ConfigHandler::GetSharedConfig()->verbose_level());
#endif  // NDEBUG
}

QtServer::~QtServer() = default;

void QtServer::AsyncExecCommand(absl::string_view command) {
  emit EmitUpdated(std::string(command));
}

void QtServer::Update(std::string command) {
  commands::RendererCommand protocol;
  if (!protocol.ParseFromString(command)) {
    LOG(WARNING) << "Parse From String Failed";
    return;
  }
  ExecCommandInternal(protocol);
}

int QtServer::StartServer(int argc, char** argv) {
#if defined(__linux__) && !defined(__ANDROID__)
  // |QWidget::move()| never works with wayland platform backend. Always use
  // 'xcb' platform backend.  https://github.com/google/mozc/issues/794
  ::setenv("QT_QPA_PLATFORM", "xcb", 1);
#endif  // __linux__ && !__ANDROID__

  qRegisterMetaType<std::string>("std::string");
  QApplication app(argc, argv);

  // send "ready" event to the client
  const std::string name = GetServiceName();
  NamedEventNotifier notifier(name);
  notifier.Notify();

  renderer_.Initialize();
  InitColorThemeWatcher();
  connect(&ipc_thread_, &QtIpcThread::EmitUpdated, this, &QtServer::Update);
  ipc_thread_.start();
  return app.exec();
}

void QtServer::InitColorThemeWatcher() {
  QDBusConnection bus = QDBusConnection::sessionBus();
  if (!bus.isConnected()) {
    return;
  }

  // Read the initial color-scheme value.
  QDBusMessage call = QDBusMessage::createMethodCall(
      kPortalService, kPortalPath, kPortalSettingsInterface, "ReadOne");
  call << QString(kAppearanceNamespace) << QString(kColorSchemeKey);
  const QDBusReply<QDBusVariant> reply = bus.call(call);
  if (reply.isValid()) {
    dark_mode_ = IsDarkColorScheme(reply.value().variant().toUInt());
    renderer_.SetDarkMode(dark_mode_);
  }

  // Subscribe to live changes.
  bus.connect(kPortalService, kPortalPath, kPortalSettingsInterface,
              "SettingChanged", this,
              SLOT(OnSettingChanged(QString, QString, QDBusVariant)));
}

void QtServer::OnSettingChanged(const QString& nameSpace, const QString& key,
                                const QDBusVariant& value) {
  if (nameSpace != QString(kAppearanceNamespace) ||
      key != QString(kColorSchemeKey)) {
    return;
  }
  const bool dark = IsDarkColorScheme(value.variant().toUInt());
  if (dark == dark_mode_) {
    return;  // Ignore duplicate notifications.
  }
  dark_mode_ = dark;
  renderer_.SetDarkMode(dark_mode_);
}

bool QtServer::ExecCommandInternal(const commands::RendererCommand& command) {
  MOZC_VLOG(2) << command;

  return renderer_.ExecCommand(command);
}

}  // namespace renderer
}  // namespace mozc
