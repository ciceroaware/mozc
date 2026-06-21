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

// Manual Phase C probe / diagnostic for the cache-service MSRPC interface.
//
//   cache_service_rpc_probe              # launch mozc_server (default)
//   cache_service_rpc_probe renderer     # launch mozc_renderer
//   cache_service_rpc_probe diag         # diagnose endpoint + auth services
//
// "diag" does not make the RPC call; it only checks whether the endpoint is
// listening and which authentication services RpcBindingSetAuthInfoExW accepts,
// so we can pick a working local-ncalrpc mutual-auth configuration.

// clang-format off
#include <windows.h>
#include <rpc.h>
#define SECURITY_WIN32
#include <security.h>
#include <sspi.h>
// clang-format on

#include <cstdio>
#include <cstring>
#include <string>

#include "base/const.h"
#include "ipc/win/cache_service_rpc_client.h"
#include "ipc/win/rpc_client_stub.h"
#include "ipc/win/rpc_interface.h"

namespace {

RPC_WSTR AsRpcWStr(const wchar_t *str) {
  return reinterpret_cast<RPC_WSTR>(const_cast<wchar_t *>(str));
}

// Issues ProcessIpcRequest(kMsgLaunchServer) on |binding| under SEH. Returns the
// HRESULT; *seh_code holds the RPC_STATUS if an exception was raised.
HRESULT CallLaunchServer(handle_t binding, DWORD *seh_code) {
  *seh_code = 0;
  unsigned long rds = 0, fc = 0, ec = 0, mc = 0, pc = 0, rc = 0, pic = 0,
                sc = 0;
  unsigned char *rd = nullptr;
  HANDLE *fh = nullptr, *eh = nullptr, *mh = nullptr, *ph = nullptr,
         *pih = nullptr, *sh = nullptr;
  HKEY *rh = nullptr;
  HRESULT hr = E_FAIL;
  __try {
    hr = ProcessIpcRequest(
        binding, mozc::ipc::win::kMsgLaunchServer, 0, nullptr, 0, nullptr, 0,
        nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, nullptr,
        &rds, &rd, &fc, &fh, &ec, &eh, &mc, &mh, &pc, &ph, &rc, &rh, &pic, &pih,
        &sc, &sh);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    *seh_code = ::GetExceptionCode();
    return E_FAIL;
  }
  return hr;
}

RPC_BINDING_HANDLE MakeBinding() {
  using mozc::ipc::win::kCacheServiceEndpoint;
  using mozc::ipc::win::kProtocolSequence;
  RPC_WSTR string_binding = nullptr;
  RPC_STATUS status = ::RpcStringBindingComposeW(
      nullptr, AsRpcWStr(kProtocolSequence), nullptr,
      AsRpcWStr(kCacheServiceEndpoint), nullptr, &string_binding);
  if (status != RPC_S_OK) {
    std::printf("  RpcStringBindingComposeW failed: %ld\n", status);
    return nullptr;
  }
  RPC_BINDING_HANDLE binding = nullptr;
  status = ::RpcBindingFromStringBindingW(string_binding, &binding);
  ::RpcStringFreeW(&string_binding);
  if (status != RPC_S_OK) {
    std::printf("  RpcBindingFromStringBindingW failed: %ld\n", status);
    return nullptr;
  }
  return binding;
}

void TrySetAuth(const char *label, unsigned long authn_svc,
                unsigned long authn_level, bool mutual_auth) {
  RPC_BINDING_HANDLE binding = MakeBinding();
  if (binding == nullptr) {
    return;
  }
  std::wstring principal =
      std::wstring(L"NT SERVICE\\") + mozc::kMozcCacheServiceName;
  RPC_SECURITY_QOS qos = {};
  qos.Version = RPC_C_SECURITY_QOS_VERSION;
  qos.Capabilities =
      mutual_auth ? RPC_C_QOS_CAPABILITIES_MUTUAL_AUTH : RPC_C_QOS_CAPABILITIES_DEFAULT;
  qos.IdentityTracking = RPC_C_QOS_IDENTITY_STATIC;
  qos.ImpersonationType = RPC_C_IMP_LEVEL_IMPERSONATE;
  RPC_STATUS status = ::RpcBindingSetAuthInfoExW(
      binding, AsRpcWStr(principal.c_str()), authn_level, authn_svc, nullptr,
      RPC_C_AUTHZ_NONE, &qos);
  std::printf("  [%-26s] svc=%lu level=%lu mutual=%d -> status=%ld\n", label,
              authn_svc, authn_level, mutual_auth ? 1 : 0, status);
  ::RpcBindingFree(&binding);
}

// Sets the given auth config then actually calls kMsgLaunchServer.
void TryFullCall(const char *label, unsigned long authn_svc,
                 unsigned long authn_level, bool mutual_auth) {
  RPC_BINDING_HANDLE binding = MakeBinding();
  if (binding == nullptr) {
    return;
  }
  // Match the real client: NULL ServerPrincName (no mutual auth over local NTLM).
  RPC_SECURITY_QOS qos = {};
  qos.Version = RPC_C_SECURITY_QOS_VERSION;
  qos.Capabilities = mutual_auth ? RPC_C_QOS_CAPABILITIES_MUTUAL_AUTH
                                 : RPC_C_QOS_CAPABILITIES_DEFAULT;
  qos.IdentityTracking = RPC_C_QOS_IDENTITY_STATIC;
  qos.ImpersonationType = RPC_C_IMP_LEVEL_IMPERSONATE;
  RPC_STATUS status = ::RpcBindingSetAuthInfoExW(
      binding, /*ServerPrincName=*/nullptr, authn_level, authn_svc, nullptr,
      RPC_C_AUTHZ_NONE, &qos);
  if (status != RPC_S_OK) {
    std::printf("  [%-26s] SetAuthInfo failed: %ld\n", label, status);
    ::RpcBindingFree(&binding);
    return;
  }
  DWORD seh = 0;
  HRESULT hr = CallLaunchServer(binding, &seh);
  if (seh != 0) {
    std::printf("  [%-26s] CALL raised RPC_STATUS=%lu\n", label, seh);
  } else {
    std::printf("  [%-26s] CALL ok, hr=0x%08lX\n", label,
                static_cast<unsigned long>(hr));
  }
  ::RpcBindingFree(&binding);
}

void ListSecurityPackages() {
  std::printf("Installed SSP security packages (EnumerateSecurityPackagesW):\n");
  unsigned long count = 0;
  PSecPkgInfoW packages = nullptr;
  SECURITY_STATUS s = ::EnumerateSecurityPackagesW(&count, &packages);
  if (s != SEC_E_OK || packages == nullptr) {
    std::printf("  EnumerateSecurityPackagesW failed: 0x%08lX\n",
                static_cast<unsigned long>(s));
    return;
  }
  for (unsigned long i = 0; i < count; ++i) {
    std::printf("  - %-22ls (max token=%lu)\n", packages[i].Name,
                packages[i].cbMaxToken);
  }
  ::FreeContextBuffer(packages);
  std::printf("\n");
}

int Diagnose() {
  std::printf("Endpoint: ncalrpc:[%ls]\n",
              mozc::ipc::win::kCacheServiceEndpoint);
  std::printf("Principal: NT SERVICE\\%ls\n\n", mozc::kMozcCacheServiceName);

  ListSecurityPackages();

  std::printf("RpcMgmtIsServerListening (endpoint alive?):\n");
  RPC_BINDING_HANDLE binding = MakeBinding();
  if (binding != nullptr) {
    RPC_STATUS status = ::RpcMgmtIsServerListening(binding);
    std::printf("  -> status=%ld (%s)\n\n", status,
                status == RPC_S_OK ? "LISTENING"
                                   : "not listening / access checked");
    ::RpcBindingFree(&binding);
  }

  std::printf("RpcBindingSetAuthInfoExW acceptance by authn service:\n");
  // (label, authn service, authn level, mutual)
  TrySetAuth("WINNT/PKT_PRIVACY", RPC_C_AUTHN_WINNT,
             RPC_C_AUTHN_LEVEL_PKT_PRIVACY, false);
  TrySetAuth("WINNT/PKT_PRIVACY+mutual", RPC_C_AUTHN_WINNT,
             RPC_C_AUTHN_LEVEL_PKT_PRIVACY, true);
  TrySetAuth("NEGOTIATE/PKT_PRIVACY", RPC_C_AUTHN_GSS_NEGOTIATE,
             RPC_C_AUTHN_LEVEL_PKT_PRIVACY, false);
  TrySetAuth("NEGOTIATE/PKT_PRIVACY+mut", RPC_C_AUTHN_GSS_NEGOTIATE,
             RPC_C_AUTHN_LEVEL_PKT_PRIVACY, true);
  TrySetAuth("KERBEROS/PKT_PRIVACY+mut", RPC_C_AUTHN_GSS_KERBEROS,
             RPC_C_AUTHN_LEVEL_PKT_PRIVACY, true);
  TrySetAuth("DEFAULT/PKT_PRIVACY+mut", RPC_C_AUTHN_DEFAULT,
             RPC_C_AUTHN_LEVEL_PKT_PRIVACY, true);
  TrySetAuth("PKT_INTEGRITY WINNT", RPC_C_AUTHN_WINNT,
             RPC_C_AUTHN_LEVEL_PKT_INTEGRITY, false);

  std::printf("\nFull ProcessIpcRequest(kMsgLaunchServer) attempts:\n");
  TryFullCall("WINNT/PKT_PRIVACY+mut", RPC_C_AUTHN_WINNT,
              RPC_C_AUTHN_LEVEL_PKT_PRIVACY, true);
  TryFullCall("WINNT/PKT_PRIVACY", RPC_C_AUTHN_WINNT,
              RPC_C_AUTHN_LEVEL_PKT_PRIVACY, false);
  TryFullCall("DEFAULT/PKT_PRIVACY+mut", RPC_C_AUTHN_DEFAULT,
              RPC_C_AUTHN_LEVEL_PKT_PRIVACY, true);
  TryFullCall("DEFAULT/PKT_PRIVACY", RPC_C_AUTHN_DEFAULT,
              RPC_C_AUTHN_LEVEL_PKT_PRIVACY, false);
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc > 1 && std::strcmp(argv[1], "diag") == 0) {
    return Diagnose();
  }

  const bool renderer = (argc > 1 && std::strcmp(argv[1], "renderer") == 0);
  const char *what = renderer ? "LaunchRenderer" : "LaunchServer";

  std::printf("Calling %s on the cache service...\n", what);
  const bool ok = renderer
                      ? mozc::ipc::win::CacheServiceRpcClient::LaunchRenderer()
                      : mozc::ipc::win::CacheServiceRpcClient::LaunchServer();
  std::printf("%s -> %s\n", what, ok ? "OK" : "FAILED");
  return ok ? 0 : 1;
}
