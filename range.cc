#include "range.h"

#include <algorithm>

namespace
{

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

}

bool
rangeIsValid
  ( std::pair<int64_t, int64_t> const & range
  )
{
  return range.first < range.second;
}

std::pair<int64_t, int64_t>
quantizeRange
  ( int64_t const blocksize
  , std::pair<int64_t, int64_t> const & rangebe
  )
{
  if (blocksize <= 0 || ! rangeIsValid(rangebe))
  {
    return std::make_pair(-1, -1);
  }

  int64_t const blockbeg(rangebe.first / blocksize);
  int64_t const blockend((rangebe.second + blocksize - 1) / blocksize);

  return std::make_pair(blockbeg * blocksize, blockend * blocksize);
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
