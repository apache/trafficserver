/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

/*****************************************************************************
 *
 *  HostLookup.cc - Implementation of a hostname/domainname matcher
 *
 *
 ****************************************************************************/

#include "tscore/ink_inet.h"
#include "tscore/ink_assert.h"
#include "tscore/HostLookup.h"
#include "tscpp/util/TextView.h"

#include <string_view>
#include <array>
#include <memory>

using std::string_view;
using ts::TextView;

namespace
{
// bool domaincmp(const char* hostname, const char* domain)
//
//    Returns true if hostname is in domain
//
bool
domaincmp(string_view hostname, string_view domain)
{
  // Check to see if were passed emtpy strings for either
  //  argument.  Empty strings do not match anything
  //
  if (domain.empty() || hostname.empty()) {
    return false;
  }

  // Walk through both strings backward - explicit declares, need these post-loop.
  auto d_idx = domain.rbegin();
  auto h_idx = hostname.rbegin();
  // Trailing dots should be ignored since they are optional
  if (*d_idx == '.') {
    ++d_idx;
  }
  if (*h_idx == '.') {
    ++h_idx;
  }
  while (d_idx != domain.rend() && h_idx != hostname.rend()) {
    // If we still have characters left on both strings and they do not match, matching fails
    if (tolower(*d_idx) != tolower(*h_idx)) {
      return false;
    }

    ++d_idx;
    ++h_idx;
  };

  // There are three possible cases that could have gotten us
  //  here
  //
  //     Case 1: we ran out of both strings
  //     Case 2: we ran out of domain but not hostname
  //     Case 3: we ran out of hostname but not domain
  //
  if (d_idx == domain.rend()) {
    // If end of hostname also, then case 1 match.
    // Otherwise it's a case 2 match iff last character match was at a domain boundary.
    // (ex: avoid 'www.inktomi.ecom' matching 'com')
    // note: d_idx[-1] == '.' --> h_idx[-1] == '.' because of match check in loop.
    return h_idx == hostname.rend() || *h_idx == '.' || *(d_idx - 1) == '.';
  } else if (h_idx == hostname.rend()) {
    // This covers the case 3 (a very unusual case)
    //  ex: example.com needing to match .example.com
    return *d_idx == '.' && d_idx + 1 == domain.rend();
  }

  ink_assert(!"Should not get here");
  return false;
}

//   Similar to strcasecmp except that if one string has a
//     trailing '.' and the other one does not
//     then they are equal
//
//   Meant to compare to host names and
//     take in out account that www.example.com
//     and www.example.com. are the same host
//     since the trailing dot is optional
//
int
hostcmp(string_view lhs, string_view rhs)
{
  ink_assert(!lhs.empty());
  ink_assert(!rhs.empty());

  // ignore any trailing .
  if (lhs.back() == '.') {
    lhs.remove_suffix(1);
  }
  if (rhs.back() == '.') {
    rhs.remove_suffix(1);
  }

  auto lidx = lhs.begin();
  auto ridx = rhs.begin();
  while (lidx != lhs.end() && ridx != rhs.end()) {
    char lc(tolower(*lidx));
    char rc(tolower(*ridx));
    if (lc < rc) {
      return -1;
    } else if (lc > rc) {
      return 1;
    }
    ++lidx;
    ++ridx;
  }
  return lidx != lhs.end() ? 1 : ridx != rhs.end() ? -1 : 0;
}

} // namespace

// static const unsigned char asciiToTable[256]
//
//   Used to Map Legal hostname characters into
//     to indexes used by char_table to
//     index strings
//
//   Legal hostname characters are 0-9, A-Z, a-z and '-'
//   '_' is also included although it is not in the spec (RFC 883)
//
//   Uppercase and lowercase "a-z" both map to same indexes
//     since hostnames are not case sensative
//
//   Illegal characters map to 255
//
static const unsigned char asciiToTable[256] = {
  255, 255, 255, 255, 255, 255, 255, 255, // 0 - 7
  255, 255, 255, 255, 255, 255, 255, 255, // 8 - 15
  255, 255, 255, 255, 255, 255, 255, 255, // 16 - 23
  255, 255, 255, 255, 255, 255, 255, 255, // 24 - 31
  255, 255, 255, 255, 255, 255, 255, 255, // 32 - 39
  255, 255, 255, 255, 255, 0,   255, 255, // 40 - 47 ('-')
  1,   2,   3,   4,   5,   6,   7,   8,   // 48 - 55 (0-7)
  9,   10,  255, 255, 255, 255, 255, 255, // 56 - 63 (8-9)
  255, 11,  12,  13,  14,  15,  16,  17,  // 64 - 71 (A-G)
  18,  19,  20,  21,  22,  23,  24,  25,  // 72 - 79 (H-O)
  26,  27,  28,  29,  30,  31,  32,  33,  // 80 - 87 (P-W)
  34,  35,  36,  255, 255, 255, 255, 37,  // 88 - 95 (X-Z, '_')
  255, 11,  12,  13,  14,  15,  16,  17,  // 96 - 103 (a-g)
  18,  19,  20,  21,  22,  23,  24,  25,  // 104 - 111 (h-0)
  26,  27,  28,  29,  30,  31,  32,  33,  // 112 - 119 (p-w)
  34,  35,  36,  255, 255, 255, 255, 255, // 120 - 127 (x-z)
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};

// Number of legal characters in the acssiToTable array
static const int numLegalChars = 38;

// struct CharIndexBlock
//
//   Used by class CharIndex.  Forms a single level in CharIndex tree
//
struct CharIndexBlock {
  struct Item {
    HostBranch *branch{nullptr};
    std::unique_ptr<CharIndexBlock> block;
  };
  std::array<Item, numLegalChars> array;
};

// class CharIndex - A constant time string matcher intended for
//    short strings in a sparsely populated DNS paritition
//
//    Creates a look up table for character in data string
//
//    Mapping from character to table index is done by
//      asciiToTable[]
//
//    The illegalKey hash table is side structure for any
//     entries that contain illegal hostname characters that
//     we can not index into the normal table
//
//    Example: com
//      c maps to 13, o maps to 25, m maps to 23
//
//                             CharIndexBlock         CharIndexBlock
//                             -----------         ------------
//                           0 |    |    |         |    |     |
//                           . |    |    |         |    |     |
//    CharIndexBlock           . |    |    |         |    |     |
//    ----------             . |    |    |         |    |     |
//  0 |   |    |             . |    |    |   |-->23| ptr|  0  |  (ptr is to the
//  . |   |    |   |-------->25| 0  |   -----|     |    |     |   hostBranch for
//  . |   |    |   |         . |    |    |         |    |     |   domain com)
// 13 | 0 |  --|---|         . |    |    |         |    |     |
//  . |   |    |             . |    |    |         |    |     |
//  . |   |    |               |    |    |         |    |     |
//  . |   |    |               |    |    |         |    |     |
//    |   |    |               -----------         -----------|
//    |   |    |
//    |   |    |
//    |   |    |
//    |--------|
//
//
//
class CharIndex
{
public:
  struct iterator : public std::iterator<std::forward_iterator_tag, HostBranch, int> {
    using self_type = iterator;

    struct State {
      int index{-1};
      CharIndexBlock *block{nullptr};
    };

    iterator() { q.reserve(HOST_TABLE_DEPTH * 2); } // was 6, guessing that was twice the table depth.

    value_type *operator->();
    value_type &operator*();
    bool operator==(self_type const &that) const;
    bool operator!=(self_type const &that) const;
    self_type &operator++();

    // Current level.
    int cur_level{-1};

    // Where we got the last element from
    State state;

    //  Queue of the above levels
    std::vector<State> q;

    // internal methods
    self_type &advance();
  };

  ~CharIndex();
  void Insert(string_view match_data, HostBranch *toInsert);
  HostBranch *Lookup(string_view match_data);

  iterator begin();
  iterator end();

private:
  CharIndexBlock root;
  using Table = std::unordered_map<string_view, HostBranch *>;
  std::unique_ptr<Table> illegalKey;
};

CharIndex::~CharIndex()
{
  // clean up the illegal key table.
  if (illegalKey) {
    for (auto spot = illegalKey->begin(), limit = illegalKey->end(); spot != limit; delete &*(spot++))
      ; // empty
  }
}

// void CharIndex::Insert(const char* match_data, HostBranch* toInsert)
//
//   Places a binding for match_data to toInsert into the index
//
void
CharIndex::Insert(string_view match_data, HostBranch *toInsert)
{
  unsigned char index;
  CharIndexBlock *cur = &root;

  ink_assert(!match_data.empty());

  if (std::any_of(match_data.begin(), match_data.end(), [](unsigned char c) { return asciiToTable[c] == 255; })) {
    // Insert into illegals hash table
    if (illegalKey == nullptr) {
      illegalKey.reset(new Table);
    }
    toInsert->key = match_data;
    illegalKey->emplace(match_data, toInsert);
  } else {
    while (true) {
      index = asciiToTable[static_cast<unsigned char>(match_data.front())];

      // Check to see if are at the level we supposed be at
      if (match_data.size() == 1) {
        // The slot should always be emtpy, no duplicate keys are allowed
        ink_assert(cur->array[index].branch == nullptr);
        cur->array[index].branch = toInsert;
        break;
      } else {
        // We need to find the next level in the table

        CharIndexBlock *next = cur->array[index].block.get();

        // Check to see if we need to expand the table
        if (next == nullptr) {
          next = new CharIndexBlock;
          cur->array[index].block.reset(next);
        }
        cur = next;
      }
      match_data.remove_prefix(1);
    }
  }
}

// HostBranch* CharIndex::Lookup(const char* match_data)
//
//  Searches the CharIndex on key match_data
//    If there is a binding for match_data, returns a pointer to it
//    otherwise a nullptr pointer is returned
//
HostBranch *
CharIndex::Lookup(string_view match_data)
{
  if (match_data.empty()) {
    return nullptr;
  }

  if (std::any_of(match_data.begin(), match_data.end(), [](unsigned char c) { return asciiToTable[c] == 255; })) {
    if (illegalKey) {
      auto spot = illegalKey->find(match_data);
      if (spot != illegalKey->end()) {
        return spot->second;
      }
    }
    return nullptr;
  }

  // Invariant: No invalid characters.
  CharIndexBlock *cur = &root;
  while (cur) {
    unsigned char index = asciiToTable[static_cast<unsigned char>(match_data.front())];

    // Check to see if we are looking for the next level or
    //    a HostBranch
    if (match_data.size() == 1) {
      return cur->array[index].branch;
    } else {
      cur = cur->array[index].block.get();
    }
    match_data.remove_prefix(1);
  }
  return nullptr;
}

auto
CharIndex::begin() -> iterator
{
  iterator zret;
  zret.state.block = &root;
  zret.state.index = 0;
  zret.cur_level   = 0;
  if (root.array[0].branch == nullptr) {
    zret.advance();
  }
  return zret;
}

auto
CharIndex::end() -> iterator
{
  return {};
}

auto CharIndex::iterator::operator-> () -> value_type *
{
  ink_assert(state.block != nullptr); // clang!
  return state.block->array[state.index].branch;
}

auto CharIndex::iterator::operator*() -> value_type &
{
  ink_assert(state.block != nullptr); // clang!
  return *(state.block->array[state.index].branch);
}

//
// HostBranch* CharIndex::iter_next(CharIndexIterState* s)
//
//    Finds the next element in the char index and returns
//      a pointer to it.  If there are no more elements, nullptr
//      is returned
//
auto
CharIndex::iterator::advance() -> self_type &
{
  bool check_branch_p{false}; // skip local branch on the first loop.
  do {
    // Check to see if we need to go back up a level
    if (state.index >= numLegalChars) {
      if (cur_level <= 0) {    // No more levels so bail out
        state.block = nullptr; // carefully make this @c equal to the end iterator.
        state.index = -1;
        break;
      } else { // Go back up to a stored level
        state = q[--cur_level];
        ++state.index; // did that one before descending.
      }
    } else if (check_branch_p && state.block->array[state.index].branch != nullptr) {
      //  Note: we check for a branch on this level before a descending a level so that when we come back up
      //  this level will be done with this index.
      break;
    } else if (state.block->array[state.index].block != nullptr) {
      // There is a lower level block to iterate over, store our current state and descend
      q[cur_level++] = state;
      state.block    = state.block->array[state.index].block.get();
      state.index    = 0;
    } else {
      ++state.index;
    }
    check_branch_p = true;
  } while (true);
  return *this;
}

auto
CharIndex::iterator::operator++() -> self_type &
{
  return this->advance();
}

bool
CharIndex::iterator::operator==(const self_type &that) const
{
  return this->state.block == that.state.block && this->state.index == that.state.index;
}

bool
CharIndex::iterator::operator!=(const self_type &that) const
{
  return !(*this == that);
}

// class HostArray
//
//   Is a fixed size array for holding HostBrach*
//   Allows only sequential access to data
//

class HostArray
{
  /// Element of the @c HostArray.
  struct Item {
    HostBranch *branch{nullptr}; ///< Next branch.
    std::string match_data;      ///< Match data for that branch.
  };
  using Array = std::array<Item, HOST_ARRAY_MAX>;

public:
  bool Insert(string_view match_data_in, HostBranch *toInsert);
  HostBranch *Lookup(string_view match_data_in, bool bNotProcess);

  Array::iterator
  begin()
  {
    return array.begin();
  }
  Array::iterator
  end()
  {
    return array.begin() + _size;
  }

private:
  int _size{0}; // number of elements currently in the array
  Array array;
};

// bool HostArray::Insert(const char* match_data_in, HostBranch* toInsert)
//
//    Places toInsert into the array with key match_data if there
//     is adequate space, in which case true is returned
//    If space is inadequate, false is returned and nothing is inserted
//
bool
HostArray::Insert(string_view match_data, HostBranch *toInsert)
{
  if (_size < static_cast<int>(array.size())) {
    array[_size].branch     = toInsert;
    array[_size].match_data = match_data;
    ++_size;
    return true;
  }
  return false;
}

// HostBranch* HostArray::Lookup(const char* match_data_in)
//
//   Looks for key match_data_in.  If a binding is found,
//     returns HostBranch* found to the key, otherwise
//     nullptr is returned
//
HostBranch *
HostArray::Lookup(string_view match_data_in, bool bNotProcess)
{
  HostBranch *r = nullptr;
  string_view pMD;

  for (int i = 0; i < _size; i++) {
    pMD = array[i].match_data;

    if (bNotProcess && '!' == pMD.front()) {
      pMD.remove_prefix(1);
      if (pMD.empty()) {
        continue;
      }

      if (pMD == match_data_in) {
        r = array[i].branch;
      }
    } else if (pMD == match_data_in) {
      r = array[i].branch;
      break;
    }
  }
  return r;
}

// maps enum LeafType to strings
const char *LeafTypeStr[] = {"Leaf Invalid", "Host (Partial)", "Host (Full)", "Domain (Full)", "Domain (Partial)"};

// HostBranch::~HostBranch()
//
//   Recursive delete all host branches below us
//
HostBranch::~HostBranch()
{
  switch (type) {
  case HOST_TERMINAL:
    break;
  case HOST_HASH: {
    HostTable *ht = next_level._table;
    for (auto spot = ht->begin(), limit = ht->end(); spot != limit; delete &*(spot++)) {
    } // empty
    delete ht;
  } break;
  case HOST_INDEX: {
    CharIndex *ci = next_level._index;
    for (auto &branch : *ci) {
      delete &branch;
    }
    delete ci;
  } break;
  case HOST_ARRAY:
    for (auto &item : *next_level._array) {
      delete item.branch;
    }
    delete next_level._array;
    break;
  }
}

HostLookup::HostLookup(string_view name) : matcher_name(name) {}

void
HostLookup::Print()
{
  Print([](void *) -> void {});
}

void
HostLookup::Print(PrintFunc const &f)
{
  PrintHostBranch(&root, f);
}

//
// void HostLookup::PrintHostBranch(HostBranch* hb, HostLookupPrintFunc f)
//
//   Recursively traverse the matching tree rooted at arg hb
//     and print out each element
//
void
HostLookup::PrintHostBranch(HostBranch *hb, PrintFunc const &f)
{
  for (auto curIndex : hb->leaf_indices) {
    auto &leaf{leaf_array[curIndex]};
    printf("\t\t%s for %.*s\n", LeafTypeStr[leaf.type], static_cast<int>(leaf.match.size()), leaf.match.data());
    f(leaf_array[curIndex].opaque_data);
  }

  switch (hb->type) {
  case HostBranch::HOST_TERMINAL:
    ink_assert(hb->next_level._ptr == nullptr);
    break;
  case HostBranch::HOST_HASH:
    for (auto &branch : *(hb->next_level._table)) {
      PrintHostBranch(branch.second, f);
    }
    break;
  case HostBranch::HOST_INDEX:
    for (auto &branch : *(hb->next_level._index)) {
      PrintHostBranch(&branch, f);
    }
    break;
  case HostBranch::HOST_ARRAY:
    for (auto &item : *(hb->next_level._array)) {
      PrintHostBranch(item.branch, f);
    }
    break;
  }
}

//
// HostBranch* HostLookup::TableNewLevel(HostBranch* from, const char* level_data)
//
//    Creates the next level structure in arg from
//       Creates a HostBranch for level_data, inserts it in to the
//         the next_level structure and returns a pointer to the new
//         HostBranch
//
HostBranch *
HostLookup::TableNewLevel(HostBranch *from, string_view level_data)
{
  ink_assert(from->type == HostBranch::HOST_TERMINAL);

  // Use the CharIndex for high speed matching at the first level of
  //   the table.  The first level is short strings, ie: com, edu, jp, fr
  if (from->level_idx == 0) {
    from->type              = HostBranch::HOST_INDEX;
    from->next_level._index = new CharIndex;
  } else {
    from->type              = HostBranch::HOST_ARRAY;
    from->next_level._array = new HostArray;
  }

  return InsertBranch(from, level_data);
}

//
// HostBranch* HostLookup::InsertBranch(HostBranch* insert_to, const char* level_data)
//
//
//    Abstrction to place a new node for level_data below node
//      insert to.  Inserts into any of the data types used by
//      by class HostMatcher
//
HostBranch *
HostLookup::InsertBranch(HostBranch *insert_in, string_view level_data)
{
  HostBranch *new_branch = new HostBranch;
  new_branch->key        = level_data;
  new_branch->type       = HostBranch::HOST_TERMINAL;
  new_branch->level_idx  = insert_in->level_idx + 1;

  switch (insert_in->type) {
  case HostBranch::HOST_TERMINAL:
    // Should not happen
    ink_release_assert(0);
    break;
  case HostBranch::HOST_HASH:
    insert_in->next_level._table->emplace(level_data, new_branch);
    break;
  case HostBranch::HOST_INDEX:
    insert_in->next_level._index->Insert(level_data, new_branch);
    break;
  case HostBranch::HOST_ARRAY: {
    auto array = insert_in->next_level._array;
    if (array->Insert(level_data, new_branch) == false) {
      // The array is out of space, time to move to a hash table
      auto ha = insert_in->next_level._array;
      auto ht = new HostTable;
      ht->emplace(level_data, new_branch);
      for (auto &item : *array) {
        ht->emplace(item.match_data, item.branch);
      }
      // Ring out the old, ring in the new
      delete ha;
      insert_in->next_level._table = ht;
      insert_in->type              = HostBranch::HOST_HASH;
    }
  } break;
  }

  return new_branch;
}

// HostBranch* HostLookup::FindNextLevel(HostBranch* from,
//                                       const char* level_data)
//
//   Searches in the branch for  the next level down in the search
//     structure bound to level data
//   If found returns a pointer to the next level HostBranch,
//    otherwise returns nullptr
//
HostBranch *
HostLookup::FindNextLevel(HostBranch *from, string_view level_data, bool bNotProcess)
{
  HostBranch *r = nullptr;

  switch (from->type) {
  case HostBranch::HOST_TERMINAL:
    // Should not happen
    ink_assert(0);
    break;
  case HostBranch::HOST_HASH: {
    auto table = from->next_level._table;
    auto spot  = table->find(level_data);
    r          = spot == table->end() ? nullptr : spot->second;
  } break;
  case HostBranch::HOST_INDEX:
    r = from->next_level._index->Lookup(level_data);
    break;
  case HostBranch::HOST_ARRAY:
    r = from->next_level._array->Lookup(level_data, bNotProcess);
    break;
  }
  return r;
}

// void HostLookup::TableInsert(const char* match_data, int index)
//
//   Insert the match data, index pair into domain/search table
//
//   arg index is the index into both Data and HostLeaf array for
//    the elements corresponding to match_data
//
void
HostLookup::TableInsert(string_view match_data, int index, bool domain_record)
{
  HostBranch *cur = &root;
  HostBranch *next;
  TextView match{match_data};

  // Traverse down the search structure until we either
  //       Get beyond the fixed number depth of the host table
  //  OR   We reach the level where the match stops
  //
  for (int i = 0; !match.rtrim('.').empty() && i < HOST_TABLE_DEPTH; ++i) {
    TextView token{match.take_suffix_at('.')};

    if (cur->next_level._ptr == nullptr) {
      cur = TableNewLevel(cur, token);
    } else {
      next = FindNextLevel(cur, token);
      if (next == nullptr) {
        cur = InsertBranch(cur, token);
      } else {
        cur = next;
      }
    }
  }

  // Update the leaf type.  There are three types:
  //     HOST_PARTIAL - Indicates that part of the hostname name
  //         was not matched by traversin the search structure since
  //         it had too elements.  A comparison must be done at the
  //         leaf node to make sure we have a match
  //     HOST_COMPLETE - Indicates that the entire domain name
  //         in the match_data was matched by traversing the search
  //         structure, no further comparison is necessary
  //
  //     DOMAIN_COMPLETE - Indicates that the entire domain name
  //         in the match_data was matched by traversing the search
  //         structure, no further comparison is necessary
  //     DOMAIN_PARTIAL - Indicates that part of the domain name
  //         was not matched by traversin the search structure since
  //         it had too elements.  A comparison must be done at the
  //         leaf node to make sure we have a match
  if (domain_record == false) {
    if (match.empty()) {
      leaf_array[index].type = HostLeaf::HOST_PARTIAL;
    } else {
      leaf_array[index].type = HostLeaf::HOST_COMPLETE;
    }
  } else {
    if (match.empty()) {
      leaf_array[index].type = HostLeaf::DOMAIN_PARTIAL;
    } else {
      leaf_array[index].type = HostLeaf::DOMAIN_COMPLETE;
    }
  }

  // Place the index in to leaf array into the match list for this
  //   HOST_BRANCH
  cur->leaf_indices.push_back(index);
}

// bool HostLookup::MatchArray(HostLookupState* s, void**opaque_ptr, vector<int>& array,
//                             bool host_done)
//
//  Helper function to iterate through arg array and update Result for each element in arg array
//
//  host_done should be passed as true if this call represents the all fields in the matched against hostname being
//  consumed.  Example: for www.example.com this would be true for the call matching against the "www", but neither of
//  the prior two fields, "inktomi" and "com"
//

bool
HostLookup::MatchArray(HostLookupState *s, void **opaque_ptr, LeafIndices &array, bool host_done)
{
  size_t i;

  for (i = s->array_index + 1; i < array.size(); ++i) {
    auto &leaf{leaf_array[array[i]]};

    switch (leaf.type) {
    case HostLeaf::HOST_PARTIAL:
      if (hostcmp(s->hostname, leaf.match) == 0) {
        *opaque_ptr    = leaf.opaque_data;
        s->array_index = i;
        return true;
      }
      break;
    case HostLeaf::HOST_COMPLETE:
      // We have to have consumed the whole hostname for this to match
      //   so that we do not match a rule for "example.com" to
      //   "www.example.com
      //
      if (host_done == true) {
        *opaque_ptr    = leaf.opaque_data;
        s->array_index = i;
        return true;
      }
      break;
    case HostLeaf::DOMAIN_PARTIAL:
      if (domaincmp(s->hostname, leaf.match) == false) {
        break;
      }
    // FALL THROUGH
    case HostLeaf::DOMAIN_COMPLETE:
      *opaque_ptr    = leaf.opaque_data;
      s->array_index = i;
      return true;
    case HostLeaf::LEAF_INVALID:
      // Should not get here
      ink_assert(0);
      break;
    }
  }

  s->array_index = i;
  return false;
}

// bool HostLookup::MatchFirst(const char* host, HostLookupState* s, void** opaque_ptr)
//
//
bool
HostLookup::MatchFirst(string_view host, HostLookupState *s, void **opaque_ptr)
{
  s->cur           = &root;
  s->table_level   = 0;
  s->array_index   = -1;
  s->hostname      = host;
  s->hostname_stub = s->hostname;

  return MatchNext(s, opaque_ptr);
}

// bool HostLookup::MatchNext(HostLookupState* s, void** opaque_ptr)
//
//  Searches our tree and updates argresult for each element matching
//    arg hostname
//
bool
HostLookup::MatchNext(HostLookupState *s, void **opaque_ptr)
{
  HostBranch *cur = s->cur;

  // Check to see if there is any work to be done
  if (leaf_array.size() <= 0) {
    return false;
  }

  while (s->table_level <= HOST_TABLE_DEPTH) {
    if (MatchArray(s, opaque_ptr, cur->leaf_indices, s->hostname_stub.empty())) {
      return true;
    }
    // Check to see if we run out of tokens in the hostname
    if (s->hostname_stub.empty()) {
      break;
    }
    // Check to see if there are any lower levels
    if (cur->type == HostBranch::HOST_TERMINAL) {
      break;
    }

    string_view token{TextView{s->hostname_stub}.suffix('.')};
    s->hostname_stub.remove_suffix(std::min(s->hostname_stub.size(), token.size() + 1));
    cur = FindNextLevel(cur, token, true);

    if (cur == nullptr) {
      break;
    } else {
      s->cur         = cur;
      s->array_index = -1;
      ++(s->table_level);
    }
  }

  return false;
}

// void HostLookup::AllocateSpace(int num_entries)
//
//   Allocate the leaf array structure
//
void
HostLookup::AllocateSpace(int num_entries)
{
  leaf_array.reserve(num_entries);
}

// void HostLookup::NewEntry(const char* match_data, bool domain_record, void* opaque_data_in)
//
//   Insert a new element in to the table
//
void
HostLookup::NewEntry(string_view match_data, bool domain_record, void *opaque_data_in)
{
  leaf_array.emplace_back(match_data, opaque_data_in);
  TableInsert(match_data, leaf_array.size() - 1, domain_record);
}
