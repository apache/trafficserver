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

#include "swoc/swoc_ip.h"

#include "tscore/ink_inet.h"
#include "tscore/Ptr.h"
#include "proxy/logging/LogAccess.h"
#include "proxy/logging/LogField.h"

/*-------------------------------------------------------------------------
  LogFilter

  This is an abstract base class from which particular filters can be
  derived.  Each filter must implement the "toss_this_entry" member
  function which, given a LogAccess object, returns true if
  the log entry is to be tossed out.
  -------------------------------------------------------------------------*/
class LogFilter : public RefCountObjInHeap
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
    LT,
    LTE,
    GT,
    GTE,
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

  bool
  is_wipe() const
  {
    return m_action == WIPE_FIELD_VALUE;
  }

  virtual bool toss_this_entry(LogAccess *lad) = 0;
  virtual void display(FILE *fd = stdout)      = 0;

  static LogFilter *parse(const char *name, Action action, const char *condition);

protected:
  char     *m_name;
  LogField *m_field;
  Action    m_action; // the action this filter takes
  Operator  m_operator;
  Type      m_type;
  size_t    m_num_values; // the number of comparison values

public:
  LINK(LogFilter, link); // so we can create a LogFilterList

  // noncopyable
  LogFilter(const LogFilter &rhs)      = delete;
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
  void display(FILE *fd = stdout) override;

  // noncopyable
  LogFilterString &operator=(LogFilterString &rhs) = delete;

  // This assumes that s1 is all uppercase, hence we hide this in here specifically
  static const char *
  strstrcase(const char *s0, const char *s1)
  {
    size_t len_s0 = std::strlen(s0);
    size_t len_s1 = std::strlen(s1);

    if (len_s1 > len_s0) {
      return nullptr; // If s1 is longer than s0, there's no match
    }

    const char *end = s0 + len_s0 - len_s1;

    while (s0 < end) {
      const char *h = s0;
      const char *n = s1;

      while (*n && (std::toupper(static_cast<unsigned char>(*h)) == *n)) {
        ++h;
        ++n;
      }

      if (!*n) {
        return s0;
      }

      ++s0;
    }

    return nullptr;
  }

private:
  char **m_value = nullptr; // the array of values

  // these are used to speed up case insensitive operations
  //
  char  **m_value_uppercase = nullptr; // m_value in all uppercase
  size_t *m_length          = nullptr; // length of m_value string

  void _setValues(size_t n, char **value);

  // note: OperatorFunction's must return 0 (zero) if condition is satisfied
  // (as strcmp does)
  using OperatorFunction = int (*)(const char *, const char *);

  // return 0 if s1 is substring of s0 and 1 otherwise, to conform to strcmp etc.
  static int
  _isSubstring(const char *s0, const char *s1)
  {
    return (strstr(s0, s1) == nullptr ? 1 : 0);
  }

  //
  static int
  _isSubstringUpper(const char *s0, const char *s1)
  {
    return (strstrcase(s0, s1) == nullptr) ? 1 : 0;
  }

  bool _checkConditionAndWipe(OperatorFunction f, char *field_value, size_t field_value_length, char **val, bool uppercase = false);

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
  void display(FILE *fd = stdout) override;

  // noncopyable
  LogFilterInt &operator=(LogFilterInt &rhs) = delete;

private:
  int64_t *m_value = nullptr; // the array of values

  void _setValues(size_t n, int64_t *value);
  int  _convertStringToInt(char *val, int64_t *ival, LogFieldAliasMap *map);

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
  LogFilterIP(const char *name, LogField *field, Action a, Operator o, swoc::IPAddr value);
  LogFilterIP(const char *name, LogField *field, Action a, Operator o, size_t num_values, IpAddr *value);
  LogFilterIP(const char *name, LogField *field, Action a, Operator o, char *values);
  LogFilterIP(const LogFilterIP &rhs);
  ~LogFilterIP() override;

  bool operator==(LogFilterIP &rhs);

  bool toss_this_entry(LogAccess *lad) override;
  void display(FILE *fd = stdout) override;

  // noncopyable
  LogFilterIP &operator=(LogFilterIP &rhs) = delete;

private:
  swoc::IPRangeSet m_addrs;

  /// Initialization common to all constructors.
  void init();

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

  void       add(LogFilter *filter, bool copy = true);
  bool       toss_this_entry(LogAccess *lad);
  LogFilter *find_by_name(const char *name);
  void       clear();

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
  void     display(FILE *fd = stdout);

  // noncopyable
  // -- member functions that are not allowed --
  LogFilterList(const LogFilterList &rhs)            = delete;
  LogFilterList &operator=(const LogFilterList &rhs) = delete;

private:
  Queue<LogFilter> m_filter_list;

  bool m_has_wipe = false;
};
