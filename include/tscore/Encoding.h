/** @file

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

#include <cstddef>
#include <string_view>

class Arena;

/*-------------------------------------------------------------------------
  Encoding::escapify_url_common

  This routine will escapify a URL to remove spaces (and perhaps other ugly
  characters) from a URL and replace them with a hex escape sequence.
  Since the escapes are larger (multi-byte) than the characters being
  replaced, the string returned will be longer than the string passed.

  This is a worker function called by escapify_url and pure_escapify_url.  These
  functions differ on whether the function tries to detect and avoid
  double URL encoding (escapify_url) or not (pure_escapify_url)
  -------------------------------------------------------------------------*/

namespace Encoding
{
char *escapify_url(Arena *arena, char *url, size_t len_in, int *len_out, char *dst = nullptr, size_t dst_size = 0,
                   const unsigned char *map = nullptr);
char *pure_escapify_url(Arena *arena, char *url, size_t len_in, int *len_out, char *dst = nullptr, size_t dst_size = 0,
                        const unsigned char *map = nullptr);

/** Escape a UTF-8 string for inclusion in HTML.

    This implements the HTML fragment serialization escaping algorithm. The
    characters @c &, @c <, @c >, and U+00A0 NO-BREAK SPACE are always escaped.
    The @c " character is additionally escaped when @a use_attribute_mode is
    @c true.

    @param[in] input string to escape.
    @param[out] dst destination buffer, which must not overlap @a input.
    @param[in] dst_size size of @a dst, including space for the terminating NUL.
    @param[out] length amount of data written, excluding the terminating NUL.
      This is set to zero if escaping fails. This may be @c nullptr.
    @param[in] use_attribute_mode whether to escape for a double-quoted HTML
      attribute value.

    @return @c true on success, @c false if @a dst is null, the output length
      overflows, or @a dst is too small.
 */
bool html_escape(std::string_view input, char *dst, size_t dst_size, size_t *length, bool use_attribute_mode);

/** Unescape the HTML character references emitted by html_escape().

    The character references @c &amp;, @c &nbsp;, @c &lt;, @c &gt;, and
    @c &quot; are replaced by their corresponding UTF-8 characters. Other
    character references are preserved. Unescaping is performed once, so an
    unescaped character reference is not unescaped again.

    @param[in] input string to unescape.
    @param[out] dst destination buffer, which must not overlap @a input.
    @param[in] dst_size size of @a dst, including space for the terminating NUL.
    @param[out] length amount of data written, excluding the terminating NUL.
      This is set to zero if unescaping fails. This may be @c nullptr.

    @return @c true on success, @c false if @a dst is null or too small.
 */
bool html_unescape(std::string_view input, char *dst, size_t dst_size, size_t *length);
}; // namespace Encoding
