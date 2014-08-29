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


#ifndef LOG_FILTER_H
#define LOG_FILTER_H

#include "libts.h"
#include "IpMap.h"
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
class LogFilter
{
public:
  enum Type
  {
    INT_FILTER = 0,
    STRING_FILTER,
    IP_FILTER,
    N_TYPES
  };

  enum Action
  {
    REJECT = 0,
    ACCEPT,
    WIPE_FIELD_VALUE,
    N_ACTIONS
  };
  static const char *ACTION_NAME[];

  // all operators "positive" (i.e., there is no NOMATCH operator anymore)
  // because one can specify through the "action" field if the record should
  // be kept or tossed away
  //
  enum Operator
  {
    MATCH = 0,
    CASE_INSENSITIVE_MATCH,
    CONTAIN,
    CASE_INSENSITIVE_CONTAIN,
    N_OPERATORS
  };
  static const char *OPERATOR_NAME[];

  LogFilter(const char *name, LogField * field, Action action, Operator oper);
  virtual ~LogFilter();

  char *name() const { return m_name; }
  Type type() const { return m_type; }
  size_t get_num_values() const { return m_num_values; };

  virtual bool toss_this_entry(LogAccess * lad) = 0;
  virtual bool wipe_this_entry(LogAccess * lad) = 0;
  virtual void display(FILE * fd = stdout) = 0;
  virtual void display_as_XML(FILE * fd = stdout) = 0;

  void reverse() { m_action = (m_action == REJECT ? ACCEPT : REJECT); }

protected:
  char *m_name;
  LogField *m_field;
  Action m_action;              // the action this filter takes
  Operator m_operator;
  Type m_type;
  size_t m_num_values;          // the number of comparison values

public:
  LINK(LogFilter, link);      // so we can create a LogFilterList

private:
  // -- member functions that are not allowed --
  LogFilter();
  LogFilter(const LogFilter & rhs);
  LogFilter & operator=(LogFilter & rhs);
};

/*-------------------------------------------------------------------------
  LogFilterString

  Filter for string fields.
  -------------------------------------------------------------------------*/
class LogFilterString:public LogFilter
{
public:
  LogFilterString(const char *name, LogField * field, Action a, Operator o, char *value);
  LogFilterString(const char *name, LogField * field, Action a, Operator o, size_t num_values, char **value);
  LogFilterString(const LogFilterString & rhs);
  ~LogFilterString();
  bool operator==(LogFilterString & rhs);

  bool toss_this_entry(LogAccess * lad);
  bool wipe_this_entry(LogAccess * lad);
  void display(FILE * fd = stdout);
  void display_as_XML(FILE * fd = stdout);

private:
  char **m_value;               // the array of values

  // these are used to speed up case insensitive operations
  //
  char **m_value_uppercase;     // m_value in all uppercase
  size_t *m_length;             // length of m_value string

  void _setValues(size_t n, char **value);

  // note: OperatorFunction's must return 0 (zero) if condition is satisfied
  // (as strcmp does)
  typedef int (*OperatorFunction) (const char *, const char *);

  static int _isSubstring(const char *s0, const char *s1)
  {
    // return 0 if s1 is substring of s0 and 1 otherwise
    // this reverse behavior is to conform to the behavior of strcmp
    // which returns 0 if strings match
    return (strstr(s0, s1) == NULL ? 1 : 0);
  };

  enum LengthCondition
  {
    DATA_LENGTH_EQUAL = 0,
    DATA_LENGTH_LARGER
  };

  inline bool _checkCondition(OperatorFunction f,
                              const char *field_value, size_t field_value_length, char **val, LengthCondition lc);

  inline bool _checkConditionAndWipe(OperatorFunction f, char **field_value, size_t field_value_length, char **val,
                                     LengthCondition lc);

  // -- member functions that are not allowed --
  LogFilterString();
  LogFilterString & operator=(LogFilterString & rhs);
};

/*-------------------------------------------------------------------------
  LogFilterInt

  Filter for int fields.
  -------------------------------------------------------------------------*/
class LogFilterInt:public LogFilter
{
public:
  LogFilterInt(const char *name, LogField * field, Action a, Operator o, int64_t value);
    LogFilterInt(const char *name, LogField * field, Action a, Operator o, size_t num_values, int64_t *value);
    LogFilterInt(const char *name, LogField * field, Action a, Operator o, char *values);
    LogFilterInt(const LogFilterInt & rhs);
   ~LogFilterInt();
  bool operator==(LogFilterInt & rhs);

  bool toss_this_entry(LogAccess * lad);
  bool wipe_this_entry(LogAccess * lad);
  void display(FILE * fd = stdout);
  void display_as_XML(FILE * fd = stdout);

private:
  int64_t *m_value;            // the array of values

  void _setValues(size_t n, int64_t *value);
  int _convertStringToInt(char *val, int64_t *ival, LogFieldAliasMap * map);

  // -- member functions that are not allowed --
  LogFilterInt();
  LogFilterInt & operator=(LogFilterInt & rhs);
};

/*-------------------------------------------------------------------------
  LogFilterIP
  
  Filter for IP fields using IpAddr.
  -------------------------------------------------------------------------*/
class LogFilterIP:public LogFilter
{
public:
  LogFilterIP(const char *name, LogField * field, Action a, Operator o, IpAddr value);
  LogFilterIP(const char *name, LogField * field, Action a, Operator o, size_t num_values,  IpAddr* value);
  LogFilterIP(const char *name, LogField * field, Action a, Operator o, char *values);
  LogFilterIP(const LogFilterIP & rhs);
  ~LogFilterIP();

  bool operator==(LogFilterIP & rhs);

  virtual bool toss_this_entry(LogAccess * lad);
  virtual bool wipe_this_entry(LogAccess * lad);
  void display(FILE * fd = stdout);
  void display_as_XML(FILE * fd = stdout);

private:
  IpMap m_map;

  /// Initialization common to all constructors.
  void init();

  void displayRanges(FILE* fd);
  void displayRange(FILE* fd, IpMap::iterator const& iter);

  // Checks for a match on this filter.
  bool is_match(LogAccess* lad);
  
  // -- member functions that are not allowed --
  LogFilterIP();
  LogFilterIP & operator=(LogFilterIP & rhs);
};

bool filters_are_equal(LogFilter * filt1, LogFilter * filt2);


/*-------------------------------------------------------------------------
  LogFilterList
  -------------------------------------------------------------------------*/
class LogFilterList
{
public:
  LogFilterList();
  ~LogFilterList();
  bool operator==(LogFilterList &);

  void add(LogFilter * filter, bool copy = true);
  bool toss_this_entry(LogAccess * lad);
  bool wipe_this_entry(LogAccess * lad);
  LogFilter *find_by_name(char *name);
  void clear();

  LogFilter *first() const { return m_filter_list.head; }
  LogFilter *next(LogFilter * here) const { return (here->link).next; }

  unsigned count();
  void display(FILE * fd = stdout);
  void display_as_XML(FILE * fd = stdout);

  bool does_conjunction() const { return m_does_conjunction;  };
  void set_conjunction(bool c) { m_does_conjunction = c;  };

private:
  Queue<LogFilter> m_filter_list;

  bool m_does_conjunction;
  // If m_does_conjunction = true
  // toss_this_entry returns true
  // if ANY filter tosses entry away.
  // If m_does_conjunction = false,
  // toss this entry returns true if
  // ALL filters toss away entry

  // -- member functions that are not allowed --
  LogFilterList(const LogFilterList & rhs);
  LogFilterList & operator=(const LogFilterList & rhs);
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
LogFilterString::_checkCondition(OperatorFunction f,
                                 const char *field_value, size_t field_value_length, char **val, LengthCondition lc)
{
  bool retVal = false;

  // make single value case a little bit faster by taking it out of loop
  //
  if (m_num_values == 1) {
    switch (lc) {
    case DATA_LENGTH_EQUAL:
      retVal = (field_value_length == *m_length ? ((*f) (field_value, *val) == 0 ? true : false) : false);
      break;
    case DATA_LENGTH_LARGER:
      retVal = (field_value_length > *m_length ? ((*f) (field_value, *val) == 0 ? true : false) : false);
      break;
    default:
      ink_assert(!"LogFilterString::checkCondition " "unknown LengthCondition");
    }
  } else {
    size_t i;
    switch (lc) {
    case DATA_LENGTH_EQUAL:
      for (i = 0; i < m_num_values; ++i) {
        // condition is satisfied if f returns zero
        if (field_value_length == m_length[i] && (*f) (field_value, val[i]) == 0) {
          retVal = true;
          break;
        }
      }
      break;
    case DATA_LENGTH_LARGER:
      for (i = 0; i < m_num_values; ++i) {
        // condition is satisfied if f returns zero
        if (field_value_length > m_length[i] && (*f) (field_value, val[i]) == 0) {
          retVal = true;
          break;
        }
      }
      break;
    default:
      ink_assert(!"LogFilterString::checkCondition " "unknown LengthCondition");
    }
  }
  return retVal;
}

/*---------------------------------------------------------------------------
  wipeField : Given a dest buffer, wipe the first occurance of the value of the
  field in the buffer.

--------------------------------------------------------------------------*/
static void
wipeField(char** dest, char* field)
{

  char* buf_dest  = *dest;

  if (buf_dest) {

    char* query_param = strstr(buf_dest, "?");

    if (!query_param) return;

    char* p1 = strstr(query_param, field);

    if (p1) {
      char tmp_text[strlen(buf_dest) + 10];
      char *temp_text = tmp_text;
      memcpy(temp_text, buf_dest, (p1 - buf_dest));
      temp_text += (p1 - buf_dest);
      char* p2 = strstr(p1, "=");
      if (p2) {
        p2++;
        memcpy(temp_text, p1, (p2 - p1));
        temp_text += (p2 - p1);
        char* p3 = strstr(p2, "&");
        if (p3) {
          for (int i=0; i<(p3 - p2); i++)
            temp_text[i] = 'X';
          temp_text += (p3 - p2);
          memcpy(temp_text, p3, ((buf_dest+strlen(buf_dest)) - p3));
        } else {
          for (int i=0; i<((buf_dest+strlen(buf_dest)) - p2); i++)
            temp_text[i] = 'X';
        }
      } else {
        return;
      }

      tmp_text[strlen(buf_dest)] = '\0';
      strcpy(*dest, tmp_text);
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
LogFilterString::_checkConditionAndWipe(OperatorFunction f, char **field_value, size_t field_value_length,
                                        char **val, LengthCondition lc)
{
  bool retVal = false;

  if (m_action != WIPE_FIELD_VALUE) return false;

  // make single value case a little bit faster by taking it out of loop
  //
  if (m_num_values == 1) {
    switch (lc) {
    case DATA_LENGTH_EQUAL:
      retVal = (field_value_length == *m_length ? ((*f) (*field_value, *val) == 0 ? true : false) : false);
      if (retVal) {
        wipeField(field_value, *val);
      }
      break;
    case DATA_LENGTH_LARGER:
      retVal = (field_value_length > *m_length ? ((*f) (*field_value, *val) == 0 ? true : false) : false);
      if (retVal) {
        wipeField(field_value, *val);
      }
      break;
    default:
      ink_assert(!"LogFilterString::checkCondition " "unknown LengthCondition");
    }
  } else {
    size_t i;
    switch (lc) {
    case DATA_LENGTH_EQUAL:
      for (i = 0; i < m_num_values; ++i) {
        // condition is satisfied if f returns zero
        if (field_value_length == m_length[i] && (*f) (*field_value, val[i]) == 0) {
          retVal = true;
          wipeField(field_value, val[i]);
        }
      }
      break;
    case DATA_LENGTH_LARGER:
      for (i = 0; i < m_num_values; ++i) {
        // condition is satisfied if f returns zero
        if (field_value_length > m_length[i] && (*f) (*field_value, val[i]) == 0) {
          retVal = true;
          wipeField(field_value, val[i]);
        }
      }
      break;
    default:
      ink_assert(!"LogFilterString::checkConditionAndWipe " "unknown LengthConditionAndWipe");
    }
  }
  return retVal;
}

#endif
