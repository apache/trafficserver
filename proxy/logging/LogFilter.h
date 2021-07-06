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

#pragma once

#include "tscore/ink_platform.h"
#include "tscore/IpMap.h"
#include "tscore/Ptr.h"
#include "LogAccess.h"
#include "LogField.h"
#include "LogFormat.h"

/*-------------------------------------------------------------------------
  LogFilter

  This is an abstract base class from which particular filters can be
  derived.  Each filter must implement the "toss_this_entry" member
  function which, given a LogAccess object, returns true if
  the log entry is to be tossed out.
  -------------------------------------------------------------------------*/
class LogFilter : public RefCountObj
{
public:
  enum Type {
    INT_FILTER = 0,
    STRING_FILTER,
    IP_FILTER,
    N_TYPES,
  };

  enum Action {
    REJECT = 0,
    ACCEPT,
    WIPE_FIELD_VALUE,
    N_ACTIONS,
  };

  static const char *ACTION_NAME[];

  // all operators "positive" (i.e., there is no NOMATCH operator anymore)
  // because one can specify through the "action" field if the record should
  // be kept or tossed away
  //
  enum Operator {
    MATCH = 0,
    CASE_INSENSITIVE_MATCH,
    CONTAIN,
    CASE_INSENSITIVE_CONTAIN,
    N_OPERATORS,
  };

  static const char *OPERATOR_NAME[];

  LogFilter(const char *name, LogField *field, Action action, Operator oper);
  ~LogFilter() override;

  char *
  name() const
  {
    return m_name;
  }

  Type
  type() const
  {
    return m_type;
  }

  size_t
  get_num_values() const
  {
    return m_num_values;
  }

  virtual bool toss_this_entry(LogAccess *lad) = 0;
  virtual bool wipe_this_entry(LogAccess *lad) = 0;
  virtual void display(FILE *fd = stdout)      = 0;

  static LogFilter *parse(const char *name, Action action, const char *condition);

protected:
  char *m_name;
  LogField *m_field;
  Action m_action; // the action this filter takes
  Operator m_operator;
  Type m_type;
  size_t m_num_values; // the number of comparison values

public:
  LINK(LogFilter, link); // so we can create a LogFilterList

  // noncopyable
  LogFilter(const LogFilter &rhs) = delete;
  LogFilter &operator=(LogFilter &rhs) = delete;

private:
  // -- member functions that are not allowed --
  LogFilter();
};

/*-------------------------------------------------------------------------
  LogFilterString

  Filter for string fields.
  -------------------------------------------------------------------------*/
class LogFilterString : public LogFilter
{
public:
  LogFilterString(const char *name, LogField *field, Action a, Operator o, char *value);
  LogFilterString(const char *name, LogField *field, Action a, Operator o, size_t num_values, char **value);
  LogFilterString(const LogFilterString &rhs);
  ~LogFilterString() override;
  bool operator==(LogFilterString &rhs);

  bool toss_this_entry(LogAccess *lad) override;
  bool wipe_this_entry(LogAccess *lad) override;
  void display(FILE *fd = stdout) override;

  // noncopyable
  LogFilterString &operator=(LogFilterString &rhs) = delete;

private:
  char **m_value = nullptr; // the array of values

  // these are used to speed up case insensitive operations
  //
  char **m_value_uppercase = nullptr; // m_value in all uppercase
  size_t *m_length         = nullptr; // length of m_value string

  void _setValues(size_t n, char **value);

  // note: OperatorFunction's must return 0 (zero) if condition is satisfied
  // (as strcmp does)
  typedef int (*OperatorFunction)(const char *, const char *);

  static int
  _isSubstring(const char *s0, const char *s1)
  {
    // return 0 if s1 is substring of s0 and 1 otherwise
    // this reverse behavior is to conform to the behavior of strcmp
    // which returns 0 if strings match
    return (strstr(s0, s1) == nullptr ? 1 : 0);
  }

  enum LengthCondition {
    DATA_LENGTH_EQUAL = 0,
    DATA_LENGTH_LARGER,
  };

  inline bool _checkCondition(OperatorFunction f, const char *field_value, size_t field_value_length, char **val,
                              LengthCondition lc);

  inline bool _checkConditionAndWipe(OperatorFunction f, char **field_value, size_t field_value_length, char **val,
                                     const char *uppercase_field_value, LengthCondition lc);

  // -- member functions that are not allowed --
  LogFilterString();
};

/*-------------------------------------------------------------------------
  LogFilterInt

  Filter for int fields.
  -------------------------------------------------------------------------*/
class LogFilterInt : public LogFilter
{
public:
  LogFilterInt(const char *name, LogField *field, Action a, Operator o, int64_t value);
  LogFilterInt(const char *name, LogField *field, Action a, Operator o, size_t num_values, int64_t *value);
  LogFilterInt(const char *name, LogField *field, Action a, Operator o, char *values);
  LogFilterInt(const LogFilterInt &rhs);
  ~LogFilterInt() override;
  bool operator==(LogFilterInt &rhs);

  bool toss_this_entry(LogAccess *lad) override;
  bool wipe_this_entry(LogAccess *lad) override;
  void display(FILE *fd = stdout) override;

  // noncopyable
  LogFilterInt &operator=(LogFilterInt &rhs) = delete;

private:
  int64_t *m_value = nullptr; // the array of values

  void _setValues(size_t n, int64_t *value);
  int _convertStringToInt(char *val, int64_t *ival, LogFieldAliasMap *map);

  // -- member functions that are not allowed --
  LogFilterInt();
};

/*-------------------------------------------------------------------------
  LogFilterIP

  Filter for IP fields using IpAddr.
  -------------------------------------------------------------------------*/
class LogFilterIP : public LogFilter
{
public:
  LogFilterIP(const char *name, LogField *field, Action a, Operator o, IpAddr value);
  LogFilterIP(const char *name, LogField *field, Action a, Operator o, size_t num_values, IpAddr *value);
  LogFilterIP(const char *name, LogField *field, Action a, Operator o, char *values);
  LogFilterIP(const LogFilterIP &rhs);
  ~LogFilterIP() override;

  bool operator==(LogFilterIP &rhs);

  bool toss_this_entry(LogAccess *lad) override;
  bool wipe_this_entry(LogAccess *lad) override;
  void display(FILE *fd = stdout) override;

  // noncopyable
  LogFilterIP &operator=(LogFilterIP &rhs) = delete;

private:
  IpMap m_map;

  /// Initialization common to all constructors.
  void init();

  void displayRanges(FILE *fd);
  void displayRange(FILE *fd, IpMap::iterator const &iter);

  // Checks for a match on this filter.
  bool is_match(LogAccess *lad);

  // -- member functions that are not allowed --
  LogFilterIP();
};

bool filters_are_equal(LogFilter *filt1, LogFilter *filt2);

/*-------------------------------------------------------------------------
  LogFilterList
  -------------------------------------------------------------------------*/
class LogFilterList
{
public:
  LogFilterList();
  ~LogFilterList();
  bool operator==(LogFilterList &);

  void add(LogFilter *filter, bool copy = true);
  bool toss_this_entry(LogAccess *lad);
  bool wipe_this_entry(LogAccess *lad);
  LogFilter *find_by_name(const char *name);
  void clear();

  LogFilter *
  first() const
  {
    return m_filter_list.head;
  }

  LogFilter *
  next(LogFilter *here) const
  {
    return (here->link).next;
  }

  unsigned count() const;
  void display(FILE *fd = stdout);

  bool
  does_conjunction() const
  {
    return m_does_conjunction;
  }

  void
  set_conjunction(bool c)
  {
    m_does_conjunction = c;
  }

  // noncopyable
  // -- member functions that are not allowed --
  LogFilterList(const LogFilterList &rhs) = delete;
  LogFilterList &operator=(const LogFilterList &rhs) = delete;

private:
  Queue<LogFilter> m_filter_list;

  bool m_does_conjunction = true;
  // If m_does_conjunction = true
  // toss_this_entry returns true
  // if ANY filter tosses entry away.
  // If m_does_conjunction = false,
  // toss this entry returns true if
  // ALL filters toss away entry
};

/*-------------------------------------------------------------------------
  Inline functions
  -------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------
  _checkCondition

  check all values for a matching condition

  the arguments to the function are:

  - a function f of type OperatorFunction that determines if the
    condition is true for a single filter value. Note that this function
    must return 0 if the condition is true.
  - the value of the field from the log record
  - the length of this field
  - the array of filter values to compare to note that we pass this as an
    argument because it can be either m_value or m_value_uppercase
  - a LengthCondition argument that determines if the length of the field value
    must be equal or larger to the length of the filter value (this is to
    compare strings only if really needed
    ------------------------------------------------------------------------*/

inline bool
LogFilterString::_checkCondition(OperatorFunction f, const char *field_value, size_t field_value_length, char **val,
                                 LengthCondition lc)
{
  bool retVal = false;

  // make single value case a little bit faster by taking it out of loop
  //
  if (m_num_values == 1) {
    switch (lc) {
    case DATA_LENGTH_EQUAL:
      retVal = (field_value_length == *m_length ? ((*f)(field_value, *val) == 0 ? true : false) : false);
      break;
    case DATA_LENGTH_LARGER:
      retVal = (field_value_length > *m_length ? ((*f)(field_value, *val) == 0 ? true : false) : false);
      break;
    default:
      ink_assert(!"LogFilterString::checkCondition "
                  "unknown LengthCondition");
    }
  } else {
    size_t i;
    switch (lc) {
    case DATA_LENGTH_EQUAL:
      for (i = 0; i < m_num_values; ++i) {
        // condition is satisfied if f returns zero
        if (field_value_length == m_length[i] && (*f)(field_value, val[i]) == 0) {
          retVal = true;
          break;
        }
      }
      break;
    case DATA_LENGTH_LARGER:
      for (i = 0; i < m_num_values; ++i) {
        // condition is satisfied if f returns zero
        if (field_value_length > m_length[i] && (*f)(field_value, val[i]) == 0) {
          retVal = true;
          break;
        }
      }
      break;
    default:
      ink_assert(!"LogFilterString::checkCondition "
                  "unknown LengthCondition");
    }
  }
  return retVal;
}

/*---------------------------------------------------------------------------
 * find pattern from the query param
 * 1) if pattern is not in the query param, return nullptr
 * 2) if got the pattern in one param's value, search again until it's in one param name, or nullptr if can't find it from param
name
---------------------------------------------------------------------------*/
static const char *
findPatternFromParamName(const char *lookup_query_param, const char *pattern)
{
  const char *pattern_in_query_param = strstr(lookup_query_param, pattern);
  while (pattern_in_query_param) {
    // wipe pattern in param name, need to search again if find pattern in param value
    const char *param_value_str = strchr(pattern_in_query_param, '=');
    if (!param_value_str) {
      // no "=" after pattern_in_query_param, means pattern_in_query_param is not in the param name, and no more param after it
      pattern_in_query_param = nullptr;
      break;
    }
    const char *param_name_str = strchr(pattern_in_query_param, '&');
    if (param_name_str && param_value_str > param_name_str) {
      //"=" is after "&" followd by pattern_in_query_param, means pattern_in_query_param is not in the param name
      pattern_in_query_param = strstr(param_name_str, pattern);
      continue;
    }
    // ensure pattern_in_query_param is in the param name now
    break;
  }
  return pattern_in_query_param;
}

/*---------------------------------------------------------------------------
 * replace param value whose name contains pattern with same count 'X' of original value str length
---------------------------------------------------------------------------*/
static void
updatePatternForFieldValue(char **field, const char *pattern_str, int field_pos, char *buf_dest)
{
  int buf_dest_len = strlen(buf_dest);
  char buf_dest_to_field[buf_dest_len + 1];
  char *temp_text = buf_dest_to_field;
  memcpy(temp_text, buf_dest, (pattern_str - buf_dest));
  temp_text += (pattern_str - buf_dest);
  const char *value_str = strchr(pattern_str, '=');
  if (value_str) {
    value_str++;
    memcpy(temp_text, pattern_str, (value_str - pattern_str));
    temp_text += (value_str - pattern_str);
    const char *next_param_str = strchr(value_str, '&');
    if (next_param_str) {
      for (int i = 0; i < (next_param_str - value_str); i++) {
        temp_text[i] = 'X';
      }
      temp_text += (next_param_str - value_str);
      memcpy(temp_text, next_param_str, ((buf_dest + buf_dest_len) - next_param_str));
    } else {
      for (int i = 0; i < ((buf_dest + buf_dest_len) - value_str); i++) {
        temp_text[i] = 'X';
      }
    }
  } else {
    return;
  }

  buf_dest_to_field[buf_dest_len] = '\0';
  strcpy(*field, buf_dest_to_field);
}

/*---------------------------------------------------------------------------
  wipeField : Given a dest buffer, wipe the first occurrence of the value of the
  field in the buffer.

--------------------------------------------------------------------------*/
static void
wipeField(char **field, char *pattern, const char *uppercase_field)
{
  char *buf_dest          = *field;
  const char *lookup_dest = uppercase_field ? uppercase_field : *field;

  if (buf_dest) {
    char *query_param              = strchr(buf_dest, '?');
    const char *lookup_query_param = strchr(lookup_dest, '?');
    if (!query_param || !lookup_query_param) {
      return;
    }

    const char *pattern_in_param_name = findPatternFromParamName(lookup_query_param, pattern);
    while (pattern_in_param_name) {
      int field_pos         = pattern_in_param_name - lookup_query_param;
      pattern_in_param_name = query_param + field_pos;
      updatePatternForFieldValue(field, pattern_in_param_name, field_pos, buf_dest);

      // search new param again
      const char *new_param = strchr(lookup_query_param + field_pos, '&');
      if (new_param && (new_param + 1)) {
        pattern_in_param_name = findPatternFromParamName(new_param + 1, pattern);
      } else {
        break;
      }
    }
  }
}

/*-------------------------------------------------------------------------
  _checkConditionAndWipe

  check all values for a matching condition and perform wipe action

  the arguments to the function are:

  - a function f of type OperatorFunction that determines if the
    condition is true for a single filter value. Note that this function
    must return 0 if the condition is true.
  - the value of the field from the log record
  - the length of this field
  - the array of filter values to compare to note that we pass this as an
    argument because it can be either m_value or m_value_uppercase
  - a LengthCondition argument that determines if the length of the field value
    must be equal or larger to the length of the filter value (this is to
    compare strings only if really needed
    ------------------------------------------------------------------------*/

inline bool
LogFilterString::_checkConditionAndWipe(OperatorFunction f, char **field_value, size_t field_value_length, char **val,
                                        const char *uppercase_field_value, LengthCondition lc)
{
  bool retVal = false;

  if (m_action != WIPE_FIELD_VALUE) {
    return false;
  }

  // make single value case a little bit faster by taking it out of loop
  //
  const char *lookup_field_value = uppercase_field_value ? uppercase_field_value : *field_value;
  if (m_num_values == 1) {
    switch (lc) {
    case DATA_LENGTH_EQUAL:
      retVal = (field_value_length == *m_length ? ((*f)(lookup_field_value, *val) == 0 ? true : false) : false);
      if (retVal) {
        wipeField(field_value, *val, uppercase_field_value);
      }
      break;
    case DATA_LENGTH_LARGER:
      retVal = (field_value_length > *m_length ? ((*f)(lookup_field_value, *val) == 0 ? true : false) : false);
      if (retVal) {
        wipeField(field_value, *val, uppercase_field_value);
      }
      break;
    default:
      ink_assert(!"LogFilterString::checkCondition "
                  "unknown LengthCondition");
    }
  } else {
    size_t i;
    switch (lc) {
    case DATA_LENGTH_EQUAL:
      for (i = 0; i < m_num_values; ++i) {
        // condition is satisfied if f returns zero
        if (field_value_length == m_length[i] && (*f)(lookup_field_value, val[i]) == 0) {
          retVal = true;
          wipeField(field_value, val[i], uppercase_field_value);
        }
      }
      break;
    case DATA_LENGTH_LARGER:
      for (i = 0; i < m_num_values; ++i) {
        // condition is satisfied if f returns zero
        if (field_value_length > m_length[i] && (*f)(lookup_field_value, val[i]) == 0) {
          retVal = true;
          wipeField(field_value, val[i], uppercase_field_value);
        }
      }
      break;
    default:
      ink_assert(!"LogFilterString::checkConditionAndWipe "
                  "unknown LengthConditionAndWipe");
    }
  }
  return retVal;
}
