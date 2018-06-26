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

#include "Range.h"
#include "slice.h"

#include <algorithm>
#include <cinttypes>
#include <cstring>
#include <iostream>

bool
Range :: isValid
  () const
{
  return m_beg < m_end;
}

int64_t
Range :: size
  () const
{
  return m_end - m_beg;
}

bool
Range :: fromStringClosed
  ( char const * const rangestr
  )
{
static char const * const BYTESTR = "bytes=";
static size_t const BYTESTRLEN = strlen(BYTESTR);

  // make sure this is in byte units
  if (0 != strncmp(BYTESTR, rangestr, BYTESTRLEN)) {
    return false;
  }

  // advance past any white space
  char const * pstr = rangestr + BYTESTRLEN;
  while ('\0' != *pstr && isblank(*pstr)) {
    ++pstr;
  }

  // rip out any whitespace
  char rangebuf[1024];
  char * pbuf = rangebuf;
  while ('\0' != *pstr) {
    if (! isblank(*pstr)) {
      *pbuf++ = *pstr;
    }
    ++pstr;
  }
  *pbuf = '\0';

  char const * const fmtclosed = "%" PRId64 "-%" PRId64;
  int64_t front = 0;
  int64_t back = 0;

  // normal range <front>-<back>
  int const fieldsclosed = sscanf(rangebuf, fmtclosed, &front, &back);
  if (2 == fieldsclosed) {
    if (0 <= front && front <= back) {
      m_beg = front;
      m_end = back + 1;
    } else { // ill formed
      m_beg = 0;
      m_end = std::numeric_limits<int64_t>::max();
    }
    return true;
  }

  // last 'n' bytes use range with negative begin and 0 end
  int64_t endbytes = 0;
  char const * const fmtend = "-%" PRId64;
  int const fieldsend = sscanf(rangebuf, fmtend, &endbytes);
  if (1 == fieldsend) {
    m_beg = -endbytes;
    m_end = 0;
    return true;
  }

  front = 0;
  char const * const fmtbeg = "%" PRId64 "-";
  int const fieldsbeg = sscanf(rangebuf, fmtbeg, &front);
  if (1 == fieldsbeg) {
    m_beg = front;
    m_end = std::numeric_limits<int64_t>::max();
  }

  return true;
} // parseRange

bool
Range :: toStringClosed
  ( char * const bufstr
  , int * const buflen // returns actual bytes used
  ) const
{
  if (! isValid())
  {
    return false;
  }

  int const lenin = *buflen;

static int64_t const threshold(std::numeric_limits<int64_t>::max() / 2);

  if (m_end < threshold)
  {
    *buflen = snprintf
      ( bufstr, lenin
      , "bytes=%" PRId64 "-%" PRId64
      , m_beg, m_end - 1 );
  }
  else
  {
    *buflen = snprintf
      (bufstr, lenin, "bytes=%" PRId64 "-" , m_beg);
  }

  return (0 < *buflen && *buflen < lenin);
}

int64_t
Range :: firstBlockFor
  ( int64_t const blocksize
  ) const
{
  if (0 < blocksize && isValid())
  {
    return m_beg / blocksize;
  }
  else
  {
    return -1;
  }
}

Range
Range :: intersectedWith
  ( Range const & other
  ) const
{
  return Range
    ( std::max(m_beg, other.m_beg)
    , std::min(m_end, other.m_end) );
}

bool
Range :: blockIsInside
  ( int64_t const blocksize
  , int64_t const blocknum
  ) const
{
  Range const blockrange
    (blocksize * blocknum, blocksize * (blocknum + 1));

  Range const isec(blockrange.intersectedWith(*this));

  return isec.isValid();
}

int64_t
Range :: skipBytesForBlock
  ( int64_t const blocksize
  , int64_t const blocknum
  ) const
{
  int64_t const blockstart(blocksize * blocknum);

  if (m_beg < blockstart)
  {
    return 0;
  }
  else
  {
    return m_beg - blockstart;
  }
}

bool
Range :: isEndBytes
  () const
{
  return m_beg < 0 && 0 == m_end;
}
