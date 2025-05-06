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
//////////////////////////////////////////////////////////////////////////////////////////////
//
// Implement the classes for the various types of hash keys we support.
//
#pragma once

#include <string>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <variant>
#include <set>

#include "swoc/swoc_ip.h"

#include "ts/ts.h"

#include "resources.h"
#include "regex_helper.h"
#include "lulu.h"

// Possible operators that we support (at least partially)
enum MatcherOps {
  MATCH_EQUAL,
  MATCH_LESS_THEN,
  MATCH_GREATER_THEN,
  MATCH_REGULAR_EXPRESSION,
  MATCH_IP_RANGES,
  MATCH_SET,
  MATCH_ERROR,
};

// Condition modifiers
enum class CondModifiers : int {
  NONE       = 0,
  OR         = 1 << 0,
  AND        = 1 << 1,
  NOT        = 1 << 2,
  MOD_NOCASE = 1 << 3,
  MOD_L      = 1 << 4,
  MOD_EXT    = 1 << 5,
  MOD_PRE    = 1 << 6,
  MOD_SUF    = 1 << 7,
  MOD_MID    = 1 << 8, // Essentially a substring
};

inline CondModifiers
operator|(CondModifiers a, const CondModifiers b)
{
  using U = std::underlying_type_t<CondModifiers>;
  return static_cast<CondModifiers>(static_cast<U>(a) | static_cast<U>(b));
}

inline CondModifiers
operator&(CondModifiers a, const CondModifiers b)
{
  using U = std::underlying_type_t<CondModifiers>;
  return static_cast<CondModifiers>(static_cast<U>(a) & static_cast<U>(b));
}

inline CondModifiers &
operator|=(CondModifiers &a, const CondModifiers b)
{
  return a = a | b;
}

inline CondModifiers &
operator&=(CondModifiers &a, const CondModifiers b)
{
  return a = a & b;
}

inline bool
has_modifier(const CondModifiers flags, const CondModifiers bit)
{
  using U = std::underlying_type_t<CondModifiers>;
  return static_cast<U>(flags) & static_cast<U>(bit);
}

///////////////////////////////////////////////////////////////////////////////
// Base class for all Matchers (this is also the interface)
//
class Matcher
{
public:
  explicit Matcher(const MatcherOps op) : _op(op) { Dbg(dbg_ctl, "Calling CTOR for Matcher"); }
  virtual ~Matcher() { Dbg(dbg_ctl, "Calling DTOR for Matcher"); }

  // noncopyable
  Matcher(const Matcher &)        = delete;
  void operator=(const Matcher &) = delete;

  MatcherOps
  op() const
  {
    return _op;
  }

protected:
  const MatcherOps _op;
};

// Template class to match on various types of data
template <class T> class Matchers : public Matcher
{
public:
  explicit Matchers(const MatcherOps op) : Matcher(op), _data() {}

  void
  set(const T &d, CondModifiers mods)
  {
    _mods = mods;
    if constexpr (std::is_same_v<T, std::string>) {
      set(d, mods, [](const std::string &in) { return in; });
    } else {
      std::get<T>(_data) = d;
    }
  }

  template <typename FN>
  void
  set(const std::string &s, CondModifiers mods, FN convert)
  {
    static_assert(std::is_same_v<decltype(convert(s)), T>, "Converter must return a value of type T");
    _mods = mods;

    // MATCH_REGULAR_EXPRESSION (only valid for std::string)
    if constexpr (std::is_same_v<T, std::string>) {
      if (_op == MATCH_REGULAR_EXPRESSION) {
        _data.template emplace<regexHelper>();

        auto &re = std::get<regexHelper>(_data);

        if (!re.setRegexMatch(s, has_modifier(mods, CondModifiers::MOD_NOCASE))) {
          TSError("[%s] Invalid regex: failed to precompile: %s", PLUGIN_NAME, s.c_str());
          Dbg(pi_dbg_ctl, "Invalid regex: failed to precompile: %s", s.c_str());
          throw std::runtime_error("Malformed regex");
        }

        Dbg(pi_dbg_ctl, "Regex precompiled successfully");
        return;
      }
    }

    // MATCH_IP_RANGES (only valid for const sockaddr *)
    if constexpr (std::is_same_v<T, const sockaddr *>) {
      if (_op == MATCH_IP_RANGES) {
        _data.template emplace<swoc::IPRangeSet>();

        auto              &ranges = std::get<swoc::IPRangeSet>(_data);
        std::istringstream stream(s);
        std::string        part;
        size_t             count = 0;

        while (std::getline(stream, part, ',')) {
          swoc::IPRange r;

          if (r.load(part)) {
            ranges.mark(r);
            ++count;
          }
        }

        if (count > 0) {
          Dbg(pi_dbg_ctl, "IP-range precompiled successfully with %zu entries", count);
        } else {
          TSError("[%s] Invalid IP-range: failed to parse: %s", PLUGIN_NAME, s.c_str());
          Dbg(pi_dbg_ctl, "Invalid IP-range: failed to parse: %s", s.c_str());
          throw std::runtime_error("Malformed IP-range");
        }
        return;
      } else {
        TSReleaseAssert(false); // This should never happen
      }
    }

    // MATCH_SET (allowed for any T)
    if (_op == MATCH_SET) {
      _data.template emplace<std::set<T>>();

      auto              &values = std::get<std::set<T>>(_data);
      std::istringstream stream(s);
      std::string        part;

      while (std::getline(stream, part, ',')) {
        values.insert(convert(part));
      }

      if (!values.empty()) {
        Dbg(pi_dbg_ctl, "    Added %zu set values while parsing", values.size());
      } else {
        Dbg(pi_dbg_ctl, "    No set values added, possibly bad input");
        throw std::runtime_error("Empty sets not allowed");
      }
    } else {
      // Default: single value
      _data.template emplace<T>(convert(s));
    }
  }

  // Evaluate this matcher
  bool
  test(const T &t, const Resources &res) const
  {
    switch (_op) {
    case MATCH_EQUAL:
      return test_eq(t);
      break;
    case MATCH_LESS_THEN:
      return test_lt(t);
      break;
    case MATCH_GREATER_THEN:
      return test_gt(t);
      break;
    case MATCH_REGULAR_EXPRESSION:
      return test_reg(t, res); // Only the regex matcher needs the resource
      break;
    case MATCH_SET:
      return test_set(t);
      break;
    case MATCH_IP_RANGES:
      // This is an error, the Matcher doesn't make sense to match on IP ranges
      TSError("[%s] Invalid matcher: MATCH_IP_RANGES", PLUGIN_NAME);
      throw std::runtime_error("Can not match on IP ranges");
      break;
    default:
      // ToDo: error
      break;
    }
    return false;
  }

private:
  void
  debug_helper(const T &t, const char *op, bool r) const
  {
    std::stringstream ss;

    std::visit(
      [&](const auto &val) {
        using V = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<V, T>) {
          ss << '"' << t << '"' << op << '"' << val << '"';
        } else if constexpr (std::is_same_v<V, std::set<T>>) {
          ss << '"' << t << '"' << op << " set[" << val.size() << " entries]";
        } else {
          ss << '"' << t << '"' << op << " type<" << typeid(V).name() << ">";
        }
      },
      _data);

    ss << " -> " << r;
    Dbg(pi_dbg_ctl, "\ttesting: %s", ss.str().c_str());
  }

  // For basic types
  bool
  test_eq(const T &t) const
  {
    TSAssert(std::holds_alternative<T>(_data));
    bool r = (t == std::get<T>(_data));

    if (pi_dbg_ctl.on()) {
      debug_helper(t, " == ", r);
    }

    return r;
  }

  bool
  test_lt(const T &t) const
  {
    TSAssert(std::holds_alternative<T>(_data));
    bool r = (t < std::get<T>(_data));

    if (pi_dbg_ctl.on()) {
      debug_helper(t, " < ", r);
    }

    return r;
  }

  bool
  test_gt(const T &t) const
  {
    TSAssert(std::holds_alternative<T>(_data));
    bool r = (t > std::get<T>(_data));

    if (pi_dbg_ctl.on()) {
      debug_helper(t, " > ", r);
    }

    return r;
  }

  bool
  test_set(const T &c) const
  {
    TSAssert(std::holds_alternative<std::set<T>>(_data));
    return std::get<std::set<T>>(_data).contains(c);
  }

  bool
  test_reg(const unsigned int /* t ATS_UNUSED */, const Resources & /* Not used */) const
  {
    // Not supported
    return false;
  }

  bool
  test_reg(const sockaddr * /* t ATS_UNUSED */, const Resources & /* Not used */) const
  {
    // Not supported
    return false;
  }

  bool
  test_reg(const std::string &t, const Resources &res) const
  {
    TSAssert(std::holds_alternative<regexHelper>(_data));
    Dbg(pi_dbg_ctl, "Test regular expression against: %s (NOCASE = %s)", t.c_str(),
        has_modifier(_mods, CondModifiers::MOD_NOCASE) ? "true" : "false");
    const auto &re    = std::get<regexHelper>(_data);
    int         count = re.regexMatch(t.c_str(), t.length(), const_cast<Resources &>(res).ovector);

    if (count > 0) {
      Dbg(pi_dbg_ctl, "Successfully found regular expression match");
      const_cast<Resources &>(res).ovector_ptr   = t.c_str();
      const_cast<Resources &>(res).ovector_count = count;

      return true;
    }

    return false;
  }

  std::variant<T, std::set<T>, swoc::IPRangeSet, regexHelper> _data;
  CondModifiers                                               _mods = CondModifiers::NONE;
};
