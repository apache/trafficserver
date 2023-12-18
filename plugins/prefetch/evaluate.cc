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

/**
 * @file evaluate.h
 * @brief Prefetch formula evaluation (header file).
 */

#include "evaluate.h"
#include <charconv>
#include <sstream>
#include <istream>
#include <iomanip>
#include <cstdint>
#include <cinttypes>

namespace
{

inline int8_t
tonum(char ch)
{
  return ch - '0';
}

inline char
tochar(int8_t ch)
{
  return ch + '0';
}

constexpr StringView const svzero{"0"};
constexpr char const *const digits = "0123456789";

inline bool
is_valid_digits(const StringView val)
{
  return val.npos != val.find_first_of(digits);
}

// both strings should have all digits
String
add(StringView const lhs, StringView const rhs)
{
  String result("0");

  StringView other;
  if (lhs.length() < rhs.length()) {
    other = lhs;
    result.append(rhs);
  } else {
    other = rhs;
    result.append(lhs);
  }

  bool carry = false;

  auto itr = result.rbegin();
  auto ito = other.crbegin();

  while (ito != other.crend()) {
    int8_t val = tonum(*itr) + tonum(*ito);
    if (carry) {
      ++val;
    }
    if (val < 10) {
      carry = false;
    } else {
      val   -= 10;
      carry  = true;
    }

    *itr = tochar(val);

    ++itr;
    ++ito;
  }

  while (result.rend() != itr && carry) {
    int8_t val = tonum(*itr) + 1;
    if (val < 10) {
      carry = false;
    } else {
      val   -= 10;
      carry  = true;
    }

    *itr = tochar(val);
    ++itr;
  }

  return result;
}

String
sub(StringView const lhs, StringView const rhs)
{
  String result;

  // ensure result length gte rhs length
  if (lhs.length() < rhs.length()) {
    result.append(rhs.length() - lhs.length(), '0');
    result.append(lhs);
  } else {
    result = lhs;
  }

  PrefetchDebug("sub init result: '%s', rhs: '%.*s'", result.c_str(), (int)rhs.length(), rhs.data());

  bool borrow = false;

  // top and bottom of subtraction
  auto itr = result.rbegin();
  auto itb = rhs.crbegin();

  while (result.rend() != itr && rhs.crend() != itb) {
    int8_t val = tonum(*itr) - tonum(*itb);
    if (borrow) {
      --val;
    }
    if (val < 0) {
      borrow  = true;
      val    += 10;
    } else {
      borrow = false;
    }

    *itr = tochar(val);

    ++itr;
    ++itb;
  }

  // keep pushing borrow
  while (result.rend() != itr && borrow) {
    int8_t val = tonum(*itr) - 1;
    if (val < 0) {
      borrow  = true;
      val    += 10;
    } else {
      borrow = false;
    }

    *itr = tochar(val);
    ++itr;
  }

  PrefetchDebug("sub result: '%s', borrow: '%s'", result.c_str(), borrow ? "true" : "false");

  // if result would have been negative.
  if (borrow) {
    result = std::string{"0"};
  }

  return result;
}

String
evaluateBignum(const StringView view)
{
  String result("0");

  StringView v = view;

  uint32_t fwide            = 0;
  StringView::size_type pos = v.find_first_of(':');
  if (v.npos != pos) {
    std::from_chars(v.begin(), v.begin() + pos, fwide);
    PrefetchDebug("statement: '%.*s', formatting length: %" PRIu32, (int)pos, v.data(), fwide);
    v = v.substr(pos + 1);
  }

  pos = v.find_first_of("+-");
  if (v.npos == pos) {
    if (is_valid_digits(v)) {
      result.assign(v);
    }
  } else {
    StringView vleft = v.substr(0, pos);
    if (!is_valid_digits(vleft)) {
      vleft = svzero;
    }
    StringView vrite = v.substr(pos + 1);
    if (!is_valid_digits(vrite)) {
      vrite = svzero;
    }
    if ('+' == v[pos]) {
      PrefetchDebug("Adding %.*s and %.*s", (int)vleft.length(), vleft.data(), (int)vrite.length(), vrite.data());
      result = add(vleft, vrite);
    } else {
      PrefetchDebug("Subbing %.*s and %.*s", (int)vleft.length(), vleft.data(), (int)vrite.length(), vrite.data());
      result = sub(vleft, vrite);
    }
  }

  // wipe out leading zeros
  while (1 < result.length() && fwide < result.length() && '0' == result.front()) {
    result.erase(0, 1);
  }

  // Left pad out with zeros
  if (result.length() < fwide) {
    result.insert(0, fwide - result.length(), '0');
  }

  return result;
}
} // namespace

/**
 * @brief Evaluate a math addition or subtraction expression.
 *
 * @param v string containing an expression, i.e. "3 + 4"
 * @return string containing the result, i.e. "7"
 */
String
evaluate(const StringView view, const EvalPolicy policy)
{
  if (view.empty()) {
    return String("");
  }

  // short circuit
  if (policy == EvalPolicy::Bignum) {
    return evaluateBignum(view);
  }

  StringView v = view;

  /* Find out if width is specified (hence leading zeros are required if the width is bigger then the result width) */
  String stmt;
  uint32_t len              = 0;
  StringView::size_type pos = v.find_first_of(':');
  if (v.npos != pos) {
    stmt.assign(v.substr(0, pos));
    std::istringstream iss(stmt);
    iss >> len;
    v = v.substr(pos + 1);
  }
  PrefetchDebug("statement: '%s', formatting length: %" PRIu32, stmt.c_str(), len);

  uint64_t result = 0;
  pos             = v.find_first_of("+-");

  if (v.npos == pos) {
    stmt.assign(v.substr(0, pos));
    std::istringstream iss(stmt);

    if (policy == EvalPolicy::Overflow64) {
      iss >> result;
    } else {
      uint32_t tmp32;
      iss >> tmp32;
      result = tmp32;
    }

    PrefetchDebug("Single-operand expression: %s -> %" PRIu64, stmt.c_str(), result);
  } else {
    const String leftOperand(v.substr(0, pos));
    std::istringstream liss(leftOperand);
    uint64_t a64 = 0;

    if (policy == EvalPolicy::Overflow64) {
      liss >> a64;
    } else {
      uint32_t a32;
      liss >> a32;
      a64 = a32;
    }
    PrefetchDebug("Left-operand expression: %s -> %" PRIu64, leftOperand.c_str(), a64);

    const String rightOperand(v.substr(pos + 1));
    std::istringstream riss(rightOperand);
    uint64_t b64 = 0;

    if (policy == EvalPolicy::Overflow64) {
      riss >> b64;
    } else {
      uint32_t b32;
      riss >> b32;
      b64 = b32;
    }

    PrefetchDebug("Right-operand expression: %s -> %" PRIu64, rightOperand.c_str(), b64);

    if ('+' == v[pos]) {
      result = a64 + b64;
    } else {
      if (a64 <= b64) {
        result = 0;
      } else {
        result = a64 - b64;
      }
    }
  }

  std::ostringstream convert;
  convert << std::setw(len) << std::setfill('0') << result;
  PrefetchDebug("evaluation of '%.*s' resulted in '%s'", (int)view.length(), view.data(), convert.str().c_str());
  return convert.str();
}
