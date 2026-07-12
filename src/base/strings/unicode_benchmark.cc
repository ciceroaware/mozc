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

// Micro benchmarks for the UTF-8 routines in base/strings: CharsLen and the
// internal Decode and Encode. The corpora are deterministic so that results
// are comparable across binaries, making before/after comparisons of
// optimizations meaningful.
//
// Usage:
//   bazelisk run -c opt --config oss_windows //base/strings:unicode_benchmark

#include <cstddef>
#include <cstdint>
#include <string>

#include "absl/strings/string_view.h"
#include "base/strings/internal/utf8_internal.h"
#include "base/strings/unicode.h"
#include "benchmark/benchmark.h"

namespace mozc {
namespace {

// Repeats unit until the result is at least min_bytes long.
std::string MakeCorpus(const absl::string_view unit, const size_t min_bytes) {
  std::string result;
  while (result.size() < min_bytes) {
    result += unit;
  }
  return result;
}

constexpr absl::string_view kAsciiUnit =
    "The quick brown fox jumps over the lazy dog. ";
constexpr absl::string_view kHiraganaUnit = "こんにちは、せかいのみなさん。";
constexpr absl::string_view kKanjiUnit = "日本語入力の変換精度と応答速度。";
constexpr absl::string_view kMixedUnit =
    "Mozcは日本語入力システムです。Google日本語入力のOSS版として2010年に公開"
    "されました。";
constexpr absl::string_view kEmojiUnit = "😀🎉🚀🍣👍🗾🎌";

// Short: one unit, resembles typical Mozc strings (tens of bytes).
// Long: ~8 KiB, shows asymptotic behavior (e.g. vectorization).
constexpr size_t kShortBytes = 1;
constexpr size_t kLongBytes = 8192;

void BM_CharsLen(benchmark::State& state, const absl::string_view unit,
                 const size_t min_bytes) {
  const std::string corpus = MakeCorpus(unit, min_bytes);
  const absl::string_view sv = corpus;
  for (auto _ : state) {
    benchmark::DoNotOptimize(sv);
    size_t len = strings::CharsLen(sv);
    benchmark::DoNotOptimize(len);
  }
  state.SetBytesProcessed(state.iterations() * corpus.size());
}

// Decodes every character in the corpus and consumes the code points.
void BM_Decode(benchmark::State& state, const absl::string_view unit,
               const size_t min_bytes) {
  const std::string corpus = MakeCorpus(unit, min_bytes);
  const absl::string_view sv = corpus;
  for (auto _ : state) {
    benchmark::DoNotOptimize(sv);
    uint64_t sum = 0;
    const char* const last = sv.data() + sv.size();
    for (const char* ptr = sv.data(); ptr != last;) {
      const utf8_internal::DecodeResult dr = utf8_internal::Decode(ptr, last);
      sum += dr.code_point();
      ptr += dr.bytes_seen();
    }
    benchmark::DoNotOptimize(sum);
  }
  state.SetBytesProcessed(state.iterations() * corpus.size());
}

// Encodes every code point in the corpus and consumes the UTF-8 bytes.
void BM_Encode(benchmark::State& state, const absl::string_view unit) {
  const std::u32string s32 = strings::Utf8ToUtf32(MakeCorpus(unit, kLongBytes));
  for (auto _ : state) {
    benchmark::DoNotOptimize(s32.data());
    uint64_t sum = 0;
    for (const char32_t cp : s32) {
      const utf8_internal::EncodeResult er = utf8_internal::Encode(cp);
      sum += er.size() + static_cast<uint8_t>(er.data()[0]);
    }
    benchmark::DoNotOptimize(sum);
  }
  state.SetItemsProcessed(state.iterations() * s32.size());
}

BENCHMARK_CAPTURE(BM_CharsLen, ascii_short, kAsciiUnit, kShortBytes);
BENCHMARK_CAPTURE(BM_CharsLen, ascii_long, kAsciiUnit, kLongBytes);
BENCHMARK_CAPTURE(BM_CharsLen, hiragana_short, kHiraganaUnit, kShortBytes);
BENCHMARK_CAPTURE(BM_CharsLen, hiragana_long, kHiraganaUnit, kLongBytes);
BENCHMARK_CAPTURE(BM_CharsLen, kanji_short, kKanjiUnit, kShortBytes);
BENCHMARK_CAPTURE(BM_CharsLen, kanji_long, kKanjiUnit, kLongBytes);
BENCHMARK_CAPTURE(BM_CharsLen, mixed_short, kMixedUnit, kShortBytes);
BENCHMARK_CAPTURE(BM_CharsLen, mixed_long, kMixedUnit, kLongBytes);
BENCHMARK_CAPTURE(BM_CharsLen, emoji_short, kEmojiUnit, kShortBytes);
BENCHMARK_CAPTURE(BM_CharsLen, emoji_long, kEmojiUnit, kLongBytes);

BENCHMARK_CAPTURE(BM_Decode, ascii_short, kAsciiUnit, kShortBytes);
BENCHMARK_CAPTURE(BM_Decode, ascii_long, kAsciiUnit, kLongBytes);
BENCHMARK_CAPTURE(BM_Decode, hiragana_short, kHiraganaUnit, kShortBytes);
BENCHMARK_CAPTURE(BM_Decode, hiragana_long, kHiraganaUnit, kLongBytes);
BENCHMARK_CAPTURE(BM_Decode, kanji_short, kKanjiUnit, kShortBytes);
BENCHMARK_CAPTURE(BM_Decode, kanji_long, kKanjiUnit, kLongBytes);
BENCHMARK_CAPTURE(BM_Decode, mixed_short, kMixedUnit, kShortBytes);
BENCHMARK_CAPTURE(BM_Decode, mixed_long, kMixedUnit, kLongBytes);
BENCHMARK_CAPTURE(BM_Decode, emoji_short, kEmojiUnit, kShortBytes);
BENCHMARK_CAPTURE(BM_Decode, emoji_long, kEmojiUnit, kLongBytes);

BENCHMARK_CAPTURE(BM_Encode, ascii, kAsciiUnit);
BENCHMARK_CAPTURE(BM_Encode, hiragana, kHiraganaUnit);
BENCHMARK_CAPTURE(BM_Encode, kanji, kKanjiUnit);
BENCHMARK_CAPTURE(BM_Encode, mixed, kMixedUnit);
BENCHMARK_CAPTURE(BM_Encode, emoji, kEmojiUnit);

}  // namespace
}  // namespace mozc
