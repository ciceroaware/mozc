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

// RpcNdr64Format.hpp - shared hand-authored NDR64 format metadata (x64).
//
// The NDR64 format-string fragment tree + procedure descriptor for
// IWinIpcInitiator::ProcessIpcRequest. It is identical for the client and the
// server (MIDL emits the same __midl_frag* tree into both _c.c and _s.c), so both
// the MIDL-free client (RpcNdr64Stub.cpp) and the MIDL-free server
// (RpcNdr64Server.cpp) include this and reference winipc_ndr64::g_Ndr64ProcTable
// and winipc_ndr64::g_Ndr64TransferSyntax.
//
// All objects are namespace-scope `const` (internal linkage), so including this
// in more than one translation unit is safe. x64 only.

#pragma once

#if defined(_M_AMD64)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <rpc.h>
#include <rpcndr.h>
#include <ndr64types.h>

namespace winipc_ndr64 {

// ---- NDR64 format-code constants (from MS-RPCE / ndr64types.h) -------------
constexpr NDR64_FORMAT_CHAR FC64_RP            = 0x20;  // ref pointer
constexpr NDR64_FORMAT_CHAR FC64_UP            = 0x21;  // unique pointer
constexpr NDR64_FORMAT_CHAR FC64_CONF_ARRAY    = 0x41;
constexpr NDR64_FORMAT_CHAR FC64_BOGUS_ARRAY   = 0x47;
constexpr NDR64_FORMAT_CHAR FC64_SYSTEM_HANDLE = 0x3c;
constexpr NDR64_FORMAT_CHAR FC64_INT8          = 0x02;
constexpr NDR64_FORMAT_CHAR FC64_INT32         = 0x05;
constexpr NDR64_FORMAT_CHAR FC64_UINT32        = 0x06;
constexpr NDR64_FORMAT_CHAR FC64_UINT64        = 0x08;
constexpr NDR64_FORMAT_CHAR FC64_BIND_PRIMITIVE = 0x72;
constexpr NDR64_FORMAT_CHAR FC_EXPR_VAR        = 0x03;
constexpr NDR64_FORMAT_CHAR FC_EXPR_OPER       = 0x04;
constexpr NDR64_FORMAT_CHAR OP_UNARY_INDIRECTION = 0x05;

// System-handle wire-type bytes (NDR64), NOT the sh_* doc ordinals.
// See SYSTEM_HANDLE_ARRAY_FINDINGS.md.
constexpr NDR64_UINT8 SH_FILE = 0x00, SH_EVENT = 0x02, SH_MUTEX = 0x03,
                      SH_PROCESS = 0x04, SH_REGKEY = 0x07, SH_PIPE = 0x0C,
                      SH_SECTION = 0x06;

// Stack offsets of each parameter (x64, 8-byte slots), matching MIDL.
constexpr NDR64_UINT32 OFF_dataSize         = 0x10;
constexpr NDR64_UINT32 OFF_fileCount        = 0x20;
constexpr NDR64_UINT32 OFF_eventCount       = 0x30;
constexpr NDR64_UINT32 OFF_mutexCount       = 0x40;
constexpr NDR64_UINT32 OFF_processCount     = 0x50;
constexpr NDR64_UINT32 OFF_registryCount    = 0x60;
constexpr NDR64_UINT32 OFF_pipeCount        = 0x70;
constexpr NDR64_UINT32 OFF_sectionCount     = 0x80;
constexpr NDR64_UINT32 OFF_respDataSize     = 0x90;
constexpr NDR64_UINT32 OFF_respFileCount    = 0xA0;
constexpr NDR64_UINT32 OFF_respEventCount   = 0xB0;
constexpr NDR64_UINT32 OFF_respMutexCount   = 0xC0;
constexpr NDR64_UINT32 OFF_respProcessCount = 0xD0;
constexpr NDR64_UINT32 OFF_respRegistryCount = 0xE0;
constexpr NDR64_UINT32 OFF_respPipeCount    = 0xF0;
constexpr NDR64_UINT32 OFF_respSectionCount = 0x100;

#pragma pack(push, 8)  // MIDL emits these fragments under Zp8.

// Conformance expression for an [in] count param: "size = <count at offset>".
struct InCorr {
    NDR64_FORMAT_UINT32 Size;
    NDR64_EXPR_VAR      Var;
};
// Conformance expression for an [out] *count param: "size = *(count ptr at offset)".
struct OutCorr {
    NDR64_FORMAT_UINT32 Size;
    NDR64_EXPR_OPERATOR Op;
    NDR64_EXPR_VAR      Var;
};
// Conformant byte array (data / responseData element layer).
struct ConfArray {
    NDR64_CONF_ARRAY_HEADER_FORMAT Hdr;
    NDR64_ARRAY_ELEMENT_INFO       Elem;
};
using Bogus   = NDR64_CONF_VAR_BOGUS_ARRAY_HEADER_FORMAT;
using Ptr     = NDR64_POINTER_FORMAT;
using SysH    = NDR64_SYSTEM_HANDLE_FORMAT;

constexpr NDR64_ARRAY_FLAGS kBogusFlags{ 0,1,0,0,0,0,0,0 };  // HasElementInfo
constexpr NDR64_ARRAY_FLAGS kConfFlags { 0,0,0,0,0,0,0,0 };

inline constexpr InCorr MakeInCorr(NDR64_UINT32 off) {
    return InCorr{ 1, { FC_EXPR_VAR, FC64_UINT32, 0, off } };
}
inline constexpr OutCorr MakeOutCorr(NDR64_UINT32 off) {
    return OutCorr{ 1,
        { FC_EXPR_OPER, OP_UNARY_INDIRECTION, FC64_UINT32, 0 },
        { FC_EXPR_VAR, FC64_UINT64, 0, off } };
}

// ---- base element types ----
const NDR64_FORMAT_CHAR f_int32 = FC64_INT32;  // counts, messageId, return
const NDR64_FORMAT_CHAR f_int8  = FC64_INT8;   // byte-array element

// ---- one system-handle frag per handle type (shared by [in] and [out]) ----
const SysH f_sh_file    { FC64_SYSTEM_HANDLE, SH_FILE,    0 };
const SysH f_sh_event   { FC64_SYSTEM_HANDLE, SH_EVENT,   0 };
const SysH f_sh_mutex   { FC64_SYSTEM_HANDLE, SH_MUTEX,   0 };
const SysH f_sh_process { FC64_SYSTEM_HANDLE, SH_PROCESS, 0 };
const SysH f_sh_regkey  { FC64_SYSTEM_HANDLE, SH_REGKEY,  0 };
const SysH f_sh_pipe    { FC64_SYSTEM_HANDLE, SH_PIPE,    0 };
const SysH f_sh_section { FC64_SYSTEM_HANDLE, SH_SECTION, 0 };

// Builds a bogus-array header (conformant array of system handles).
inline constexpr Bogus MakeBogus(const SysH* elem, const void* conf) {
    return Bogus{
        { FC64_BOGUS_ARRAY, 0, kBogusFlags, 1, 0, (PNDR64_FORMAT)elem },
        (PNDR64_FORMAT)conf, 0, 0 };
}

// =================== [in] conformant byte array (data) ===================
const InCorr     c_data_in = MakeInCorr(OFF_dataSize);
const ConfArray  a_data_in = {
    { FC64_CONF_ARRAY, 0, kConfFlags, 0, 1, (PNDR64_FORMAT)&c_data_in },
    { 1, (PNDR64_FORMAT)&f_int8 } };
const Ptr        p_data_in = { FC64_UP, 0x20, 0, (PNDR64_FORMAT)&a_data_in };

// =================== [in] system-handle arrays ===================
#define IN_ARRAY(name, sh, off)                                              \
    const InCorr c_##name = MakeInCorr(off);                                 \
    const Bogus  a_##name = MakeBogus(&sh, &c_##name);                       \
    const Ptr    p_##name = { FC64_UP, 0x20, 0, (PNDR64_FORMAT)&a_##name }

IN_ARRAY(file_in,     f_sh_file,    OFF_fileCount);
IN_ARRAY(event_in,    f_sh_event,   OFF_eventCount);
IN_ARRAY(mutex_in,    f_sh_mutex,   OFF_mutexCount);
IN_ARRAY(process_in,  f_sh_process, OFF_processCount);
IN_ARRAY(registry_in, f_sh_regkey,  OFF_registryCount);
IN_ARRAY(pipe_in,     f_sh_pipe,    OFF_pipeCount);
IN_ARRAY(section_in,  f_sh_section, OFF_sectionCount);
#undef IN_ARRAY

// =================== [out] conformant byte array (responseData) ===========
const OutCorr    c_data_out = MakeOutCorr(OFF_respDataSize);
const ConfArray  a_data_out = {
    { FC64_CONF_ARRAY, 0, kConfFlags, 0, 1, (PNDR64_FORMAT)&c_data_out },
    { 1, (PNDR64_FORMAT)&f_int8 } };
const Ptr        u_data_out = { FC64_UP, 0x20, 0, (PNDR64_FORMAT)&a_data_out };
const Ptr        p_data_out = { FC64_RP, 0x14, 0, (PNDR64_FORMAT)&u_data_out };

// =================== [out] system-handle arrays ===================
// [out] is a ref pointer (0x20) to a unique pointer (0x21) to the bogus array.
#define OUT_ARRAY(name, sh, off)                                             \
    const OutCorr c_##name = MakeOutCorr(off);                               \
    const Bogus   a_##name = MakeBogus(&sh, &c_##name);                      \
    const Ptr     u_##name = { FC64_UP, 0x20, 0, (PNDR64_FORMAT)&a_##name }; \
    const Ptr     p_##name = { FC64_RP, 0x14, 0, (PNDR64_FORMAT)&u_##name }

OUT_ARRAY(file_out,     f_sh_file,    OFF_respFileCount);
OUT_ARRAY(event_out,    f_sh_event,   OFF_respEventCount);
OUT_ARRAY(mutex_out,    f_sh_mutex,   OFF_respMutexCount);
OUT_ARRAY(process_out,  f_sh_process, OFF_respProcessCount);
OUT_ARRAY(registry_out, f_sh_regkey,  OFF_respRegistryCount);
OUT_ARRAY(pipe_out,     f_sh_pipe,    OFF_respPipeCount);
OUT_ARRAY(section_out,  f_sh_section, OFF_respSectionCount);
#undef OUT_ARRAY

// ---- parameter attribute flag patterns ----
constexpr NDR64_PARAM_FLAGS F_IN_BASE  { .IsIn = 1, .IsBasetype = 1, .IsByValue = 1 };
constexpr NDR64_PARAM_FLAGS F_IN_PTR   { .MustSize = 1, .MustFree = 1, .IsIn = 1 };
constexpr NDR64_PARAM_FLAGS F_OUT_BASE { .IsOut = 1, .IsBasetype = 1, .IsSimpleRef = 1, .UseCache = 1 };
constexpr NDR64_PARAM_FLAGS F_OUT_PTR  { .MustSize = 1, .MustFree = 1, .IsOut = 1, .UseCache = 1 };
constexpr NDR64_PARAM_FLAGS F_RETURN   { .IsOut = 1, .IsReturn = 1, .IsBasetype = 1, .IsByValue = 1 };

inline constexpr NDR64_PARAM_FORMAT P(const void* type, NDR64_PARAM_FLAGS f, NDR64_UINT32 off) {
    return NDR64_PARAM_FORMAT{ (PNDR64_FORMAT)type, f, 0, off };
}

// ---- the procedure format: header + bind extension + 34 params ----
struct ProcBlob {
    NDR64_PROC_FORMAT               Proc;
    NDR64_BIND_AND_NOTIFY_EXTENSION BindExt;
    NDR64_PARAM_FORMAT              Params[34];
};

const ProcBlob g_proc = {
    // NDR64_PROC_FORMAT: flags, stack size, client buf, server buf, rpcflags,
    // floatmask, num params, extension size  (verbatim from MIDL golden data)
    { 0x016e0040, 0x118, 0x48, 0x108, 0, 0, 34, 8 },
    // bind extension: explicit primitive handle at stack offset 0
    { { FC64_BIND_PRIMITIVE, 0, 0, 0, 0 }, 0 },
    {
        P(&f_int32,      F_IN_BASE, 0x08),  // messageId
        P(&f_int32,      F_IN_BASE, 0x10),  // dataSize
        P(&p_data_in,    F_IN_PTR,  0x18),  // data
        P(&f_int32,      F_IN_BASE, 0x20),  // fileHandleCount
        P(&p_file_in,    F_IN_PTR,  0x28),  // fileHandles
        P(&f_int32,      F_IN_BASE, 0x30),  // eventHandleCount
        P(&p_event_in,   F_IN_PTR,  0x38),  // eventHandles
        P(&f_int32,      F_IN_BASE, 0x40),  // mutexHandleCount
        P(&p_mutex_in,   F_IN_PTR,  0x48),  // mutexHandles
        P(&f_int32,      F_IN_BASE, 0x50),  // processHandleCount
        P(&p_process_in, F_IN_PTR,  0x58),  // processHandles
        P(&f_int32,      F_IN_BASE, 0x60),  // registryHandleCount
        P(&p_registry_in,F_IN_PTR,  0x68),  // registryHandles
        P(&f_int32,      F_IN_BASE, 0x70),  // pipeHandleCount
        P(&p_pipe_in,    F_IN_PTR,  0x78),  // pipeHandles
        P(&f_int32,      F_IN_BASE, 0x80),  // sectionHandleCount
        P(&p_section_in, F_IN_PTR,  0x88),  // sectionHandles
        P(&f_int32,      F_OUT_BASE,0x90),  // responseDataSize
        P(&p_data_out,   F_OUT_PTR, 0x98),  // responseData
        P(&f_int32,      F_OUT_BASE,0xA0),  // responseFileHandleCount
        P(&p_file_out,   F_OUT_PTR, 0xA8),  // responseFileHandles
        P(&f_int32,      F_OUT_BASE,0xB0),  // responseEventHandleCount
        P(&p_event_out,  F_OUT_PTR, 0xB8),  // responseEventHandles
        P(&f_int32,      F_OUT_BASE,0xC0),  // responseMutexHandleCount
        P(&p_mutex_out,  F_OUT_PTR, 0xC8),  // responseMutexHandles
        P(&f_int32,      F_OUT_BASE,0xD0),  // responseProcessHandleCount
        P(&p_process_out,F_OUT_PTR, 0xD8),  // responseProcessHandles
        P(&f_int32,      F_OUT_BASE,0xE0),  // responseRegistryHandleCount
        P(&p_registry_out,F_OUT_PTR,0xE8),  // responseRegistryHandles
        P(&f_int32,      F_OUT_BASE,0xF0),  // responsePipeHandleCount
        P(&p_pipe_out,   F_OUT_PTR, 0xF8),  // responsePipeHandles
        P(&f_int32,      F_OUT_BASE,0x100), // responseSectionHandleCount
        P(&p_section_out,F_OUT_PTR, 0x108), // responseSectionHandles
        P(&f_int32,      F_RETURN,  0x110), // HRESULT (return)
    }
};

#pragma pack(pop)

// proc table + NDR64 transfer syntax GUID (shared by client proxy info and
// server info).
const void* const g_Ndr64ProcTable[1] = { &g_proc };

const RPC_SYNTAX_IDENTIFIER g_Ndr64TransferSyntax =
    { { 0x71710533, 0xbeba, 0x4937, { 0x83,0x19,0xb5,0xdb,0xef,0x9c,0xcc,0x36 } }, { 1, 0 } };

} // namespace winipc_ndr64

#endif // defined(_M_AMD64)
