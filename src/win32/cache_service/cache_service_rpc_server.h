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

#ifndef MOZC_WIN32_CACHE_SERVICE_CACHE_SERVICE_RPC_SERVER_H_
#define MOZC_WIN32_CACHE_SERVICE_CACHE_SERVICE_RPC_SERVER_H_

#ifdef _WIN32

namespace mozc {

// Hosts the experimental MSRPC (ncalrpc) interface exposed by
// mozc_cache_service.exe. The interface multiplexes operations through a single
// method (see ipc/win/rpc_interface.h); milestone 1 implements the "launch
// helper" that starts mozc_server.exe / mozc_renderer.exe in the calling user's
// session on behalf of (possibly sandboxed) clients.
//
// Start() registers the interface and begins listening on RPC runtime-managed
// threads (it does not block). The destructor (or Stop()) stops listening. The
// manager routine is stateless, so the RPC thread pool needs no extra
// serialization. RPC server registration is process-global; create at most one
// instance.
class CacheServiceRpcServer {
 public:
  CacheServiceRpcServer() = default;
  ~CacheServiceRpcServer();

  CacheServiceRpcServer(const CacheServiceRpcServer &) = delete;
  CacheServiceRpcServer &operator=(const CacheServiceRpcServer &) = delete;

  // Sets up the protocol sequence + endpoint security descriptor, registers the
  // authentication info and the interface, and starts listening without
  // blocking. Returns true on success. Failure is intended to be non-fatal to
  // the caller (the cache service can still perform its memory-locking duty).
  bool Start();

  // Stops listening and unregisters the interface. Safe to call multiple times.
  void Stop();

 private:
  bool listening_ = false;
  bool registered_ = false;
};

}  // namespace mozc

#endif  // _WIN32
#endif  // MOZC_WIN32_CACHE_SERVICE_CACHE_SERVICE_RPC_SERVER_H_
