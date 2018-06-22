#include "Range.h"
#include "slice.h"

#include <algorithm>
#include <cinttypes>
#include <cstring>

bool
Range :: isValid
  () const
{
  return m_beg < m_end;
}

bool
Range :: fromStringClosed
  ( char const * const rangestr
  )
{
  static char const DELIM_DASH = '-';
//  static char const DELIM_MULTI = ',';
  static char const * const BYTESTR = "bytes=";
  static size_t const BYTESTRLEN = 6;

  // make sure this is in byte units
  if (0 != strncmp(BYTESTR, rangestr, BYTESTRLEN)) {
    return false;
  }

  // advance past any white space
  char const * pfront = rangestr + BYTESTRLEN;
  while ('\0' != *pfront && isblank(*pfront)) {
    ++pfront;
  }

  // check for last N request
  if ('-' == *pfront) {
    ERROR_LOG("Last N byte request not handled");
    return false;
  }

  if ('\0' == *pfront) {
    ERROR_LOG("First Range number not found in '%s'", rangestr);
    return false;
  }

  char const * const pdash = strchr(pfront, DELIM_DASH);
  if (nullptr == pdash) {
    ERROR_LOG("Range Delim '%c' not found", DELIM_DASH);
    return false;
  }

  // interpret front value
  char * pfe = nullptr;
  int64_t const front = strtoll(pfront, &pfe, 10);

  if (pfe == pfront) {
    ERROR_LOG("Range front invalid: '%s'", rangestr);
   return false;
  }

  char const * pback = pdash + 1;

  // interpret back value
  char * pfb = nullptr;
  int64_t back = strtoll(pback, &pfb, 10);

  if (pfb == pback) { // blank value, assume end of (unknown) file
    back = std::numeric_limits<int64_t>::max() - 1;
  }

  if (front <= back)
  {
    m_beg = front;
    m_end = back + 1;
  }
  else
  {
    return false;
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
