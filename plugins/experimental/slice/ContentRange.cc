/** @file
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

#include "ContentRange.h"

#include <cinttypes>
#include <cstdio>

static char const *const format = "bytes %" PRId64 "-%" PRId64 "/%" PRId64;

bool
ContentRange::fromStringClosed(char const *const valstr)
{
  int const fields = sscanf(valstr, format, &m_beg, &m_end, &m_length);

  if (3 == fields && m_beg <= m_end) {
    m_end += 1;
  } else {
    m_beg = m_end = m_length = -1;
  }

  return isValid();
}

bool
ContentRange::toStringClosed(char *const rangestr, int *const rangelen) const
{
  if (!isValid()) {
    if (0 < *rangelen) {
      rangestr[0] = '\0';
    }
    *rangelen = 0;
    return false;
  }

  int const lenin = *rangelen;
  *rangelen       = snprintf(rangestr, lenin, format, m_beg, (m_end - 1), m_length);

  return (0 < *rangelen && *rangelen < lenin);
}
