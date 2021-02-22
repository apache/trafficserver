/** @file

  SSL client certificate verification plugin, header file.

  Checks for specificate names in the client provided certificate and
  fails the handshake if none of the good names are present

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

#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <utility>
#include <algorithm>

#include <pcre.h>

#include <tscore/HashFNV.h>

#if defined(CLIENT_ALLOW_LIST_UNIT_TEST)

struct ClientAllowListUTException {
};

#else

#include <ts/ts.h>

#endif

#include <string_view>

#define PN "client_allow_list"

namespace client_allow_list_plugin
{
struct cname_matcher {
  std::string _CName;
  pcre *_compiled_RE = nullptr;

  cname_matcher() = default;

  // No copying.
  //
  cname_matcher(cname_matcher const &) = delete;
  cname_matcher &operator=(cname_matcher const &) = delete;

  // Must be moveable to be able to make a vector of these.
  //
  cname_matcher(cname_matcher &&that)
  {
    _CName            = std::move(that._CName);
    _compiled_RE      = that._compiled_RE;
    that._compiled_RE = nullptr;
  }
  cname_matcher &
  operator=(cname_matcher &&that)
  {
    this->~cname_matcher();
    ::new (this) cname_matcher(std::move(that));
    return *this;
  }

  ~cname_matcher()
  {
    if (_compiled_RE != nullptr) {
      pcre_free(_compiled_RE);
    }
  }
};

// Matchers for cert subject/associated names.
//
extern std::vector<cname_matcher> matcher;

// Indexes into matcher vector of matchers to use if there is no list of matchers to use specific to the
// SNI server name.
//
extern std::vector<unsigned> other_matcher_idxs;

// Indexes into matcher vector of matchers to use if there is no list of matchers to use specific to the
// SNI server name.
//
extern std::vector<unsigned> none_matcher_idxs;

// Lookup table from C strings to vectors of unsigned.  Comparison of string keys is case-insensitive.
//
class MapCStrToUVec
{
public:
  MapCStrToUVec() {}

  // Adds a new entry.  Saves a copy of the key string, returns a pointer to the new, empty vector.
  // Returns nullptr if there is already an entry for key.
  //
  std::vector<unsigned> *add(std::string_view key);

  // Returns nullptr if no entry with given key.
  //
  std::vector<unsigned> const *find(char const *key);

  std::size_t
  size() const
  {
    return _map.size();
  }

  ~MapCStrToUVec();

  // No copying.
  //
  MapCStrToUVec(MapCStrToUVec const &) = delete;
  MapCStrToUVec &operator=(MapCStrToUVec const &) = delete;

private:
  struct _Hash {
    std::size_t operator()(char const *key) const;
  };

  struct _Eq {
    bool operator()(char const *lhs, char const *rhs) const;
  };

  std::unordered_map<char const *, std::vector<unsigned>, _Hash, _Eq> _map;
};

inline std::size_t
MapCStrToUVec::_Hash::operator()(char const *key) const
{
  static_assert(sizeof(std::size_t) >= sizeof(std::uint32_t), "size_t must have at least 32 bits of precision");

  ATSHash32FNV1a h;

  char c;

  while (*key) {
    c = std::tolower(*key);
    h.update(&c, 1);
    ++key;
  }
  // ATSHash32FNV1a::final() does nothing.
  return (h.get());
}

inline bool
MapCStrToUVec::_Eq::operator()(char const *lhs, char const *rhs) const
{
  // Check if two C-strings are equal, ignoring case differences.

  while (*lhs && *rhs) {
    if (std::tolower(*lhs) != std::tolower(*rhs)) {
      return false;
    }
    ++lhs;
    ++rhs;
  }
  return !(*lhs || *rhs);
}

inline std::vector<unsigned> *
MapCStrToUVec::add(std::string_view key)
{
  // Copy key into a heap buffer, forcing it to lower case.
  //
  char *key_ = new char[key.size() + 1];
  std::transform(key.begin(), key.end(), key_, [](char c) -> int { return std::tolower(c); });
  key_[key.size()] = '\0';

  auto result = _map.emplace(key_, std::vector<unsigned>());

  if (!result.second) {
    // Entry with this key already exists.
    //
    delete[] key_;
    return nullptr;
  }

  return &(result.first->second);
}

inline std::vector<unsigned> const *
MapCStrToUVec::find(char const *key)
{
  auto result = _map.find(key);

  if (_map.end() == result) {
    return nullptr;
  }

  return &(result->second);
}

// Mapping from SNI server names to vector of indexes into vector of cert subject/associated name matchers.
// This does not need mutex protection, because only the find() member is called when there are multiple
// threads running.
//
extern MapCStrToUVec sname_to_matcher_idxs;

// Returns true if the given cert name matches any of the matchers, specified by a vector of indexes into
// the 'matcher' vector.  Names configured by the user are allow to have wildcards on them,
// ie: *.foo.com, bar.foo.*
//
bool check_name(std::vector<unsigned> const &matcher_idxs, std::string_view name);

class Init
{
public:
  Init() { _insert_tmp.resize(128); };

  // Do initialization based on plugin arguments.
  //
  void operator()(int n_args, char const *const *arg);

  // No copying.
  //
  Init(Init const &) = delete;
  Init &operator=(Init const &) = delete;

private:
  // Map from name pattern to index into its entry in 'matcher' vector.
  //
  std::unordered_map<std::string_view, unsigned> _name_to_idx_map;

  // Temporary string buffer for reformatting name patterns for PCRE compilation.
  //
  std::vector<char> _insert_tmp;

  // Add new name patterns to matcher vector, and then add index of pattern in matcher vector to (initially
  // empty) matcher_idxs array.  Make sure name patterns don't appear more than once in vector.
  //
  class _Populator
  {
  public:
    _Populator(Init *init) : _init(init) { _idx_present_flag.resize(matcher.size()); }

    // Add name pattern, Return false on error.
    //
    bool add_cert_name(std::string_view name);

    std::vector<unsigned> matcher_idxs;

  private:
    Init *_init;

    // _idx_present_flag[idx] is true if idx is an element in matcher_idxs.
    //
    std::vector<bool> _idx_present_flag;
  };

  // Process list of cert names.
  //
  void _process_name_args(int n_names, char const *const *name);

  // Process YAML config file.
  //
  void _yaml_process(char const *config_filespec);
};

} // end namespace client_allow_list_plugin
