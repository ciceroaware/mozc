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

// Shared identity and constants for the Mozc cache-service MSRPC interface.
//
// This interface is the "opaque bytes in / opaque bytes out (+ optional typed
// handle arrays)" transport vendored from the win_ipc_initiator project, hosted
// by mozc_cache_service.exe over ncalrpc (ALPC). It deliberately keeps a single
// method, ProcessIpcRequest; concrete operations are multiplexed by |messageId|
// and the protobuf payload in |data| / |responseData|.
//
// The NDR64 (x64) and DCE (x86) format metadata in rpc_ndr64_format.h /
// rpc_dce_format.h are MIDL golden output and must not be edited by hand. The
// interface UUID below is also embedded (in struct form) in the hand-authored
// RPC_SERVER_INTERFACE / RPC_CLIENT_INTERFACE in the *_server.cc / *_stub.cc
// files; keep all copies in sync.

#ifndef MOZC_IPC_WIN_RPC_INTERFACE_H_
#define MOZC_IPC_WIN_RPC_INTERFACE_H_

#ifdef _WIN32

// clang-format off
#include <windows.h>
#include <rpc.h>
#include <guiddef.h>
// clang-format on

#include <cstddef>
#include <cstdint>

namespace mozc::ipc::win {

// Interface UUID for the Mozc cache-service RPC interface.
// {D7A9C3E1-5B2F-4A8D-9C6E-1F3B7D2A4C8E}
inline constexpr GUID kCacheServiceRpcInterfaceId = {
    0xd7a9c3e1,
    0x5b2f,
    0x4a8d,
    {0x9c, 0x6e, 0x1f, 0x3b, 0x7d, 0x2a, 0x4c, 0x8e}};

// Protocol sequence for local RPC (ALPC).
inline constexpr wchar_t kProtocolSequence[] = L"ncalrpc";

// Predictable ncalrpc endpoint name. Squatting is mitigated by mutual
// authentication against the service principal (see RPC_DESIGN.md), so the name
// does not need to be unpredictable.
// TODO(yukawa): derive the branded name for GoogleJapaneseInput builds.
inline constexpr wchar_t kCacheServiceEndpoint[] = L"mozc.cache_service";

// Operation selector carried in ProcessIpcRequest's |messageId| parameter.
enum CacheServiceMessageId : std::int32_t {
  kMsgInvalid = 0,
  // Launch mozc_server.exe in the calling user's session on behalf of a
  // (possibly sandboxed) client.
  kMsgLaunchServer = 1,
  // Launch mozc_renderer.exe in the calling user's session.
  kMsgLaunchRenderer = 2,
};

// Default per-call timeout (milliseconds).
inline constexpr std::uint32_t kDefaultTimeoutMs = 5000;

// System-handle wire-type bytes for the NDR64 FC64_SYSTEM_HANDLE (0x3c) format
// code, as emitted by MIDL (see win_ipc_initiator's
// SYSTEM_HANDLE_ARRAY_FINDINGS.md). These are NOT the sh_* documentation
// ordinals; using those feeds the NDR interpreter a malformed format string and
// can fault with 0xC0000005. Unused by the launch helper (no handle transfer),
// kept for the future registry-key key-value-store feature.
inline constexpr std::uint8_t kHandleTypeFile = 0x00;
inline constexpr std::uint8_t kHandleTypeEvent = 0x02;
inline constexpr std::uint8_t kHandleTypeMutex = 0x03;
inline constexpr std::uint8_t kHandleTypeProcess = 0x04;
inline constexpr std::uint8_t kHandleTypeRegKey = 0x07;
inline constexpr std::uint8_t kHandleTypePipe = 0x0C;
inline constexpr std::uint8_t kHandleTypeSection = 0x06;

}  // namespace mozc::ipc::win

#endif  // _WIN32
#endif  // MOZC_IPC_WIN_RPC_INTERFACE_H_
