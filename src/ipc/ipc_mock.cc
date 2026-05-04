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

// Unified fake of IPCClientInterface and the IPCClientFactory test seam.

#include "ipc/ipc_mock.h"

#include <memory>
#include <string>

#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "base/version.h"
#include "ipc/ipc.h"

namespace mozc {

FakeIPCClientFactory::FakeIPCClientFactory()
    : server_product_version_(Version::GetMozcVersion()) {}

std::unique_ptr<IPCClientInterface> FakeIPCClientFactory::NewClient(
    absl::string_view unused_name, absl::string_view path_name) {
  return std::make_unique<FakeIPCClient>(this);
}

std::unique_ptr<IPCClientInterface> FakeIPCClientFactory::NewClient(
    absl::string_view unused_name) {
  return std::make_unique<FakeIPCClient>(this);
}

IPCClientFactory FakeIPCClientFactory::Bind() {
  return [this](absl::string_view name, absl::string_view path) {
    return NewClient(name, path);
  };
}

void FakeIPCClientFactory::RecordCall(absl::string_view request) {
  last_request_.assign(request.data(), request.size());
  ++call_count_;
}

bool FakeIPCClient::Call(absl::string_view request, std::string *response,
                         const absl::Duration timeout) {
  factory_->RecordCall(request);
  if (!factory_->connection() || !factory_->result()) {
    return false;
  }
  const absl::string_view canned = factory_->canned_response();
  response->assign(canned.data(), canned.size());
  return true;
}

}  // namespace mozc
