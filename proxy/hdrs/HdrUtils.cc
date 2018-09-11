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

   HdrUtils.cc

   Description: Convenience routines for dealing with hdrs and
                 values


 ****************************************************************************/

#include "tscore/ink_platform.h"
#include "HdrUtils.h"

#define GETNEXT()     \
  {                   \
    cur += 1;         \
    if (cur >= end) { \
      goto done;      \
    }                 \
  }

void
HdrCsvIter::find_csv()
{
  const char *cur, *end, *last_data, *csv_start;

RETRY:

  cur       = m_csv_start;
  end       = m_value_start + m_value_len;
  last_data = nullptr;
  csv_start = nullptr;

  if (cur >= end) {
    goto done;
  }
skip_leading_whitespace:
  if (ParseRules::is_ws(*cur)) {
    GETNEXT();
    goto skip_leading_whitespace;
  }
  csv_start = cur;
parse_value:
  // ink_assert((',' > '"') && (',' > ' ') && (',' > '\t'));
  // Cookie/Set-Cookie use ';' as the separator
  if (m_separator == ',') {
    while ((cur < end - 1) && (*cur > ',')) {
      last_data = cur;
      cur++;
    }
  }

  if (*cur == m_separator) {
    goto done;
  }
  if (*cur == '\"') {
    // If the quote come before any text
    //   skip it
    if (cur == csv_start) {
      csv_start++;
    }
    GETNEXT();
    goto parse_value_quote;
  }
  if (!ParseRules::is_ws(*cur)) {
    last_data = cur;
  }
  GETNEXT();
  goto parse_value;
parse_value_quote:
  if ((*cur == '\"') && (cur[-1] != '\\')) {
    GETNEXT();
    goto parse_value;
  }
  last_data = cur;
  GETNEXT();
  goto parse_value_quote;
done:
  m_csv_end   = cur;
  m_csv_start = csv_start;

  if (last_data) {
    m_csv_len = (int)(last_data - csv_start) + 1;
  } else {
    // Nothing found.  See if there is another
    //    field in the dup list
    if (m_cur_field->m_next_dup && m_follow_dups) {
      field_init(m_cur_field->m_next_dup);
      goto RETRY;
    }

    m_csv_len = 0;
  }
}

const char *
HdrCsvIter::get_nth(MIMEField *field, int *len, int n, bool follow_dups)
{
  const char *s;
  int i, l;

  ink_assert(n >= 0);
  i = 0;

  s = get_first(field, &l, follow_dups); // get index 0
  while (s && (n > i)) {
    s = get_next(&l);
    i++;
  }                   // if had index i, but want n > i, get next
  *len = (s ? l : 0); // length is zero if NULL string
  return (s);
}

int
HdrCsvIter::count_values(MIMEField *field, bool follow_dups)
{
  const char *s;
  int count, l;

  count = 0;
  s     = get_first(field, &l, follow_dups); // get index 0
  while (s) {
    s = get_next(&l);
    ++count;
  } // get next
  return (count);
}

/*
int main() {

    char* tests[] = {"\"I\", \"hate\", \"strings\"",
                     "This, is a , test",
                     "a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p",
                     "\"This is,\" a test"
    };

    for (int i = 0; i < 4; i++) {
        printf ("Testing: %s\n", tests[i]);

        HdrCsvIter iter;
        int len;
        const char* v = iter.get_first(tests[i], strlen(tests[i]), &len);

        while (v) {
            char* str_v = (char*)ats_malloc(len+1);
            memcpy(str_v, v, len);
            str_v[len] = '\0';
            printf ("%s|", str_v);
            v = iter.get_next(&len);
        }

        printf("\n\n");
    }

}
*/
