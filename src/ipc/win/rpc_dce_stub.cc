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

// RpcDceStub.cpp - MIDL-free DCE NDR client stub (Path B, x86).
//
// The x86 counterpart of RpcNdr64Stub.cpp. x86 has no NDR64 / NdrClientCall3;
// it uses traditional DCE NDR with flat format-string byte arrays and
// NdrClientCall4. The shared format strings live in RpcDceFormat.hpp (also used
// by the MIDL-free server); this file adds only the client interface, stub
// descriptor, and the ProcessIpcRequest entry point.
//
// Compiles to nothing off x86, so it can sit in the build for both architectures
// alongside RpcNdr64Stub.cpp.

#if defined(_M_IX86)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <rpc.h>
#include <rpcndr.h>

#include "ipc/win/rpc_client_stub.h"
#include "ipc/win/rpc_dce_format.h"

// Allocators defined in ClientImpl.cpp.
extern "C" void* __RPC_USER MIDL_user_allocate(size_t);
extern "C" void  __RPC_USER MIDL_user_free(void*);

namespace {
using namespace winipc_dce;

const RPC_CLIENT_INTERFACE g_ClientInterface = {
    sizeof(RPC_CLIENT_INTERFACE),
    { { 0xd7a9c3e1, 0x5b2f, 0x4a8d, { 0x9c,0x6e,0x1f,0x3b,0x7d,0x2a,0x4c,0x8e } }, { 1, 0 } },
    { { 0x8A885D04, 0x1CEB, 0x11C9, { 0x9F,0xE8,0x08,0x00,0x2B,0x10,0x48,0x60 } }, { 2, 0 } }, // DCE
    0, 0, 0, 0, 0, 0x00000000
};

RPC_BINDING_HANDLE g_AutoBindHandle;

const MIDL_STUB_DESC g_StubDesc = {
    (void*)&g_ClientInterface,
    MIDL_user_allocate,
    MIDL_user_free,
    { &g_AutoBindHandle },
    0, 0, 0, 0,
    g_TypeFormatString.Format,  // pFormatTypes
    1,                          // -error bounds_check flag
    0xa0000,                    // NDR library version
    0,
    0x8010274,                  // MIDL version 8.1.628
    0, 0, 0,
    0x1,                        // MIDL flags (DCE)
    0,
    0,                          // no proxy/server info (DCE)
    0
};

} // anonymous namespace

// ---- the client entry point: same shape MIDL would generate ----
extern "C" HRESULT ProcessIpcRequest(
    handle_t IDL_handle,
    long messageId,
    unsigned long dataSize,
    unsigned char* data,
    unsigned long fileHandleCount,     HANDLE* fileHandles,
    unsigned long eventHandleCount,    HANDLE* eventHandles,
    unsigned long mutexHandleCount,    HANDLE* mutexHandles,
    unsigned long processHandleCount,  HANDLE* processHandles,
    unsigned long registryHandleCount, HKEY*   registryHandles,
    unsigned long pipeHandleCount,     HANDLE* pipeHandles,
    unsigned long sectionHandleCount,  HANDLE* sectionHandles,
    unsigned long* responseDataSize,            unsigned char** responseData,
    unsigned long* responseFileHandleCount,     HANDLE** responseFileHandles,
    unsigned long* responseEventHandleCount,    HANDLE** responseEventHandles,
    unsigned long* responseMutexHandleCount,    HANDLE** responseMutexHandles,
    unsigned long* responseProcessHandleCount,  HANDLE** responseProcessHandles,
    unsigned long* responseRegistryHandleCount, HKEY**   responseRegistryHandles,
    unsigned long* responsePipeHandleCount,     HANDLE** responsePipeHandles,
    unsigned long* responseSectionHandleCount,  HANDLE** responseSectionHandles)
{
    CLIENT_CALL_RETURN ret = NdrClientCall4(
        (PMIDL_STUB_DESC)&g_StubDesc,
        (PFORMAT_STRING)&g_ProcFormatString.Format[0],
        IDL_handle,
        messageId, dataSize, data,
        fileHandleCount, fileHandles,
        eventHandleCount, eventHandles,
        mutexHandleCount, mutexHandles,
        processHandleCount, processHandles,
        registryHandleCount, registryHandles,
        pipeHandleCount, pipeHandles,
        sectionHandleCount, sectionHandles,
        responseDataSize, responseData,
        responseFileHandleCount, responseFileHandles,
        responseEventHandleCount, responseEventHandles,
        responseMutexHandleCount, responseMutexHandles,
        responseProcessHandleCount, responseProcessHandles,
        responseRegistryHandleCount, responseRegistryHandles,
        responsePipeHandleCount, responsePipeHandles,
        responseSectionHandleCount, responseSectionHandles);
    return (HRESULT)ret.Simple;
}

#endif // defined(_M_IX86)
