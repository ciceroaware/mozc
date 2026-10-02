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

#ifndef MOZC_CONVERTER_CONNECTOR_H_
#define MOZC_CONVERTER_CONNECTOR_H_

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "base/bits.h"

namespace mozc {

class Connector final {
 public:
  static constexpr int16_t kInvalidCost = 30000;

  static absl::StatusOr<Connector> Create(absl::string_view connection_data);

  int GetTransitionCost(uint16_t rid, uint16_t lid) const;
  int GetResolution() const { return resolution_; }

 private:
  class Row;

  absl::Status Init(absl::string_view connection_data);

  int LookupCost(uint16_t rid, uint16_t lid) const;

  // Storage for the rank indices of all rows, allocated once with the exact
  // size. Each row points into it.
  std::unique_ptr<uint16_t[]> rank_index_;
  std::vector<Row> rows_;
  const uint16_t* default_cost_ = nullptr;
  int resolution_ = 0;
  // Cache for transition cost.
  using cache_t = std::vector<std::atomic<uint64_t>>;
  mutable std::unique_ptr<cache_t> cache_;
};

class Connector::Row final {
 public:
  Row() = default;

  // Stores the pointers to and the sizes of the row data. The rank indices are
  // built later by BuildIndex, once the storage for all rows is allocated.
  void Init(const uint8_t* chunk_bits, size_t chunk_bits_size,
            const uint8_t* compact_bits, size_t compact_bits_size,
            const uint8_t* values, bool use_1byte_value);
  // Returns the number of uint16_t entries that BuildIndex needs.
  size_t IndexSize() const {
    return (chunk_bits_size_ + compact_bits_size_) / 4;
  }
  // Builds the rank indices in `index`, which must have IndexSize() entries,
  // and returns the pointer past the entries used.
  uint16_t* BuildIndex(uint16_t* index);
  // Returns the value in the row if found.
  std::optional<uint16_t> GetValue(uint16_t index) const;

 private:
  // Bit vector that supports only "is bit n set, and if so how many 1-bits
  // precede it", which is all a row needs. Stores the cumulative number of
  // 1-bits before each 32-bit word. A row has at most lsize bits, which is a
  // uint16_t in the connection data, so the counts fit in uint16_t.
  class RankBitVector final {
   public:
    void set_data(const uint8_t* data) { data_ = data; }
    // Builds the index for the first `length` bytes of the data in `index`,
    // which must have length / 4 entries, and returns the pointer past them.
    uint16_t* BuildIndex(size_t length, uint16_t* index);

    // Returns the number of 1-bits in [0, n) if bit n is set.
    std::optional<int> Rank1IfSet(int n) const {
      const uint32_t word = LoadUnaligned<uint32_t>(data_ + 4 * (n >> 5));
      const uint32_t bit = uint32_t{1} << (n & 31);
      if ((word & bit) == 0) {
        return std::nullopt;
      }
      return rank_[n >> 5] + std::popcount(word & (bit - 1));
    }

   private:
    const uint8_t* data_ = nullptr;
    const uint16_t* rank_ = nullptr;
  };

  RankBitVector chunk_bits_index_;
  RankBitVector compact_bits_index_;
  const uint8_t* values_ = nullptr;
  uint16_t chunk_bits_size_ = 0;
  uint16_t compact_bits_size_ = 0;
  bool use_1byte_value_ = false;
};

}  // namespace mozc

#endif  // MOZC_CONVERTER_CONNECTOR_H_
