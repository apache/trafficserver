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

#include "ink_config.h"
#include "StatType.h"
#include "MgmtUtils.h"
#include "ink_hrtime.h"

bool StatError = false;         // global error flag
bool StatDebug = false;         // global debug flag

/**
 * StatExprToken()
 * ---------------
 */
StatExprToken::StatExprToken()
  : m_arith_symbol('\0'),
    m_token_name(NULL),
    m_token_type((StatDataT)RECD_NULL),
    m_token_value(0.0),
    m_token_value_max(FLT_MAX),
    m_token_value_min(-1 * FLT_MAX), m_token_value_delta(NULL), m_sum_var(false), m_node_var(true)
{
}


/**
 * StatExprToken::copy()
 * ---------------------
 */
void
StatExprToken::copy(const StatExprToken & source)
{
  m_arith_symbol = source.m_arith_symbol;

  if (source.m_token_name != NULL) {
    m_token_name = ats_strdup(source.m_token_name);
  }

  m_token_type = source.m_token_type;
  m_token_value = source.m_token_value;
  m_token_value_min = source.m_token_value_min;
  m_token_value_max = source.m_token_value_max;

  if (source.m_token_value_delta) {
    m_token_value_delta = NEW(new StatFloatSamples());
    m_token_value_delta->previous_time = source.m_token_value_delta->previous_time;
    m_token_value_delta->current_time = source.m_token_value_delta->current_time;
    m_token_value_delta->previous_value = source.m_token_value_delta->previous_value;
    m_token_value_delta->current_value = source.m_token_value_delta->current_value;
  }

  m_node_var = source.m_node_var;
  m_sum_var = source.m_sum_var;
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
    m_token_type = STAT_CONST;

  } else {

    m_token_name = ats_strdup(name);
    assignTokenType();
  }

  switch (m_token_type) {
  case STAT_INT:
    if (StatDebug) {
      StatInt tempInt;
      if (!varIntFromName(m_token_name, &tempInt)) {
        tempInt = (RecInt) ERROR_VALUE;
      }
      Debug(MODULE_INIT, "\tvar: %s, type: %d, value: %" PRId64 "\n", m_token_name, m_token_type, tempInt);
    }
    break;

  case STAT_FLOAT:
    if (StatDebug) {
      StatFloat tempFloat;
      if (!varFloatFromName(m_token_name, &tempFloat)) {
        tempFloat = (RecFloat) ERROR_VALUE;
      }
      Debug(MODULE_INIT, "\tvar: %s, type: %d, value: %f\n", m_token_name, m_token_type, tempFloat);
    }
    break;

  case STAT_CONST:
    // assign pre-defined constant in here
    if (!strcmp(m_token_name, "CONSTANT")) {
      m_token_value = (StatFloat) atof(name);
    } else if (!strcmp(m_token_name, "$BYTES_TO_MB_SCALE")) {
      m_token_value = (StatFloat) BYTES_TO_MB_SCALE;
    } else if (!strcmp(m_token_name, "$MBIT_TO_KBIT_SCALE")) {
      m_token_value = (StatFloat) MBIT_TO_KBIT_SCALE;
    } else if (!strcmp(m_token_name, "$SECOND_TO_MILLISECOND_SCALE")) {
      m_token_value = (StatFloat) SECOND_TO_MILLISECOND_SCALE;
    } else if (!strcmp(m_token_name, "$PCT_TO_INTPCT_SCALE")) {
      m_token_value = (StatFloat) PCT_TO_INTPCT_SCALE;
    } else if (!strcmp(m_token_name, "$HRTIME_SECOND")) {
      m_token_value = (StatFloat) HRTIME_SECOND;
    } else if (!strcmp(m_token_name, "$BYTES_TO_MBIT_SCALE")) {
      m_token_value = (StatFloat) BYTES_TO_MBIT_SCALE;
    } else {
      mgmt_log(stderr, "[StatPro] ERROR: Undefined constant: %s\n", m_token_name);
      StatError = true;
    }

    if (StatDebug) {
      Debug(MODULE_INIT, "\tconst: %s, type: %d, value: %f\n", m_token_name, m_token_type, m_token_value);
    }
    break;

  case STAT_FX:
    if (StatDebug) {
      Debug(MODULE_INIT, "\tfunction: %s, type: %d\n", m_token_name, m_token_type);
    }
    break;

  default:
    /*
       mgmt_log(stderr, "[StatPro] ERROR: Undefined token: %s\n",
       m_token_name);
       StatError = true;
     */
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
bool StatExprToken::assignTokenType()
{
  ink_debug_assert(m_token_name != NULL);
  m_token_type = (StatDataT)varType(m_token_name);

  if (m_token_name[0] == '$') {
    m_token_type = STAT_CONST;
  } else if (m_token_name[0] == '_') {
    m_token_type = STAT_FX;
  }

  if (m_token_type == STAT_COUNTER) {
    m_token_type = STAT_INT;
  }
  // I'm guessing here that we want to check if we're still RECD_NULL,
  // it used to be INVALID, which is not in the m_token_type's enum. /leif
  return (m_token_type != (StatDataT)RECD_NULL);
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
  case '^':                    // fall through
  case '!':
    return 3;
  case '*':                    // fall through
  case '/':
    return 2;
  case '+':                    // fall through
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
bool StatExprToken::statVarSet(StatFloat value)
{
  if (StatError) {
    /* fix this after librecords is done
       mgmt_log(stderr,
       "[StatPro] ERROR in a statistics aggregation operations\n");
     */
    return varSetFloat(m_token_name, ERROR_VALUE, true);
  }

  if (value < m_token_value_min) {
    if (StatDebug) {
      Debug(MODULE, "[StatPro] Reset min. value: %f < %f\n", value, m_token_value_min);
    }
    value = m_token_value_min;
  }

  if (value > m_token_value_max) {
    if (StatDebug) {
      Debug(MODULE, "[StatPro] Reset max. value: %f > %f\n", value, m_token_value_max);
    }
    value = m_token_value_max;
  }

  return varSetFloat(m_token_name, value, true);
}


/***********************************************************************
					    	 StatExprList
 **********************************************************************/

/**
 * StatExprList::StatExprList()
 * ----------------------------
 */
StatExprList::StatExprList()
 : m_size(0)
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
    delete(temp);
    m_size -= 1;
  }
  ink_assert(m_size == 0);
}


void
StatExprList::enqueue(StatExprToken * entry)
{
  ink_assert(entry);
  m_tokenList.enqueue(entry);
  m_size += 1;
}


void
StatExprList::push(StatExprToken * entry)
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
  return (StatExprToken *) m_tokenList.dequeue();
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
StatExprList::next(StatExprToken * current)
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
  for (StatExprToken * token = first(); token; token = next(token)) {
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
   m_current_time(-1), m_update_interval(-1), m_stats_max(FLT_MAX), m_stats_min(-1 * FLT_MAX), m_has_delta(false)
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
    m_current_time(-1), m_update_interval(-1), m_stats_max(FLT_MAX), m_stats_min(-1 * FLT_MAX), m_has_delta(false)
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

  StatExprToken *statToken = NEW(new StatExprToken());

  statToken->assignTokenName(str);
  statToken->m_node_var = m_node_var;
  statToken->m_sum_var = m_sum_var;

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
  ink_debug_assert(m_expr_string == NULL);
  // We take ownership here
  m_expr_string = str;

  Tokenizer exprTok(" ");
  exprTok.Initialize(str);
  tok_iter_state exprTok_state;
  const char *token = exprTok.iterFirst(&exprTok_state);

  ink_debug_assert(m_expression == NULL);
  m_expression = NEW(new StatExprList());

  while (token) {

    statToken = NEW(new StatExprToken());

    if (isOperator(token[0])) {

      statToken->m_arith_symbol = token[0];
      ink_debug_assert(statToken->m_token_name == NULL);

      if (StatDebug) {
        Debug(MODULE_INIT, "\toperator: ->%c<-\n", statToken->m_arith_symbol);
      }

    } else {

      ink_debug_assert(statToken->m_arith_symbol == '\0');

      // delta
      if (token[0] == '#') {

        token += 1;             // skip '#'
        statToken->m_token_value_delta = NEW(new StatFloatSamples());
        statToken->m_token_value_delta->previous_time = (ink_hrtime) 0;
        statToken->m_token_value_delta->current_time = (ink_hrtime) 0;
        statToken->m_token_value_delta->previous_value = (StatFloat) 0.0;
        statToken->m_token_value_delta->current_value = (StatFloat) 0.0;

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
  StatExprToken *curToken = NULL;
  m_postfix = NEW(new StatExprList());

  while (m_expression->top()) {
    curToken = m_expression->dequeue();

    if (!isOperator(curToken->m_arith_symbol)) {
      //printf("I2P~: enqueue %s\n", curToken->m_token_name);
      m_postfix->enqueue(curToken);

    } else {
      ink_debug_assert(curToken->m_arith_symbol != '\0');

      if (curToken->m_arith_symbol == '(') {
        stack.push(curToken);
      } else if (curToken->m_arith_symbol == ')') {
        tempToken = (StatExprToken *) stack.pop();

        while (tempToken->m_arith_symbol != '(') {
          //printf("I2P@: enqueue %c\n", tempToken->m_arith_symbol);
          m_postfix->enqueue(tempToken);
          tempToken = (StatExprToken *) stack.pop();
        }

        // Free up memory for ')'
        delete(curToken);
        delete(tempToken);

      } else {
        if (stack.count() == 0) {
          stack.push(curToken);
        } else {
          tempToken = (StatExprToken *) stack.top();

          while ((tempToken->m_arith_symbol != '(') && (tempToken->precedence() >= curToken->precedence())) {
            tempToken = (StatExprToken *) stack.pop();  // skip the (
            //printf("I2P$: enqueue %c\n", tempToken->m_arith_symbol);
            m_postfix->enqueue(tempToken);
            if (stack.count() == 0) {
              break;
            }
            tempToken = (StatExprToken *) stack.top();
          }                     // while

          stack.push(curToken);
        }
      }
    }
  }

  while (stack.count() > 0) {
    tempToken = (StatExprToken *) stack.pop();
    //printf("I2P?: enqueue %c\n", tempToken->m_arith_symbol);
    m_postfix->enqueue(tempToken);
  }

  // dump infix expression
  delete(m_expression);
  m_expression = NULL;
}


/**
 * StatObject::NodeStatEval()
 * --------------------------
 *
 *
 */
StatFloat StatObject::NodeStatEval(bool cluster)
{
  StatExprList stack;
  StatExprToken *left = NULL;
  StatExprToken *right = NULL;
  StatExprToken *result = NULL;
  StatExprToken *curToken = NULL;
  StatFloat tempValue = ERROR_VALUE;

  /* Express checkout lane -- Stat. object with on 1 source variable */
  if (m_postfix->count() == 1) {
    StatExprToken *
      src = m_postfix->top();

    // in librecords, not all statistics are register at initialization
    // must assign proper type if it is undefined.
    if (src->m_token_type == (StatDataT)RECD_NULL) {
      src->assignTokenType();
    }

    if (src->m_token_type == STAT_CONST) {
      tempValue = src->m_token_value;
    } else if (src->m_token_value_delta) {
      tempValue = src->m_token_value_delta->diff_value();;
    } else if (!cluster) {
      if (!varFloatFromName(src->m_token_name, &tempValue)) {
        tempValue = (RecFloat) ERROR_VALUE;
      }
    } else {
      if (!overviewGenerator->varClusterFloatFromName(src->m_token_name, &tempValue)) {
        tempValue = (RecFloat) ERROR_VALUE;
      }
    }

    if (StatDebug) {
      Debug(MODULE, "\tExpress checkout : %s:%f\n", src->m_token_name, tempValue);
    }
  } else {

    /* standard postfix evaluation */
    for (StatExprToken * token = m_postfix->first(); token; token = m_postfix->next(token)) {
      /* carbon-copy the token. */
      curToken = NEW(new StatExprToken());
      curToken->copy(*token);

      if (!isOperator(curToken->m_arith_symbol)) {
        stack.push(curToken);
      } else {
        ink_debug_assert(isOperator(curToken->m_arith_symbol));
        right = stack.pop();
        left = stack.pop();

        if (left->m_token_type == (StatDataT)RECD_NULL) {
          left->assignTokenType();
        }
        if (right->m_token_type == (StatDataT)RECD_NULL) {
          right->assignTokenType();
        }

        result = StatBinaryEval(left, curToken->m_arith_symbol, right, cluster);

        stack.push(result);
        delete(curToken);
        delete(left);
        delete(right);
      }
    }

    /* should only be 1 value left on stack -- the resulting value */
    if (stack.count() > 1) {
      stack.print("\t");
      ink_debug_assert(false);
    }

    tempValue = stack.top()->m_token_value;
  }

  return (tempValue);

}


/**
 * StatObject::ClusterStatEval()
 * -----------------------------
 *
 *
 */
StatFloat StatObject::ClusterStatEval()
{
  StatFloat tempValue = ERROR_VALUE;

  /* Sanity check */
  ink_debug_assert(m_cluster_dest && !m_cluster_dest->m_node_var);

  // what is this?
  if ((m_node_dest == NULL) || (m_cluster_dest->m_sum_var == false)) {
    return NodeStatEval(true);
  } else {
    if (!overviewGenerator->varClusterFloatFromName(m_node_dest->m_token_name, &tempValue)) {
      tempValue = (RecFloat) ERROR_VALUE;
    }
    if (StatDebug) {
      Debug(MODULE, "Exp. chkout write: %s:%f\n", m_node_dest->m_token_name, tempValue);
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
 * If m_token_type is STAT_CONST, nothing to do.
 * If m_token_type is STAT_FX, right->m_token_value is the diff. in time.
 * If m_token_type is either STAT_INT or STAT_FLOAT, it can either
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
StatObject::setTokenValue(StatExprToken * token, bool cluster)
{
  if (token->m_token_name) {
    // it is NOT an intermediate value

    switch (token->m_token_type) {
    case STAT_CONST:
      break;

    case STAT_FX:
      // only support time function
      token->m_token_value = (StatFloat) (m_current_time - m_last_update);
      if (StatDebug) {
        Debug(MODULE, "m_current_time(%"  PRId64 ") - m_last_update(%"  PRId64 ") = %"  PRId64 "\n",
              (int64_t)m_current_time, (int64_t)m_last_update, (int64_t)token->m_token_value);
      }
      break;

    case STAT_INT:             // fallthought
    case STAT_FLOAT:
      if (cluster) {
        if (!overviewGenerator->varClusterFloatFromName(token->m_token_name, &(token->m_token_value))) {
          token->m_token_value = (RecFloat) ERROR_VALUE;
        }
      } else {
        if (token->m_token_value_delta) {
          token->m_token_value = token->m_token_value_delta->diff_value();

          if (StatDebug) {
            Debug(MODULE, "\tDelta value: %f %f %f\n",
                  token->m_token_value_delta->previous_value,
                  token->m_token_value_delta->current_value, token->m_token_value);
          }
        } else {
          if (!varFloatFromName(token->m_token_name, &(token->m_token_value))) {
            token->m_token_value = (RecFloat) ERROR_VALUE;
          }
        }                       // delta?
      }                         // cluster?
      break;

    default:
      if (StatDebug) {
        Debug(MODULE, "Unrecognized token \"%s\" of type %d.\n", token->m_token_name, token->m_token_type);
      }
    }                           // switch
  }                             // m_token_name?
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

StatExprToken *StatObject::StatBinaryEval(StatExprToken * left, char op, StatExprToken * right, bool cluster)
{
  StatExprToken *result = NEW(new StatExprToken());
  result->m_token_type = STAT_FLOAT;

  setTokenValue(left, cluster);
  setTokenValue(right, cluster);

  switch (op) {
  case '+':
    result->m_token_value = (StatFloat) (left->m_token_value + right->m_token_value);
    break;

  case '-':
    result->m_token_value = (StatFloat) (left->m_token_value - right->m_token_value);
    break;

  case '*':
    result->m_token_value = (StatFloat) (left->m_token_value * right->m_token_value);
    break;

  case '/':
    result->m_token_value = (StatFloat) ((right->m_token_value == 0) ?
                                         0.0 : (left->m_token_value / right->m_token_value));
    break;

  default:
    // should never reach here
    StatError = true;
  }

  if (StatDebug) {
    Debug(MODULE, "%s(%f) %c %s(%f) = %f\n",
          left->m_token_name ? left->m_token_name : "in stack",
          left->m_token_value,
          op, right->m_token_name ? right->m_token_name : "in stack", right->m_token_value, result->m_token_value);
  }

  return (result);
}


/***********************************************************************
 					    	   StatObjectList
 **********************************************************************/

StatObjectList::StatObjectList()
 : m_size(0)
{
}


void
StatObjectList::clean()
{
  StatObject *temp = NULL;

  while ((temp = m_statList.dequeue())) {
    m_size -= 1;
    delete(temp);
  }

  ink_assert(m_size == 0);
}


void
StatObjectList::enqueue(StatObject * object)
{
  for (StatExprToken * token = object->m_postfix->first(); token; token = object->m_postfix->next(token)) {
    if (token->m_token_value_delta) {
      object->m_has_delta = true;
      break;
    }
  }

  if (object->m_node_dest) {
    object->m_node_dest->m_token_value_max = object->m_stats_max;
    object->m_node_dest->m_token_value_min = object->m_stats_min;
  }

  if (object->m_cluster_dest) {
    object->m_cluster_dest->m_token_value_max = object->m_stats_max;
    object->m_cluster_dest->m_token_value_min = object->m_stats_min;
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
StatObjectList::next(StatObject * current)
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
  StatFloat tempValue = ERROR_VALUE;
  StatFloat result = ERROR_VALUE;
  ink_hrtime threshold = 0;
  ink_hrtime delta = 0;
  short count = 0;

  for (StatObject * object = first(); object; object = next(object)) {
    StatError = false;
    StatDebug = object->m_debug;

    if (StatDebug) {
      Debug(MODULE, "\n##### %d #####\n", object->m_id);
    }

    if (object->m_update_interval <= 0) {
      // non-time statistics
      object->m_current_time = ink_get_hrtime_internal();

      if (object->m_node_dest) {
        result = object->NodeStatEval(false);
        object->m_node_dest->statVarSet(result);
        if (StatDebug) {
          Debug(MODULE, "\t==>Result: %s -> %f\n", object->m_node_dest->m_token_name, result);
        }
      }

      if (object->m_cluster_dest) {
        result = object->ClusterStatEval();
        object->m_cluster_dest->statVarSet(result);
        if (StatDebug) {
          Debug(MODULE, "\t==>Result: %s -> %f\n", object->m_cluster_dest->m_token_name, result);
        }
      }

      object->m_last_update = object->m_current_time;
    } else {
      // timed statisitics
      object->m_current_time = ink_get_hrtime_internal();

      threshold = object->m_update_interval * HRTIME_SECOND;
      delta = object->m_current_time - object->m_last_update;

      if (StatDebug) {
        Debug(MODULE, "\tUPDATE:%" PRId64 " THRESHOLD:%" PRId64 ", DELTA:%" PRId64 "\n", object->m_update_interval, threshold, delta);
      }

      /* Should we do the calculation? */
      if ((delta > threshold) ||        /* sufficient elapsed time? */
          (object->m_last_update == -1) ||      /*       first time?       */
          (object->m_last_update > object->m_current_time)) {   /*wrapped */

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
            result = object->NodeStatEval(false);
            object->m_node_dest->statVarSet(result);
            if (StatDebug) {
              Debug(MODULE, "\t==>Result: %s -> %f\n", object->m_node_dest->m_token_name, result);
            }
          }

          if (object->m_cluster_dest) {
            result = object->ClusterStatEval();
            object->m_cluster_dest->statVarSet(result);
            if (StatDebug) {
              Debug(MODULE, "\t==>Result: %s -> %f\n", object->m_cluster_dest->m_token_name, result);
            }
          }

          object->m_last_update = object->m_current_time;
        } else {
          /* has delta */
          if (StatDebug) {
            Debug(MODULE, "\tEVAL: Complicated time-condition.\n");
          }
          // scroll old values
          for (StatExprToken * token = object->m_postfix->first(); token; token = object->m_expression->next(token)) {
            if (token->m_token_value_delta) {
              if (!varFloatFromName(token->m_token_name, &tempValue)) {
                tempValue = (RecFloat) ERROR_VALUE;
              }

              token->m_token_value_delta->previous_time = token->m_token_value_delta->current_time;
              token->m_token_value_delta->previous_value = token->m_token_value_delta->current_value;
              token->m_token_value_delta->current_time = object->m_current_time;
              token->m_token_value_delta->current_value = tempValue;
            }
          }

          if (delta > threshold) {
            if (object->m_node_dest) {
              result = object->NodeStatEval(false);
              object->m_node_dest->statVarSet(result);
              if (StatDebug) {
                Debug(MODULE, "\t==>Result: %s -> %f\n", object->m_node_dest->m_token_name, result);
              }
            }

            if (object->m_cluster_dest) {
              result = object->ClusterStatEval();
              object->m_cluster_dest->statVarSet(result);
              if (StatDebug) {
                Debug(MODULE, "\t==>Result: %s -> %f\n", object->m_cluster_dest->m_token_name, result);
              }
            }

            object->m_last_update = object->m_current_time;
          } else {
            if (StatDebug) {
              Debug(MODULE, "\tEVAL: Timer not expired, do nothing\n");
            }
          }
        }                       /* delta? */
      } else {
        if (StatDebug) {
          Debug(MODULE, "\tEVAL: Timer not expired, nor 1st time, nor wrapped, SORRY!\n");
        }
      }                         /* timed event */
    }
    count += 1;
  }                             /* for */

  return count;
}                               /* Eval() */


/**
 * StatObjectList::print()
 * --------------------------
 * Print the list of of statistics object in a human-readable format. :)
 */
void
StatObjectList::print(const char *prefix)
{
  for (StatObject * object = first(); object; object = next(object)) {
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
