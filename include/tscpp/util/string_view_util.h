/** @file

   Utility overloads for @c std::string_view

   @section license License

   Licensed to the Apache Software Foundation (ASF) under one or more contributor license
   agreements.  See the NOTICE file distributed with this work for additional information regarding
   copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
   (the "License"); you may not use this file except in compliance with the License.  You may obtain
   a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied. See the License for the specific language governing permissions and limitations under
   the License.
 */

#pragma once
#include <string_view>
#include <string.h>
#include <strings.h>

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
strcmp(const std::string_view &lhs, const std::string_view &rhs)
{
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
memcpy(void *dst, const std::string_view &src)
{
  return memcpy(dst, src.data(), src.size());
}
