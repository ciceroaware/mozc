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

#include "ipc/win/cache_service_rpc_client.h"

// clang-format off
#include <windows.h>
#include <rpc.h>
// clang-format on

#include <cstdint>
#include <cstdlib>

#include "absl/log/log.h"
#include "ipc/win/rpc_client_stub.h"
#include "ipc/win/rpc_interface.h"

// RPC allocator hooks used by the NDR client interpreter to allocate any [out]
// data. The launch operations return no payload, but these symbols must exist.
extern "C" void *__RPC_USER MIDL_user_allocate(size_t size) {
  return ::malloc(size);
}

extern "C" void __RPC_USER MIDL_user_free(void *ptr) { ::free(ptr); }

namespace mozc::ipc::win {
namespace {

inline RPC_WSTR AsRpcWStr(const wchar_t *str) {
  return reinterpret_cast<RPC_WSTR>(const_cast<wchar_t *>(str));
}

// Invokes ProcessIpcRequest under Win32 SEH so RPC runtime exceptions (server
// unavailable, access denied, timeout, ...) become a return code instead of
// crashing the caller. No request/response payload or handles are used by the
// launch operations, so all those parameters are zero/null. On a raised
// exception, *seh_code holds the RPC_STATUS and the return value is E_FAIL.
//
// This function is intentionally free of C++ objects with destructors so SEH and
// C++ unwinding do not mix (avoids C2712 under /EHsc).
HRESULT InvokeProcessIpcRequest(handle_t binding, long message_id,
                                DWORD *seh_code) {
  *seh_code = 0;

  unsigned long response_data_size = 0;
  unsigned char *response_data = nullptr;
  unsigned long file_count = 0, event_count = 0, mutex_count = 0,
                process_count = 0, registry_count = 0, pipe_count = 0,
                section_count = 0;
  HANDLE *file_handles = nullptr, *event_handles = nullptr,
         *mutex_handles = nullptr, *process_handles = nullptr,
         *pipe_handles = nullptr, *section_handles = nullptr;
  HKEY *registry_handles = nullptr;

  HRESULT hr = E_FAIL;
  __try {
    hr = ProcessIpcRequest(
        binding, message_id,
        /*dataSize=*/0, /*data=*/nullptr,
        /*fileHandleCount=*/0, /*fileHandles=*/nullptr,
        /*eventHandleCount=*/0, /*eventHandles=*/nullptr,
        /*mutexHandleCount=*/0, /*mutexHandles=*/nullptr,
        /*processHandleCount=*/0, /*processHandles=*/nullptr,
        /*registryHandleCount=*/0, /*registryHandles=*/nullptr,
        /*pipeHandleCount=*/0, /*pipeHandles=*/nullptr,
        /*sectionHandleCount=*/0, /*sectionHandles=*/nullptr,
        &response_data_size, &response_data, &file_count, &file_handles,
        &event_count, &event_handles, &mutex_count, &mutex_handles,
        &process_count, &process_handles, &registry_count, &registry_handles,
        &pipe_count, &pipe_handles, &section_count, &section_handles);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    *seh_code = ::GetExceptionCode();
    return E_FAIL;
  }

  // The launch operations return no payload; free any [out] memory defensively
  // so we do not leak if the server allocated something unexpected.
  if (response_data != nullptr) {
    MIDL_user_free(response_data);
  }
  if (file_handles != nullptr) MIDL_user_free(file_handles);
  if (event_handles != nullptr) MIDL_user_free(event_handles);
  if (mutex_handles != nullptr) MIDL_user_free(mutex_handles);
  if (process_handles != nullptr) MIDL_user_free(process_handles);
  if (registry_handles != nullptr) MIDL_user_free(registry_handles);
  if (pipe_handles != nullptr) MIDL_user_free(pipe_handles);
  if (section_handles != nullptr) MIDL_user_free(section_handles);

  return hr;
}

// Binds to the authenticated cache-service endpoint and invokes |message_id|.
// Returns true if the round-trip succeeded; the manager-routine HRESULT is
// written to *result_hr.
bool Call(int32_t message_id, HRESULT *result_hr) {
  RPC_WSTR string_binding = nullptr;
  RPC_STATUS status = ::RpcStringBindingComposeW(
      /*ObjUuid=*/nullptr, AsRpcWStr(kProtocolSequence),
      /*NetworkAddr=*/nullptr, AsRpcWStr(kCacheServiceEndpoint),
      /*Options=*/nullptr, &string_binding);
  if (status != RPC_S_OK) {
    LOG(ERROR) << "RpcStringBindingComposeW failed: " << status;
    return false;
  }

  RPC_BINDING_HANDLE binding = nullptr;
  status = ::RpcBindingFromStringBindingW(string_binding, &binding);
  ::RpcStringFreeW(&string_binding);
  if (status != RPC_S_OK) {
    LOG(ERROR) << "RpcBindingFromStringBindingW failed: " << status;
    return false;
  }

  // Authenticate ourselves to the server with NTLM (WinNT) - the only
  // authentication service local ncalrpc supports; Negotiate/Kerberos are
  // rejected on an ncalrpc binding with RPC_S_UNKNOWN_AUTHN_SERVICE (1747). We
  // request PKT_PRIVACY and grant impersonation so the server can act on our
  // behalf (the launch helper). We do NOT request mutual auth and pass a NULL
  // ServerPrincName: local NTLM has no usable mutual-auth principal for the
  // service's Virtual Service Account, so server identity is instead assured by
  // endpoint ownership (see cache_service_rpc_server.cc / RPC_DESIGN.md).
  RPC_SECURITY_QOS qos = {};
  qos.Version = RPC_C_SECURITY_QOS_VERSION;
  qos.Capabilities = RPC_C_QOS_CAPABILITIES_DEFAULT;
  qos.IdentityTracking = RPC_C_QOS_IDENTITY_STATIC;
  qos.ImpersonationType = RPC_C_IMP_LEVEL_IMPERSONATE;
  status = ::RpcBindingSetAuthInfoExW(
      binding, /*ServerPrincName=*/nullptr, RPC_C_AUTHN_LEVEL_PKT_PRIVACY,
      RPC_C_AUTHN_WINNT, /*AuthIdentity=*/nullptr, RPC_C_AUTHZ_NONE, &qos);
  if (status != RPC_S_OK) {
    LOG(ERROR) << "RpcBindingSetAuthInfoExW failed: " << status;
    ::RpcBindingFree(&binding);
    return false;
  }

  status = ::RpcBindingSetOption(binding, RPC_C_OPT_CALL_TIMEOUT,
                                 kDefaultTimeoutMs);
  if (status != RPC_S_OK) {
    // Non-fatal: proceed with the default timeout.
    LOG(WARNING) << "RpcBindingSetOption(CALL_TIMEOUT) failed: " << status;
  }

  DWORD seh_code = 0;
  const HRESULT hr = InvokeProcessIpcRequest(binding, message_id, &seh_code);
  ::RpcBindingFree(&binding);

  if (seh_code != 0) {
    LOG(ERROR) << "ProcessIpcRequest raised: " << seh_code;
    return false;
  }
  if (result_hr != nullptr) {
    *result_hr = hr;
  }
  return true;
}

bool CallLaunch(int32_t message_id, const char *what) {
  HRESULT hr = E_FAIL;
  if (!Call(message_id, &hr)) {
    return false;
  }
  if (FAILED(hr)) {
    LOG(ERROR) << what << " failed: hr=" << hr;
    return false;
  }
  return true;
}

}  // namespace

bool CacheServiceRpcClient::LaunchServer() {
  return CallLaunch(kMsgLaunchServer, "LaunchServer");
}

bool CacheServiceRpcClient::LaunchRenderer() {
  return CallLaunch(kMsgLaunchRenderer, "LaunchRenderer");
}

}  // namespace mozc::ipc::win

#endif  // _WIN32
