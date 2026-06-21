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

// RpcClientStub.hpp - MIDL-free client stub declaration (Path B).
//
// This declares the single RPC client entry point, ProcessIpcRequest, with the
// exact signature MIDL would have generated for the IWinIpcInitiator interface.
// The implementation drives the NDR interpreter directly with hand-authored
// format metadata, so the library does NOT depend on midl.exe-generated stubs
// (WinIpcInitiator_c.c / WinIpcInitiator_h.h). The signature is identical on both
// architectures; the wire format and call differ:
//   - x64: RpcNdr64Stub.cpp -> NdrClientCall3 with NDR64 metadata
//   - x86: RpcDceStub.cpp    -> NdrClientCall4 with DCE NDR format strings

#pragma once

#include <windows.h>
#include <rpc.h>

extern "C" HRESULT ProcessIpcRequest(
    /* [in] */ handle_t IDL_handle,
    /* [in] */ long messageId,
    /* [in] */ unsigned long dataSize,
    /* [size_is][unique][in] */ unsigned char* data,
    /* [in] */ unsigned long fileHandleCount,
    /* [system_handle][size_is][unique][in] */ HANDLE* fileHandles,
    /* [in] */ unsigned long eventHandleCount,
    /* [system_handle][size_is][unique][in] */ HANDLE* eventHandles,
    /* [in] */ unsigned long mutexHandleCount,
    /* [system_handle][size_is][unique][in] */ HANDLE* mutexHandles,
    /* [in] */ unsigned long processHandleCount,
    /* [system_handle][size_is][unique][in] */ HANDLE* processHandles,
    /* [in] */ unsigned long registryHandleCount,
    /* [system_handle][size_is][unique][in] */ HKEY* registryHandles,
    /* [in] */ unsigned long pipeHandleCount,
    /* [system_handle][size_is][unique][in] */ HANDLE* pipeHandles,
    /* [in] */ unsigned long sectionHandleCount,
    /* [system_handle][size_is][unique][in] */ HANDLE* sectionHandles,
    /* [out] */ unsigned long* responseDataSize,
    /* [size_is][size_is][out] */ unsigned char** responseData,
    /* [out] */ unsigned long* responseFileHandleCount,
    /* [system_handle][size_is][size_is][out] */ HANDLE** responseFileHandles,
    /* [out] */ unsigned long* responseEventHandleCount,
    /* [system_handle][size_is][size_is][out] */ HANDLE** responseEventHandles,
    /* [out] */ unsigned long* responseMutexHandleCount,
    /* [system_handle][size_is][size_is][out] */ HANDLE** responseMutexHandles,
    /* [out] */ unsigned long* responseProcessHandleCount,
    /* [system_handle][size_is][size_is][out] */ HANDLE** responseProcessHandles,
    /* [out] */ unsigned long* responseRegistryHandleCount,
    /* [system_handle][size_is][size_is][out] */ HKEY** responseRegistryHandles,
    /* [out] */ unsigned long* responsePipeHandleCount,
    /* [system_handle][size_is][size_is][out] */ HANDLE** responsePipeHandles,
    /* [out] */ unsigned long* responseSectionHandleCount,
    /* [system_handle][size_is][size_is][out] */ HANDLE** responseSectionHandles);
