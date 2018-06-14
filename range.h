#pragma once

#include "ts/ts.h"

#include <utility>

bool
rangeIsValid
  ( std::pair<int64_t, int64_t> const & range
  );

std::pair<int64_t, int64_t>
quantizeRange
  ( int64_t const blocksize
  , std::pair<int64_t, int64_t> const & range
  );

int64_t
firstBlockInRange
  ( int64_t const blocksize
  , std::pair<int64_t, int64_t> const & rangebegend
  );

std::pair<int64_t, int64_t>
rangeForBlock
  ( int64_t const blocksize
  , int64_t const blocknum
  );

bool
blockIsInRange
  ( int64_t const blocksize
  , int64_t const blocknum
  , std::pair<int64_t, int64_t> const & rangebegend
  );
