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

#include "ts/ts.h"

#include "regex_helper.h"
#include "ipranges_helper.h"
#include "lulu.h"

// Possible operators that we support (at least partially)
enum MatcherOps {
  MATCH_EQUAL,
  MATCH_LESS_THEN,
  MATCH_GREATER_THEN,
  MATCH_REGULAR_EXPRESSION,
  MATCH_IP_RANGES,
};

///////////////////////////////////////////////////////////////////////////////
// Base class for all Matchers (this is also the interface)
//
class Matcher
{
public:
  explicit Matcher(const MatcherOps op) : _op(op) { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for Matcher"); }
  virtual ~Matcher() { TSDebug(PLUGIN_NAME_DBG, "Calling DTOR for Matcher"); }

  // noncopyable
  Matcher(const Matcher &) = delete;
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
  explicit Matchers<T>(const MatcherOps op) : Matcher(op), _data() {}
  // Getters / setters
  const T &
  get() const
  {
    return _data;
  };

  void
  setRegex(const std::string & /* data ATS_UNUSED */)
  {
    if (!reHelper.setRegexMatch(_data)) {
      std::stringstream ss;
      ss << _data;
      TSError("[%s] Invalid regex: failed to precompile: %s", PLUGIN_NAME, ss.str().c_str());
      TSDebug(PLUGIN_NAME, "Invalid regex: failed to precompile: %s", ss.str().c_str());
      throw std::runtime_error("Malformed regex");
    } else {
      TSDebug(PLUGIN_NAME, "Regex precompiled successfully");
    }
  }

  void
  setRegex(const unsigned int /* t ATS_UNUSED */)
  {
    return;
  }
  void
  setRegex(const TSHttpStatus /* t ATS_UNUSED */)
  {
    return;
  }

  void
  set(const T &d)
  {
    _data = d;
    if (_op == MATCH_REGULAR_EXPRESSION) {
      setRegex(d);
    }
  }

  // Evaluate this matcher
  bool
  test(const T &t) const
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
      return test_reg(t);
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

    ss << '"' << t << '"' << op << '"' << _data << '"' << " -> " << r;
    TSDebug(PLUGIN_NAME, "\ttesting: %s", ss.str().c_str());
  }

  // For basic types
  bool
  test_eq(const T &t) const
  {
    bool r = (t == _data);

    if (TSIsDebugTagSet(PLUGIN_NAME)) {
      debug_helper(t, " == ", r);
    }
    return r;
  }

  bool
  test_lt(const T &t) const
  {
    bool r = (t < _data);

    if (TSIsDebugTagSet(PLUGIN_NAME)) {
      debug_helper(t, " < ", r);
    }
    return r;
  }

  bool
  test_gt(const T &t) const
  {
    bool r = t > _data;

    if (TSIsDebugTagSet(PLUGIN_NAME)) {
      debug_helper(t, " > ", r);
    }
    return r;
  }

  bool
  test_reg(const unsigned int /* t ATS_UNUSED */) const
  {
    // Not supported
    return false;
  }

  bool
  test_reg(const TSHttpStatus /* t ATS_UNUSED */) const
  {
    // Not supported
    return false;
  }

  bool
  test_reg(const std::string &t) const
  {
    int ovector[OVECCOUNT];

    TSDebug(PLUGIN_NAME, "Test regular expression %s : %s", _data.c_str(), t.c_str());
    if (reHelper.regexMatch(t.c_str(), t.length(), ovector) > 0) {
      TSDebug(PLUGIN_NAME, "Successfully found regular expression match");
      return true;
    }
    return false;
  }

  T _data;
  regexHelper reHelper;
};

// Specialized case matcher for the IP addresses matches.
// ToDo: we should specialize the regex matcher as well.
template <> class Matchers<const sockaddr *> : public Matcher
{
public:
  explicit Matchers<const sockaddr *>(const MatcherOps op) : Matcher(op) {}

  void
  set(const std::string &data)
  {
    if (!ipHelper.addIpRanges(data)) {
      TSError("[%s] Invalid IP-range: failed to parse: %s", PLUGIN_NAME, data.c_str());
      TSDebug(PLUGIN_NAME, "Invalid IP-range: failed to parse: %s", data.c_str());
      throw std::runtime_error("Malformed IP-range");
    } else {
      TSDebug(PLUGIN_NAME, "IP-range precompiled successfully");
    }
  }

  bool
  test(const sockaddr *addr) const
  {
    if (ipHelper.ipRangesMatch(addr) > 0) {
      if (TSIsDebugTagSet(PLUGIN_NAME)) {
        char text[INET6_ADDRSTRLEN];

        TSDebug(PLUGIN_NAME, "Successfully found IP-range match on %s", getIP(addr, text));
      }
      return true;
    } else {
      return false;
    }
  }

private:
  ipRangesHelper ipHelper;
};
