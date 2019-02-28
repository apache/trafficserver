/** @file

   Utilities for HTTP MIME headers.

   @section license License

   Licensed to the Apache Software Foundation (ASF) under one or more contributor license agreements.
   See the NOTICE file distributed with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance with the License.  You may obtain a
   copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied. See the License for the specific language governing permissions and limitations under
   the License.
 */

/****************************************************************************

   HdrUtils.cc

   Description: Convenience routines for dealing with hdrs and
                 values


 ****************************************************************************/

#include "tscore/ink_platform.h"
#include "HdrUtils.h"

void
HdrCsvIter::find_csv()
{
  char sep_list[3] = {'"', m_separator, 0};

  m_csv.clear();

  while (m_cur_field) {
    while (m_value) {
      bool in_quote_p = false;
      // Index of next interesting character.
      TextView::size_type idx = 0;
      // Characters of interest in a null terminated string.
      while (idx < m_value.size()) {
        // Next character of interest.
        idx = m_value.find_first_of(sep_list, idx);
        if (TextView::npos == idx) {
          // no more, consume all of @a src.
          break;
        } else if ('"' == m_value[idx]) {
          if (in_quote_p) {
            // quoted-pair only allowed inside a quoted-string.
            // Can always back up because if @a in_quote_p there's a least a preceding quote.
            if (m_value[idx - 1] != '\\') {
              in_quote_p = false;
            }
          } else {
            in_quote_p = true;
          }
          ++idx;
        } else if (m_separator == m_value[idx]) { // separator.
          if (in_quote_p) {
            // quoted separator, skip and continue.
            ++idx;
          } else {
            // found token, finish up.
            break;
          }
        }
      }

      // Trim and then see if there's anything left.
      m_csv = m_value.take_prefix_at(idx).trim_if(&ParseRules::is_ws);
      if (m_csv && '"' == m_csv[0]) {
        m_csv.remove_prefix(1);
      }
      if (m_csv && '"' == m_csv[m_csv.size() - 1]) {
        m_csv.remove_suffix(1);
      }
      // If there's any value after that, we're done.
      if (m_csv) {
        return;
      }
    }
    // Current field is exhausted, try the next if following dupes.
    if (m_follow_dups && m_cur_field->m_next_dup) {
      this->field_init(m_cur_field->m_next_dup);
    } else {
      m_cur_field = nullptr;
    }
  }
}

ts::TextView
HdrCsvIter::get_nth(MIMEField *field, int nth, bool follow_dups)
{
  ink_assert(nth >= 0);
  int i = 0;

  auto tv = get_first(field, follow_dups); // index zero sub-value.
  while (tv && nth > i) {
    tv = get_next();
    ++i;
  }
  return tv;
}

const char *
HdrCsvIter::get_nth(MIMEField *field, int *len, int n, bool follow_dups)
{
  auto tv = this->get_nth(field, n, follow_dups);
  *len    = int(tv.size());
  return tv.data();
}

int
HdrCsvIter::count_values(MIMEField *field, bool follow_dups)
{
  int count = 0;
  auto val  = get_first(field, follow_dups); // get index 0
  while (val) {
    val = get_next();
    ++count;
  }
  return count;
}
