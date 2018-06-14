#include "range.h"

#include <algorithm>

bool
rangeIsValid
  ( std::pair<int64_t, int64_t> const & range
  )
{
  return range.first < range.second;
}

static
inline
std::pair<int64_t, int64_t>
rangeIntersection
  ( std::pair<int64_t, int64_t> const & lhs
  , std::pair<int64_t, int64_t> const & rhs
  )
{
  return std::make_pair
    ( std::max(lhs.first, rhs.first)
    , std::min(lhs.second, rhs.second) );
}

int64_t
firstBlockInRange
  ( int64_t const blocksize
  , std::pair<int64_t, int64_t> const & rangebegend
  )
{
  if (0 < blocksize && rangeIsValid(rangebegend))
  {
    return rangebegend.first / blocksize;
  }
  else
  {
    return -1;
  }
}

std::pair<int64_t, int64_t>
rangeForBlock
  ( int64_t const blocksize
  , int64_t const blocknum
  )
{
  return std::make_pair(blocknum * blocksize, (blocknum + 1) * blocksize);
}

bool
blockIsInRange
  ( int64_t const blocksize
  , int64_t const blocknum
  , std::pair<int64_t, int64_t> const & rangebegend
  )
{
  std::pair<int64_t, int64_t> const rangeisect
    (rangeIntersection
      (rangebegend, rangeForBlock(blocksize, blocknum)));
  return rangeIsValid(rangeisect);
}
