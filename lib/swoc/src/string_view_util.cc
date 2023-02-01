// SPDX-License-Identifier: Apache-2.0
// Copyright Apache Software Foundation 2019
/** @file

    Additional handy utilities for @c string_view and hence also @c TextView.
*/

#include "swoc/string_view_util.h"

int
memcmp(std::string_view const &lhs, std::string_view const &rhs) {
  int zret = 0;
  size_t n = rhs.size();

  // Seems a bit ugly but size comparisons must be done anyway to get the memcmp args.
  if (lhs.size() < rhs.size()) {
    zret = 1;
    n    = lhs.size();
  } else if (lhs.size() > rhs.size()) {
    zret = -1;
  } else if (lhs.data() == rhs.data()) { // same memory, obviously equal.
    return 0;
  }

  int r = ::memcmp(lhs.data(), rhs.data(), n);
  return r ? r : zret;
}

int
strcasecmp(const std::string_view &lhs, const std::string_view &rhs) {
  int zret = 0;
  size_t n = rhs.size();

  // Seems a bit ugly but size comparisons must be done anyway to get the @c strncasecmp args.
  if (lhs.size() < rhs.size()) {
    zret = 1;
    n    = lhs.size();
  } else if (lhs.size() > rhs.size()) {
    zret = -1;
  } else if (lhs.data() == rhs.data()) { // the same memory, obviously equal.
    return 0;
  }

  int r = ::strncasecmp(lhs.data(), rhs.data(), n);

  return r ? r : zret;
}
