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

// rpc_dce_format_x64.h - x64-environment DCE NDR format metadata (dual-syntax
// server).
//
// The traditional DCE NDR proc + type format strings for
// ProcessIpcRequest, transcribed from MIDL's *x64* golden output
// (midl /env x64 /protocol all -> _s.c). These differ from the x86 strings in
// rpc_dce_format.h: x64 uses 8-byte stack slots, so the procedure stack size
// (0x118 vs 0x8c) and every parameter / correlation stack offset is doubled.
// The embedded type back-offsets (0xffe8, 0xfef2..0xfefe) are identical because
// the type-string *layout* is the same shape on both architectures.
//
// This header exists so the 64-bit cache-service server can advertise the DCE
// transfer syntax to 32-bit (DCE-only) clients alongside NDR64 - see
// rpc_dual_server.cc. It is x64 only; the x86 DCE client keeps using
// rpc_dce_format.h.
//
// All objects are namespace-scope `const` (internal linkage); including this in
// more than one translation unit is safe.

#pragma once

#if defined(_M_AMD64)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <rpc.h>
#include <rpcndr.h>

namespace winipc_dce_x64 {

#define WINIPC_DCE_X64_PROC_FORMAT_STRING_SIZE 235
#define WINIPC_DCE_X64_TYPE_FORMAT_STRING_SIZE 477

#pragma pack(push, 8)
struct ProcFormatString { short Pad; unsigned char Format[WINIPC_DCE_X64_PROC_FORMAT_STRING_SIZE]; };
struct TypeFormatString { short Pad; unsigned char Format[WINIPC_DCE_X64_TYPE_FORMAT_STRING_SIZE]; };
#pragma pack(pop)

// ---- procedure format string (one method: ProcessIpcRequest), x64 env ----
const ProcFormatString g_ProcFormatString = { 0, {
    /* Procedure ProcessIpcRequest */
            0x0, 0x48,            /* old flags */
            NdrFcLong(0x0),
            NdrFcShort(0x0),
            NdrFcShort(0x118),    /* x64 stack size = 280 */
            0x32, 0x0,            /* FC_BIND_PRIMITIVE */
            NdrFcShort(0x0),
            NdrFcShort(0x48),     /* constant client buffer = 72 */
            NdrFcShort(0xe8),     /* constant server buffer = 232 */
            0x47, 0x22,           /* Oi2 flags; 34 params */
            0xa, 0x7,             /* ext size 10; ext flags */
            NdrFcShort(0x1),
            NdrFcShort(0x1),
            NdrFcShort(0x0),
            NdrFcShort(0x0),
    /* messageId */            NdrFcShort(0x48), NdrFcShort(0x8),   0x8, 0x0,
    /* dataSize */             NdrFcShort(0x48), NdrFcShort(0x10),  0x8, 0x0,
    /* data */                 NdrFcShort(0xb),  NdrFcShort(0x18),  NdrFcShort(0x2),
    /* fileHandleCount */      NdrFcShort(0x48), NdrFcShort(0x20),  0x8, 0x0,
    /* fileHandles */          NdrFcShort(0xb),  NdrFcShort(0x28),  NdrFcShort(0x12),
    /* eventHandleCount */     NdrFcShort(0x48), NdrFcShort(0x30),  0x8, 0x0,
    /* eventHandles */         NdrFcShort(0xb),  NdrFcShort(0x38),  NdrFcShort(0x32),
    /* mutexHandleCount */     NdrFcShort(0x48), NdrFcShort(0x40),  0x8, 0x0,
    /* mutexHandles */         NdrFcShort(0xb),  NdrFcShort(0x48),  NdrFcShort(0x52),
    /* processHandleCount */   NdrFcShort(0x48), NdrFcShort(0x50),  0x8, 0x0,
    /* processHandles */       NdrFcShort(0xb),  NdrFcShort(0x58),  NdrFcShort(0x72),
    /* registryHandleCount */  NdrFcShort(0x48), NdrFcShort(0x60),  0x8, 0x0,
    /* registryHandles */      NdrFcShort(0xb),  NdrFcShort(0x68),  NdrFcShort(0x92),
    /* pipeHandleCount */      NdrFcShort(0x48), NdrFcShort(0x70),  0x8, 0x0,
    /* pipeHandles */          NdrFcShort(0xb),  NdrFcShort(0x78),  NdrFcShort(0xb2),
    /* sectionHandleCount */   NdrFcShort(0x48), NdrFcShort(0x80),  0x8, 0x0,
    /* sectionHandles */       NdrFcShort(0xb),  NdrFcShort(0x88),  NdrFcShort(0xd2),
    /* responseDataSize */     NdrFcShort(0x2150), NdrFcShort(0x90),  0x8, 0x0,
    /* responseData */         NdrFcShort(0x2013), NdrFcShort(0x98),  NdrFcShort(0xf6),
    /* responseFileHandleCount */ NdrFcShort(0x2150), NdrFcShort(0xa0), 0x8, 0x0,
    /* responseFileHandles */  NdrFcShort(0x2013), NdrFcShort(0xa8), NdrFcShort(0x10a),
    /* responseEventHandleCount */ NdrFcShort(0x2150), NdrFcShort(0xb0), 0x8, 0x0,
    /* responseEventHandles */ NdrFcShort(0x2013), NdrFcShort(0xb8), NdrFcShort(0x128),
    /* responseMutexHandleCount */ NdrFcShort(0x2150), NdrFcShort(0xc0), 0x8, 0x0,
    /* responseMutexHandles */ NdrFcShort(0x2013), NdrFcShort(0xc8), NdrFcShort(0x146),
    /* responseProcessHandleCount */ NdrFcShort(0x2150), NdrFcShort(0xd0), 0x8, 0x0,
    /* responseProcessHandles */ NdrFcShort(0x2013), NdrFcShort(0xd8), NdrFcShort(0x164),
    /* responseRegistryHandleCount */ NdrFcShort(0x2150), NdrFcShort(0xe0), 0x8, 0x0,
    /* responseRegistryHandles */ NdrFcShort(0x2013), NdrFcShort(0xe8), NdrFcShort(0x182),
    /* responsePipeHandleCount */ NdrFcShort(0x2150), NdrFcShort(0xf0), 0x8, 0x0,
    /* responsePipeHandles */  NdrFcShort(0x2013), NdrFcShort(0xf8), NdrFcShort(0x1a0),
    /* responseSectionHandleCount */ NdrFcShort(0x2150), NdrFcShort(0x100), 0x8, 0x0,
    /* responseSectionHandles */ NdrFcShort(0x2013), NdrFcShort(0x108), NdrFcShort(0x1be),
    /* Return value */         NdrFcShort(0x70), NdrFcShort(0x110), 0x8, 0x0,
            0x0
} };

// ---- type format string (pointers, arrays, system handles), x64 env ----
const TypeFormatString g_TypeFormatString = { 0, {
            NdrFcShort(0x0),
    /* data: FC_UP -> FC_CARRAY of FC_BYTE */
            0x12, 0x20,  NdrFcShort(0x2),
            0x1b, 0x0,   NdrFcShort(0x1),
            0x29, 0x0,   NdrFcShort(0x10), NdrFcShort(0x1),
            0x1,  0x5b,
    /* fileHandles: FC_UP -> FC_SYSTEM_HANDLE(file) ; FC_BOGUS_ARRAY */
            0x12, 0x20,  NdrFcShort(0x8),
            0x3c, 0x0,   NdrFcLong(0x0),
            0x21, 0x0,   NdrFcShort(0x0),
            0x29, 0x0,   NdrFcShort(0x20), NdrFcShort(0x1),
            NdrFcLong(0xffffffff),  NdrFcShort(0x0),
            0x4c, 0x0,   NdrFcShort(0xffe8),
            0x5c, 0x5b,
    /* eventHandles (system handle type 0x2) */
            0x12, 0x20,  NdrFcShort(0x8),
            0x3c, 0x2,   NdrFcLong(0x0),
            0x21, 0x0,   NdrFcShort(0x0),
            0x29, 0x0,   NdrFcShort(0x30), NdrFcShort(0x1),
            NdrFcLong(0xffffffff),  NdrFcShort(0x0),
            0x4c, 0x0,   NdrFcShort(0xffe8),
            0x5c, 0x5b,
    /* mutexHandles (0x3) */
            0x12, 0x20,  NdrFcShort(0x8),
            0x3c, 0x3,   NdrFcLong(0x0),
            0x21, 0x0,   NdrFcShort(0x0),
            0x29, 0x0,   NdrFcShort(0x40), NdrFcShort(0x1),
            NdrFcLong(0xffffffff),  NdrFcShort(0x0),
            0x4c, 0x0,   NdrFcShort(0xffe8),
            0x5c, 0x5b,
    /* processHandles (0x4) */
            0x12, 0x20,  NdrFcShort(0x8),
            0x3c, 0x4,   NdrFcLong(0x0),
            0x21, 0x0,   NdrFcShort(0x0),
            0x29, 0x0,   NdrFcShort(0x50), NdrFcShort(0x1),
            NdrFcLong(0xffffffff),  NdrFcShort(0x0),
            0x4c, 0x0,   NdrFcShort(0xffe8),
            0x5c, 0x5b,
    /* registryHandles (0x7) */
            0x12, 0x20,  NdrFcShort(0x8),
            0x3c, 0x7,   NdrFcLong(0x0),
            0x21, 0x0,   NdrFcShort(0x0),
            0x29, 0x0,   NdrFcShort(0x60), NdrFcShort(0x1),
            NdrFcLong(0xffffffff),  NdrFcShort(0x0),
            0x4c, 0x0,   NdrFcShort(0xffe8),
            0x5c, 0x5b,
    /* pipeHandles (0xc) */
            0x12, 0x20,  NdrFcShort(0x8),
            0x3c, 0xc,   NdrFcLong(0x0),
            0x21, 0x0,   NdrFcShort(0x0),
            0x29, 0x0,   NdrFcShort(0x70), NdrFcShort(0x1),
            NdrFcLong(0xffffffff),  NdrFcShort(0x0),
            0x4c, 0x0,   NdrFcShort(0xffe8),
            0x5c, 0x5b,
    /* sectionHandles (0x6) */
            0x12, 0x20,  NdrFcShort(0x8),
            0x3c, 0x6,   NdrFcLong(0x0),
            0x21, 0x0,   NdrFcShort(0x0),
            0x29, 0x0,   NdrFcShort(0x80), NdrFcShort(0x1),
            NdrFcLong(0xffffffff),  NdrFcShort(0x0),
            0x4c, 0x0,   NdrFcShort(0xffe8),
            0x5c, 0x5b,
    /* responseDataSize: FC_RP simple long */
            0x11, 0xc,   0x8, 0x5c,
    /* responseData: FC_RP deref -> FC_UP -> FC_CARRAY of FC_BYTE */
            0x11, 0x14,  NdrFcShort(0x2),
            0x12, 0x20,  NdrFcShort(0x2),
            0x1b, 0x0,   NdrFcShort(0x1),
            0x29, 0x54,  NdrFcShort(0x90), NdrFcShort(0x1),
            0x1,  0x5b,
    /* responseFileHandles: FC_RP deref -> FC_UP -> FC_BOGUS_ARRAY */
            0x11, 0x14,  NdrFcShort(0x2),
            0x12, 0x20,  NdrFcShort(0x2),
            0x21, 0x0,   NdrFcShort(0x0),
            0x29, 0x54,  NdrFcShort(0xa0), NdrFcShort(0x1),
            NdrFcLong(0xffffffff),  NdrFcShort(0x0),
            0x4c, 0x0,   NdrFcShort(0xfef2),
            0x5c, 0x5b,
    /* responseEventHandles */
            0x11, 0x14,  NdrFcShort(0x2),
            0x12, 0x20,  NdrFcShort(0x2),
            0x21, 0x0,   NdrFcShort(0x0),
            0x29, 0x54,  NdrFcShort(0xb0), NdrFcShort(0x1),
            NdrFcLong(0xffffffff),  NdrFcShort(0x0),
            0x4c, 0x0,   NdrFcShort(0xfef4),
            0x5c, 0x5b,
    /* responseMutexHandles */
            0x11, 0x14,  NdrFcShort(0x2),
            0x12, 0x20,  NdrFcShort(0x2),
            0x21, 0x0,   NdrFcShort(0x0),
            0x29, 0x54,  NdrFcShort(0xc0), NdrFcShort(0x1),
            NdrFcLong(0xffffffff),  NdrFcShort(0x0),
            0x4c, 0x0,   NdrFcShort(0xfef6),
            0x5c, 0x5b,
    /* responseProcessHandles */
            0x11, 0x14,  NdrFcShort(0x2),
            0x12, 0x20,  NdrFcShort(0x2),
            0x21, 0x0,   NdrFcShort(0x0),
            0x29, 0x54,  NdrFcShort(0xd0), NdrFcShort(0x1),
            NdrFcLong(0xffffffff),  NdrFcShort(0x0),
            0x4c, 0x0,   NdrFcShort(0xfef8),
            0x5c, 0x5b,
    /* responseRegistryHandles */
            0x11, 0x14,  NdrFcShort(0x2),
            0x12, 0x20,  NdrFcShort(0x2),
            0x21, 0x0,   NdrFcShort(0x0),
            0x29, 0x54,  NdrFcShort(0xe0), NdrFcShort(0x1),
            NdrFcLong(0xffffffff),  NdrFcShort(0x0),
            0x4c, 0x0,   NdrFcShort(0xfefa),
            0x5c, 0x5b,
    /* responsePipeHandles */
            0x11, 0x14,  NdrFcShort(0x2),
            0x12, 0x20,  NdrFcShort(0x2),
            0x21, 0x0,   NdrFcShort(0x0),
            0x29, 0x54,  NdrFcShort(0xf0), NdrFcShort(0x1),
            NdrFcLong(0xffffffff),  NdrFcShort(0x0),
            0x4c, 0x0,   NdrFcShort(0xfefc),
            0x5c, 0x5b,
    /* responseSectionHandles */
            0x11, 0x14,  NdrFcShort(0x2),
            0x12, 0x20,  NdrFcShort(0x2),
            0x21, 0x0,   NdrFcShort(0x0),
            0x29, 0x54,  NdrFcShort(0x100), NdrFcShort(0x1),
            NdrFcLong(0xffffffff),  NdrFcShort(0x0),
            0x4c, 0x0,   NdrFcShort(0xfefe),
            0x5c, 0x5b,
            0x0
} };

// The interpreter walks these arrays by absolute offset; lengths must be exact.
static_assert(sizeof(g_ProcFormatString.Format) == WINIPC_DCE_X64_PROC_FORMAT_STRING_SIZE, "proc fmt size");
static_assert(sizeof(g_TypeFormatString.Format) == WINIPC_DCE_X64_TYPE_FORMAT_STRING_SIZE, "type fmt size");

const RPC_SYNTAX_IDENTIFIER g_DceTransferSyntax =
    { { 0x8A885D04, 0x1CEB, 0x11C9, { 0x9F,0xE8,0x08,0x00,0x2B,0x10,0x48,0x60 } }, { 2, 0 } };

}  // namespace winipc_dce_x64

#endif  // defined(_M_AMD64)
