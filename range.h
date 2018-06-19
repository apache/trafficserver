#pragma once

#include "ts/ts.h"

#include <utility>

namespace range
{

/**
  Parses an http closed interval range into a half
  open range for internal use.
  Expects null terminated string
*/

std::pair<int64_t, int64_t>
parseHalfOpenFrom
  ( char const * const rangestr
  );

bool
closedStringFor
  ( std::pair<int64_t, int64_t> const & rangebegend
  , char * const bufstr
  , int * const buflen // returns actual bytes used
  );

/**
  Next set of functions all expect half open intervals.
*/

bool
isValid
  ( std::pair<int64_t, int64_t> const & range
  );

std::pair<int64_t, int64_t>
quantize
  ( int64_t const blocksize
  , std::pair<int64_t, int64_t> const & range
  );

int64_t
firstBlock
  ( int64_t const blocksize
  , std::pair<int64_t, int64_t> const & rangebegend
  );

std::pair<int64_t, int64_t>
forBlock
  ( int64_t const blocksize
  , int64_t const blocknum
  );

bool
blockIsInside
  ( int64_t const blocksize
  , int64_t const blocknum
  , std::pair<int64_t, int64_t> const & rangebegend
  );

int64_t
skipBytesForBlock
  ( int64_t const blocksize
  , int64_t const blocknum
  , std::pair<int64_t, int64_t> const & rangebegend
  );

} // namespace range
