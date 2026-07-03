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

#ifndef MOZC_BASE_CONTAINER_FLAT_TRIE_H_
#define MOZC_BASE_CONTAINER_FLAT_TRIE_H_

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "absl/strings/string_view.h"

namespace mozc {
namespace flat_trie_internal {

template <typename A>
inline constexpr bool kIsEntryArray = false;

template <typename T, size_t N>
inline constexpr bool
    kIsEntryArray<std::array<std::pair<absl::string_view, T>, N>> = true;

// Matches std::array<std::pair<absl::string_view, T>, N>.
template <typename A>
concept EntryArray = kIsEntryArray<std::remove_cvref_t<A>>;

// Returns the number of trie nodes (excluding the root node) required to
// store `entries`, i.e. the number of distinct non-empty byte prefixes of
// the keys.
template <typename T, size_t N>
constexpr size_t CountNodes(
    const std::array<std::pair<absl::string_view, T>, N>& entries) {
  size_t num_nodes = 0;
  for (size_t i = 0; i < N; ++i) {
    const absl::string_view key = entries[i].first;
    // Prefixes shared with any preceding key are already counted.
    size_t max_common = 0;
    for (size_t j = 0; j < i; ++j) {
      const absl::string_view prev = entries[j].first;
      size_t common = 0;
      while (common < key.size() && common < prev.size() &&
             key[common] == prev[common]) {
        ++common;
      }
      max_common = std::max(max_common, common);
    }
    num_nodes += key.size() - max_common;
  }
  return num_nodes;
}

}  // namespace flat_trie_internal

// Read-only trie that is backed by a `constexpr`-built node array. Unlike
// Trie<T>, instances can be declared `constexpr` (or `constinit`), so the
// whole data structure is placed in the read-only data section of the binary;
// neither heap allocation nor dynamic initialization happens at runtime.
//
// T must be a literal type that is default-constructible and copyable in
// constant expressions (e.g. built-in types, absl::string_view, and
// aggregates of them).
//
// Keys are matched byte-wise, so the lookup results are equivalent to
// Trie<T>, which matches keys by Unicode characters, as long as the keys are
// well-formed UTF-8 strings.
//
// Consider calling `CreateFlatTrie` instead of the constructor, so you don't
// have to manually specify the number of nodes, `kNumNodes`.
template <typename T, size_t kNumNodes>
class FlatTrie final {
 public:
  template <size_t N>
  consteval explicit FlatTrie(
      const std::array<std::pair<absl::string_view, T>, N>& entries) {
    for (const auto& [key, data] : entries) {
      AddEntry(key, data);
    }
  }

  constexpr bool LookUp(absl::string_view key, T* data) const {
    int32_t node = 0;  // root
    for (const char c : key) {
      node = FindChild(node, c);
      if (node < 0) {
        return false;
      }
    }
    if (!nodes_[node].has_data) {
      return false;
    }
    *data = nodes_[node].data;
    return true;
  }

  // If a prefix of `key` matches an entry, returns true and sets `data` and
  // `key_length` for the longest such entry.
  // For example, if a trie has data for 'abc', 'abd', and 'a';
  //  - Return true for the key, 'abc'
  //  -- Matches in exact
  //  - Return true for the key, 'abcd'
  //  -- Matches in prefix
  //  - Return TRUE for the key, 'abe'
  //  -- Matches in prefix by 'a'.
  //  - Return true for the key, 'ac'
  //  -- Matches in prefix by 'a', and 'a' have data
  constexpr bool LongestMatch(absl::string_view key, T* data,
                              size_t* key_length) const {
    *key_length = 0;
    bool found = false;
    int32_t node = 0;  // root
    for (size_t i = 0; i < key.size(); ++i) {
      node = FindChild(node, key[i]);
      if (node < 0) {
        break;
      }
      if (nodes_[node].has_data) {
        *data = nodes_[node].data;
        *key_length = i + 1;
        found = true;
      }
    }
    return found;
  }

 private:
  struct Node {
    char key = 0;
    // Indices into `nodes_`, or -1 for none.
    int32_t first_child = -1;
    int32_t next_sibling = -1;
    bool has_data = false;
    T data{};
  };

  consteval void AddEntry(absl::string_view key, const T& data) {
    int32_t node = 0;  // root
    for (const char c : key) {
      int32_t child = FindChild(node, c);
      if (child < 0) {
        // Index access to `nodes_` fails the constant evaluation (i.e. the
        // build) if kNumNodes is smaller than the required number of nodes.
        child = static_cast<int32_t>(size_++);
        nodes_[child].key = c;
        nodes_[child].next_sibling = nodes_[node].first_child;
        nodes_[node].first_child = child;
      }
      node = child;
    }
    nodes_[node].has_data = true;
    nodes_[node].data = data;
  }

  constexpr int32_t FindChild(int32_t node, char c) const {
    for (int32_t child = nodes_[node].first_child; child >= 0;
         child = nodes_[child].next_sibling) {
      if (nodes_[child].key == c) {
        return child;
      }
    }
    return -1;
  }

  // nodes_[0] is the root node, which corresponds to the empty key.
  std::array<Node, kNumNodes + 1> nodes_{};
  size_t size_ = 1;
};

// Creates a `FlatTrie` from a `constexpr` array of key-value pairs with the
// number of nodes deduced.
//
// Unlike `CreateFlatSet` and `CreateFlatMap`, the entries are passed as a
// template argument because the number of trie nodes depends on the contents
// of the keys, which a function argument cannot provide in constant
// expressions. `entries` must be a constexpr
// std::array<std::pair<absl::string_view, T>> with static storage duration:
// the array type is enforced by the requires-clause below, the static
// storage duration by the reference template parameter, and the
// constexpr-ness by the consteval evaluation.
//
// Example:
//
//   constexpr auto kEntries =
//       std::to_array<std::pair<absl::string_view, int>>({
//           {"one", 1},
//           {"two", 2},
//           {"three", 3},
//       });
//   constexpr auto kTrie = CreateFlatTrie<kEntries>();
template <const auto& entries>
  requires flat_trie_internal::EntryArray<decltype(entries)>
consteval auto CreateFlatTrie() {
  using T =
      typename std::remove_cvref_t<decltype(entries)>::value_type::second_type;
  return FlatTrie<T, flat_trie_internal::CountNodes(entries)>(entries);
}

}  // namespace mozc

#endif  // MOZC_BASE_CONTAINER_FLAT_TRIE_H_
