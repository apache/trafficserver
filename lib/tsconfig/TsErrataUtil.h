/** @file

    TS Configuration utilities for Errata and logging.

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

# if !defined(TS_ERRATA_UTIL_HEADER)
# define TS_ERRATA_UTIL_HEADER

// TBD: Need to do something better than this to get around using
// 'DEBUG' as the name of a constant.
# if defined DEBUG
# undef DEBUG
# define DEBUG DEBUG
# endif

# include <Errata.h>

namespace ts { namespace msg {

/// @name Message severity levels.
//@{
extern Errata::Code FATAL; ///< Fatal, cannot continue.
extern Errata::Code WARN; ///< Significant, function degraded.
extern Errata::Code INFO; ///< Interesting, not necessarily a problem.
extern Errata::Code DEBUG; ///< Debugging information.
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
Errata& log(
  Errata& err,///< Target errata.
  Errata::Id id, ///< Message ID.
  Errata::Code code, ///< Severity level.
  char const* text ///< Message text.
);
/// Report literal string to an Errata.
/// Use message ID 0.
/// @return @a err.
Errata& log(
    Errata& err,///< Target errata.
    Errata::Code code, ///< Severity level.
    char const* text ///< Message text.
);
/// Report literal string to a return value.
/// Use message ID 0.
/// @return The @c Errata in @a rv.
Errata& log(
    RvBase& rv,///< Return value.
    Errata::Code code, ///< Severity level.
    char const* text ///< Message text.
);
/// printf style log to Errata.
/// @return @a err.
Errata& logf(
  Errata& err,///< Target errata.
  Errata::Id id, ///< Message ID.
  Errata::Code code, ///< Severity level.
  char const* format, ///< Format string.
  ... ///< Format string parameters.
);
/// printf style log to Errata.
/// The message id is set to zero.
/// @return @a err.
Errata& logf(
  Errata& err,///< Target errata.
  Errata::Code code, ///< Severity level.
  char const* format, ///< Format string.
  ... ///< Format string parameters.
);
/// Return an Errata in a return value populated with a printf style formatted string.
/// Use message ID 0.
/// @return The @c Errata in @a rv.
Errata& logf(
    RvBase& rv, ///< Rv value.
    Errata::Code code, ///< Severity level.
    char const* format, ///< Message text.
    ...
);
/// Return an Errata populated with a literal string.
/// Use message ID 0.
/// @return @a err.
Errata log(
  Errata::Code code, ///< Severity level.
  char const* text ///< Message text.
);
/// Return an Errata populated with a printf style formatted string.
/// Use message ID 0.
/// @return @a err.
Errata logf(
  Errata::Code code, ///< Severity level.
  char const* format, ///< Message text.
  ...
);
/** Return an Errata based on @c errno.
    The literal string is combined with the system text for the current
    value of @c errno. This is modeled on @c perror. Message ID 0 is used.
    @return The new @c Errata.
 */
Errata log_errno(
  Errata::Code code, ///< Severity level.
  char const* text ///< Message text.
);
/** Return an @c Errata based on @c errno.
    @c errno and the corresponding system error string are appended to
    the results from the @a format and following arguments.
    @return The new @c Errata.
 */
Errata
logf_errno(
  Errata::Code code,  ///< Severity code.
  char const* format, ///< Format string.
  ... ///< Arguments for @a format.
);
/** Add a message to an @a errata based on @c errno.
    @c errno and the corresponding system error string are appended to
    the results from the @a format and following arguments.
    @return @a errata.
 */
Errata
logf_errno(
  Errata& errata, ///< Errata to use.
  Errata::Code code,  ///< Severity code.
  char const* format, ///< Format string.
  ... ///< Arguments for @a format.
);
/** Add a message to a return value based on @c errno.
    @c errno and the corresponding system error string are appended to
    the results from the @a format and following arguments.
    @return The errata in @a rv.
 */
Errata
logf_errno(
  RvBase& rv, ///< Return value.
  Errata::Code code,  ///< Severity code.
  char const* format, ///< Format string.
  ... ///< Arguments for @a format.
);
//@}

}} // namespace ts::msg

# endif // define TS_ERRATA_UTIL_HEADER
