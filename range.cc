#include "range.h"
#include "slice.h"

#include <algorithm>
#include <cinttypes>
#include <cstring>

std::pair<int64_t, int64_t>
range :: parseHalfOpenFrom
  ( char const * const rangestr
  )
{
  std::pair<int64_t, int64_t> frontback(-1, -1);

  static char const DELIM_DASH = '-';
//  static char const DELIM_MULTI = ',';
  static char const * const BYTESTR = "bytes=";
  static size_t const BYTESTRLEN = 6;

  // make sure this is in byte units
  if (0 != strncmp(BYTESTR, rangestr, BYTESTRLEN)) {
    return frontback;
  }

  // advance past any white space
  char const * pfront = rangestr + BYTESTRLEN;
  while ('\0' != *pfront && isblank(*pfront)) {
    ++pfront;
  }

  // check for last N request
  if ('-' == *pfront) {
    ERROR_LOG("Last N byte request not handled");
    return frontback;
  }

  if ('\0' == *pfront) {
    ERROR_LOG("First Range number not found in '%s'", rangestr);
    return frontback;
  }

  char const * const pdash = strchr(pfront, DELIM_DASH);
  if (nullptr == pdash) {
    ERROR_LOG("Range Delim '%c' not found", DELIM_DASH);
    return frontback;
  }

  // interpret front value
  char * pfe = nullptr;
  int64_t const front = strtoll(pfront, &pfe, 10);

  if (pfe == pfront) {
    ERROR_LOG("Range front invalid: '%s'", rangestr);
   return frontback;
  }

  char const * pback = pdash + 1;

  // interpret back value
  char * pfb = nullptr;
  int64_t back = strtoll(pback, &pfb, 10);

  if (pfb == pback) { // blank value, assume end of (unknown) file
    back = std::numeric_limits<int64_t>::max();
  }

  if (front <= back)
  {
    frontback = std::make_pair(front, back + 1); // half open conversion
  }

  return frontback;
} // parseRange

bool
range :: closedStringFor
  ( std::pair<int64_t, int64_t> const & rangebegend
  , char * const bufstr
  , int * const buflen // returns actual bytes used
  )
{
  if (! isValid(rangebegend))
  {
    return false;
  }

  int const lenin = *buflen;
  *buflen = snprintf
    ( bufstr, lenin
    , "bytes=%" PRId64 "-%" PRId64
    , rangebegend.first, rangebegend.second - 1 );

  return (*buflen < lenin);
}

namespace
{

inline
std::pair<int64_t, int64_t>
intersection
  ( std::pair<int64_t, int64_t> const & lhs
  , std::pair<int64_t, int64_t> const & rhs
  )
{
  return std::make_pair
    ( std::max(lhs.first, rhs.first)
    , std::min(lhs.second, rhs.second) );
}

}

bool
range :: isValid
  ( std::pair<int64_t, int64_t> const & range
  )
{
  return range.first < range.second;
}

std::pair<int64_t, int64_t>
range :: quantize
  ( int64_t const blocksize
  , std::pair<int64_t, int64_t> const & rangebe
  )
{
  if (blocksize <= 0 || ! isValid(rangebe))
  {
    return std::make_pair(-1, -1);
  }

  int64_t const blockbeg(rangebe.first / blocksize);
  int64_t const blockend((rangebe.second + blocksize - 1) / blocksize);

  return std::make_pair(blockbeg * blocksize, blockend * blocksize);
}

int64_t
range :: firstBlock
  ( int64_t const blocksize
  , std::pair<int64_t, int64_t> const & rangebegend
  )
{
  if (0 < blocksize && isValid(rangebegend))
  {
    return rangebegend.first / blocksize;
  }
  else
  {
    return -1;
  }
}

std::pair<int64_t, int64_t>
range :: forBlock
  ( int64_t const blocksize
  , int64_t const blocknum
  )
{
  return std::make_pair(blocknum * blocksize, (blocknum + 1) * blocksize);
}

bool
range :: blockIsInside
  ( int64_t const blocksize
  , int64_t const blocknum
  , std::pair<int64_t, int64_t> const & rangebegend
  )
{
  std::pair<int64_t, int64_t> const rangeisect
    (intersection
      (rangebegend, range::forBlock(blocksize, blocknum)));
  return isValid(rangeisect);
}
