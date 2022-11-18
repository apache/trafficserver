// SPDX-License-Identifier: Apache-2.0
// Copyright Apache Software Foundation 2019
/** @file

    Additional handy utilities for @c string_view and hence also @c TextView.
*/

#pragma once
#include <bitset>
#include <iosfwd>
#include <memory.h>
#include <string>
#include <string_view>
#include <limits>

/** Compare views with ordering, ignoring case.
 *
 * @param lhs input view
 * @param rhs input view
 * @return The ordered comparison value.
 *
 * - -1 if @a lhs is less than @a rhs
 * -  1 if @a lhs is greater than @a rhs
 * -  0 if the views have identical content.
 *
 * If one view is the prefix of the other, the shorter view is less (first in the ordering).
 */
int strcasecmp(const std::string_view &lhs, const std::string_view &rhs);

/** Compare views with ordering.
 *
 * @param lhs input view
 * @param rhs input view
 * @return The ordered comparison value.
 *
 * - -1 if @a lhs is less than @a rhs
 * -  1 if @a lhs is greater than @a rhs
 * -  0 if the views have identical content.
 *
 * If one view is the prefix of the other, the shorter view is less (first in the ordering).
 *
 * @note For string views, there is no difference between @c strcmp and @c memcmp.
 * @see strcmp
 */
int memcmp(const std::string_view &lhs, const std::string_view &rhs);

/** Compare views with ordering.
 *
 * @param lhs input view
 * @param rhs input view
 * @return The ordered comparison value.
 *
 * - -1 if @a lhs is less than @a rhs
 * -  1 if @a lhs is greater than @a rhs
 * -  0 if the views have identical content.
 *
 * If one view is the prefix of the other, the shorter view is less (first in the ordering).
 *
 * @note For string views, there is no difference between @c strcmp and @c memcmp.
 * @see memcmp
 */
inline int
strcmp(const std::string_view &lhs, const std::string_view &rhs) {
  return memcmp(lhs, rhs);
}

/** Copy bytes.
 *
 * @param dst Destination buffer.
 * @param src Original string.
 * @return @a dest
 *
 * This is a convenience for
 * @code
 *   memcpy(dst, src.data(), size.size());
 * @endcode
 * Therefore @a dst must point at a buffer large enough to hold @a src. If this is not already
 * determined, then presuming @c DST_SIZE is the size of the buffer at @a dst
 * @code
 *   memcpy(dst, src.prefix(DST_SIZE));
 * @endcode
 *
 */
inline void *
memcpy(void *dst, const std::string_view &src) {
  return memcpy(dst, src.data(), src.size());
}
