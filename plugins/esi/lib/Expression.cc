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

#include "Expression.h"
#include "Utils.h"

using std::string;
using namespace EsiLib;

const string Expression::EMPTY_STRING("");
const string Expression::TRUE_STRING("true");
const Expression::OperatorString Expression::OPERATOR_STRINGS[N_OPERATORS] = {
  Expression::OperatorString("==", 2), Expression::OperatorString("!=", 2), Expression::OperatorString("<=", 2),
  Expression::OperatorString(">=", 2), Expression::OperatorString("<", 1),  Expression::OperatorString(">", 1),
  Expression::OperatorString("!", 1),  Expression::OperatorString("|", 1),  Expression::OperatorString("&", 1)};

Expression::Expression(const char *debug_tag, ComponentBase::Debug debug_func, ComponentBase::Error error_func,
                       Variables &variables)
  : ComponentBase(debug_tag, debug_func, error_func), _variables(variables), _value("")
{
}

inline bool
Expression::_stripQuotes(const char *&expr, int &expr_len) const
{
  char quote_char = 0;
  if (expr[0] == '\'') {
    quote_char = '\'';
  } else if (expr[0] == '"') {
    quote_char = '"';
  }
  if (quote_char) {
    if (expr[expr_len - 1] != quote_char) {
      _errorLog("[%s] Unterminated quote in expression [%.*s]", __FUNCTION__, expr_len, expr);
      return false;
    }
    expr_len -= 2;
    ++expr;
  }
  return true;
}

const string &
Expression::expand(const char *expr, int expr_len /* = -1 */)
{
  int var_start_index = -1, var_size;
  Utils::trimWhiteSpace(expr, expr_len);
  if (!expr_len) {
    _debugLog(_debug_tag, "[%s] Returning empty string for empty expression", __FUNCTION__);
    goto lFail;
  }
  if (!_stripQuotes(expr, expr_len)) {
    goto lFail;
  }
  _value.clear();
  bool last_variable_expanded;
  for (int i = 0; i < expr_len; ++i) {
    if ((expr[i] == '$') && ((expr_len - i) >= 3) && (expr[i + 1] == '(')) {
      if (var_start_index != -1) {
        _debugLog(_debug_tag, "[%s] Cannot have nested variables in expression [%.*s]", __FUNCTION__, expr_len, expr);
        goto lFail;
      }
      var_start_index = i + 2; // skip the '$('
      ++i;                     // skip past '$'; for loop's incrementor will skip past '('
    } else if (((expr[i] == ')') || (expr[i] == '|')) && (var_start_index != -1)) {
      last_variable_expanded = false;
      var_size               = i - var_start_index;
      if (var_size) {
        const string &var_value = _variables.getValue(expr + var_start_index, var_size);
        _debugLog(_debug_tag, "[%s] Got value [%.*s] for variable [%.*s]", __FUNCTION__, var_value.size(), var_value.data(),
                  var_size, expr + var_start_index);
        last_variable_expanded = (var_value.size() > 0);
        _value += var_value;
      } else {
        _debugLog(_debug_tag, "[%s] Parsing out empty variable", __FUNCTION__);
      }
      if (expr[i] == '|') {
        int default_value_start = ++i;
        while (i < expr_len) {
          if (expr[i] == ')') {
            break;
          }
          ++i;
        }
        if (i == expr_len) {
          _debugLog(_debug_tag, "[%s] Expression [%.*s] has unterminated variable (with default value)", __FUNCTION__, expr_len,
                    expr);
          goto lFail;
        }
        const char *default_value = expr + default_value_start;
        int default_value_len     = i - default_value_start;
        if (!_stripQuotes(default_value, default_value_len)) {
          goto lFail;
        }
        if (!last_variable_expanded) {
          _debugLog(_debug_tag, "[%s] Using default value [%.*s] as variable expanded to empty string", __FUNCTION__,
                    default_value_len, default_value);
          _value.append(default_value, default_value_len);
        }
      }
      var_start_index = -1;
    } else if (var_start_index == -1) {
      _value += expr[i];
    }
  }
  if (var_start_index != -1) {
    _debugLog(_debug_tag, "[%s] Returning empty string for expression with unterminated variable [%.*s]", __FUNCTION__,
              expr_len - var_start_index, expr + var_start_index);
    goto lFail;
  }
  _debugLog(_debug_tag, "[%s] Returning final expanded expression [%.*s]", __FUNCTION__, _value.size(), _value.data());
  return _value;

lFail:
  return EMPTY_STRING;
}

inline int
Expression::_findOperator(const char *expr, int expr_len, Operator &op) const
{
  string expr_str(expr, expr_len);
  size_t sep;
  for (int i = 0; i < N_OPERATORS; ++i) {
    const OperatorString &op_str = OPERATOR_STRINGS[i];
    sep                          = (op_str.str_len == 1) ? expr_str.find(op_str.str[0]) : expr_str.find(op_str.str);
    if (sep < expr_str.size()) {
      op = static_cast<Operator>(i);
      return static_cast<int>(sep);
    }
  }
  return -1;
}

inline bool
Expression::_evalSimpleExpr(const char *expr, int expr_len)
{
  const string &lhs = expand(expr, expr_len);
  _debugLog(_debug_tag, "[%s] simple expression [%.*s] evaluated to [%.*s]", __FUNCTION__, expr_len, expr, lhs.size(), lhs.data());
  double val;
  return _convert(lhs, val) ? val : !lhs.empty();
}

bool
Expression::evaluate(const char *expr, int expr_len /* = -1 */)
{
  Utils::trimWhiteSpace(expr, expr_len);
  if (!expr_len) {
    _debugLog(_debug_tag, "[%s] Returning false for empty expression", __FUNCTION__);
    return false;
  }
  Operator op = OP_EQ; // stupid initialized checking, make gcc happy
  const char *subexpr;
  int subexpr_len;
  string lhs, rhs;
  bool retval = false;
  int sep     = _findOperator(expr, expr_len, op);

  if (sep == -1) {
    retval = _evalSimpleExpr(expr, expr_len);
  } else if (_isBinaryOperator(op)) {
    subexpr     = expr;
    subexpr_len = sep;
    lhs         = expand(subexpr, subexpr_len);
    _debugLog(_debug_tag, "[%s] LHS [%.*s] expanded to [%.*s]", __FUNCTION__, subexpr_len, subexpr, lhs.size(), lhs.data());
    subexpr     = expr + sep + OPERATOR_STRINGS[op].str_len;
    subexpr_len = expr_len - subexpr_len - OPERATOR_STRINGS[op].str_len;
    rhs         = expand(subexpr, subexpr_len);
    _debugLog(_debug_tag, "[%s] RHS [%.*s] expanded to [%.*s]", __FUNCTION__, subexpr_len, subexpr, rhs.size(), rhs.data());
    double lhs_numerical = 0;
    double rhs_numerical = 0;
    bool are_numerical   = _convert(lhs, lhs_numerical);
    are_numerical        = are_numerical ? _convert(rhs, rhs_numerical) : false;
    switch (op) {
    case OP_EQ:
      retval = are_numerical ? (lhs_numerical == rhs_numerical) : (lhs == rhs);
      break;
    case OP_NEQ:
      retval = are_numerical ? (lhs_numerical != rhs_numerical) : (lhs != rhs);
      break;
    case OP_OR:
      retval = are_numerical ? (lhs_numerical || rhs_numerical) : (lhs.size() || rhs.size());
      break;
    case OP_AND:
      retval = are_numerical ? (lhs_numerical && rhs_numerical) : (lhs.size() && rhs.size());
      break;
    default:
      if (lhs.empty() || rhs.empty()) {
        // one of the sides expanded to nothing; invalid comparison
        _debugLog(_debug_tag, "[%s] LHS/RHS empty. Cannot evaluate comparisons", __FUNCTION__);
        retval = false;
      } else {
        switch (op) {
        case OP_LT:
          retval = are_numerical ? (lhs_numerical < rhs_numerical) : (lhs < rhs);
          break;
        case OP_GT:
          retval = are_numerical ? (lhs_numerical > rhs_numerical) : (lhs > rhs);
          break;
        case OP_LTEQ:
          retval = are_numerical ? (lhs_numerical <= rhs_numerical) : (lhs <= rhs);
          break;
        case OP_GTEQ:
          retval = are_numerical ? (lhs_numerical >= rhs_numerical) : (lhs >= rhs);
          break;
        default:
          _debugLog(_debug_tag, "[%s] Unknown operator in expression [%.*s]; returning false", __FUNCTION__, expr_len, expr);
        }
      }
      break;
    }
  } else if (op == OP_NOT) {
    if (sep == 0) {
      retval = !_evalSimpleExpr(expr + 1, expr_len - 1);
    } else {
      _debugLog(_debug_tag, "[%s] Unary negation not preceding literal in expression [%.*s]; assuming true", __FUNCTION__, expr_len,
                expr);
      retval = true;
    }
  } else {
    _debugLog(_debug_tag, "[%s] Unknown operator in expression [%.*s]; returning false", __FUNCTION__, expr_len, expr);
  }
  _debugLog(_debug_tag, "[%s] Returning [%s] for expression [%.*s]", __FUNCTION__, (retval ? "true" : "false"), expr_len, expr);
  return retval;
}
