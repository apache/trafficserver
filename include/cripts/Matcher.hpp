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

namespace Matcher
{
namespace Range
{
  class IP : public swoc::IPRangeSet
  {
    using super_type = swoc::IPRangeSet;
    using self_type  = Range::IP;

  public:
    IP() = delete;
    explicit IP(Cript::string_view ip) { add(ip); }
    void operator=(const IP &) = delete;

    IP(IP const &ip)
    {
      for (auto &it : ip) {
        mark(it);
      }
    }

    IP(const std::initializer_list<IP> &list)
    {
      for (auto &it : list) {
        for (auto &it2 : it) {
          mark(it2);
        }
      }
    }

    IP(std::initializer_list<Cript::string_view> list)
    {
      for (auto &it : list) {
        add(it);
      }
    }

    bool
    match(sockaddr const *target, void **ptr) const
    {
      return contains(swoc::IPAddr(target));
    }

    bool
    match(in_addr_t target, void **ptr) const
    {
      return contains(swoc::IPAddr(target));
    }

    void
    add(Cript::string_view str)
    {
      if (swoc::IPRange r; r.load(str)) {
        mark(r);
      } else {
        TSReleaseAssert("Bad IP range");
      }
    }

  }; // End class IP
} // namespace Range

namespace List
{
  class Method : public std::vector<Header::Method>
  {
#undef DELETE // ToDo: macOS shenanigans here, defining DELETE as a macro

    using super_type = std::vector<Header::Method>;
    using self_type  = Method;

  public:
    Method() = delete;
    explicit Method(Header::Method method) : std::vector<Header::Method>() { push_back(method); }
    void operator=(const Method &) = delete;

    Method(Method const &method) : std::vector<Header::Method>() { insert(end(), std::begin(method), std::end(method)); }

    Method(const std::initializer_list<Method> &list)
    {
      for (auto &it : list) {
        insert(end(), std::begin(it), std::end(it));
      }
    }

    Method(std::initializer_list<Header::Method> list)
    {
      for (auto &it : list) {
        push_back(it);
      }
    }

    // Make sure we only allow the Cript::Method::* constants

    [[nodiscard]] bool
    contains(Header::Method method) const
    {
      auto data = method.data();

      return end() != std::find_if(begin(), end(), [&](const Header::Method &header) { return header.data() == data; });
    }

    [[nodiscard]] bool
    match(Header::Method method) const
    {
      return contains(method);
    }

  }; // End class Method

} // namespace List

class PCRE
{
public:
  static constexpr size_t MAX_CAPTURES = 32;

  using Regex        = std::tuple<Cript::string, pcre2_code *>;
  using RegexEntries = std::vector<Regex>;

  class Result
  {
  public:
    friend class PCRE;

    Result() = delete;
    Result(Cript::string_view subject) : _subject(subject) {}

    ~Result() { pcre2_match_data_free(_data); }

    explicit
    operator bool() const
    {
      return matched();
    }

    Cript::StringViewWrapper
    operator[](size_t ix) const
    {
      Cript::StringViewWrapper ret;

      if ((count() > ix) && _ovector) {
        ret = {_subject.substr(_ovector[ix * 2], _ovector[ix * 2 + 1] - _ovector[ix * 2])};
      }

      return ret;
    }

    [[nodiscard]] bool
    matched() const
    {
      return _match != 0;
    }

    [[nodiscard]] RegexEntries::size_type
    matchIX() const
    {
      return _match;
    }

    [[nodiscard]] uint32_t
    count() const
    {
      return _data ? pcre2_get_ovector_count(_data) : 0;
    }

    // The allocator for the PCRE2 contexts, which puts the match data etc. on the stack when used appropriately
    static void *malloc(PCRE2_SIZE size, void *context);

  private:
    RegexEntries::size_type _match = 0; // The index into the regex vector that match, starting at 1 for the first regex
    pcre2_match_data *_data        = nullptr;
    PCRE2_SIZE *_ovector           = nullptr;
    PCRE2_SIZE _ctx_ix             = 0;
    std::byte _ctx_data[24 * 2 + 96 + 16 * MAX_CAPTURES];
    Cript::string_view _subject;
  };

  PCRE() = default;
  PCRE(Cript::string_view regex, uint32_t options = 0) { add(regex, options); }
  PCRE(const PCRE &)           = delete;
  void operator=(const PCRE &) = delete;

  PCRE(std::initializer_list<Cript::string_view> list, uint32_t options = 0)
  {
    for (auto &it : list) {
      add(it, options);
    }
  }

  ~PCRE();

  void add(Cript::string_view regex, uint32_t options = 0, bool jit = true);
  Result contains(Cript::string_view subject, PCRE2_SIZE offset = 0, uint32_t options = 0);

  Result
  match(Cript::string_view subject, PCRE2_SIZE offset = 0, uint32_t options = 0)
  {
    return contains(subject, offset, options);
  }

private:
  RegexEntries _regexes;
};

} // namespace Matcher
