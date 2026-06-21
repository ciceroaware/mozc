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

// rpc_dual_server.cc - MIDL-free *dual-syntax* server stub (Path B, server).
//
// The cache-service server stub. The server is always a 64-bit, dual-syntax
// server: it advertises BOTH transfer syntaxes from a single interface
// registration, so the RPC runtime can negotiate per-bind and one server binary
// serves clients of either bitness:
//   - NDR64  {71710533-...} v1.0  <- x64 clients          -> NdrServerCallAll
//   - DCE NDR{8A885D04-...} v2.0  <- x86 (32-bit) clients  -> NdrServerCall2
// Both syntaxes dispatch to the same manager routine, ProcessIpcRequest. This is
// what `midl /env x64 /protocol all` emits into _s.c; the structures below are
// hand-authored from that golden output.
//
// 64-bit ONLY, by design: a 32-bit process cannot register/advertise the NDR64
// transfer syntax, so a 32-bit "dual-syntax" server is impossible. Building the
// server as x86 is therefore a hard error (below) rather than a silent
// single-syntax fallback. (A 32-bit *client* is fully supported - it negotiates
// DCE against this x64 server; see rpc_dce_stub.cc.)
//
// Wire data is shared with the client stubs:
//   - NDR64 fragment tree / proc table: winipc_ndr64 (rpc_ndr64_format.h)
//   - x64-env DCE proc/type format strings: winipc_dce_x64 (rpc_dce_format_x64.h)

#if !defined(_M_AMD64)
#error "Mozc cache-service RPC server is x64-only: the dual-syntax server requires NDR64, which a 32-bit process cannot register/advertise. Build the server as x64."
#endif

#if defined(_M_AMD64)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <rpc.h>
#include <rpcndr.h>
#include <ndr64types.h>

#include "ipc/win/rpc_ndr64_format.h"   // winipc_ndr64::g_Ndr64ProcTable / g_Ndr64TransferSyntax
#include "ipc/win/rpc_dce_format_x64.h"  // winipc_dce_x64::g_ProcFormatString / g_TypeFormatString
#include "ipc/win/rpc_server_stub.h"     // ProcessIpcRequest prototype + ifspec

// Allocators + the manager routine are defined in the server application.
// ProcessIpcRequest's prototype comes from rpc_server_stub.h -> rpc_client_stub.h.
extern "C" void* __RPC_USER MIDL_user_allocate(size_t);
extern "C" void  __RPC_USER MIDL_user_free(void*);

namespace {

extern const MIDL_SERVER_INFO g_ServerInfo;
extern const MIDL_STUB_DESC   g_StubDesc;

// ---- per-syntax dispatch tables (route to the matching NDR interpreter) ----
// DCE NDR -> NdrServerCall2 ; this is also the interface's "default" dispatch.
const RPC_DISPATCH_FUNCTION g_DceDispatchFns[] = { NdrServerCall2, 0 };
const RPC_DISPATCH_TABLE g_DceDispatchTable = { 1, (RPC_DISPATCH_FUNCTION*)g_DceDispatchFns };

// NDR64 -> NdrServerCallAll (MIDL's /protocol all multi-syntax NDR64 entry; NOT
// NdrServerCallNdr64, which a single-syntax /protocol ndr64 server would use).
const RPC_DISPATCH_FUNCTION g_Ndr64DispatchFns[] = { NdrServerCallAll, 0 };
const RPC_DISPATCH_TABLE g_Ndr64DispatchTable = { 1, (RPC_DISPATCH_FUNCTION*)g_Ndr64DispatchFns };

// The interface's registered/default transfer syntax is DCE NDR; NDR64 is offered
// through the syntax-info array below (driven by Flags bit 0x02000000).
const RPC_SERVER_INTERFACE g_ServerInterface = {
    sizeof(RPC_SERVER_INTERFACE),
    { { 0xd7a9c3e1, 0x5b2f, 0x4a8d, { 0x9c,0x6e,0x1f,0x3b,0x7d,0x2a,0x4c,0x8e } }, { 1, 0 } },
    { { 0x8A885D04, 0x1CEB, 0x11C9, { 0x9F,0xE8,0x08,0x00,0x2B,0x10,0x48,0x60 } }, { 2, 0 } }, // DCE default
    (RPC_DISPATCH_TABLE*)&g_DceDispatchTable,
    0, 0, 0,
    &g_ServerInfo,
    0x06000000   // single-syntax server uses 0x04000000; the extra 0x02000000 bit
                 // signals the runtime to consult the multi-syntax info (NDR64).
};

// DCE server maps procedure number -> proc-string offset (single method at 0).
const unsigned short g_DceFormatStringOffsetTable[] = { 0 };

const MIDL_STUB_DESC g_StubDesc = {
    (void*)&g_ServerInterface,
    MIDL_user_allocate,
    MIDL_user_free,
    {},                                            // no implicit/auto handle on the server
    0, 0, 0, 0,
    winipc_dce_x64::g_TypeFormatString.Format,     // pFormatTypes (DCE type string)
    1,                                             // -error bounds_check flag
    0xa0000,                                       // NDR library version
    0,
    0x8010274,                                     // MIDL version 8.1.628
    0, 0, 0,
    0x2000001,                                     // MIDL flags
    0,
    (void*)&g_ServerInfo,
    0
};

// ---- the two syntaxes the server offers, in MIDL's emitted order ----
// [0] DCE NDR: proc + type format strings + offset table.
// [1] NDR64  : proc table only (the format tree carries the type info).
const MIDL_SYNTAX_INFO g_SyntaxInfo[2] = {
    {
        { { 0x8A885D04, 0x1CEB, 0x11C9, { 0x9F,0xE8,0x08,0x00,0x2B,0x10,0x48,0x60 } }, { 2, 0 } },
        (RPC_DISPATCH_TABLE*)&g_DceDispatchTable,
        winipc_dce_x64::g_ProcFormatString.Format,
        g_DceFormatStringOffsetTable,
        winipc_dce_x64::g_TypeFormatString.Format,
        0, 0, 0
    },
    {
        { { 0x71710533, 0xbeba, 0x4937, { 0x83,0x19,0xb5,0xdb,0xef,0x9c,0xcc,0x36 } }, { 1, 0 } },
        (RPC_DISPATCH_TABLE*)&g_Ndr64DispatchTable,
        0,
        (const unsigned short*)winipc_ndr64::g_Ndr64ProcTable,
        0,
        0, 0, 0
    }
};

// The manager routine table: one entry for the single method.
const SERVER_ROUTINE g_ServerRoutines[1] = { (SERVER_ROUTINE)ProcessIpcRequest };

const MIDL_SERVER_INFO g_ServerInfo = {
    &g_StubDesc,
    g_ServerRoutines,
    winipc_dce_x64::g_ProcFormatString.Format,     // top-level ProcString (DCE)
    (const unsigned short*)g_DceFormatStringOffsetTable,
    0,
    (RPC_SYNTAX_IDENTIFIER*)&winipc_ndr64::g_Ndr64TransferSyntax,  // pTransferSyntax
    2,                                             // nCount: two syntaxes
    (MIDL_SYNTAX_INFO*)g_SyntaxInfo
};

}  // anonymous namespace

extern "C" RPC_IF_HANDLE MozcCacheServiceRpc_v1_0_s_ifspec = (RPC_IF_HANDLE)&g_ServerInterface;

#endif  // defined(_M_AMD64)
