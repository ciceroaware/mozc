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

#ifndef MOZC_BASE_WIN32_WIN_SANDBOX_H_
#define MOZC_BASE_WIN32_WIN_SANDBOX_H_

#include <accctrl.h>
#include <wil/resource.h>
#include <windows.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/string_view.h"

namespace mozc {
class Sid {
 public:
  explicit Sid(const SID* sid);
  explicit Sid(WELL_KNOWN_SID_TYPE type);
  const SID* GetPSID() const;
  SID* GetPSID();
  std::wstring GetName() const;
  std::wstring GetAccountName() const;

 private:
  BYTE sid_[SECURITY_MAX_SID_SIZE];
};

class WinSandbox {
 public:
  WinSandbox() = delete;
  WinSandbox(const WinSandbox&) = delete;
  WinSandbox& operator=(const WinSandbox&) = delete;

  // This emum is not compatible with the same name enum in Chromium sandbox
  // library.  This num has INTEGRITY_LEVEL_MEDIUM_PLUS and lacks of
  // INTEGRITY_LEVEL_MEDIUM_LOW and INTEGRITY_LEVEL_BELOW_LOW, which are not
  // listed the predefined SID page on Microsoft KB page.
  // http://msdn.microsoft.com/en-us/library/cc980032.aspx
  // http://support.microsoft.com/kb/243330
  // c.f.)
  // http://src.chromium.org/viewvc/chrome/trunk/src/sandbox/src/security_level.h?view=markup
  enum IntegrityLevel {
    INTEGRITY_LEVEL_SYSTEM,
    INTEGRITY_LEVEL_HIGH,
    INTEGRITY_LEVEL_MEDIUM_PLUS,
    INTEGRITY_LEVEL_MEDIUM,
    INTEGRITY_LEVEL_LOW,
    INTEGRITY_LEVEL_UNTRUSTED,
    INTEGRITY_LEVEL_LAST,
  };

  // Clone of sandbox library's constants.
  // http://src.chromium.org/viewvc/chrome/trunk/src/sandbox/src/security_level.h?view=markup
  enum TokenLevel {
    USER_LOCKDOWN = 0,
    USER_RESTRICTED,
    USER_LIMITED,
    USER_INTERACTIVE,
    USER_NON_ADMIN,
    USER_RESTRICTED_SAME_ACCESS,
    USER_UNPROTECTED,
  };

  // Returns a security descriptor that only permit current user and system to
  // access the target resource.
  //
  // Usage:
  // auto security_descriptor =
  //     MakeSecurityDescriptor(WinSandbox::kSharablePipe);
  // if (!security_descriptor) {
  //  LOG(ERROR) << "Cannot make SecurityDescriptor";
  //  return;
  // }
  //
  // SECURITY_ATTRIBUTES security_attributes = {
  //    .nLength = sizeof(SECURITY_ATTRIBUTES),
  //    .lpSecurityDescriptor = security_descriptor.get(),
  //    .bInheritHandle = FALSE,
  // };
  //
  // handle_ = ::CreateNamedPipe(..
  //                             PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
  //                             FILE_FLAG_FIRST_PIPE_INSTANCE,
  //                             PIPE_REJECT_REMOTE_CLIENTS | ...,
  //                             ...
  //                             &security_attributes);
  enum ObjectSecurityType {
    // Used for an object that is inaccessible from lower sandbox level.
    kPrivateObject = 0,
    // Used for a namedpipe object that is accessible from lower sandbox level.
    kSharablePipe,
    // Used for a namedpipe object that is accessible from lower sandbox level
    // including processes with restricted tokens.
    kLooseSharablePipe,
    // Used for an event object that is accessible from lower sandbox level.
    kSharableEvent,
    // Used for a mutex object that is accessible from lower sandbox level.
    kSharableMutex,
    // Used for a file object that can be read from lower sandbox level.
    kSharableFileForRead,
    // Used for an IPC process object that is queriable from lower sandbox
    // level.
    kIPCServerProcess,
  };
  static wil::unique_hlocal_security_descriptor MakeSecurityDescriptor(
      ObjectSecurityType shareble_object_type);

  // Variant whose ACEs reference |token|'s user / primary-group SIDs instead of
  // the calling thread/process's. Use this when one security context (e.g. a
  // LocalSystem service) must prepare an object that has to be accessible to a
  // different user (e.g. the client a sandboxed child is launched for).
  static wil::unique_hlocal_security_descriptor MakeSecurityDescriptor(
      ObjectSecurityType shareble_object_type, HANDLE token);

  // Adds an ACE represented by |known_sid| and |access| to the dacl of the
  // kernel object referenced by |object|. |inheritance_flag| is a set of bit
  // flags that determines whether other containers or objects can inherit the
  // ACE from the primary object to which the ACL is attached.
  // This method is basically compatible with the same name function in the
  // Chromium sandbox library except for |inheritance_flag|.
  static bool AddKnownSidToKernelObject(HANDLE object, const SID* known_sid,
                                        DWORD inheritance_flag,
                                        ACCESS_MASK access_mask);

  struct SecurityInfo {
    SecurityInfo();
    TokenLevel primary_level;
    TokenLevel impersonation_level;
    IntegrityLevel integrity_level;
    uint32_t creation_flags;
    bool use_locked_down_job;
    bool allow_ui_operation;
    bool in_system_dir;
    // If non-empty, sets STARTUPINFO::lpDesktop for the child process, e.g.
    // L"WinSta0\\Default" to target the interactive window station/desktop when
    // launching a GUI process such as mozc_renderer.exe (notably across
    // sessions). If empty, lpDesktop is left null and the child inherits the
    // default desktop.
    std::wstring desktop_name;
  };

  // Spawn a process specified by path as the specified integrity level and job
  // level.
  // Return true if process is successfully launched.
  // if pid is specified, pid of child process is set.
  static bool SpawnSandboxedProcess(absl::string_view path,
                                    absl::string_view arg,
                                    const SecurityInfo& info, DWORD* pid);

  // Variant of SpawnSandboxedProcess that derives the restricted, low-integrity
  // tokens from |effective_token| instead of the current process token. Use
  // this to launch a child on behalf of another user from a different security
  // context, e.g. a service launching mozc_server.exe / mozc_renderer.exe after
  // impersonating an RPC client. |effective_token| must be a primary token
  // (convert an impersonation token with DuplicateTokenEx/TokenPrimary first).
  // The child receives an environment block built from |effective_token| so
  // user-specific variables (USERPROFILE, APPDATA, ...) resolve to the launching
  // user rather than the caller's account.
  // Return true if the process is successfully launched; if pid is specified,
  // pid of the child process is set.
  // |error_code|, if non-null, receives the Win32 error from the failing step
  // when the launch fails (0 on success), so callers (e.g. a remote RPC client)
  // can surface a precise reason instead of a generic failure. |fail_stage|, if
  // non-null, receives a small integer identifying which step failed (see
  // SpawnFailureStage), 0 on success.
  enum SpawnFailureStage {
    kSpawnStageNone = 0,
    kSpawnStagePrimaryToken = 1,
    kSpawnStageImpersonationToken = 2,
    kSpawnStageThreadTokenDacl = 3,
    kSpawnStageJob = 4,
    kSpawnStageCreateProcess = 5,
    kSpawnStageSetThreadToken = 6,
  };
  static bool SpawnSandboxedProcessAs(absl::string_view path,
                                      absl::string_view arg,
                                      HANDLE effective_token,
                                      const SecurityInfo& info, DWORD* pid,
                                      DWORD* error_code = nullptr,
                                      int* fail_stage = nullptr);

  // Following three methods returns corresponding list of SID or LUID for
  // CreateRestrictedToken API, depending on given |effective_token| and
  // |security_level|.  These methods emulates CreateRestrictedToken
  // method in the Chromium sandbox library.
  // http://src.chromium.org/viewvc/chrome/trunk/src/sandbox/src/restricted_token_utils.cc?view=markup
  static std::vector<Sid> GetSidsToDisable(HANDLE effective_token,
                                           TokenLevel security_level);
  static std::vector<LUID> GetPrivilegesToDisable(HANDLE effective_token,
                                                  TokenLevel security_level);
  static std::vector<Sid> GetSidsToRestrict(HANDLE effective_token,
                                            TokenLevel security_level);

  // Returns true if a restricted token handle is successfully assigned into
  // |restricted_token|.
  static wil::unique_process_handle GetRestrictedTokenHandle(
      HANDLE effective_token, TokenLevel security_level,
      IntegrityLevel integrity_level);

  // Returns true if a restricted token handle for impersonation is
  // successfully assigned into |restricted_token|.
  static wil::unique_process_handle GetRestrictedTokenHandleForImpersonation(
      HANDLE effective_token, TokenLevel security_level,
      IntegrityLevel integrity_level);

  enum class AppContainerVisibilityType {
    kProgramFiles = 0,
    kConfigFile = 1,
  };

  // Returns true |file_name| already has or is updated to have an ACE
  // (Access Control Entry) for "All Application Packages" group.
  static bool EnsureAllApplicationPackagesPermisssion(
      std::wstring_view file_name, AppContainerVisibilityType type);

 protected:
  // Returns SDDL for given |shareble_object_type|.
  // This method is placed here for unit testing.
  static std::wstring GetSDDL(ObjectSecurityType shareble_object_type,
                              std::wstring_view token_user_sid,
                              std::wstring_view token_primary_group_sid);
};

}  // namespace mozc

#endif  // MOZC_BASE_WIN32_WIN_SANDBOX_H_
