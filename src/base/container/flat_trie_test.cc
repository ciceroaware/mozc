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

#include "base/container/flat_trie.h"

#include <array>
#include <cstddef>
#include <utility>

#include "absl/strings/string_view.h"
#include "testing/gunit.h"

namespace mozc {
namespace {

constexpr auto kTestEntries = std::to_array<std::pair<absl::string_view, int>>({
    {"abc", 1},
    {"abd", 2},
    {"a", 3},
    {"データ", 4},
    {"データベース", 5},
});

constexpr auto kTrie = CreateFlatTrie<kTestEntries>();

// The trie is usable in constant expressions.
static_assert([] {
  int data = 0;
  return kTrie.LookUp("abc", &data) && data == 1;
}());
static_assert([] {
  int data = 0;
  size_t key_length = 0;
  return kTrie.LongestMatch("データベースの", &data, &key_length) &&
         data == 5 && key_length == absl::string_view("データベース").size();
}());

// CreateFlatTrie only accepts an array of std::pair<absl::string_view, T>.
template <const auto& entries>
concept CanCreateFlatTrie = requires { CreateFlatTrie<entries>(); };

constexpr std::array<int, 3> kNotEntries = {1, 2, 3};
static_assert(CanCreateFlatTrie<kTestEntries>);
static_assert(!CanCreateFlatTrie<kNotEntries>);

TEST(FlatTrieTest, LookUp) {
  int data = 0;
  EXPECT_TRUE(kTrie.LookUp("abc", &data));
  EXPECT_EQ(data, 1);
  EXPECT_TRUE(kTrie.LookUp("abd", &data));
  EXPECT_EQ(data, 2);
  EXPECT_TRUE(kTrie.LookUp("a", &data));
  EXPECT_EQ(data, 3);
  EXPECT_TRUE(kTrie.LookUp("データ", &data));
  EXPECT_EQ(data, 4);
  EXPECT_TRUE(kTrie.LookUp("データベース", &data));
  EXPECT_EQ(data, 5);

  EXPECT_FALSE(kTrie.LookUp("", &data));
  EXPECT_FALSE(kTrie.LookUp("ab", &data));
  EXPECT_FALSE(kTrie.LookUp("abcd", &data));
  EXPECT_FALSE(kTrie.LookUp("b", &data));
  EXPECT_FALSE(kTrie.LookUp("データベー", &data));
}

TEST(FlatTrieTest, LongestMatch) {
  int data = 0;
  size_t key_length = 0;

  // Matches in exact.
  EXPECT_TRUE(kTrie.LongestMatch("abc", &data, &key_length));
  EXPECT_EQ(data, 1);
  EXPECT_EQ(key_length, 3);

  // Matches in prefix.
  EXPECT_TRUE(kTrie.LongestMatch("abcd", &data, &key_length));
  EXPECT_EQ(data, 1);
  EXPECT_EQ(key_length, 3);

  // Matches in prefix by 'a'.
  EXPECT_TRUE(kTrie.LongestMatch("abe", &data, &key_length));
  EXPECT_EQ(data, 3);
  EXPECT_EQ(key_length, 1);

  EXPECT_TRUE(kTrie.LongestMatch("ac", &data, &key_length));
  EXPECT_EQ(data, 3);
  EXPECT_EQ(key_length, 1);

  // The longest entry wins.
  EXPECT_TRUE(kTrie.LongestMatch("データベースの", &data, &key_length));
  EXPECT_EQ(data, 5);
  EXPECT_EQ(key_length, absl::string_view("データベース").size());

  EXPECT_TRUE(kTrie.LongestMatch("データの", &data, &key_length));
  EXPECT_EQ(data, 4);
  EXPECT_EQ(key_length, absl::string_view("データ").size());

  // A key that diverges in the middle of a multi-byte character still
  // matches the shorter entry.
  EXPECT_TRUE(kTrie.LongestMatch("データ\xEF\xBE\x9E", &data, &key_length));
  EXPECT_EQ(data, 4);
  EXPECT_EQ(key_length, absl::string_view("データ").size());

  EXPECT_FALSE(kTrie.LongestMatch("", &data, &key_length));
  EXPECT_EQ(key_length, 0);
  EXPECT_FALSE(kTrie.LongestMatch("b", &data, &key_length));
  EXPECT_EQ(key_length, 0);
  EXPECT_FALSE(kTrie.LongestMatch("デー", &data, &key_length));
  EXPECT_EQ(key_length, 0);
}

constexpr auto kStringEntries =
    std::to_array<std::pair<absl::string_view, absl::string_view>>({
        {"まん", "万"},
        {"おく", "億"},
    });

TEST(FlatTrieTest, ValueWithStringView) {
  // Entry values holding string_views to string literals also work.
  constexpr auto trie = CreateFlatTrie<kStringEntries>();

  absl::string_view data;
  size_t key_length = 0;
  EXPECT_TRUE(trie.LongestMatch("まんいち", &data, &key_length));
  EXPECT_EQ(data, "万");
  EXPECT_EQ(key_length, absl::string_view("まん").size());
}

}  // namespace
}  // namespace mozc
