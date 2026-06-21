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

// RpcNdr64Stub.cpp - MIDL-free NDR64 client stub (Path B).
//
// Drives NdrClientCall3 with hand-authored NDR64 format metadata instead of the
// midl.exe-generated WinIpcInitiator_c.c. The shared format tree / proc descriptor
// live in RpcNdr64Format.hpp (also used by the MIDL-free server); this file adds
// only the client-side proxy info and the ProcessIpcRequest entry point.
//
// x64 / NDR64 only (NdrClientCall3 is unavailable on x86; the x86 DCE NDR path
// lives in RpcDceStub.cpp). This whole file compiles to nothing off x64, so both
// stub .cpp files can sit in the build unconditionally.

#if defined(_M_AMD64)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <rpc.h>
#include <rpcndr.h>
#include <ndr64types.h>

#include "ipc/win/rpc_client_stub.h"
#include "ipc/win/rpc_ndr64_format.h"

// Allocators defined in ClientImpl.cpp (same link unit set).
extern "C" void* __RPC_USER MIDL_user_allocate(size_t);
extern "C" void  __RPC_USER MIDL_user_free(void*);

namespace {
using namespace winipc_ndr64;

extern const MIDL_STUBLESS_PROXY_INFO g_ProxyInfo;

const RPC_CLIENT_INTERFACE g_ClientInterface = {
    sizeof(RPC_CLIENT_INTERFACE),
    { { 0xd7a9c3e1, 0x5b2f, 0x4a8d, { 0x9c,0x6e,0x1f,0x3b,0x7d,0x2a,0x4c,0x8e } }, { 1, 0 } },
    { { 0x71710533, 0xbeba, 0x4937, { 0x83,0x19,0xb5,0xdb,0xef,0x9c,0xcc,0x36 } }, { 1, 0 } },
    0, 0, 0, 0,
    &g_ProxyInfo,
    0x00000000
};

RPC_BINDING_HANDLE g_AutoBindHandle;

const MIDL_STUB_DESC g_StubDesc = {
    (void*)&g_ClientInterface,
    MIDL_user_allocate,
    MIDL_user_free,
    { &g_AutoBindHandle },
    0, 0, 0, 0, 0,
    1,            // -error bounds_check flag
    0xa0000,      // NDR library version
    0,
    0x8010274,    // MIDL version 8.1.628
    0, 0,
    0,            // notify routine table
    0x2000001,    // MIDL flags
    0,            // cs routines
    (void*)&g_ProxyInfo,
    0
};

const MIDL_SYNTAX_INFO g_SyntaxInfo[1] = {
    {
        { { 0x71710533, 0xbeba, 0x4937, { 0x83,0x19,0xb5,0xdb,0xef,0x9c,0xcc,0x36 } }, { 1, 0 } },
        0,
        0,
        (const unsigned short*)g_Ndr64ProcTable,
        0, 0, 0, 0
    }
};

const MIDL_STUBLESS_PROXY_INFO g_ProxyInfo = {
    &g_StubDesc,
    0,
    (const unsigned short*)g_Ndr64ProcTable,
    (RPC_SYNTAX_IDENTIFIER*)&g_Ndr64TransferSyntax,
    1,
    (MIDL_SYNTAX_INFO*)g_SyntaxInfo
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
    CLIENT_CALL_RETURN ret = NdrClientCall3(
        (PMIDL_STUBLESS_PROXY_INFO)&g_ProxyInfo,
        0,    // procedure number (single method)
        0,    // return value slot (HRESULT comes back via ret.Simple)
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

#endif // defined(_M_AMD64)
