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

#include "tscpp/util/TextView.h"
#include "tscore/ParseRules.h"
#include "MIME.h"

/** Accessor class to iterate over values in a multi-valued field.
 *
 * This implements the logic for quoted strings as specified in the RFC.
 */
class HdrCsvIter
{
  using TextView = ts::TextView;

public:
  /** Construct the iterator in the initial state.
   *
   * @param s The separator character for sub-values.
   */
  HdrCsvIter(char s = ',') : m_separator(s) {}

  /** Get the first sub-value.
   *
   * @param m The multi-valued field.
   * @param follow_dups Continue on to duplicate fields flag.
   * @return A view of the first sub-value in multi-valued data.
   */
  TextView get_first(const MIMEField *m, bool follow_dups = true);
  const char *get_first(const MIMEField *m, int *len, bool follow_dups = true);

  /** Get the next sub-value.
   *
   * @return A view of the next subvalue, or an empty view if no more values.
   *
   * If @a follow_dups was set in the constructor, this will continue on to additional fields
   * if those fields have the same name as the original field (e.g, are duplicates).
   */
  TextView get_next();
  const char *get_next(int *len);

  /** Get the current sub-value.
   *
   * @return A view of the current subvalue, or an empty view if no more values.
   *
   * The state of the iterator is not modified.
   */
  TextView get_current();
  const char *get_current(int *len);

  int count_values(MIMEField *field, bool follow_dups = true);

  /** Get the first sub-value as an integer.
   *
   * @param m Field with the value.
   * @param result [out] Set to the integer sub-value.
   * @return @c true if there was an integer and @a result was set, @c false otherwise.
   */
  bool get_first_int(MIMEField *m, int &result);

  /** Get the next subvalue as an integer.
   *
   * @param result [out] Set to the integer sub-value.
   * @return @c true if there was an integer and @a result was set, @c false otherwise.
   */
  bool get_next_int(int &result);

private:
  void find_csv();

  /// The current field value.
  TextView m_value;

  /// Whether duplicates are being followed.
  bool m_follow_dups = false;

  /// The current sub-value.
  TextView m_csv;

  /// The field containing the current sub-value.
  const MIMEField *m_cur_field = nullptr;

  /// Separator for sub-values.
  /// for the Cookie/Set-cookie headers, the separator is ';'
  const char m_separator; // required constructor parameter, no initialization here.

  void field_init(const MIMEField *m);
};

inline void
HdrCsvIter::field_init(const MIMEField *m)
{
  m_cur_field = m;
  m_value.assign(m->m_ptr_value, m->m_len_value);
}

inline const char *
HdrCsvIter::get_first(const MIMEField *m, int *len, bool follow_dups)
{
  auto tv = this->get_first(m, follow_dups);
  *len    = static_cast<int>(tv.size());
  return tv.data();
}

inline ts::TextView
HdrCsvIter::get_first(const MIMEField *m, bool follow_dups)
{
  field_init(m);
  m_follow_dups = follow_dups;
  this->find_csv();
  return m_csv;
}

inline ts::TextView
HdrCsvIter::get_next()
{
  this->find_csv();
  return m_csv;
}

inline const char *
HdrCsvIter::get_next(int *len)
{
  auto tv = this->get_next();
  *len    = static_cast<int>(tv.size());
  return tv.data();
}

inline ts::TextView
HdrCsvIter::get_current()
{
  return m_csv;
}

inline const char *
HdrCsvIter::get_current(int *len)
{
  *len = static_cast<int>(m_csv.size());
  return m_csv.data();
}

inline bool
HdrCsvIter::get_first_int(MIMEField *m, int &result)
{
  auto val = this->get_first(m);

  if (val) {
    TextView parsed;
    int n = ts::svtoi(val, &parsed);
    if (parsed) {
      result = n;
      return true;
    }
  }
  return false;
}

inline bool
HdrCsvIter::get_next_int(int &result)
{
  auto val = this->get_next();

  if (val) {
    TextView parsed;
    int n = ts::svtoi(val, &parsed);
    if (parsed) {
      result = n;
      return true;
    }
  }
  return false;
}
