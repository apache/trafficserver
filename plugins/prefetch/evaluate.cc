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
#include <sstream>
#include <istream>
#include <iomanip>
#include <cstdint>
#include <cinttypes>

/**
 * @brief Evaluate a math addition or subtraction expression.
 *
 * @param v string containing an expression, i.e. "3 + 4"
 * @return string containing the result, i.e. "7"
 */
String
evaluate(const String &v)
{
  if (v.empty()) {
    return String("");
  }

  /* Find out if width is specified (hence leading zeros are required if the width is bigger then the result width) */
  String stmt;
  uint32_t len = 0;
  size_t pos   = v.find_first_of(':');
  if (String::npos != pos) {
    stmt.assign(v.substr(0, pos));
    std::istringstream iss(stmt);
    iss >> len;
    stmt.assign(v.substr(pos + 1));
  } else {
    stmt.assign(v);
  }
  PrefetchDebug("statement: '%s', formatting length: %" PRIu32, stmt.c_str(), len);

  uint64_t result = 0;
  pos             = stmt.find_first_of("+-");

  if (String::npos == pos) {
    uint32_t tmp;
    std::istringstream iss(stmt);
    iss >> tmp;
    result = tmp;

    PrefetchDebug("Single-operand expression: %s -> %" PRIu64, stmt.c_str(), result);
  } else {
    String leftOperand = stmt.substr(0, pos);
    std::istringstream liss(leftOperand);
    uint32_t a;
    liss >> a;

    String rightOperand = stmt.substr(pos + 1);
    std::istringstream riss(rightOperand);
    uint32_t b;
    riss >> b;

    if ('+' == stmt[pos]) {
      result = static_cast<uint64_t>(a) + static_cast<uint64_t>(b);
    } else {
      if (a <= b) {
        result = 0;
      } else {
        result = a - b;
      }
    }
  }

  std::ostringstream convert;
  convert << std::setw(len) << std::setfill('0') << result;
  PrefetchDebug("evaluation of '%s' resulted in '%s'", v.c_str(), convert.str().c_str());
  return convert.str();
}
