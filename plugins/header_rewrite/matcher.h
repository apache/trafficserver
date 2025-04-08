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
#include <concepts>
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
enum CondModifiers {
  COND_NONE   = 0,
  COND_OR     = 1,
  COND_AND    = 2,
  COND_NOT    = 4,
  COND_NOCASE = 8,
  COND_LAST   = 16,
  COND_CHAIN  = 32 // Not implemented
};

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

  // Getters / setters. ToDo: What about sets ?
  const T &
  get() const
  {
    TSAssert(std::holds_alternative<T>(_data));
    return std::get<T>(_data);
  }

  void
  set(const T &d, CondModifiers mods)
  {
    TSAssert(std::holds_alternative<T>(_data));
    std::get<T>(_data) = d;
    if (mods & COND_NOCASE) {
      _nocase = true;
    }
  }

  void
  set(const std::string &s, CondModifiers mods)
    requires std::is_same_v<T, std::string>
  {
    set(s, mods, [](const std::string &in) { return in; });
  }

  void
  set(const std::string &s, CondModifiers mods, auto convert)
  {
    static_assert(std::is_same_v<decltype(convert(s)), T>, "Converter must return a value of type T");

    if (mods & COND_NOCASE) {
      _nocase = true;
    }

    if (_op == MATCH_REGULAR_EXPRESSION) {
      if (!_reHelper.setRegexMatch(s, _nocase)) {
        std::stringstream ss;

        ss << s;
        TSError("[%s] Invalid regex: failed to precompile: %s", PLUGIN_NAME, ss.str().c_str());
        Dbg(pi_dbg_ctl, "Invalid regex: failed to precompile: %s", ss.str().c_str());
        throw std::runtime_error("Malformed regex");
      } else {
        Dbg(pi_dbg_ctl, "Regex precompiled successfully");
      }
    } else if (_op == MATCH_SET) {
      std::set<T>        values;
      std::istringstream stream(s);
      std::string        part;

      while (std::getline(stream, part, ',')) {
        values.insert(convert(part));
      }

      if (values.size() > 0) {
        Dbg(pi_dbg_ctl, "    Added %zu set values while parsing", values.size());
        _data = std::move(values);
      } else {
        Dbg(pi_dbg_ctl, "    No set values added, possibly bad input");
      }
    } else {
      std::get<T>(_data) = convert(s);
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

    ss << '"' << t << '"' << op << '"' << std::get<T>(_data) << '"' << " -> " << r;
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
  test_reg(const std::string &t, const Resources &res) const
  {
    Dbg(pi_dbg_ctl, "Test regular expression %s : %s (NOCASE = %d)", std::get<T>(_data).c_str(), t.c_str(),
        static_cast<int>(_nocase));
    int count = _reHelper.regexMatch(t.c_str(), t.length(), const_cast<Resources &>(res).ovector);

    if (count > 0) {
      Dbg(pi_dbg_ctl, "Successfully found regular expression match");
      const_cast<Resources &>(res).ovector_ptr   = t.c_str();
      const_cast<Resources &>(res).ovector_count = count;

      return true;
    }

    return false;
  }

  std::variant<T, std::set<T>> _data;
  regexHelper                  _reHelper;
  bool                         _nocase = false;
};

// Specializations for the strings, since they can be both strings and regexes
template <> bool Matchers<std::string>::test_eq(const std::string &t) const;

// Specialized case matcher for the IP addresses matches.
template <> class Matchers<const sockaddr *> : public Matcher
{
public:
  explicit Matchers(const MatcherOps op) : Matcher(op) {}

  void set(const std::string &data);
  bool test(const sockaddr *addr, const Resources &) const;

private:
  bool             extract_ranges(swoc::TextView text);
  swoc::IPRangeSet _ipHelper;
};
