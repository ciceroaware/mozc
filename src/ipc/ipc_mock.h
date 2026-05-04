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

#ifndef MOZC_IPC_IPC_MOCK_H_
#define MOZC_IPC_IPC_MOCK_H_

#include <cstdint>
#include <memory>
#include <string>

#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "ipc/ipc.h"

namespace mozc {

// Unified fake for IPC unit tests.  FakeIPCClientFactory hands out
// FakeIPCClient instances; both share state via the factory.  Tests configure
// the factory via Set* methods and inspect the recorded request and call
// count via the observer methods.  Plug into a production seam via Bind(),
// which returns an IPCClientFactory callable that delegates to *this.
class FakeIPCClientFactory {
 public:
  FakeIPCClientFactory();
  FakeIPCClientFactory(const FakeIPCClientFactory &) = delete;
  FakeIPCClientFactory &operator=(const FakeIPCClientFactory &) = delete;

  std::unique_ptr<IPCClientInterface> NewClient(
      absl::string_view unused_name, absl::string_view path_name);
  std::unique_ptr<IPCClientInterface> NewClient(absl::string_view unused_name);

  // Returns a callable that delegates to NewClient on this fake.  The
  // returned callable holds a non-owning pointer; *this must outlive it.
  IPCClientFactory Bind();

  // Knobs (configure before triggering code that creates a FakeIPCClient).
  void SetConnection(bool connection) { connection_ = connection; }
  void SetResult(bool result) { result_ = result; }
  void SetMockResponse(absl::string_view response) { response_ = response; }
  void SetServerProtocolVersion(uint32_t version) {
    server_protocol_version_ = version;
  }
  void SetServerProductVersion(absl::string_view version) {
    server_product_version_ = std::string(version);
  }
  void SetServerProcessId(uint32_t pid) { server_process_id_ = pid; }

  // Observers (read after triggering code that calls NewClient/Call).
  absl::string_view GetGeneratedRequest() const { return last_request_; }
  int call_count() const { return call_count_; }
  void ResetCallCount() { call_count_ = 0; }

  // Hooks for FakeIPCClient.  Not intended for direct use by tests.
  bool connection() const { return connection_; }
  bool result() const { return result_; }
  uint32_t server_protocol_version() const { return server_protocol_version_; }
  absl::string_view server_product_version() const {
    return server_product_version_;
  }
  uint32_t server_process_id() const { return server_process_id_; }
  absl::string_view canned_response() const { return response_; }
  void RecordCall(absl::string_view request);

 private:
  bool connection_ = false;
  bool result_ = false;
  uint32_t server_protocol_version_ = IPC_PROTOCOL_VERSION;
  std::string server_product_version_;
  uint32_t server_process_id_ = 0;
  std::string response_;
  std::string last_request_;
  int call_count_ = 0;
};

class FakeIPCClient : public IPCClientInterface {
 public:
  explicit FakeIPCClient(FakeIPCClientFactory *factory) : factory_(factory) {}
  FakeIPCClient(const FakeIPCClient &) = delete;
  FakeIPCClient &operator=(const FakeIPCClient &) = delete;

  bool Connected() const override { return factory_->connection(); }
  uint32_t GetServerProtocolVersion() const override {
    return factory_->server_protocol_version();
  }
  absl::string_view GetServerProductVersion() const override {
    return factory_->server_product_version();
  }
  uint32_t GetServerProcessId() const override {
    return factory_->server_process_id();
  }
  IPCErrorType GetLastIPCError() const override { return IPC_NO_ERROR; }

  bool Call(absl::string_view request, std::string *response,
            absl::Duration timeout) override;

 private:
  FakeIPCClientFactory *factory_;
};

}  // namespace mozc
#endif  // MOZC_IPC_IPC_MOCK_H_
