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

#ifndef _ESI_EXPRESSION_H

#define _ESI_EXPRESSION_H

#include <string>
#include <cstdlib>

#include "ComponentBase.h"
#include "Variables.h"

namespace EsiLib
{
class Expression : private ComponentBase
{
public:
  Expression(const char *debug_tag, ComponentBase::Debug debug_func, ComponentBase::Error error_func, Variables &variables);

  /** substitutes variables (if any) in given expression */
  const std::string &expand(const char *expr, int expr_len = -1);

  /** convenient alternative for method above */
  const std::string &
  expand(const std::string &expr)
  {
    return expand(expr.data(), expr.size());
  }

  /** evaluates boolean value of given expression */
  bool evaluate(const char *expr, int expr_len = -1);

  /** convenient alternative for method above */
  bool
  evaluate(const std::string &expr)
  {
    return evaluate(expr.data(), expr.size());
  }

  ~Expression() override{};

private:
  static const std::string EMPTY_STRING;
  static const std::string TRUE_STRING;

  Variables &_variables;
  std::string _value;

  // these are arranged in parse priority format indices correspond to op strings array
  enum Operator {
    OP_EQ,
    OP_NEQ,
    OP_LTEQ,
    OP_GTEQ,
    OP_LT,
    OP_GT,
    OP_NOT,
    OP_OR,
    OP_AND,
    N_OPERATORS,
  };

  struct OperatorString {
    const char *str;
    int str_len;
    OperatorString(const char *s = nullptr, int s_len = -1) : str(s), str_len(s_len){};
  };

  static const OperatorString OPERATOR_STRINGS[N_OPERATORS];

  inline void _trimWhiteSpace(const char *&expr, int &expr_len) const;

  inline bool _stripQuotes(const char *&expr, int &expr_len) const;

  inline int _findOperator(const char *expr, int expr_len, Operator &op) const;

  inline bool
  _isBinaryOperator(Operator &op) const
  {
    return ((op == OP_EQ) || (op == OP_NEQ) || (op == OP_LT) || (op == OP_GT) || (op == OP_LTEQ) || (op == OP_GTEQ) ||
            (op == OP_OR) || (op == OP_AND));
  }

  inline bool
  _convert(const std::string &str, double &value)
  {
    size_t str_size = str.size();
    if (str_size) {
      char *endp;
      const char *str_ptr = str.c_str();
      // Solaris is messed up, in that strtod() does not honor C99/SUSv3 mode.
      value = strtold(str_ptr, &endp);
      return (static_cast<unsigned int>(endp - str_ptr) == str_size);
    }
    return false;
  }

  inline bool _evalSimpleExpr(const char *expr, int expr_len);
};
}; // namespace EsiLib

#endif // _ESI_EXPRESSION_H
