/*
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

#include "cripts/Headers.hpp"
#include "cripts/Lulu.hpp"
#include "swoc/IPRange.h"
// Setup for PCRE2
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <vector>
#include <tuple>

#include "ts/ts.h"
#include "cripts/Lulu.hpp"

namespace cripts::Matcher
{
namespace Range
{
  class IP : public swoc::IPRangeSet
  {
    using super_type = swoc::IPRangeSet;
    using self_type  = Range::IP;

  public:
    IP()                              = delete;
    void operator=(const self_type &) = delete;

    explicit IP(cripts::string_view ip) { Add(ip); }

    IP(self_type const &ip)
    {
      for (auto &it : ip) {
        mark(it);
      }
    }

    IP(const std::initializer_list<self_type> &list)
    {
      for (auto &it : list) {
        for (auto &it2 : it) {
          mark(it2);
        }
      }
    }

    IP(std::initializer_list<cripts::string_view> list)
    {
      for (auto &it : list) {
        Add(it);
      }
    }

    bool
    Match(sockaddr const *target) const
    {
      return contains(swoc::IPAddr(target));
    }

    [[nodiscard]] bool
    Match(in_addr_t target) const
    {
      return contains(swoc::IPAddr(target));
    }

    [[nodiscard]] bool
    Match(swoc::IPAddr const &target) const
    {
      return contains(target);
    }

    [[nodiscard]] bool
    Contains(swoc::IPAddr const &target) const
    {
      return contains(target);
    }

    bool
    Contains(sockaddr const *target) const
    {
      return contains(swoc::IPAddr(target));
    }

    [[nodiscard]] bool
    Contains(in_addr_t target) const
    {
      return contains(swoc::IPAddr(target));
    }

    void
    Add(cripts::string_view str)
    {
      if (swoc::IPRange r; r.load(str)) {
        mark(r);
      } else {
        CFatal("[Matcher::Range::IP] Invalid IP range: %.*s", static_cast<int>(str.size()), str.data());
      }
    }

  }; // End class IP
} // namespace Range

namespace List
{
  class Method : public std::vector<cripts::Header::Method>
  {
#undef DELETE // ToDo: macOS shenanigans here, defining DELETE as a macro

    using super_type = std::vector<cripts::Header::Method>;
    using self_type  = Method;

  public:
    Method() = delete;
    explicit Method(cripts::Header::Method method) : std::vector<cripts::Header::Method>() { push_back(method); }
    void operator=(const self_type &) = delete;

    Method(self_type const &method) : std::vector<cripts::Header::Method>() { insert(end(), std::begin(method), std::end(method)); }

    Method(const std::initializer_list<self_type> &list)
    {
      for (auto &it : list) {
        insert(end(), std::begin(it), std::end(it));
      }
    }

    Method(std::initializer_list<cripts::Header::Method> list)
    {
      for (auto &it : list) {
        push_back(it);
      }
    }

    // Make sure we only allow the cripts::Method::* constants

    [[nodiscard]] bool
    Contains(cripts::Header::Method method) const
    {
      auto data = method.Data();

      return end() != std::find_if(begin(), end(), [&](const cripts::Header::Method &header) { return header.Data() == data; });
    }

    [[nodiscard]] bool
    Match(cripts::Header::Method method) const
    {
      return Contains(method);
    }

  }; // End class Method

} // namespace List

class PCRE
{
public:
  static constexpr size_t MAX_CAPTURES = 32;

  using Regex        = std::tuple<cripts::string, pcre2_code *>;
  using RegexEntries = std::vector<Regex>;

  using self_type = PCRE;

  class Result
  {
  public:
    friend class PCRE;

    Result() = delete;
    Result(cripts::string_view subject) : _subject(subject) {}

    ~Result() { pcre2_match_data_free(_data); }

    explicit
    operator bool() const
    {
      return Matched();
    }

    cripts::string_view
    operator[](size_t ix) const
    {
      cripts::string_view ret;

      if ((Count() > ix) && _ovector) {
        ret = {_subject.substr(_ovector[ix * 2], _ovector[ix * 2 + 1] - _ovector[ix * 2])};
      }

      return ret;
    }

    [[nodiscard]] bool
    Matched() const
    {
      return _match != 0;
    }

    [[nodiscard]] RegexEntries::size_type
    MatchIX() const
    {
      return _match;
    }

    [[nodiscard]] uint32_t
    Count() const
    {
      return _data ? pcre2_get_ovector_count(_data) : 0;
    }

    // The allocator for the PCRE2 contexts, which puts the match data etc. on the stack when used appropriately
    static void *malloc(PCRE2_SIZE size, void *context);

  private:
    RegexEntries::size_type _match   = 0; // The index into the regex vector that match, starting at 1 for the first regex
    pcre2_match_data       *_data    = nullptr;
    PCRE2_SIZE             *_ovector = nullptr;
    PCRE2_SIZE              _ctx_ix  = 0;
    std::byte               _ctx_data[24 * 2 + 96 + 16 * MAX_CAPTURES];
    cripts::string_view     _subject;
  };

  PCRE()                            = default;
  PCRE(const self_type &)           = delete;
  void operator=(const self_type &) = delete;

  PCRE(cripts::string_view regex, uint32_t options = 0) { Add(regex, options); }

  PCRE(std::initializer_list<cripts::string_view> list, uint32_t options = 0)
  {
    for (auto &it : list) {
      Add(it, options);
    }
  }

  ~PCRE();

  void   Add(cripts::string_view regex, uint32_t options = 0, bool jit = true);
  Result Contains(cripts::string_view subject, PCRE2_SIZE offset = 0, uint32_t options = 0);

  Result
  Match(cripts::string_view subject, PCRE2_SIZE offset = 0, uint32_t options = 0)
  {
    return Contains(subject, offset, options);
  }

private:
  RegexEntries _regexes;
};

} // namespace cripts::Matcher
