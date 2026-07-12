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

#include "storage/louds/simple_succinct_bit_vector_index.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/log/check.h"
#include "absl/types/span.h"
#include "base/bits.h"

namespace mozc {
namespace storage {
namespace louds {
namespace {

// Performs lower bound search on the 0-bit view of the 1-bit index over
// |range|, which must be a subrange of |index|.  Each entry of |index|
// stores the cumulative number of 1-bits, from which the number of 0-bits
// is derived as follows:
//   The number of 0-bits
//     = (total num bits) - (1-bits)
//     = (chunk_size [bytes] * 8 [bits/byte] * (entry's offset) - (1-bits)
const int* LowerBound0Bit(absl::Span<const int> index, int chunk_size,
                          absl::Span<const int> range, int value) {
  const auto compare = [index, chunk_size](const int& num_1bits, int v) {
    const ptrdiff_t offset = &num_1bits - index.data();
    return ((chunk_size * 8 * offset) - num_1bits) < v;
  };
  return absl::c_lower_bound(range, value, compare);
}

// Block size in bytes of the branchless select fast path below.  The default
// chunk size of the index is one block.
constexpr int kSelectBlockSize = 32;

constexpr uint64_t kOnesStep8 = 0x0101010101010101ULL;
constexpr uint64_t kMsbsStep8 = 0x8080808080808080ULL;

// kSelectInByte[b][r] is the position (0-7) of the r-th (0-based) 1-bit of
// the byte b.  Entries with r >= popcount(b) are unused.
struct SelectInByteTable {
  constexpr SelectInByteTable() : table() {
    for (int b = 0; b < 256; ++b) {
      int r = 0;
      for (int i = 0; i < 8; ++i) {
        if ((b >> i) & 1) {
          table[b][r++] = i;
        }
      }
    }
  }
  uint8_t table[256][8];
};
constexpr SelectInByteTable kSelectInByte;

// Returns the position (0-63) of the k-th (0-based) 1-bit of |x| without any
// branch or loop (broadword select).
//
// REQUIRES: k < std::popcount(x).
inline int SelectInWord(uint64_t x, int k) {
  DCHECK_GE(k, 0);
  DCHECK_LT(k, std::popcount(x));

  // Popcount of each byte.
  uint64_t s = x - ((x >> 1) & 0x5555555555555555ULL);
  s = (s & 0x3333333333333333ULL) + ((s >> 2) & 0x3333333333333333ULL);
  s = (s + (s >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
  // Byte i now holds the popcount of bytes 0..i (all values are < 128).
  const uint64_t sums = s * kOnesStep8;
  // The MSB of byte i is set iff sums_i <= k, i.e. the k-th bit is beyond
  // byte i.  No borrow crosses a byte boundary because sums_i <= 64 < 128.
  const uint64_t k_step8 = static_cast<uint64_t>(k) * kOnesStep8;
  const uint64_t byte_flags = ((k_step8 | kMsbsStep8) - sums) & kMsbsStep8;
  // The number of flagged bytes, summed into the top byte by a multiply.
  const int shift =
      static_cast<int>(((byte_flags >> 7) * kOnesStep8) >> 56) * 8;
  // Rank of the target bit inside its byte.
  const int rank = k - static_cast<int>(((sums << 8) >> shift) & 0xFF);
  return shift + kSelectInByte.table[(x >> shift) & 0xFF][rank];
}

// Returns the position (0-255) of the n-th (1-based) 1-bit (or 0-bit if
// |kInvert|) of the 32-byte block at |block|, without any branch or loop.
//
// REQUIRES: 1 <= n <= the number of such bits in the block.
template <bool kInvert>
inline int SelectInBlock(const uint8_t* block, int n) {
  uint64_t words[4];
  for (int i = 0; i < 4; ++i) {
    const uint64_t w = LoadUnaligned<uint64_t>(block + 8 * i);
    words[i] = kInvert ? ~w : w;
  }
  // preceding[i] is the number of bits in the words before word i.
  int preceding[4];
  preceding[0] = 0;
  for (int i = 1; i < 4; ++i) {
    preceding[i] = preceding[i - 1] + std::popcount(words[i - 1]);
  }
  // Index of the word that contains the n-th bit.  Indexing small local
  // arrays instead of chaining conditionals keeps this free of data
  // dependent branches.
  const int k = (preceding[1] < n) + (preceding[2] < n) + (preceding[3] < n);
  return k * 64 + SelectInWord(words[k], n - preceding[k] - 1);
}

inline int BitCount0(uint32_t x) {
  // Flip all bits, and count 1-bits.
  return std::popcount(~x);
}

// Returns 1-bits in the data to length 32-bit words.
// Processes in 64-bit words as much as possible for speed.
int Count1Bits(const uint8_t* data, int length) {
  int num_bits = 0;
  for (; length >= 2; length -= 2) {
    num_bits += std::popcount(LoadUnalignedAdvance<uint64_t>(data));
  }
  if (length == 1) {
    num_bits += std::popcount(LoadUnalignedAdvance<uint32_t>(data));
  }
  return num_bits;
}

// Stores index (the cumulative number of the 1-bits from begin of each chunk).
void InitIndex(const uint8_t* data, int length, int chunk_size,
               std::vector<int>* index) {
  DCHECK_GE(chunk_size, 4);
  DCHECK(std::has_single_bit<uint32_t>(chunk_size)) << chunk_size;
  DCHECK_EQ(length % 4, 0);

  index->clear();

  // Count the number of chunks with ceiling.
  const int chunk_length = (length + chunk_size - 1) / chunk_size;

  // Reserve the memory including a sentinel.
  index->reserve(chunk_length + 1);

  int num_bits = 0;
  for (int remaining_num_words = length / 4; remaining_num_words > 0;
       data += chunk_size, remaining_num_words -= chunk_size / 4) {
    index->push_back(num_bits);
    num_bits += Count1Bits(data, std::min(chunk_size / 4, remaining_num_words));
  }
  index->push_back(num_bits);

  CHECK_EQ(chunk_length + 1, index->size());
}

void InitLowerBound0Cache(absl::Span<const int> index, int chunk_size,
                          size_t increment, size_t size,
                          std::vector<const int*>* cache) {
  DCHECK_GT(increment, 0);
  cache->clear();
  cache->reserve(size + 2);
  cache->push_back(index.data());
  for (size_t i = 1; i <= size; ++i) {
    const int target_index = increment * i;
    const int* ptr = LowerBound0Bit(index, chunk_size, index, target_index);
    cache->push_back(ptr);
  }
  cache->push_back(index.data() + index.size());
}

void InitLowerBound1Cache(absl::Span<const int> index, int chunk_size,
                          size_t increment, size_t size,
                          std::vector<const int*>* cache) {
  DCHECK_GT(increment, 0);
  cache->clear();
  cache->reserve(size + 2);
  cache->push_back(index.data());
  for (size_t i = 1; i <= size; ++i) {
    const int target_index = increment * i;
    const int* ptr = std::lower_bound(index.data(), index.data() + index.size(),
                                      target_index);
    cache->push_back(ptr);
  }
  cache->push_back(index.data() + index.size());
}

}  // namespace

void SimpleSuccinctBitVectorIndex::Init(const uint8_t* data, int length,
                                        size_t lb0_cache_size,
                                        size_t lb1_cache_size) {
  data_ = data;
  length_ = length;
  InitIndex(data, length, chunk_size_, &index_);

  // TODO(noriyukit): Currently, we simply use uniform increment width for lower
  // bound cache.  Nonuniform increment width may improve performance.
  lb0_cache_increment_ =
      lb0_cache_size == 0 ? GetNum0Bits() : GetNum0Bits() / lb0_cache_size;
  if (lb0_cache_increment_ == 0) {
    lb0_cache_increment_ = 1;
  }
  InitLowerBound0Cache(index_, chunk_size_, lb0_cache_increment_,
                       lb0_cache_size, &lb0_cache_);

  lb1_cache_increment_ =
      lb1_cache_size == 0 ? GetNum1Bits() : GetNum1Bits() / lb1_cache_size;
  if (lb1_cache_increment_ == 0) {
    lb1_cache_increment_ = 1;
  }
  InitLowerBound1Cache(index_, chunk_size_, lb1_cache_increment_,
                       lb1_cache_size, &lb1_cache_);
}

void SimpleSuccinctBitVectorIndex::Reset() {
  data_ = nullptr;
  length_ = 0;
  index_.clear();
  lb0_cache_increment_ = 1;
  lb0_cache_.clear();
  lb1_cache_increment_ = 1;
  lb1_cache_.clear();
}

int SimpleSuccinctBitVectorIndex::Rank1(int n) const {
  // Look up pre-computed 1-bits for the preceding chunks.
  const int num_chunks = n / (chunk_size_ * 8);
  int result = index_[num_chunks];

  // Count 1-bits for remaining "words".
  result += Count1Bits(data_ + num_chunks * chunk_size_,
                       (n / 32) - num_chunks * (chunk_size_ / 4));

  // Count 1-bits for remaining "bits".
  if (n % 32 > 0) {
    const int offset = 4 * (n / 32);
    const int shift = 32 - n % 32;
    result += std::popcount(LoadUnaligned<uint32_t>(data_ + offset) << shift);
  }

  return result;
}

int SimpleSuccinctBitVectorIndex::Select0(int n) const {
  DCHECK_GT(n, 0);

  // Narrow down the range of |index_| on which lower bound is performed.
  int lb0_cache_index = n / lb0_cache_increment_;
  if (lb0_cache_index > lb0_cache_.size() - 2) {
    lb0_cache_index = lb0_cache_.size() - 2;
  }
  DCHECK_GE(lb0_cache_index, 0);

  // Binary search on chunks.
  const int* chunk_ptr = LowerBound0Bit(
      index_, chunk_size_,
      absl::MakeConstSpan(lb0_cache_[lb0_cache_index],
                          lb0_cache_[lb0_cache_index + 1]),
      n);
  const int chunk_index = (chunk_ptr - index_.data()) - 1;
  DCHECK_GE(chunk_index, 0);
  n -= chunk_size_ * 8 * chunk_index - index_[chunk_index];

  const int offset = (chunk_index * chunk_size_) & ~int{3};
  if (chunk_size_ == kSelectBlockSize && offset + kSelectBlockSize <= length_) {
    return offset * 8 + SelectInBlock<true>(data_ + offset, n);
  }

  // Generic path for other chunk sizes and for a partial last chunk.
  // Linear search on remaining "words"
  const uint8_t* ptr = data_ + offset;
  while (true) {
    const int bit_count = BitCount0(LoadUnaligned<uint32_t>(ptr));
    if (bit_count >= n) {
      break;
    }
    n -= bit_count;
    ptr += 4;
  }

  // Select the n-th 0-bit in the word: clear the lowest (n - 1) 1-bits of the
  // inverted word, then the target position is the number of trailing zeros.
  uint32_t word = ~LoadUnaligned<uint32_t>(ptr);
  for (; n > 1; --n) {
    word &= word - 1;
  }
  return (ptr - data_) * 8 + std::countr_zero(word);
}

int SimpleSuccinctBitVectorIndex::Select1(int n) const {
  DCHECK_GT(n, 0);

  // Narrow down the range of |index_| on which lower bound is performed.
  int lb1_cache_index = n / lb1_cache_increment_;
  if (lb1_cache_index > lb1_cache_.size() - 2) {
    lb1_cache_index = lb1_cache_.size() - 2;
  }
  DCHECK_GE(lb1_cache_index, 0);

  // Binary search on chunks.
  const int* chunk_ptr = std::lower_bound(lb1_cache_[lb1_cache_index],
                                          lb1_cache_[lb1_cache_index + 1], n);
  const int chunk_index = (chunk_ptr - index_.data()) - 1;
  DCHECK_GE(chunk_index, 0);
  n -= index_[chunk_index];

  const int offset = (chunk_index * chunk_size_) & ~int{3};
  if (chunk_size_ == kSelectBlockSize && offset + kSelectBlockSize <= length_) {
    return offset * 8 + SelectInBlock<false>(data_ + offset, n);
  }

  // Generic path for other chunk sizes and for a partial last chunk.
  // Linear search on remaining "words"
  const uint8_t* ptr = data_ + offset;
  while (true) {
    const int bit_count = std::popcount(LoadUnaligned<uint32_t>(ptr));
    if (bit_count >= n) {
      break;
    }
    n -= bit_count;
    ptr += 4;
  }

  // Select the n-th 1-bit in the word: clear the lowest (n - 1) 1-bits, then
  // the target position is the number of trailing zeros.
  uint32_t word = LoadUnaligned<uint32_t>(ptr);
  for (; n > 1; --n) {
    word &= word - 1;
  }
  return (ptr - data_) * 8 + std::countr_zero(word);
}

}  // namespace louds
}  // namespace storage
}  // namespace mozc
