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

/****************************************************************************

   HdrUtils.h

   Description: Convenience routines for dealing with hdrs and
                 values


 ****************************************************************************/

#pragma once

#include "tscore/ParseRules.h"
#include "MIME.h"

// csv = comma separated value
class HdrCsvIter
{
public:
  // MIME standard separator ',' is used as the default value
  // Set-cookie/Cookie uses ';'
  HdrCsvIter(const char s = ',')
    : m_value_start(nullptr),
      m_value_len(0),
      m_bytes_consumed(0),
      m_follow_dups(false),
      m_csv_start(nullptr),
      m_csv_len(0),
      m_csv_end(nullptr),
      m_csv_index(0),
      m_cur_field(nullptr),
      m_separator(s)
  {
  }
  const char *get_first(const MIMEField *m, int *len, bool follow_dups = true);
  const char *get_next(int *len);
  const char *get_current(int *len);

  const char *get_nth(MIMEField *m, int *len, int n, bool follow_dups = true);
  int count_values(MIMEField *field, bool follow_dups = true);

  int get_index();

  int get_first_int(MIMEField *m, int *valid = nullptr);
  int get_next_int(int *valid = nullptr);

private:
  void find_csv();

  const char *m_value_start;
  int m_value_len;
  int m_bytes_consumed;
  bool m_follow_dups;

  // m_csv_start - the start of the current comma separated value
  //                 leading white space, and leading quotes have
  //                 been skipped over
  // m_csv_len - the length of the current comma separated value
  //                 not including leading whitespace, trailing
  //                 whitespace (unless quoted) or the terminating
  //                 comma, or trailing quotes have been removed
  // m_csv_end - the terminating comma of the csv unit.  Either
  //                 the terminating comma or the final character
  //                 if this is the last csv in the string
  // m_cvs_index - the integer index of current csv starting
  //                 at zero
  const char *m_csv_start;
  int m_csv_len;
  const char *m_csv_end;
  int m_csv_index;
  const MIMEField *m_cur_field;

  // for the Cookie/Set-cookie headers, the separator is ';'
  const char m_separator;

  void field_init(const MIMEField *m);
};

inline void
HdrCsvIter::field_init(const MIMEField *m)
{
  m_cur_field   = m;
  m_value_start = m->m_ptr_value;
  m_value_len   = m->m_len_value;
  m_csv_start   = m_value_start;
}

inline const char *
HdrCsvIter::get_first(const MIMEField *m, int *len, bool follow_dups)
{
  field_init(m);

  m_follow_dups = follow_dups;

  m_bytes_consumed = 0;
  m_csv_index      = -1;

  if (m_csv_start) {
    find_csv();
  } else {
    m_csv_len = 0;
  }

  *len = m_csv_len;
  return m_csv_start;
}

inline const char *
HdrCsvIter::get_next(int *len)
{
  if (m_csv_start) {
    // Skip past the current csv
    m_csv_start = m_csv_end + 1;
    find_csv();
  }

  *len = m_csv_len;
  return m_csv_start;
}

inline const char *
HdrCsvIter::get_current(int *len)
{
  *len = m_csv_len;
  return m_csv_start;
}

inline int
HdrCsvIter::get_first_int(MIMEField *m, int *valid)
{
  int len;
  const char *r = get_first(m, &len);

  if (r) {
    if (valid) {
      *valid = 1;
    }
    return ink_atoi(r, len);
  } else {
    if (valid) {
      *valid = 0;
    }
    return 0;
  }
}

inline int
HdrCsvIter::get_next_int(int *valid)
{
  int len;
  const char *r = get_next(&len);

  if (r) {
    if (valid) {
      *valid = 1;
    }
    return ink_atoi(r, len);
  } else {
    if (valid) {
      *valid = 0;
    }
    return 0;
  }
}
