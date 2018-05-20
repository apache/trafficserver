/** @file
    WCCP utilities and logging.

    @section license License

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

#pragma once

#include <vector>
#include "tsconfig/Errata.h"

namespace wccp
{
// ------------------------------------------------------
/*  Template support for access to raw message fields.
    We have three overloads, one for each size of field.
    By passing the member pointer we avoid having to specify
    any template argument at the call site. For instance, to
    get a 16 bit field out of a "raw_t" struct with a member
    name of "field", you would use

    get_field(&raw_t::field, buffer);

    Note these templates handle network ordering internally.
    See access_field for completely unmediated access.
*/

template <typename R>
uint8_t
get_field(uint8_t R::*M, char *buffer)
{
  return reinterpret_cast<R *>(buffer)->*M;
}

template <typename R>
uint16_t
get_field(uint16_t R::*M, char *buffer)
{
  return ntohs(reinterpret_cast<R *>(buffer)->*M);
}

template <typename R>
uint32_t
get_field(uint32_t R::*M, char *buffer)
{
  return ntohl(reinterpret_cast<R *>(buffer)->*M);
}

template <typename R>
void
set_field(uint16_t R::*M, char *buffer, uint16_t value)
{
  reinterpret_cast<R *>(buffer)->*M = htons(value);
}

template <typename R>
void
set_field(uint32_t R::*M, char *buffer, uint32_t value)
{
  reinterpret_cast<R *>(buffer)->*M = htonl(value);
}

// IP address fields are kept locally in network order so
// we need variants without re-ordering.
// Also required for member structures.
template <typename R, typename T>
T &
access_field(T R::*M, void *buffer)
{
  return reinterpret_cast<R *>(buffer)->*M;
}

// Access an array @a T starting at @a buffer.
template <typename T>
T *
access_array(void *buffer)
{
  return reinterpret_cast<T *>(buffer);
}
// Access an array of @a T starting at @a buffer.
template <typename T>
T const *
access_array(void const *buffer)
{
  return reinterpret_cast<T const *>(buffer);
}

/// Find an element in a vector by the value of a member.
/// @return An iterator for the element, or equal to @c end if not found.
template <typename T, ///< Vector element type.
          typename V  ///< Member type.
          >
typename std::vector<T>::iterator
find_by_member(std::vector<T> &container, ///< Vector with elements.
               V T::*member,              ///< Pointer to member to compare.
               V const &value             ///< Value to match.
)
{
  typename std::vector<T>::iterator spot  = container.begin();
  typename std::vector<T>::iterator limit = container.end();
  while (spot != limit && (*spot).*member != value)
    ++spot;
  return spot;
}
// ------------------------------------------------------
/// Find a non-loop back IP address from an open socket.
uint32_t Get_Local_Address(int s ///< Open socket.
);
// ------------------------------------------------------
} // namespace wccp
