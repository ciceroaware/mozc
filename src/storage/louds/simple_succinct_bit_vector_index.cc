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
#include <iterator>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/log/check.h"
#include "absl/types/span.h"
#include "base/bits.h"

namespace mozc {
namespace storage {
namespace louds {
namespace {

// Select0 and Select1 keep two small arrays on the stack (see SelectInChunk),
// which makes compilers with a "strong" stack protector, e.g. clang-cl with
// its default /GS, guard the frame with a stack cookie. The arrays are indexed
// only with values in [0, 3] computed from booleans, so the guard cannot fire
// and just costs a load, a store and a compare per query.
#if ABSL_HAVE_ATTRIBUTE(no_stack_protector)
#define MOZC_LOUDS_NO_STACK_PROTECTOR __attribute__((no_stack_protector))
#else
#define MOZC_LOUDS_NO_STACK_PROTECTOR
#endif

// The size of a chunk in bytes. The index stores the cumulative number of
// 1-bits at the beginning of each chunk.
constexpr int kChunkSize = 32;

// An iterator adaptor that gives the view of 1-bit index as 0-bit index.
class ZeroBitIndexIterator {
 public:
  using difference_type = ptrdiff_t;
  using value_type = int;
  using pointer = const int*;
  using reference = const int&;
  using iterator_category = std::forward_iterator_tag;

  ZeroBitIndexIterator(absl::Span<const int> index, const int* ptr)
      : data_{index.data()}, ptr_{ptr} {}

  const int* ptr() const { return ptr_; }

  ZeroBitIndexIterator& operator++() {
    ++ptr_;
    return *this;
  }

  friend bool operator!=(const ZeroBitIndexIterator& x,
                         const ZeroBitIndexIterator& y) {
    return x.ptr_ != y.ptr_;
  }

  int operator*() const {
    // The number of 0-bits
    //   = (total num bits) - (1-bits)
    //   = (chunk_size [bytes] * 8 [bits/byte] * (ptr's offset) - (1-bits)
    return kChunkSize * 8 * (ptr_ - data_) - *ptr_;
  }

 private:
  const int* data_;
  const int* ptr_;
};

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
// |kInvert|) of the chunk at |chunk|, without any branch or loop.
//
// REQUIRES: 1 <= n <= the number of such bits in the chunk.
template <bool kInvert>
inline int SelectInChunk(const uint8_t* chunk, int n) {
  static_assert(kChunkSize == 32, "The chunk is loaded as four 64-bit words");
  uint64_t words[4];
  for (int i = 0; i < 4; ++i) {
    const uint64_t w = LoadUnaligned<uint64_t>(chunk + 8 * i);
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
void InitIndex(const uint8_t* data, int length, std::vector<int>* index) {
  DCHECK_EQ(length % 4, 0);

  index->clear();

  // Count the number of chunks with ceiling.
  const int chunk_length = (length + kChunkSize - 1) / kChunkSize;

  // Reserve the memory including a sentinel.
  index->reserve(chunk_length + 1);

  int num_bits = 0;
  for (int remaining_num_words = length / 4; remaining_num_words > 0;
       data += kChunkSize, remaining_num_words -= kChunkSize / 4) {
    index->push_back(num_bits);
    num_bits += Count1Bits(data, std::min(kChunkSize / 4, remaining_num_words));
  }
  index->push_back(num_bits);

  CHECK_EQ(chunk_length + 1, index->size());
}

void InitLowerBound0Cache(absl::Span<const int> index, size_t increment,
                          size_t size, std::vector<const int*>* cache) {
  DCHECK_GT(increment, 0);
  cache->clear();
  cache->reserve(size + 2);
  cache->push_back(index.data());
  for (size_t i = 1; i <= size; ++i) {
    const int target_index = increment * i;
    const int* ptr = std::lower_bound(ZeroBitIndexIterator(index, index.data()),
                                      ZeroBitIndexIterator(
                                          index, index.data() + index.size()),
                                      target_index)
                         .ptr();
    cache->push_back(ptr);
  }
  cache->push_back(index.data() + index.size());
}

void InitLowerBound1Cache(absl::Span<const int> index, size_t increment,
                          size_t size, std::vector<const int*>* cache) {
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
  InitIndex(data, length, &index_);

  // TODO(noriyukit): Currently, we simply use uniform increment width for lower
  // bound cache.  Nonuniform increment width may improve performance.
  lb0_cache_increment_ =
      lb0_cache_size == 0 ? GetNum0Bits() : GetNum0Bits() / lb0_cache_size;
  if (lb0_cache_increment_ == 0) {
    lb0_cache_increment_ = 1;
  }
  InitLowerBound0Cache(index_, lb0_cache_increment_, lb0_cache_size,
                       &lb0_cache_);

  lb1_cache_increment_ =
      lb1_cache_size == 0 ? GetNum1Bits() : GetNum1Bits() / lb1_cache_size;
  if (lb1_cache_increment_ == 0) {
    lb1_cache_increment_ = 1;
  }
  InitLowerBound1Cache(index_, lb1_cache_increment_, lb1_cache_size,
                       &lb1_cache_);
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
  const int num_chunks = n / (kChunkSize * 8);
  int result = index_[num_chunks];

  // Count 1-bits for remaining "words".
  result += Count1Bits(data_ + num_chunks * kChunkSize,
                       (n / 32) - num_chunks * (kChunkSize / 4));

  // Count 1-bits for remaining "bits".
  if (n % 32 > 0) {
    const int offset = 4 * (n / 32);
    const int shift = 32 - n % 32;
    result += std::popcount(LoadUnaligned<uint32_t>(data_ + offset) << shift);
  }

  return result;
}

MOZC_LOUDS_NO_STACK_PROTECTOR
int SimpleSuccinctBitVectorIndex::Select0(int n) const {
  DCHECK_GT(n, 0);

  // Narrow down the range of |index_| on which lower bound is performed.
  int lb0_cache_index = n / lb0_cache_increment_;
  if (lb0_cache_index > lb0_cache_.size() - 2) {
    lb0_cache_index = lb0_cache_.size() - 2;
  }
  DCHECK_GE(lb0_cache_index, 0);

  // Binary search on chunks.
  const int* chunk_ptr =
      std::lower_bound(
          ZeroBitIndexIterator(index_, lb0_cache_[lb0_cache_index]),
          ZeroBitIndexIterator(index_, lb0_cache_[lb0_cache_index + 1]), n)
          .ptr();
  const int chunk_index = (chunk_ptr - index_.data()) - 1;
  DCHECK_GE(chunk_index, 0);
  n -= kChunkSize * 8 * chunk_index - index_[chunk_index];

  const int offset = chunk_index * kChunkSize;
  if (offset + kChunkSize <= length_) {
    return offset * 8 + SelectInChunk<true>(data_ + offset, n);
  }

  // Generic path for a partial last chunk.
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

MOZC_LOUDS_NO_STACK_PROTECTOR
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

  const int offset = chunk_index * kChunkSize;
  if (offset + kChunkSize <= length_) {
    return offset * 8 + SelectInChunk<false>(data_ + offset, n);
  }

  // Generic path for a partial last chunk.
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

#undef MOZC_LOUDS_NO_STACK_PROTECTOR
