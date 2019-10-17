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

#include "tscore/Errata.h"

namespace wccp
{
/// @name Message severity levels.
//@{
extern ts::Errata::Code LVL_FATAL; ///< Fatal, cannot continue.
extern ts::Errata::Code LVL_WARN;  ///< Significant, function degraded.
extern ts::Errata::Code LVL_INFO;  ///< Interesting, not necessarily a problem.
extern ts::Errata::Code LVL_DEBUG; ///< Debugging information.
extern ts::Errata::Code LVL_TMP;   ///< For temporary debugging only.
                                   // Handy so that temporary debugging messages can be located by grep.
                                   //@}

/** Logging / reporting support.
    We build on top of @c Errata but we want to be able to prevent
    message generation / allocation for messages with severity levels
    less than a run time controllable value.

    @internal Far from complete but serving as a prototype / experiment
    to learn what's actually useful.
*/
//@{
/// Report literal string to an Errata.
/// @return @a err.
ts::Errata &log(ts::Errata &err,       ///< Target errata.
                ts::Errata::Id id,     ///< Message ID.
                ts::Errata::Code code, ///< Severity level.
                char const *text       ///< Message text.
);
/// Report literal string to an Errata.
/// Use message ID 0.
/// @return @a err.
ts::Errata &log(ts::Errata &err,       ///< Target errata.
                ts::Errata::Code code, ///< Severity level.
                char const *text       ///< Message text.
);
/// printf style log to Errata.
/// @return @a err.
ts::Errata &logf(ts::Errata &err,       ///< Target errata.
                 ts::Errata::Id id,     ///< Message ID.
                 ts::Errata::Code code, ///< Severity level.
                 char const *format,    ///< Format string.
                 ...                    ///< Format string parameters.
);
/// printf style log to Errata.
/// The message id is set to zero.
/// @return @a err.
ts::Errata &logf(ts::Errata &err,       ///< Target errata.
                 ts::Errata::Code code, ///< Severity level.
                 char const *format,    ///< Format string.
                 ...                    ///< Format string parameters.
);
/// Return an Errata populated with a literal string.
/// Use message ID 0.
/// @return @a err.
ts::Errata log(ts::Errata::Code code, ///< Severity level.
               char const *text       ///< Message text.
);
/// Return an Errata populated with a printf style formatted string.
/// Use message ID 0.
/// @return @a err.
ts::Errata logf(ts::Errata::Code code, ///< Severity level.
                char const *format,    ///< Message text.
                ...);
/** Return an Errata based on @c errno.
    The literal string is combined with the system text for the current
    value of @c errno. This is modeled on @c perror. Message ID 0 is used.
    @return @a err.
 */
ts::Errata log_errno(ts::Errata::Code code, ///< Severity level.
                     char const *text       ///< Message text.
);
/** Return an @c Errata based on @c errno.
    @c errno and the corresponding system error string are appended to
    the results from the @a format and following arguments.
 */
ts::Errata logf_errno(ts::Errata::Code code, ///< Severity code.
                      char const *format,    ///< Format string.
                      ...                    ///< Arguments for @a format.
);
//@}

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
/// Cheap and dirty conversion to string for debugging.
/// @note Uses a static buffer so won't work across threads or
/// twice in the same argument list.
char const *ip_addr_to_str(uint32_t addr ///< Address to convert.
);

/** Used for printing IP address.
    @code
    uint32_t addr; // IP address.
    printf("IP address = " ATS_IP_PRINTF_CODE,ATS_IP_OCTETS(addr));
    @endcode
 */
#define ATS_IP_PRINTF_CODE "%d.%d.%d.%d"
#define ATS_IP_OCTETS(x)                                                                              \
  reinterpret_cast<unsigned char const *>(&(x))[0], reinterpret_cast<unsigned char const *>(&(x))[1], \
    reinterpret_cast<unsigned char const *>(&(x))[2], reinterpret_cast<unsigned char const *>(&(x))[3]
// ------------------------------------------------------

} // namespace wccp
