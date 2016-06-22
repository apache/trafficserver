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

/***************************************/
/****************************************************************************
 *
 *  StatType.cc - Functions for computing node and cluster stat
 *                          aggregation
 *
 *
 ****************************************************************************/

#include "ts/ink_config.h"
#include "StatType.h"
#include "MgmtUtils.h"
#include "ts/ink_hrtime.h"
#include "WebOverview.h"
#include "ts/Tokenizer.h"

bool StatError = false; // global error flag
bool StatDebug = false; // global debug flag

/**
 * StatExprToken()
 * ---------------
 */
StatExprToken::StatExprToken()
  : m_arith_symbol('\0'), m_token_name(NULL), m_token_type(RECD_NULL), m_sum_var(false), m_node_var(true)
{
  RecDataZero(RECD_NULL, &m_token_value);
  RecDataZero(RECD_NULL, &m_token_value_max);
  RecDataZero(RECD_NULL, &m_token_value_min);
  memset(&m_token_value_delta, 0, sizeof(m_token_value_delta));
}

/**
 * StatExprToken::copy()
 * ---------------------
 */
void
StatExprToken::copy(const StatExprToken &source)
{
  m_arith_symbol = source.m_arith_symbol;

  if (source.m_token_name != NULL) {
    m_token_name = ats_strdup(source.m_token_name);
  }

  m_token_type      = source.m_token_type;
  m_token_value     = source.m_token_value;
  m_token_value_min = source.m_token_value_min;
  m_token_value_max = source.m_token_value_max;

  if (source.m_token_value_delta) {
    m_token_value_delta                 = new StatDataSamples();
    m_token_value_delta->previous_time  = source.m_token_value_delta->previous_time;
    m_token_value_delta->current_time   = source.m_token_value_delta->current_time;
    m_token_value_delta->previous_value = source.m_token_value_delta->previous_value;
    m_token_value_delta->current_value  = source.m_token_value_delta->current_value;
  }

  m_node_var = source.m_node_var;
  m_sum_var  = source.m_sum_var;
}

/**
 * StatExprToken::assignTokenName()
 * --------------------------------
 *  Assign the token name. If the token is a predefined constant,
 *  assign the value as well. Also, assign the token type as well.
 */
void
StatExprToken::assignTokenName(const char *name)
{
  if (isdigit(name[0])) {
    // numerical constant
    m_token_name = ats_strdup("CONSTANT");
    m_token_type = RECD_CONST;
  } else {
    m_token_name = ats_strdup(name);
    assignTokenType();
  }

  switch (m_token_type) {
  case RECD_INT:
  case RECD_COUNTER:
  case RECD_FLOAT:
    break;
  case RECD_CONST:
    // assign pre-defined constant in here
    // constant will be stored as RecFloat type.
    if (!strcmp(m_token_name, "CONSTANT")) {
      m_token_value.rec_float = (RecFloat)atof(name);
    } else if (!strcmp(m_token_name, "$BYTES_TO_MB_SCALE")) {
      m_token_value.rec_float = (RecFloat)BYTES_TO_MB_SCALE;
    } else if (!strcmp(m_token_name, "$MBIT_TO_KBIT_SCALE")) {
      m_token_value.rec_float = (RecFloat)MBIT_TO_KBIT_SCALE;
    } else if (!strcmp(m_token_name, "$SECOND_TO_MILLISECOND_SCALE")) {
      m_token_value.rec_float = (RecFloat)SECOND_TO_MILLISECOND_SCALE;
    } else if (!strcmp(m_token_name, "$PCT_TO_INTPCT_SCALE")) {
      m_token_value.rec_float = (RecFloat)PCT_TO_INTPCT_SCALE;
    } else if (!strcmp(m_token_name, "$HRTIME_SECOND")) {
      m_token_value.rec_float = (RecFloat)HRTIME_SECOND;
    } else if (!strcmp(m_token_name, "$BYTES_TO_MBIT_SCALE")) {
      m_token_value.rec_float = (RecFloat)BYTES_TO_MBIT_SCALE;
    } else {
      mgmt_log(stderr, "[StatPro] ERROR: Undefined constant: %s\n", m_token_name);
      StatError = true;
    }
  case RECD_FX:
  default:
    break;
  }
}

/**
 * assignTokenType()
 * -----------------
 * Assign the proper token type based on the token name.
 * Do some token type conversion if necessary. Return true
 * if the token type is recognizable; false otherwise.
 */
bool
StatExprToken::assignTokenType()
{
  ink_assert(m_token_name != NULL);
  m_token_type = varType(m_token_name);

  if (m_token_name[0] == '$') {
    m_token_type = RECD_CONST;
  } else if (m_token_name[0] == '_') {
    m_token_type = RECD_FX;
  }

  if (m_token_value_delta) {
    m_token_value_delta->data_type = m_token_type;
  }

  // I'm guessing here that we want to check if we're still RECD_NULL,
  // it used to be INVALID, which is not in the m_token_type's enum. /leif
  return (m_token_type != RECD_NULL);
}

void
StatExprToken::clean()
{
  ats_free(m_token_name);
  delete m_token_value_delta;
}

/**
 * FOR DEBUGGING ONLY
 * Print the token according to its type in a human-readable format. :)
 */
void
StatExprToken::print(const char *prefix)
{
  if (m_token_name != NULL) {
    printf("%s\t%s\n", prefix, m_token_name);
  } else {
    printf("%s\t%c\n", prefix, m_arith_symbol);
  }
}

/**
 * StatExprToken::precedence()
 * ---------------------------
 * Return the binary operator precedence. The higher returning value,
 * the higher the precedence value.
 */
short
StatExprToken::precedence()
{
  switch (m_arith_symbol) {
  case '(':
    return 4;
  case '^': // fall through
  case '!':
    return 3;
  case '*': // fall through
  case '/':
    return 2;
  case '+': // fall through
  case '-':
    return 1;
  default:
    return -1;
  }
}

/**
 * StatExprToken::statVarSet()
 * ---------------------------
 * This method is responsible for ensuring the assigning value
 * fall within the min. and max. bound. If it's smaller than min.
 * or larger than max, then the error value is assigned. If no
 * error value is assigned, either min. or max. is assigned.
 */
bool
StatExprToken::statVarSet(RecDataT type, RecData value)
{
  RecData converted_value;

  if (StatError) {
    /* fix this after librecords is done
       mgmt_log(stderr,
       "[StatPro] ERROR in a statistics aggregation operations\n");
     */
    RecData err_value;
    RecDataZero(m_token_type, &err_value);
    return varSetData(m_token_type, m_token_name, err_value);
  }

  /*
   * do conversion if necessary.
   */
  if (m_token_type != type) {
    switch (m_token_type) {
    case RECD_INT:
    case RECD_COUNTER:
      if (type == RECD_NULL)
        converted_value = value;
      else if (type == RECD_INT || type == RECD_COUNTER || type == RECD_FX)
        converted_value.rec_int = value.rec_int;
      else if (type == RECD_FLOAT || type == RECD_CONST)
        converted_value.rec_int = (RecInt)value.rec_float;
      else
        Fatal("%s, invalid value type:%d\n", m_token_name, type);
      break;
    case RECD_FLOAT:
      if (type == RECD_NULL)
        converted_value = value;
      else if (type == RECD_INT || type == RECD_COUNTER || type == RECD_FX)
        converted_value.rec_float = (RecFloat)value.rec_int;
      else if (type == RECD_FLOAT || type == RECD_CONST)
        converted_value.rec_float = value.rec_float;
      else
        Fatal("%s, invalid value type:%d\n", m_token_name, type);
      break;
    default:
      Fatal("%s, unsupported token type:%d\n", m_token_name, m_token_type);
    }
  } else {
    converted_value = value;
  }

  if (RecDataCmp(m_token_type, converted_value, m_token_value_min) < 0) {
    value = m_token_value_min;
  } else if (RecDataCmp(m_token_type, converted_value, m_token_value_max) > 0) {
    value = m_token_value_max;
  }

  return varSetData(m_token_type, m_token_name, converted_value);
}

/***********************************************************************
                                                 StatExprList
 **********************************************************************/

/**
 * StatExprList::StatExprList()
 * ----------------------------
 */
StatExprList::StatExprList() : m_size(0)
{
}

/**
 * StatExprList::clean()
 * ---------------------
 */
void
StatExprList::clean()
{
  StatExprToken *temp = NULL;

  while ((temp = m_tokenList.dequeue())) {
    delete (temp);
    m_size -= 1;
  }
  ink_assert(m_size == 0);
}

void
StatExprList::enqueue(StatExprToken *entry)
{
  ink_assert(entry);
  m_tokenList.enqueue(entry);
  m_size += 1;
}

void
StatExprList::push(StatExprToken *entry)
{
  ink_assert(entry);
  m_tokenList.push(entry);
  m_size += 1;
}

StatExprToken *
StatExprList::dequeue()
{
  if (m_size == 0) {
    return NULL;
  }
  m_size -= 1;
  return (StatExprToken *)m_tokenList.dequeue();
}

StatExprToken *
StatExprList::pop()
{
  if (m_size == 0) {
    return NULL;
  }
  m_size -= 1;
  return m_tokenList.pop();
}

StatExprToken *
StatExprList::top()
{
  if (m_size == 0) {
    return NULL;
  }
  return m_tokenList.head;
}

StatExprToken *
StatExprList::first()
{
  if (m_size == 0) {
    return NULL;
  }
  return m_tokenList.head;
}

StatExprToken *
StatExprList::next(StatExprToken *current)
{
  if (!current) {
    return NULL;
  }
  return (current->link).next;
}

/**
 * StatExprList::print()
 * ---------------------
 *  Print the token in the expression in a human-readable format. :)
 */
void
StatExprList::print(const char *prefix)
{
  for (StatExprToken *token = first(); token; token = next(token)) {
    token->print(prefix);
  }
}

/**
 * StatExprToken::count()
 * ----------------------
 * Counts the number of token in the expression list and return it.
 */
unsigned
StatExprList::count()
{
  return m_size;
}

/***********************************************************************
                                                     StatObject
 **********************************************************************/

/**
 * StatObject::StatObject()
 * ------------------------
 */

StatObject::StatObject()
  : m_id(1),
    m_debug(false),
    m_expr_string(NULL),
    m_node_dest(NULL),
    m_cluster_dest(NULL),
    m_expression(NULL),
    m_postfix(NULL),
    m_last_update(-1),
    m_current_time(-1),
    m_update_interval(-1),
    m_stats_max(FLT_MAX),
    m_stats_min(FLT_MIN),
    m_has_max(false),
    m_has_min(false),
    m_has_delta(false)
{
}

StatObject::StatObject(unsigned identifier)
  : m_id(identifier),
    m_debug(false),
    m_expr_string(NULL),
    m_node_dest(NULL),
    m_cluster_dest(NULL),
    m_expression(NULL),
    m_postfix(NULL),
    m_last_update(-1),
    m_current_time(-1),
    m_update_interval(-1),
    m_stats_max(FLT_MAX),
    m_stats_min(FLT_MIN),
    m_has_max(false),
    m_has_min(false),
    m_has_delta(false)
{
}

/**
 * StatObject::clean()
 * -------------------
 */
void
StatObject::clean()
{
  ats_free(m_expr_string);
  delete m_node_dest;
  delete m_cluster_dest;
  delete m_postfix;
}

/**
 * StatObject::assignDst()
 * -----------------------
 */
void
StatObject::assignDst(const char *str, bool m_node_var, bool m_sum_var)
{
  if (StatDebug) {
    Debug(MODULE_INIT, "DESTINTATION: %s\n", str);
  }

  StatExprToken *statToken = new StatExprToken();

  statToken->assignTokenName(str);
  statToken->m_node_var = m_node_var;
  statToken->m_sum_var  = m_sum_var;

  // The type of dst token should be always not NULL
  if (statToken->m_token_type == RECD_NULL) {
    Fatal("token:%s, invalid token type!", statToken->m_token_name);
  }

  // Set max/min value
  if (m_has_max)
    RecDataSetFromFloat(statToken->m_token_type, &statToken->m_token_value_max, m_stats_max);
  else
    RecDataSetMax(statToken->m_token_type, &statToken->m_token_value_max);

  if (m_has_min)
    RecDataSetFromFloat(statToken->m_token_type, &statToken->m_token_value_min, m_stats_min);
  else
    RecDataSetMin(statToken->m_token_type, &statToken->m_token_value_min);

  if (m_node_var) {
    ink_assert(m_node_dest == NULL);
    m_node_dest = statToken;
  } else {
    ink_assert(m_cluster_dest == NULL);
    m_cluster_dest = statToken;
  }
}

/**
 * StatObject::assignExpr()
 * ------------------------
 */
void
StatObject::assignExpr(char *str)
{
  StatExprToken *statToken = NULL;

  if (StatDebug) {
    Debug(MODULE_INIT, "EXPRESSION: %s\n", str);
  }
  ink_assert(m_expr_string == NULL);
  // We take ownership here
  m_expr_string = str;

  Tokenizer exprTok(" ");
  exprTok.Initialize(str);
  tok_iter_state exprTok_state;
  const char *token = exprTok.iterFirst(&exprTok_state);

  ink_assert(m_expression == NULL);
  m_expression = new StatExprList();

  while (token) {
    statToken = new StatExprToken();

    if (isOperator(token[0])) {
      statToken->m_arith_symbol = token[0];
      ink_assert(statToken->m_token_name == NULL);

      if (StatDebug) {
        Debug(MODULE_INIT, "\toperator: ->%c<-\n", statToken->m_arith_symbol);
      }

    } else {
      ink_assert(statToken->m_arith_symbol == '\0');

      // delta
      if (token[0] == '#') {
        token += 1; // skip '#'
        statToken->m_token_value_delta                = new StatDataSamples();
        statToken->m_token_value_delta->previous_time = (ink_hrtime)0;
        statToken->m_token_value_delta->current_time  = (ink_hrtime)0;
        statToken->m_token_value_delta->data_type     = RECD_NULL;
        RecDataZero(RECD_NULL, &statToken->m_token_value_delta->previous_value);
        RecDataZero(RECD_NULL, &statToken->m_token_value_delta->current_value);
      }

      statToken->assignTokenName(token);

      if (StatDebug) {
        Debug(MODULE_INIT, "\toperand:  ->%s<-\n", token);
      }
    }

    token = exprTok.iterNext(&exprTok_state);
    m_expression->enqueue(statToken);
  }

  infix2postfix();
}

/**
 * StatObject::infix2postfix()
 * ---------------------------
 * Takes the infix "expression" and convert it to a postfix for future
 * evaluation.
 *
 * SIDE EFFECT: consume all token in "expression"
 */
void
StatObject::infix2postfix()
{
  StatExprList stack;
  StatExprToken *tempToken = NULL;
  StatExprToken *curToken  = NULL;
  m_postfix                = new StatExprList();

  while (m_expression->top()) {
    curToken = m_expression->dequeue();

    if (!isOperator(curToken->m_arith_symbol)) {
      // printf("I2P~: enqueue %s\n", curToken->m_token_name);
      m_postfix->enqueue(curToken);

    } else {
      ink_assert(curToken->m_arith_symbol != '\0');

      if (curToken->m_arith_symbol == '(') {
        stack.push(curToken);
      } else if (curToken->m_arith_symbol == ')') {
        tempToken = (StatExprToken *)stack.pop();

        while (tempToken->m_arith_symbol != '(') {
          // printf("I2P@: enqueue %c\n", tempToken->m_arith_symbol);
          m_postfix->enqueue(tempToken);
          tempToken = (StatExprToken *)stack.pop();
        }

        // Free up memory for ')'
        delete (curToken);
        delete (tempToken);

      } else {
        if (stack.count() == 0) {
          stack.push(curToken);
        } else {
          tempToken = (StatExprToken *)stack.top();

          while ((tempToken->m_arith_symbol != '(') && (tempToken->precedence() >= curToken->precedence())) {
            tempToken = (StatExprToken *)stack.pop(); // skip the (
            // printf("I2P$: enqueue %c\n", tempToken->m_arith_symbol);
            m_postfix->enqueue(tempToken);
            if (stack.count() == 0) {
              break;
            }
            tempToken = (StatExprToken *)stack.top();
          } // while

          stack.push(curToken);
        }
      }
    }
  }

  while (stack.count() > 0) {
    tempToken = (StatExprToken *)stack.pop();
    // printf("I2P?: enqueue %c\n", tempToken->m_arith_symbol);
    m_postfix->enqueue(tempToken);
  }

  // dump infix expression
  delete (m_expression);
  m_expression = NULL;
}

/**
 * StatObject::NodeStatEval()
 * --------------------------
 *
 *
 */
RecData
StatObject::NodeStatEval(RecDataT *result_type, bool cluster)
{
  StatExprList stack;
  StatExprToken *left     = NULL;
  StatExprToken *right    = NULL;
  StatExprToken *result   = NULL;
  StatExprToken *curToken = NULL;
  RecData tempValue;
  RecDataZero(RECD_NULL, &tempValue);

  *result_type = RECD_NULL;

  /* Express checkout lane -- Stat. object with on 1 source variable */
  if (m_postfix->count() == 1) {
    StatExprToken *src = m_postfix->top();

    // in librecords, not all statistics are register at initialization
    // must assign proper type if it is undefined.
    if (src->m_token_type == RECD_NULL) {
      src->assignTokenType();
    }

    *result_type = src->m_token_type;
    if (src->m_token_type == RECD_CONST) {
      tempValue = src->m_token_value;
    } else if (src->m_token_value_delta) {
      tempValue = src->m_token_value_delta->diff_value(src->m_token_name);
    } else if (!cluster) {
      if (!varDataFromName(src->m_token_type, src->m_token_name, &tempValue)) {
        RecDataZero(src->m_token_type, &tempValue);
      }
    } else {
      if (!overviewGenerator->varClusterDataFromName(src->m_token_type, src->m_token_name, &tempValue)) {
        RecDataZero(src->m_token_type, &tempValue);
      }
    }
  } else {
    /* standard postfix evaluation */
    for (StatExprToken *token = m_postfix->first(); token; token = m_postfix->next(token)) {
      /* carbon-copy the token. */
      curToken = new StatExprToken();
      curToken->copy(*token);

      if (!isOperator(curToken->m_arith_symbol)) {
        stack.push(curToken);
      } else {
        ink_assert(isOperator(curToken->m_arith_symbol));
        right = stack.pop();
        left  = stack.pop();

        if (left->m_token_type == RECD_NULL) {
          left->assignTokenType();
        }
        if (right->m_token_type == RECD_NULL) {
          right->assignTokenType();
        }

        result = StatBinaryEval(left, curToken->m_arith_symbol, right, cluster);

        stack.push(result);
        delete (curToken);
        delete (left);
        delete (right);
      }
    }

    /* should only be 1 value left on stack -- the resulting value */
    if (stack.count() > 1) {
      stack.print("\t");
      ink_assert(false);
    }

    *result_type = stack.top()->m_token_type;
    tempValue    = stack.top()->m_token_value;
  }

  return tempValue;
}

/**
 * StatObject::ClusterStatEval()
 * -----------------------------
 *
 *
 */
RecData
StatObject::ClusterStatEval(RecDataT *result_type)
{
  /* Sanity check */
  ink_assert(m_cluster_dest && !m_cluster_dest->m_node_var);

  // what is this?
  if ((m_node_dest == NULL) || (m_cluster_dest->m_sum_var == false)) {
    return NodeStatEval(result_type, true);
  } else {
    RecData tempValue;

    if (!overviewGenerator->varClusterDataFromName(m_node_dest->m_token_type, m_node_dest->m_token_name, &tempValue)) {
      *result_type = RECD_NULL;
      RecDataZero(*result_type, &tempValue);
    }

    return (tempValue);
  }
}

/**
 * StatObject::setTokenValue()
 * ---------------------------
 * The logic of the following code segment is the following.
 * The objective is to extract the appropriate right->m_token_value.
 * If 'right' is an intermediate value, nothing to do.
 * If m_token_type is RECD_CONST, nothing to do.
 * If m_token_type is RECD_FX, right->m_token_value is the diff. in time.
 * If m_token_type is either RECD_INT or RECD_FLOAT, it can either
 * by a cluster variable or a node variable.
 *     If it is a cluster variable, just use varClusterFloatFromName
 *     to set right->m_token_value.
 *     If it is a node variable, then it can either be a variable
 *     with delta. To determine whether it has a delta, simply search
 *     the m_token_name in the delta list. If found then it has delta.
 *     If it has delta then use the delta's diff. in value,
 *     otherwise simply set right->m_token_value with varFloatFromName.
 */
void
StatObject::setTokenValue(StatExprToken *token, bool cluster)
{
  if (token->m_token_name) {
    // it is NOT an intermediate value

    switch (token->m_token_type) {
    case RECD_CONST:
      break;

    case RECD_FX:
      // only support time function
      // use rec_int to store time value
      token->m_token_value.rec_int = (m_current_time - m_last_update);
      break;

    case RECD_INT: // fallthought
    case RECD_COUNTER:
    case RECD_FLOAT:
      if (cluster) {
        if (!overviewGenerator->varClusterDataFromName(token->m_token_type, token->m_token_name, &(token->m_token_value))) {
          RecDataZero(token->m_token_type, &token->m_token_value);
        }
      } else {
        if (token->m_token_value_delta) {
          token->m_token_value = token->m_token_value_delta->diff_value(token->m_token_name);
        } else {
          if (!varDataFromName(token->m_token_type, token->m_token_name, &(token->m_token_value))) {
            RecDataZero(token->m_token_type, &token->m_token_value);
          }
        } // delta?
      }   // cluster?
      break;

    default:
      if (StatDebug) {
        Debug(MODULE, "Unrecognized token \"%s\" of type %d.\n", token->m_token_name, token->m_token_type);
      }
    } // switch
  }   // m_token_name?
}

/**
 * StatObject::StatBinaryEval()
 * ------------------------
 * Take the left token, the right token, an binary operation and perform an
 * arithmatic operations on them. This function is responsible for getting the
 * correct value from:
 * - (1) node variable
 * - (2) node variable with a delta structure
 * - (3) cluster variable
 * - (4) an immediate value
 */
StatExprToken *
StatObject::StatBinaryEval(StatExprToken *left, char op, StatExprToken *right, bool cluster)
{
  RecData l, r;
  StatExprToken *result = new StatExprToken();
  result->m_token_type  = RECD_INT;

  if (left->m_token_type == RECD_NULL && right->m_token_type == RECD_NULL) {
    return result;
  }

  if (left->m_token_type != RECD_NULL) {
    setTokenValue(left, cluster);
    result->m_token_type = left->m_token_type;
  }

  if (right->m_token_type != RECD_NULL) {
    setTokenValue(right, cluster);
    switch (result->m_token_type) {
    case RECD_NULL:
      result->m_token_type = right->m_token_type;
      break;
    case RECD_FX:
    case RECD_INT:
    case RECD_COUNTER:
      /*
       * When types of left and right are different, select RECD_FLOAT
       * as result type. It's may lead to loss of precision when do
       * conversion, be careful!
       */
      if (right->m_token_type == RECD_FLOAT || right->m_token_type == RECD_CONST) {
        result->m_token_type = right->m_token_type;
      }
      break;
    case RECD_CONST:
    case RECD_FLOAT:
      break;
    default:
      Fatal("Unexpected RecData Type:%d", result->m_token_type);
      break;
    }
  }

  /*
   * We should make the operands with the same type before calculating.
   */
  RecDataZero(RECD_NULL, &l);
  RecDataZero(RECD_NULL, &r);

  if (left->m_token_type == right->m_token_type) {
    l = left->m_token_value;
    r = right->m_token_value;
  } else if (result->m_token_type != left->m_token_type) {
    if (left->m_token_type != RECD_NULL) {
      ink_assert(result->m_token_type == RECD_FLOAT || result->m_token_type == RECD_CONST);

      l.rec_float = (RecFloat)left->m_token_value.rec_int;
    }
    r = right->m_token_value;
    ink_assert(result->m_token_type == right->m_token_type);
  } else {
    l = left->m_token_value;
    if (right->m_token_type != RECD_NULL) {
      ink_assert(result->m_token_type == RECD_FLOAT || result->m_token_type == RECD_CONST);

      r.rec_float = (RecFloat)right->m_token_value.rec_int;
    }
    ink_assert(result->m_token_type == left->m_token_type);
  }

  /*
   * Start to calculate
   */
  switch (op) {
  case '+':
    result->m_token_value = RecDataAdd(result->m_token_type, l, r);
    break;

  case '-':
    result->m_token_value = RecDataSub(result->m_token_type, l, r);
    break;

  case '*':
    result->m_token_value = RecDataMul(result->m_token_type, l, r);
    break;

  case '/':
    RecData recTmp;
    RecDataZero(RECD_NULL, &recTmp);

    /*
     * Force the type of result to be RecFloat on div operation
     */
    if (result->m_token_type != RECD_FLOAT && result->m_token_type != RECD_CONST) {
      RecFloat t;

      result->m_token_type = RECD_FLOAT;

      t           = (RecFloat)l.rec_int;
      l.rec_float = t;

      t           = (RecFloat)r.rec_int;
      r.rec_float = t;
    }

    if (RecDataCmp(result->m_token_type, r, recTmp)) {
      result->m_token_value = RecDataDiv(result->m_token_type, l, r);
    }
    break;

  default:
    // should never reach here
    StatError = true;
  }

  return (result);
}

/***********************************************************************
                                                   StatObjectList
 **********************************************************************/

StatObjectList::StatObjectList() : m_size(0)
{
}

void
StatObjectList::clean()
{
  StatObject *temp = NULL;

  while ((temp = m_statList.dequeue())) {
    m_size -= 1;
    delete (temp);
  }

  ink_assert(m_size == 0);
}

void
StatObjectList::enqueue(StatObject *object)
{
  for (StatExprToken *token = object->m_postfix->first(); token; token = object->m_postfix->next(token)) {
    if (token->m_token_value_delta) {
      object->m_has_delta = true;
      break;
    }
  }

  m_statList.enqueue(object);
  m_size += 1;
}

StatObject *
StatObjectList::first()
{
  return m_statList.head;
}

StatObject *
StatObjectList::next(StatObject *current)
{
  return (current->link).next;
}

/**
 * StatObjectList::Eval()
 * ----------------------
 * The statisitic processor entry point to perform the calculation.
 */
short
StatObjectList::Eval()
{
  RecData tempValue;
  RecData result;
  RecDataT result_type;
  ink_hrtime threshold = 0;
  ink_hrtime delta     = 0;
  short count          = 0;

  RecDataZero(RECD_NULL, &tempValue);
  RecDataZero(RECD_NULL, &result);

  for (StatObject *object = first(); object; object = next(object)) {
    StatError = false;
    StatDebug = object->m_debug;

    if (StatDebug) {
      Debug(MODULE, "\n##### %d #####\n", object->m_id);
    }

    if (object->m_update_interval <= 0) {
      // non-time statistics
      object->m_current_time = ink_get_hrtime_internal();

      if (object->m_node_dest) {
        result = object->NodeStatEval(&result_type, false);
        object->m_node_dest->statVarSet(result_type, result);
      }

      if (object->m_cluster_dest) {
        result = object->ClusterStatEval(&result_type);
        object->m_cluster_dest->statVarSet(result_type, result);
      }

      object->m_last_update = object->m_current_time;
    } else {
      // timed statisitics
      object->m_current_time = ink_get_hrtime_internal();

      threshold = object->m_update_interval * HRTIME_SECOND;
      delta     = object->m_current_time - object->m_last_update;

      if (StatDebug) {
        Debug(MODULE, "\tUPDATE:%" PRId64 " THRESHOLD:%" PRId64 ", DELTA:%" PRId64 "\n", object->m_update_interval, threshold,
              delta);
      }

      /* Should we do the calculation? */
      if ((delta > threshold) ||                              /* sufficient elapsed time? */
          (object->m_last_update == -1) ||                    /*       first time?       */
          (object->m_last_update > object->m_current_time)) { /*wrapped */

        if (StatDebug) {
          if (delta > threshold) {
            Debug(MODULE, "\t\tdelta > threshold IS TRUE!\n");
          }
          if (object->m_last_update == -1) {
            Debug(MODULE, "\t\tm_last_update = -1 IS TRUE!\n");
          }
          if (object->m_last_update > object->m_current_time) {
            Debug(MODULE, "\t\tm_last_update > m_current_time IS TRUE\n");
          }
        }

        if (!object->m_has_delta) {
          if (StatDebug) {
            Debug(MODULE, "\tEVAL: Simple time-condition.\n");
          }

          if (object->m_node_dest) {
            result = object->NodeStatEval(&result_type, false);
            object->m_node_dest->statVarSet(result_type, result);
          }

          if (object->m_cluster_dest) {
            result = object->ClusterStatEval(&result_type);
            object->m_cluster_dest->statVarSet(result_type, result);
          }

          object->m_last_update = object->m_current_time;
        } else {
          /* has delta */
          if (StatDebug) {
            Debug(MODULE, "\tEVAL: Complicated time-condition.\n");
          }
          // scroll old values
          for (StatExprToken *token = object->m_postfix->first(); token; token = object->m_expression->next(token)) {
            // in librecords, not all statistics are register at initialization
            // must assign proper type if it is undefined.
            if (!isOperator(token->m_arith_symbol) && token->m_token_type == RECD_NULL) {
              token->assignTokenType();
            }

            if (token->m_token_value_delta) {
              if (!varDataFromName(token->m_token_type, token->m_token_name, &tempValue)) {
                RecDataZero(RECD_NULL, &tempValue);
              }

              token->m_token_value_delta->previous_time  = token->m_token_value_delta->current_time;
              token->m_token_value_delta->previous_value = token->m_token_value_delta->current_value;
              token->m_token_value_delta->current_time   = object->m_current_time;
              token->m_token_value_delta->current_value  = tempValue;
            }
          }

          if (delta > threshold) {
            if (object->m_node_dest) {
              result = object->NodeStatEval(&result_type, false);
              object->m_node_dest->statVarSet(result_type, result);
            }

            if (object->m_cluster_dest) {
              result = object->ClusterStatEval(&result_type);
              object->m_cluster_dest->statVarSet(result_type, result);
            }

            object->m_last_update = object->m_current_time;
          } else {
            if (StatDebug) {
              Debug(MODULE, "\tEVAL: Timer not expired, do nothing\n");
            }
          }
        } /* delta? */
      } else {
        if (StatDebug) {
          Debug(MODULE, "\tEVAL: Timer not expired, nor 1st time, nor wrapped, SORRY!\n");
        }
      } /* timed event */
    }
    count += 1;
  } /* for */

  return count;
} /* Eval() */

/**
 * StatObjectList::print()
 * --------------------------
 * Print the list of of statistics object in a human-readable format. :)
 */
void
StatObjectList::print(const char *prefix)
{
  for (StatObject *object = first(); object; object = next(object)) {
    if (StatDebug) {
      Debug(MODULE, "\n%sSTAT OBJECT#: %d\n", prefix, object->m_id);
    }

    if (object->m_expression) {
      object->m_expression->print("\t");
    }

    if (object->m_postfix) {
      object->m_postfix->print("\t");
    }
  }
}
