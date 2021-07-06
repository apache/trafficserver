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

/***************************************************************************
 LogFilter.cc


 ***************************************************************************/
#include "tscore/ink_platform.h"

#include "LogUtils.h"
#include "LogFilter.h"
#include "LogField.h"
#include "LogFormat.h"
#include "LogFile.h"
#include "LogBuffer.h"
#include "LogObject.h"
#include "LogConfig.h"
#include "Log.h"
#include "tscore/SimpleTokenizer.h"

const char *LogFilter::OPERATOR_NAME[] = {"MATCH", "CASE_INSENSITIVE_MATCH", "CONTAIN", "CASE_INSENSITIVE_CONTAIN"};
const char *LogFilter::ACTION_NAME[]   = {"REJECT", "ACCEPT", "WIPE_FIELD_VALUE"};

/*-------------------------------------------------------------------------
  LogFilter::LogFilter

  note: it may be convenient to have the LogFilter constructor access the
  global_field_list to get the log field, but this is an unnecessary dependency
  between the classes and I think should be removed.     ltavera
  -------------------------------------------------------------------------*/
LogFilter::LogFilter(const char *name, LogField *field, LogFilter::Action action, LogFilter::Operator oper)
  : m_name(ats_strdup(name)), m_field(nullptr), m_action(action), m_operator(oper), m_type(INT_FILTER), m_num_values(0)
{
  m_field = new LogField(*field);
  ink_assert(m_field);
}

/*-------------------------------------------------------------------------
  LogFilter::~LogFilter
  -------------------------------------------------------------------------*/
LogFilter::~LogFilter()
{
  ats_free(m_name);
  delete m_field;
}

LogFilter *
LogFilter::parse(const char *name, Action action, const char *condition)
{
  SimpleTokenizer tok(condition);
  ats_scoped_obj<LogField> logfield;

  ink_release_assert(action != N_ACTIONS);

  if (tok.getNumTokensRemaining() < 3) {
    Error("Invalid condition syntax '%s'; cannot create filter '%s'", condition, name);
    return nullptr;
  }

  char *field_str = tok.getNext();
  char *oper_str  = tok.getNext();
  char *val_str   = tok.getRest();

  // validate field symbol
  if (strlen(field_str) > 2 && field_str[0] == '%' && field_str[1] == '<') {
    Debug("log", "Field symbol has <> form: %s", field_str);
    char *end = field_str;
    while (*end && *end != '>') {
      end++;
    }
    *end = '\0';
    field_str += 2;
    Debug("log", "... now field symbol is %s", field_str);
  }

  if (LogField *f = Log::global_field_list.find_by_symbol(field_str)) {
    logfield = new LogField(*f);
  }

  if (!logfield) {
    // check for container fields
    if (*field_str == '{') {
      Debug("log", "%s appears to be a container field", field_str);

      char *fname;
      char *cname;
      char *fname_end;

      fname_end = strchr(field_str, '}');
      if (nullptr == fname_end) {
        Error("Invalid container field specification: no trailing '}' in '%s' cannot create filter '%s'", field_str, name);
        return nullptr;
      }

      fname      = field_str + 1;
      *fname_end = 0; // changes '}' to '\0'

      // start of container symbol
      cname = fname_end + 1;

      Debug("log", "found container field: Name = %s, symbol = %s", fname, cname);

      LogField::Container container = LogField::valid_container_name(cname);
      if (container == LogField::NO_CONTAINER) {
        Error("'%s' is not a valid container; cannot create filter '%s'", cname, name);
        return nullptr;
      }

      logfield = new LogField(fname, container);
      ink_assert(logfield != nullptr);
    }
  }

  if (!logfield) {
    Error("'%s' is not a valid field; cannot create filter '%s'", field_str, name);
    return nullptr;
  }

  // convert the operator string to an enum value and validate it
  LogFilter::Operator oper = LogFilter::N_OPERATORS;
  for (unsigned i = 0; i < LogFilter::N_OPERATORS; ++i) {
    if (strcasecmp(oper_str, LogFilter::OPERATOR_NAME[i]) == 0) {
      oper = static_cast<LogFilter::Operator>(i);
      break;
    }
  }

  if (oper == LogFilter::N_OPERATORS) {
    Error("'%s' is not a valid operator; cannot create filter '%s'", oper_str, name);
    return nullptr;
  }

  // now create the correct LogFilter
  LogField::Type field_type = logfield->type();
  LogFilter *filter;

  switch (field_type) {
  case LogField::sINT:
    filter = new LogFilterInt(name, logfield, action, oper, val_str);
    break;

  case LogField::dINT:
    Error("Invalid field type (double int); cannot create filter '%s'", name);
    return nullptr;

  case LogField::STRING:
    filter = new LogFilterString(name, logfield, action, oper, val_str);
    break;

  case LogField::IP:
    filter = new LogFilterIP(name, logfield, action, oper, val_str);
    break;

  default:
    Error("Unknown logging field type %d; cannot create filter '%s'", field_type, name);
    return nullptr;
  }

  if (filter->get_num_values() == 0) {
    Error("'%s' does not specify any valid values; cannot create filter '%s'", val_str, name);
    delete filter;
    return nullptr;
  }

  return filter;
}

/*-------------------------------------------------------------------------
  LogFilterString::LogFilterString
  -------------------------------------------------------------------------*/
void
LogFilterString::_setValues(size_t n, char **value)
{
  m_type       = STRING_FILTER;
  m_num_values = n;
  if (n) {
    m_value           = new char *[n];
    m_value_uppercase = new char *[n];
    m_length          = new size_t[n];
    ink_assert(m_value && m_value_uppercase && m_length);
    for (size_t i = 0; i < n; ++i) {
      m_value[i]           = ats_strdup(value[i]);
      m_length[i]          = strlen(value[i]);
      m_value_uppercase[i] = static_cast<char *>(ats_malloc(static_cast<unsigned int>(m_length[i]) + 1));
      size_t j;
      for (j = 0; j < m_length[i]; ++j) {
        m_value_uppercase[i][j] = ParseRules::ink_toupper(m_value[i][j]);
      }
      m_value_uppercase[i][j] = 0;
    }
  }
}

LogFilterString::LogFilterString(const char *name, LogField *field, LogFilter::Action action, LogFilter::Operator oper,
                                 char *values)
  : LogFilter(name, field, action, oper)
{
  // parse the comma-separated list of values and construct array
  //
  char **val_array = nullptr;
  size_t i         = 0;
  SimpleTokenizer tok(values, ',');
  size_t n = tok.getNumTokensRemaining();
  if (n) {
    val_array = new char *[n];
    char *t;
    while (t = tok.getNext(), t != nullptr) {
      val_array[i++] = t;
    }
    if (i < n) {
      Warning("There were invalid values in the definition of filter %s"
              "only %zu out of %zu values will be used",
              name, i, n);
    }
  }
  _setValues(i, val_array);
  delete[] val_array;
}

LogFilterString::LogFilterString(const char *name, LogField *field, LogFilter::Action action, LogFilter::Operator oper,
                                 size_t num_values, char **value)
  : LogFilter(name, field, action, oper)
{
  _setValues(num_values, value);
}

LogFilterString::LogFilterString(const LogFilterString &rhs) : LogFilter(rhs.m_name, rhs.m_field, rhs.m_action, rhs.m_operator)
{
  _setValues(rhs.m_num_values, rhs.m_value);
}

/*-------------------------------------------------------------------------
  LogFilterString::~LogFilterString
  -------------------------------------------------------------------------*/

LogFilterString::~LogFilterString()
{
  if (m_num_values > 0) {
    for (size_t i = 0; i < m_num_values; ++i) {
      ats_free(m_value[i]);
      ats_free(m_value_uppercase[i]);
    }
    delete[] m_value;
    delete[] m_value_uppercase;
    delete[] m_length;
  }
}

/*-------------------------------------------------------------------------
  LogFilterString::operator==

  This operator is not very intelligent and expects the objects being
  compared to have the same values specified *in the same order*.
  Filters with the same values specified in different order are considered
  to be different.

  -------------------------------------------------------------------------*/

bool
LogFilterString::operator==(LogFilterString &rhs)
{
  if (m_type == rhs.m_type && *m_field == *rhs.m_field && m_action == rhs.m_action && m_operator == rhs.m_operator &&
      m_num_values == rhs.m_num_values) {
    for (size_t i = 0; i < m_num_values; i++) {
      if (m_length[i] != rhs.m_length[i] || strncmp(m_value[i], rhs.m_value[i], m_length[i])) {
        return false;
      }
    }
    return true;
  }
  return false;
}

/*-------------------------------------------------------------------------
  LogFilterString::wipe_this_entry

  For strings, we need to marshal the given string into a buffer so that we
  can compare it with the filter value.  Most strings are snall, so we'll
  only allocate space dynamically if the marshal_len is very large (eg,
  URL).

  The m_substr field tells us whether we can match based on substrings, or
  whether we should compare the entire string.
  -------------------------------------------------------------------------*/

bool
LogFilterString::wipe_this_entry(LogAccess *lad)
{
  if (m_num_values == 0 || m_field == nullptr || lad == nullptr || m_action != WIPE_FIELD_VALUE) {
    return false;
  }

  static const unsigned BUFSIZE = 1024;
  char small_buf[BUFSIZE];
  char small_buf_upper[BUFSIZE];
  char *big_buf       = nullptr;
  char *big_buf_upper = nullptr;
  char *buf           = small_buf;
  char *buf_upper     = small_buf_upper;
  size_t marsh_len    = m_field->marshal_len(lad); // includes null termination

  if (marsh_len > BUFSIZE) {
    big_buf = static_cast<char *>(ats_malloc(marsh_len));
    ink_assert(big_buf != nullptr);
    buf = big_buf;
  }

  ink_assert(buf != nullptr);
  m_field->marshal(lad, buf);

  ink_assert(buf != nullptr);

  bool cond_satisfied = false;
  switch (m_operator) {
  case MATCH:
    // marsh_len is an upper bound on the length of the marshalled string
    // because marsh_len counts padding and the eos. So for a MATCH
    // operator, we use the DATA_LENGTH_LARGER length condition rather
    // than DATA_LENGTH_EQUAL, which we would use if we had the actual
    // length of the string. It is probably not worth computing the
    // actual length, so we just use the fact that a MATCH is not possible
    // when marsh_len <= (length of the filter string)
    //
    cond_satisfied = _checkConditionAndWipe(&strcmp, &buf, marsh_len, m_value, nullptr, DATA_LENGTH_LARGER);
    break;
  case CASE_INSENSITIVE_MATCH:
    cond_satisfied = _checkConditionAndWipe(&strcasecmp, &buf, marsh_len, m_value, nullptr, DATA_LENGTH_LARGER);
    break;
  case CONTAIN:
    cond_satisfied = _checkConditionAndWipe(&_isSubstring, &buf, marsh_len, m_value, nullptr, DATA_LENGTH_LARGER);
    break;
  case CASE_INSENSITIVE_CONTAIN:
    if (big_buf) {
      big_buf_upper = static_cast<char *>(ats_malloc(static_cast<unsigned int>(marsh_len)));
      buf_upper     = big_buf_upper;
    } else {
      buf = small_buf; // make clang happy
    }
    for (size_t i = 0; i < marsh_len; i++) {
      buf_upper[i] = ParseRules::ink_toupper(buf[i]);
    }
    cond_satisfied = _checkConditionAndWipe(&_isSubstring, &buf, marsh_len, m_value_uppercase, buf_upper, DATA_LENGTH_LARGER);
    break;
  default:
    ink_assert(!"INVALID FILTER OPERATOR");
  }

  if (cond_satisfied) {
    m_field->updateField(lad, buf, strlen(buf));
  }

  ats_free(big_buf);
  ats_free(big_buf_upper);
  return cond_satisfied;
}

/*-------------------------------------------------------------------------
  LogFilterString::toss_this_entry

  For strings, we need to marshal the given string into a buffer so that we
  can compare it with the filter value.  Most strings are snall, so we'll
  only allocate space dynamically if the marshal_len is very large (eg,
  URL).

  The m_substr field tells us whether we can match based on substrings, or
  whether we should compare the entire string.
  -------------------------------------------------------------------------*/

bool
LogFilterString::toss_this_entry(LogAccess *lad)
{
  if (m_num_values == 0 || m_field == nullptr || lad == nullptr) {
    return false;
  }

  static const unsigned BUFSIZE = 1024;
  char small_buf[BUFSIZE];
  char small_buf_upper[BUFSIZE];
  char *big_buf       = nullptr;
  char *big_buf_upper = nullptr;
  char *buf           = small_buf;
  char *buf_upper     = small_buf_upper;
  size_t marsh_len    = m_field->marshal_len(lad); // includes null termination

  if (marsh_len > BUFSIZE) {
    big_buf = static_cast<char *>(ats_malloc(static_cast<unsigned int>(marsh_len)));
    ink_assert(big_buf != nullptr);
    buf = big_buf;
  }

  m_field->marshal(lad, buf);

  bool cond_satisfied = false;
  switch (m_operator) {
  case MATCH:
    // marsh_len is an upper bound on the length of the marshalled string
    // because marsh_len counts padding and the eos. So for a MATCH
    // operator, we use the DATA_LENGTH_LARGER length condition rather
    // than DATA_LENGTH_EQUAL, which we would use if we had the actual
    // length of the string. It is probably not worth computing the
    // actual length, so we just use the fact that a MATCH is not possible
    // when marsh_len <= (length of the filter string)
    //
    cond_satisfied = _checkCondition(&strcmp, buf, marsh_len, m_value, DATA_LENGTH_LARGER);
    break;
  case CASE_INSENSITIVE_MATCH:
    cond_satisfied = _checkCondition(&strcasecmp, buf, marsh_len, m_value, DATA_LENGTH_LARGER);
    break;
  case CONTAIN:
    cond_satisfied = _checkCondition(&_isSubstring, buf, marsh_len, m_value, DATA_LENGTH_LARGER);
    break;
  case CASE_INSENSITIVE_CONTAIN: {
    if (big_buf) {
      big_buf_upper = static_cast<char *>(ats_malloc(static_cast<unsigned int>(marsh_len)));
      buf_upper     = big_buf_upper;
    } else {
      buf = small_buf; // make clang happy
    }
    for (size_t i = 0; i < marsh_len; i++) {
      buf_upper[i] = ParseRules::ink_toupper(buf[i]);
    }
    cond_satisfied = _checkCondition(&_isSubstring, buf_upper, marsh_len, m_value_uppercase, DATA_LENGTH_LARGER);
    break;
  }
  default:
    ink_assert(!"INVALID FILTER OPERATOR");
  }

  ats_free(big_buf);
  ats_free(big_buf_upper);

  return ((m_action == REJECT && cond_satisfied) || (m_action == ACCEPT && !cond_satisfied));
}

/*-------------------------------------------------------------------------
  LogFilterString::display
  -------------------------------------------------------------------------*/

void
LogFilterString::display(FILE *fd)
{
  ink_assert(fd != nullptr);
  if (m_num_values == 0) {
    fprintf(fd, "Filter \"%s\" is inactive, no values specified\n", m_name);
  } else {
    fprintf(fd, "Filter \"%s\" %sS records if %s %s ", m_name, ACTION_NAME[m_action], m_field->symbol(), OPERATOR_NAME[m_operator]);
    fprintf(fd, "%s", m_value[0]);
    for (size_t i = 1; i < m_num_values; ++i) {
      fprintf(fd, ", %s", m_value[i]);
    }
    fprintf(fd, "\n");
  }
}

/*-------------------------------------------------------------------------
  LogFilterInt::LogFilterInt
  -------------------------------------------------------------------------*/

void
LogFilterInt::_setValues(size_t n, int64_t *value)
{
  m_type       = INT_FILTER;
  m_num_values = n;
  if (n) {
    m_value = new int64_t[n];
    memcpy(m_value, value, n * sizeof(int64_t));
  }
}

// TODO: ival should be int64_t
int
LogFilterInt::_convertStringToInt(char *value, int64_t *ival, LogFieldAliasMap *map)
{
  size_t i, l = strlen(value);
  for (i = 0; i < l && ParseRules::is_digit(value[i]); i++) {
    ;
  }

  if (i < l) {
    // not all characters of value are digits, assume that
    // value is an alias and try to get the actual integer value
    // from the log field alias map if field has one
    //
    if (map == nullptr || map->asInt(value, ival) != LogFieldAliasMap::ALL_OK) {
      return -1; // error
    };
  } else {
    // all characters of value are digits, simply convert
    // the string to int
    //
    *ival = ink_atoui(value);
  }
  return 0; // all OK
}

LogFilterInt::LogFilterInt(const char *name, LogField *field, LogFilter::Action action, LogFilter::Operator oper, int64_t value)
  : LogFilter(name, field, action, oper)
{
  int64_t v[1];
  v[0] = value;
  _setValues(1, v);
}

LogFilterInt::LogFilterInt(const char *name, LogField *field, LogFilter::Action action, LogFilter::Operator oper, size_t num_values,
                           int64_t *value)
  : LogFilter(name, field, action, oper)
{
  _setValues(num_values, value);
}

LogFilterInt::LogFilterInt(const char *name, LogField *field, LogFilter::Action action, LogFilter::Operator oper, char *values)
  : LogFilter(name, field, action, oper)
{
  // parse the comma-separated list of values and construct array
  //
  int64_t *val_array = nullptr;
  size_t i           = 0;
  SimpleTokenizer tok(values, ',');
  size_t n = tok.getNumTokensRemaining();
  auto field_map{field->map()}; // because clang-analyzer freaks out if this is inlined.
                                // It doesn't realize the value is held in place by the smart
                                // pointer in @c field.

  if (n) {
    val_array = new int64_t[n];
    char *t;
    while (t = tok.getNext(), t != nullptr) {
      int64_t ival;
      if (!_convertStringToInt(t, &ival, field_map.get())) {
        // conversion was successful, add entry to array
        //
        val_array[i++] = ival;
      }
    }
    if (i < n) {
      Warning("There were invalid values in the definition of filter %s"
              " only %zu out of %zu values will be used.",
              name, i, n);
    }
  } else {
    Warning("No values in the definition of filter %s.", name);
  }

  _setValues(i, val_array);

  if (n) {
    delete[] val_array;
  }
}

LogFilterInt::LogFilterInt(const LogFilterInt &rhs) : LogFilter(rhs.m_name, rhs.m_field, rhs.m_action, rhs.m_operator)
{
  _setValues(rhs.m_num_values, rhs.m_value);
}

/*-------------------------------------------------------------------------
  LogFilterInt::~LogFilterInt
  -------------------------------------------------------------------------*/

LogFilterInt::~LogFilterInt()
{
  if (m_num_values > 0) {
    delete[] m_value;
  }
}

/*-------------------------------------------------------------------------
  LogFilterInt::operator==

  This operator is not very intelligent and expects the objects being
  compared to have the same values specified *in the same order*.
  Filters with the same values specified in different order are considered
  to be different.

  -------------------------------------------------------------------------*/

bool
LogFilterInt::operator==(LogFilterInt &rhs)
{
  if (m_type == rhs.m_type && *m_field == *rhs.m_field && m_action == rhs.m_action && m_operator == rhs.m_operator &&
      m_num_values == rhs.m_num_values) {
    for (size_t i = 0; i < m_num_values; i++) {
      if (m_value[i] != rhs.m_value[i]) {
        return false;
      }
    }
    return true;
  }
  return false;
}

/*-------------------------------------------------------------------------
  LogFilterInt::wipe_this_entry
  -------------------------------------------------------------------------*/

bool
LogFilterInt::wipe_this_entry(LogAccess *lad)
{
  if (m_num_values == 0 || m_field == nullptr || lad == nullptr || m_action != WIPE_FIELD_VALUE) {
    return false;
  }

  bool cond_satisfied = false;
  int64_t value;

  m_field->marshal(lad, reinterpret_cast<char *>(&value));
  // This used to do an ntohl() on value, but that breaks various filters.
  // Long term we should move IPs to their own log type.

  // we don't use m_operator because we consider all operators to be
  // equivalent to "MATCH" for an integer field
  //

  // most common case is single value, speed it up a little bit by unrolling
  //
  if (m_num_values == 1) {
    cond_satisfied = (value == *m_value);
  } else {
    for (size_t i = 0; i < m_num_values; ++i) {
      if (value == m_value[i]) {
        cond_satisfied = true;
        break;
      }
    }
  }

  return cond_satisfied;
}

/*-------------------------------------------------------------------------
  LogFilterInt::toss_this_entry
  -------------------------------------------------------------------------*/

bool
LogFilterInt::toss_this_entry(LogAccess *lad)
{
  if (m_num_values == 0 || m_field == nullptr || lad == nullptr) {
    return false;
  }

  bool cond_satisfied = false;
  int64_t value;

  m_field->marshal(lad, reinterpret_cast<char *>(&value));
  // This used to do an ntohl() on value, but that breaks various filters.
  // Long term we should move IPs to their own log type.

  // we don't use m_operator because we consider all operators to be
  // equivalent to "MATCH" for an integer field
  //

  // most common case is single value, speed it up a little bit by unrolling
  //
  if (m_num_values == 1) {
    cond_satisfied = (value == *m_value);
  } else {
    for (size_t i = 0; i < m_num_values; ++i) {
      if (value == m_value[i]) {
        cond_satisfied = true;
        break;
      }
    }
  }

  return (m_action == REJECT && cond_satisfied) || (m_action == ACCEPT && !cond_satisfied);
}

/*-------------------------------------------------------------------------
  LogFilterInt::display
  -------------------------------------------------------------------------*/

void
LogFilterInt::display(FILE *fd)
{
  ink_assert(fd != nullptr);
  if (m_num_values == 0) {
    fprintf(fd, "Filter \"%s\" is inactive, no values specified\n", m_name);
  } else {
    fprintf(fd, "Filter \"%s\" %sS records if %s %s ", m_name, ACTION_NAME[m_action], m_field->symbol(), OPERATOR_NAME[m_operator]);
    fprintf(fd, "%" PRId64 "", m_value[0]);
    for (size_t i = 1; i < m_num_values; ++i) {
      fprintf(fd, ", %" PRId64 "", m_value[i]);
    }
    fprintf(fd, "\n");
  }
}

/*-------------------------------------------------------------------------
  LogFilterIP::LogFilterIP
  -------------------------------------------------------------------------*/
LogFilterIP::LogFilterIP(const char *name, LogField *field, LogFilter::Action action, LogFilter::Operator oper, IpAddr value)
  : LogFilter(name, field, action, oper)
{
  m_map.mark(value, value);
  this->init();
}

LogFilterIP::LogFilterIP(const char *name, LogField *field, LogFilter::Action action, LogFilter::Operator oper, size_t num_values,
                         IpAddr *value)
  : LogFilter(name, field, action, oper)
{
  for (IpAddr *limit = value + num_values; value != limit; ++value) {
    m_map.mark(*value, *value);
  }
  this->init();
}

LogFilterIP::LogFilterIP(const char *name, LogField *field, LogFilter::Action action, LogFilter::Operator oper, char *values)
  : LogFilter(name, field, action, oper)
{
  // parse the comma-separated list of values and construct array
  //
  size_t i = 0;
  SimpleTokenizer tok(values, ',');
  size_t n = tok.getNumTokensRemaining();
  char *t; // temp token pointer.

  if (n) {
    while (t = tok.getNext(), t != nullptr) {
      IpAddr min, max;
      char *x = strchr(t, '-');
      if (x) {
        *x++ = 0;
      }
      if (0 == min.load(t)) {
        if (x) {
          if (0 != max.load(x)) {
            Warning("LogFilterIP Configuration: '%s-%s' looks like a range but the second address was ill formed", t, x);
            continue;
          }
        } else {
          max = min;
        }
        m_map.mark(min, max);
        ++i;
      } else {
        Warning("LogFilterIP Configuration:  '%s' is ill formed", t);
      }
    }
    if (i < n) {
      Warning("There were invalid IP values in the definition of filter %s"
              " only %zu out of %zu values will be used.",
              name, i, n);
    }
  } else {
    Warning("No values in the definition of filter %s.", name);
  }
  this->init();
}

LogFilterIP::LogFilterIP(const LogFilterIP &rhs) : LogFilter(rhs.m_name, rhs.m_field, rhs.m_action, rhs.m_operator)
{
  for (auto &spot : rhs.m_map) {
    m_map.mark(spot.min(), spot.max(), spot.data());
  }
  this->init();
}

void
LogFilterIP::init()
{
  m_type       = IP_FILTER;
  m_num_values = m_map.count();
}

/*-------------------------------------------------------------------------
  LogFilterIP::~LogFilterIP
  -------------------------------------------------------------------------*/

LogFilterIP::~LogFilterIP() = default;

/*-------------------------------------------------------------------------
  LogFilterIP::operator==

  This operator is not very intelligent and expects the objects being
  compared to have the same values specified *in the same order*.
  Filters with the same values specified in different order are considered
  to be different.

  -------------------------------------------------------------------------*/

bool
LogFilterIP::operator==(LogFilterIP &rhs)
{
  if (m_type == rhs.m_type && *m_field == *rhs.m_field && m_action == rhs.m_action && m_operator == rhs.m_operator &&
      m_num_values == rhs.m_num_values) {
    IpMap::iterator left_spot(m_map.begin());
    IpMap::iterator left_limit(m_map.end());
    IpMap::iterator right_spot(rhs.m_map.begin());
    IpMap::iterator right_limit(rhs.m_map.end());

    while (left_spot != left_limit && right_spot != right_limit) {
      if (!ats_ip_addr_eq(left_spot->min(), right_spot->min()) || !ats_ip_addr_eq(left_spot->max(), right_spot->max())) {
        break;
      }
      ++left_spot;
      ++right_spot;
    }

    return left_spot == left_limit && right_spot == right_limit;
  }
  return false;
}

/*-------------------------------------------------------------------------
  LogFilterIP::toss_this_entry
  -------------------------------------------------------------------------*/

bool
LogFilterIP::is_match(LogAccess *lad)
{
  bool zret = false;

  if (m_field && lad) {
    LogFieldIpStorage value;
    m_field->marshal(lad, reinterpret_cast<char *>(&value));
    // This is bad, we abuse the fact that the initial layout of LogFieldIpStorage and IpAddr
    // are identical. We should look at converting the log stuff to use IpAddr directly.
    zret = m_map.contains(reinterpret_cast<IpAddr &>(value));
  }

  return zret;
}

bool
LogFilterIP::toss_this_entry(LogAccess *lad)
{
  bool cond_satisfied = this->is_match(lad);
  return (m_action == REJECT && cond_satisfied) || (m_action == ACCEPT && !cond_satisfied);
}

bool
LogFilterIP::wipe_this_entry(LogAccess *)
{
  return false;
}

/*-------------------------------------------------------------------------
  LogFilterIP::display
  -------------------------------------------------------------------------*/

void
LogFilterIP::displayRange(FILE *fd, IpMap::iterator const &iter)
{
  ip_text_buffer ipb;

  fprintf(fd, "%s", ats_ip_ntop(iter->min(), ipb, sizeof(ipb)));

  if (!ats_ip_addr_eq(iter->min(), iter->max())) {
    fprintf(fd, "-%s", ats_ip_ntop(iter->max(), ipb, sizeof(ipb)));
  }
}

void
LogFilterIP::displayRanges(FILE *fd)
{
  IpMap::iterator spot(m_map.begin()), limit(m_map.end());
  ink_assert(spot != limit);

  this->displayRange(fd, spot);
  for (++spot; spot != limit; ++spot) {
    for (size_t i = 1; i < m_num_values; ++i) {
      fprintf(fd, ",");
      this->displayRange(fd, spot);
    }
  }
}

void
LogFilterIP::display(FILE *fd)
{
  ink_assert(fd != nullptr);

  if (0 == m_map.count()) {
    fprintf(fd, "Filter \"%s\" is inactive, no values specified\n", m_name);
  } else {
    fprintf(fd, "Filter \"%s\" %sS records if %s %s ", m_name, ACTION_NAME[m_action], m_field->symbol(), OPERATOR_NAME[m_operator]);
    this->displayRanges(fd);
    fprintf(fd, "\n");
  }
}

bool
filters_are_equal(LogFilter *filt1, LogFilter *filt2)
{
  bool ret = false;

  // TODO: we should check name here
  if (filt1->type() == filt2->type()) {
    if (filt1->type() == LogFilter::INT_FILTER) {
      Debug("log-filter-compare", "int compare");
      ret = (*((LogFilterInt *)filt1) == *((LogFilterInt *)filt2));
    } else if (filt1->type() == LogFilter::IP_FILTER) {
      ret = (*((LogFilterIP *)filt1) == *((LogFilterIP *)filt2));
    } else if (filt1->type() == LogFilter::STRING_FILTER) {
      ret = (*((LogFilterString *)filt1) == *((LogFilterString *)filt2));
    } else {
      ink_assert(!"invalid filter type");
    }
  } else {
    Debug("log-filter-compare", "type diff");
  }
  return ret;
}

/*-------------------------------------------------------------------------
  LogFilterList

  It is ASSUMED that each element on this list has been allocated from the
  heap with "new" and that each element is on at most ONE list.  To enforce
  this, we allow for copies to be made by the system, which is why the
  add() function is overloaded for each sub-type of LogFilter.
  -------------------------------------------------------------------------*/

LogFilterList::LogFilterList() = default;

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LogFilterList::~LogFilterList()
{
  clear();
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

bool
LogFilterList::operator==(LogFilterList &rhs)
{
  if (m_does_conjunction == rhs.does_conjunction()) {
    LogFilter *f    = first();
    LogFilter *rhsf = rhs.first();

    while (true) {
      if (!(f || rhsf)) {
        return true;
      } else if (!f || !rhsf) {
        return false;
      } else if (!filters_are_equal(f, rhsf)) {
        return false;
      } else {
        f    = next(f);
        rhsf = rhs.next(rhsf);
      }
    }
  } else {
    return false;
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
LogFilterList::clear()
{
  LogFilter *f;
  while ((f = m_filter_list.dequeue())) {
    delete f; // safe given the semantics stated above
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
LogFilterList::add(LogFilter *filter, bool copy)
{
  ink_assert(filter != nullptr);
  if (copy) {
    if (filter->type() == LogFilter::INT_FILTER) {
      LogFilterInt *f = new LogFilterInt(*((LogFilterInt *)filter));
      m_filter_list.enqueue(f);
    } else if (filter->type() == LogFilter::IP_FILTER) {
      LogFilterIP *f = new LogFilterIP(*((LogFilterIP *)filter));
      m_filter_list.enqueue(f);
    } else {
      LogFilterString *f = new LogFilterString(*((LogFilterString *)filter));
      m_filter_list.enqueue(f);
    }
  } else {
    m_filter_list.enqueue(filter);
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

bool
LogFilterList::wipe_this_entry(LogAccess *lad)
{
  bool wipeFlag = false;
  for (LogFilter *f = first(); f; f = next(f)) {
    if (f->wipe_this_entry(lad)) {
      wipeFlag = true;
    }
  }
  return wipeFlag;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

bool
LogFilterList::toss_this_entry(LogAccess *lad)
{
  if (m_does_conjunction) {
    // toss if any filter rejects the entry (all filters should accept)
    //
    for (LogFilter *f = first(); f; f = next(f)) {
      if (f->toss_this_entry(lad)) {
        return true;
      }
    }
    return false;
  } else {
    // toss if all filters reject the entry (any filter accepts)
    //
    for (LogFilter *f = first(); f; f = next(f)) {
      if (!f->toss_this_entry(lad)) {
        return false;
      }
    }
    return true;
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

LogFilter *
LogFilterList::find_by_name(const char *name)
{
  for (LogFilter *f = first(); f; f = next(f)) {
    if (strcmp(f->name(), name) == 0) {
      return f;
    }
  }
  return nullptr;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

unsigned
LogFilterList::count() const
{
  unsigned cnt = 0;

  for (LogFilter *f = first(); f; f = next(f)) {
    cnt++;
  }
  return cnt;
}

void
LogFilterList::display(FILE *fd)
{
  for (LogFilter *f = first(); f; f = next(f)) {
    f->display(fd);
  }
}

#if TS_HAS_TESTS
#include "tscore/TestBox.h"

REGRESSION_TEST(Log_FilterParse)(RegressionTest *t, int /* atype */, int *pstatus)
{
  TestBox box(t, pstatus);

#define CHECK_FORMAT_PARSE(fmt)                                   \
  do {                                                            \
    LogFilter *f = LogFilter::parse(fmt, LogFilter::ACCEPT, fmt); \
    box.check(f != NULL, "failed to parse filter '%s'", fmt);     \
    delete f;                                                     \
  } while (0)

  *pstatus = REGRESSION_TEST_PASSED;
  LogFilter *retfilter;

  retfilter = LogFilter::parse("t1", LogFilter::ACCEPT, "tok1 tok2");
  box.check(retfilter == nullptr, "At least 3 tokens are required");
  delete retfilter;
  retfilter = LogFilter::parse("t2", LogFilter::ACCEPT, "%<sym operator value");
  box.check(retfilter == nullptr, "Unclosed symbol token");
  delete retfilter;
  retfilter = LogFilter::parse("t3", LogFilter::ACCEPT, "%<{Age ssh> operator value");
  box.check(retfilter == nullptr, "Unclosed container field");
  delete retfilter;
  retfilter = LogFilter::parse("t4", LogFilter::ACCEPT, "%<james> operator value");
  box.check(retfilter == nullptr, "Invalid log field");
  delete retfilter;
  retfilter = LogFilter::parse("t5", LogFilter::ACCEPT, "%<chi> invalid value");
  box.check(retfilter == nullptr, "Invalid operator name");
  delete retfilter;

  CHECK_FORMAT_PARSE("pssc MATCH 200");
  CHECK_FORMAT_PARSE("shn CASE_INSENSITIVE_CONTAIN unwanted.com");

#undef CHECK_FORMAT_PARSE
}

#endif
