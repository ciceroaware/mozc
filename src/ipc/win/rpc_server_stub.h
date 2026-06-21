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

// RpcServerStub.hpp - MIDL-free server interface declaration (Path B, server).
//
// The server application registers IWinIpcInitiator via RpcServerRegisterIf3 with
// the interface spec below, exactly as it would with the midl.exe-generated
// IWinIpcInitiator_v1_0_s_ifspec. The spec is defined by the hand-authored server
// stub (RpcNdr64Server.cpp on x64, RpcDceServer.cpp on x86).
//
// The server app implements the manager routine ProcessIpcRequest (declared via
// RpcClientStub.hpp - the entry-point and manager share one signature).

#pragma once

#include <windows.h>
#include <rpc.h>

#include "ipc/win/rpc_client_stub.h"  // ProcessIpcRequest prototype (manager == client entry)

// Interface spec to pass to RpcServerRegisterIf3 (hand-authored, no MIDL).
extern "C" RPC_IF_HANDLE MozcCacheServiceRpc_v1_0_s_ifspec;

// RPC allocator hooks the server app must define (MIDL's _h.h declares these in
// Path A; declare them here for the MIDL-free build).
extern "C" void* __RPC_USER MIDL_user_allocate(size_t);
extern "C" void  __RPC_USER MIDL_user_free(void*);
