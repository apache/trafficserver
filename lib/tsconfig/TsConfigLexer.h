/** @file

    A brief file description

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

# if ! defined(TS_CONFIG_LEXER_HEADER)
# define TS_CONFIG_LEXER_HEADER

struct TsConfigHandlers; // forward declare.

# if defined(__cplusplus)
    extern "C" {
# endif

/// Get the current line in the buffer during parsing.
/// @return 1 based line number.
extern int tsconfiglex_current_line(void);
/// Get the current column in the buffer during parsing.
/// @return 0 base column number.
extern int tsconfiglex_current_col(void);

/** Parse @a buffer.

    The @a buffer is parsed and the events dispatched via @a handlers.

    @note The contents of @a buffer are in general modified to handle
    null termination and processing escape codes in strings. Tokens
    passed to the handlers are simply offsets in to @a buffer and
    therefore have the same lifetime as @a buffer.

    @return Not sure.
 */
extern int tsconfig_parse_buffer(
  struct TsConfigHandlers* handlers, ///< Syntax handlers.
  char* buffer, ///< Input buffer.
  size_t buffer_len ///< Length of input buffer.
);

# if defined(__cplusplus)
}
# endif

# endif // TS_CONFIG_LEXER_HEADER
