# if ! defined(TS_CONFIG_TYPES_HEADER)
# define TS_CONFIG_TYPES_HEADER

/** @file

    Basic types for configuration parsing.

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

# if defined(_MSC_VER)
#   include <stddef.h>
# else
#   include <unistd.h>
# endif

# if defined(__cplusplus)
namespace ts { namespace config {
# endif

/** A location in the source stream.
    @internal At some point we may need to add stream information,
    e.g. file name, once includes are supported. Or should that
    be the caller's responsibility?
 */
struct Location {
  int _col; ///< Column.
  int _line; ///< Line.
};

/** A token from the source stream.
    @internal We should use ts::Buffer here, but because this
    has to work in C as well, it's less painful to do it by hand.
 */
struct Token {
  char* _s; ///< Text of token.
  size_t _n; ///< Text length.
  int _type; ///< Type of token.
  struct Location _loc; ///< Location of token.
};

# if defined(__cplusplus)
}} // namespace ts::config
# define YYSTYPE ts::config::Token
# else
# define YYSTYPE struct Token
#endif


# endif // TS_CONFIG_TYPES_HEADER
