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

#ifdef _WIN32

#include "win32/cache_service/cache_service_rpc_server.h"

// clang-format off
#include <windows.h>
#include <rpc.h>
#include <sddl.h>
// clang-format on

#include <wil/resource.h>

#include <cstdlib>

#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "base/system_util.h"
#include "base/win32/win_sandbox.h"
#include "ipc/win/rpc_interface.h"
#include "ipc/win/rpc_server_stub.h"

namespace {

// Security profile for mozc_server.exe, mirroring client/server_launcher.cc:
// a non-admin restricted primary token, low integrity, locked-down job, system
// startup directory, and no window (the converter is windowless).
mozc::WinSandbox::SecurityInfo MakeServerLaunchSecurityInfo() {
  mozc::WinSandbox::SecurityInfo info;
  info.primary_level = mozc::WinSandbox::USER_NON_ADMIN;
  info.impersonation_level = mozc::WinSandbox::USER_RESTRICTED_SAME_ACCESS;
  info.integrity_level = mozc::WinSandbox::INTEGRITY_LEVEL_LOW;
  info.use_locked_down_job = true;
  info.allow_ui_operation = false;
  info.in_system_dir = true;
  info.creation_flags = CREATE_DEFAULT_ERROR_MODE | CREATE_NO_WINDOW;
  // This service runs in session 0, so the launch is cross-session. Even though
  // mozc_server.exe is windowless, CreateProcessAsUser must attach the child to
  // a window station / desktop that lives in the *token's* session (the user's),
  // not the service's session-0 desktop - otherwise it fails with
  // ERROR_NO_SYSTEM_RESOURCES (1450). Naming the interactive desktop resolves it
  // in the user's session. (The in-session launchers inherit this implicitly.)
  info.desktop_name = L"WinSta0\\Default";
  return info;
}

// Security profile for mozc_renderer.exe, mirroring the RendererLauncher in
// renderer/renderer_client.cc (USER_INTERACTIVE primary level, UI operation
// allowed, no CREATE_NO_WINDOW). Unlike that in-session launcher, this service
// runs in session 0, so we additionally target the interactive desktop
// explicitly via desktop_name; the in-session launcher inherits it implicitly.
mozc::WinSandbox::SecurityInfo MakeRendererLaunchSecurityInfo() {
  mozc::WinSandbox::SecurityInfo info;
  info.primary_level = mozc::WinSandbox::USER_INTERACTIVE;
  info.impersonation_level = mozc::WinSandbox::USER_RESTRICTED_SAME_ACCESS;
  info.integrity_level = mozc::WinSandbox::INTEGRITY_LEVEL_LOW;
  info.use_locked_down_job = true;
  info.allow_ui_operation = true;  // skip UI protection (renderer draws a window)
  info.in_system_dir = true;
  info.creation_flags = CREATE_DEFAULT_ERROR_MODE;
  info.desktop_name = L"WinSta0\\Default";
  return info;
}

// Launches |app_path| (UTF-8) in the calling RPC client's session, sandboxed
// with the client's identity. |binding| is the manager-routine handle_t used to
// impersonate.
//
// The RPC interpreter delivers calls on its own threads. We impersonate the
// caller to capture its token, revert to the service's own (LocalSystem)
// identity (so we keep SeAssignPrimaryTokenPrivilege / SeIncreaseQuotaPrivilege
// for the launch), then hand the caller's token to WinSandbox, which derives the
// restricted, low-integrity tokens and spawns the child - exactly like
// client/server_launcher.cc, but based on the client's token instead of this
// process's token.
HRESULT LaunchAsClient(handle_t binding, absl::string_view app_path,
                       const mozc::WinSandbox::SecurityInfo &info) {
  RPC_STATUS rpc_status = ::RpcImpersonateClient(binding);
  if (rpc_status != RPC_S_OK) {
    LOG(ERROR) << "RpcImpersonateClient failed: " << rpc_status;
    return HRESULT_FROM_WIN32(rpc_status);
  }

  // Grab the caller's impersonation token, then immediately revert so the rest
  // runs with the service identity.
  wil::unique_handle impersonation_token;
  const BOOL opened = ::OpenThreadToken(
      ::GetCurrentThread(),
      TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY,
      /*OpenAsSelf=*/TRUE, impersonation_token.put());
  const DWORD open_error = ::GetLastError();
  ::RpcRevertToSelf();
  if (!opened) {
    LOG(ERROR) << "OpenThreadToken failed: " << open_error;
    return HRESULT_FROM_WIN32(open_error);
  }

  // WinSandbox::SpawnSandboxedProcessAs derives the restricted token from a
  // primary token (CreateRestrictedToken preserves the token type), so convert
  // the impersonation token to a primary token first.
  wil::unique_handle primary_token;
  if (!::DuplicateTokenEx(impersonation_token.get(), TOKEN_ALL_ACCESS, nullptr,
                          SecurityImpersonation, TokenPrimary,
                          primary_token.put())) {
    const DWORD error = ::GetLastError();
    LOG(ERROR) << "DuplicateTokenEx failed: " << error;
    return HRESULT_FROM_WIN32(error);
  }

  DWORD pid = 0;
  DWORD spawn_error = 0;
  int fail_stage = 0;
  if (!mozc::WinSandbox::SpawnSandboxedProcessAs(
          app_path, /*arg=*/"", primary_token.get(), info, &pid, &spawn_error,
          &fail_stage)) {
    LOG(ERROR) << "SpawnSandboxedProcessAs failed: stage=" << fail_stage
               << " error=" << spawn_error;
    return spawn_error != 0 ? HRESULT_FROM_WIN32(spawn_error) : E_FAIL;
  }
  return S_OK;
}

}  // namespace

// RPC allocator hooks required by the NDR interpreter. The launch helper returns
// no [out] payload, but the runtime still needs these symbols.
extern "C" void *__RPC_USER MIDL_user_allocate(size_t size) {
  return ::malloc(size);
}

extern "C" void __RPC_USER MIDL_user_free(void *ptr) { ::free(ptr); }

// The single interface method (manager routine). Operations are selected by
// |messageId|; the launch helper ignores all payload / handle parameters and
// returns no [out] data. Every [out] parameter must still be initialized for the
// NDR interpreter to marshal the reply.
extern "C" HRESULT ProcessIpcRequest(
    /* [in] */ handle_t IDL_handle,
    /* [in] */ long messageId,
    /* [in] */ unsigned long /*dataSize*/,
    /* [in] */ unsigned char * /*data*/,
    /* [in] */ unsigned long /*fileHandleCount*/,
    /* [in] */ HANDLE * /*fileHandles*/,
    /* [in] */ unsigned long /*eventHandleCount*/,
    /* [in] */ HANDLE * /*eventHandles*/,
    /* [in] */ unsigned long /*mutexHandleCount*/,
    /* [in] */ HANDLE * /*mutexHandles*/,
    /* [in] */ unsigned long /*processHandleCount*/,
    /* [in] */ HANDLE * /*processHandles*/,
    /* [in] */ unsigned long /*registryHandleCount*/,
    /* [in] */ HKEY * /*registryHandles*/,
    /* [in] */ unsigned long /*pipeHandleCount*/,
    /* [in] */ HANDLE * /*pipeHandles*/,
    /* [in] */ unsigned long /*sectionHandleCount*/,
    /* [in] */ HANDLE * /*sectionHandles*/,
    /* [out] */ unsigned long *responseDataSize,
    /* [out] */ unsigned char **responseData,
    /* [out] */ unsigned long *responseFileHandleCount,
    /* [out] */ HANDLE **responseFileHandles,
    /* [out] */ unsigned long *responseEventHandleCount,
    /* [out] */ HANDLE **responseEventHandles,
    /* [out] */ unsigned long *responseMutexHandleCount,
    /* [out] */ HANDLE **responseMutexHandles,
    /* [out] */ unsigned long *responseProcessHandleCount,
    /* [out] */ HANDLE **responseProcessHandles,
    /* [out] */ unsigned long *responseRegistryHandleCount,
    /* [out] */ HKEY **responseRegistryHandles,
    /* [out] */ unsigned long *responsePipeHandleCount,
    /* [out] */ HANDLE **responsePipeHandles,
    /* [out] */ unsigned long *responseSectionHandleCount,
    /* [out] */ HANDLE **responseSectionHandles) {
  // No [out] payload from the launch helper: zero every reply parameter.
  *responseDataSize = 0;
  *responseData = nullptr;
  *responseFileHandleCount = 0;
  *responseFileHandles = nullptr;
  *responseEventHandleCount = 0;
  *responseEventHandles = nullptr;
  *responseMutexHandleCount = 0;
  *responseMutexHandles = nullptr;
  *responseProcessHandleCount = 0;
  *responseProcessHandles = nullptr;
  *responseRegistryHandleCount = 0;
  *responseRegistryHandles = nullptr;
  *responsePipeHandleCount = 0;
  *responsePipeHandles = nullptr;
  *responseSectionHandleCount = 0;
  *responseSectionHandles = nullptr;

  switch (messageId) {
    case mozc::ipc::win::kMsgLaunchServer:
      return LaunchAsClient(IDL_handle, mozc::SystemUtil::GetServerPath(),
                            MakeServerLaunchSecurityInfo());
    case mozc::ipc::win::kMsgLaunchRenderer:
      // Verified end to end: the restricted low-IL token attaches to the user's
      // interactive desktop (info.desktop_name = "WinSta0\\Default") and runs a
      // message loop in the caller's session without extra window-station /
      // desktop ACL grants. See RPC_DESIGN.md.
      return LaunchAsClient(IDL_handle, mozc::SystemUtil::GetRendererPath(),
                            MakeRendererLaunchSecurityInfo());
    default:
      LOG(ERROR) << "Unknown messageId: " << messageId;
      return E_INVALIDARG;
  }
}

namespace mozc {

CacheServiceRpcServer::~CacheServiceRpcServer() { Stop(); }

bool CacheServiceRpcServer::Start() {
  // Endpoint security descriptor. This service runs as LocalSystem and serves
  // arbitrary logged-in users, so we must grant connect access to authenticated
  // users (plus sandboxed client SIDs) - we cannot reuse
  // WinSandbox::MakeSecurityDescriptor(), which grants "the current user", i.e.
  // LocalSystem here, locking out every real client.
  //
  //   DACL: SY LocalSystem, BA Built-in Administrators, AU Authenticated Users,
  //         AC ALL APPLICATION PACKAGES (AppContainer), RC Restricted Code.
  //   SACL: low integrity label, no-execute-up, so low-IL clients may connect.
  // TODO(yukawa): tighten to specific AppContainer capability SIDs if needed.
  constexpr wchar_t kEndpointSddl[] =
      L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;AU)(A;;GA;;;AC)(A;;GA;;;RC)"
      L"S:(ML;;NX;;;LW)";
  wil::unique_hlocal_security_descriptor security_descriptor;
  if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(
          kEndpointSddl, SDDL_REVISION_1, security_descriptor.put(),
          /*SecurityDescriptorSize=*/nullptr)) {
    LOG(ERROR) << "ConvertStringSecurityDescriptorToSecurityDescriptorW failed: "
               << ::GetLastError();
    return false;
  }

  RPC_STATUS status = ::RpcServerUseProtseqEpW(
      reinterpret_cast<RPC_WSTR>(
          const_cast<wchar_t *>(ipc::win::kProtocolSequence)),
      RPC_C_PROTSEQ_MAX_REQS_DEFAULT,
      reinterpret_cast<RPC_WSTR>(
          const_cast<wchar_t *>(ipc::win::kCacheServiceEndpoint)),
      security_descriptor.get());
  // A previous incarnation may already own the endpoint within this process.
  if (status != RPC_S_OK && status != RPC_S_DUPLICATE_ENDPOINT) {
    LOG(ERROR) << "RpcServerUseProtseqEpW failed: " << status;
    return false;
  }

  // Register the NTLM (WinNT) SSP so callers can authenticate. Over local
  // ncalrpc this is the ONLY authentication service the RPC runtime supports:
  // Negotiate/Kerberos are network-transport features (they need an SPN exchange
  // that local ALPC does not carry), and a client's RpcBindingSetAuthInfoEx
  // rejects them on an ncalrpc binding with RPC_S_UNKNOWN_AUTHN_SERVICE (1747).
  //
  // This authenticates the *client* to us, which is all we require here: it
  // satisfies RPC_IF_ALLOW_SECURE_ONLY and lets RpcImpersonateClient yield the
  // caller's identity for the launch. We deliberately do NOT depend on this for
  // client->server (anti-squatting) authentication - local NTLM offers no usable
  // mutual-auth principal for a Virtual Service Account. Instead, the predictable
  // endpoint is protected by ownership: this auto-start LocalSystem service binds
  // the ALPC port before any user code runs, so a same-user process cannot
  // preempt the name. (Revisit if we expose features that hand back secrets.)
  status = ::RpcServerRegisterAuthInfoW(/*ServerPrincName=*/nullptr,
                                        RPC_C_AUTHN_WINNT,
                                        /*GetKeyFn=*/nullptr, /*Arg=*/nullptr);
  if (status != RPC_S_OK) {
    LOG(ERROR) << "RpcServerRegisterAuthInfoW failed: " << status;
    return false;
  }

  // Local, authenticated callers only.
  // TODO(yukawa): add a security callback to enforce a minimum authentication
  // level (PKT_PRIVACY) via RpcServerInqCallAttributes.
  status = ::RpcServerRegisterIf3(
      MozcCacheServiceRpc_v1_0_s_ifspec, /*MgrTypeUuid=*/nullptr,
      /*MgrEpv=*/nullptr, RPC_IF_ALLOW_LOCAL_ONLY | RPC_IF_ALLOW_SECURE_ONLY,
      RPC_C_LISTEN_MAX_CALLS_DEFAULT, /*MaxRpcSize=*/0,
      /*IfCallback=*/nullptr, /*SecurityDescriptor=*/nullptr);
  if (status != RPC_S_OK) {
    LOG(ERROR) << "RpcServerRegisterIf3 failed: " << status;
    return false;
  }
  registered_ = true;

  // Listen on RPC runtime-managed threads without blocking (DontWait = TRUE).
  status = ::RpcServerListen(/*MinimumCallThreads=*/1,
                             RPC_C_LISTEN_MAX_CALLS_DEFAULT, /*DontWait=*/TRUE);
  if (status != RPC_S_OK && status != RPC_S_ALREADY_LISTENING) {
    LOG(ERROR) << "RpcServerListen failed: " << status;
    Stop();
    return false;
  }
  listening_ = true;
  return true;
}

void CacheServiceRpcServer::Stop() {
  if (listening_) {
    ::RpcMgmtStopServerListening(nullptr);
    ::RpcMgmtWaitServerListen();
    listening_ = false;
  }
  if (registered_) {
    ::RpcServerUnregisterIf(MozcCacheServiceRpc_v1_0_s_ifspec,
                            /*MgrTypeUuid=*/nullptr, /*WaitForCallsToComplete=*/
                            TRUE);
    registered_ = false;
  }
}

}  // namespace mozc

#endif  // _WIN32
