/*
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

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* return codes */
#define STRIP_RESULT_OK 0            /**< success */
#define STRIP_RESULT_BAD_PARAM -1    /**< one or more invalid arguments */
#define STRIP_RESULT_OUTLEN_SMALL -2 /**< output buffer not large enough */
#define STRIP_RESULT_EMPTY_IN -3     /**< in consists solely of whitespace */

/* defined flags */
#define STRIP_FLAG_NONE 0x0            /**< no flags */
#define STRIP_FLAG_STRIP_LOW 0x1       /**< stripped, html: strip low */
#define STRIP_FLAG_STRIP_HIGH 0x2      /**< stripped, html: strip high */
#define STRIP_FLAG_LEAVE_WHITESP 0x4   /**< all: avoid trimming spaces */
#define STRIP_FLAG_UNSAFE_QUOTES 0x8   /**< html: dont encode quotes */
#define STRIP_FLAG_UNSAFE_SLASHES 0x10 /**< all: dont encode backslashes */
#define STRIP_FLAG_UNSAFE_SPACES 0x20  /**< html: stripped tag isnt space */

/** Output the input after stripping all characters that are
 *  unsafe in an HTML context.
 *
 * This function performs the following treatment:
 *
 *   - strips from a '<' to the next unquoted '>'
 *
 *   - strips "&{" to the next unquoted '}' or "};"
 *
 *   - strips the character '>'
 *
 *   - strips the following characters: '\'', '"', unless the flag
 *     STRIP_FLAG_UNSAFE_QUOTES is present.
 *
 *   - strips the character '\\' unless the flag STRIP_FLAG_UNSAFE_SLASHES
 *     is present.
 *
 *   - leaves a single space character in place of each
 *     sequence of stripped characters if no other space
 *     preceded the stripped sequence (e.g., "a <b>b" becomes
 *     "a b", but "a<b>b" becomes "a b")
 *
 * @param[in]     in        character array (string)
 * @param[in]     in_len    number of bytes in character array
 * @param[in,out] out       (in)storage for the resulting character array,
 *                          (out)contains the null terminated result
 * @param[in,out] out_len   (in)number of available bytes in out parameter,
 *                          (out)number of bytes used or required in out
 *                          (including NUL)
 * @param[in]     flags     zero or more function-specific flags:
 *                - STRIP_FLAG_LEAVE_WHITESP  - Leave whitespace in place.
 *                - STRIP_FLAG_STRIP_LOW      - Strip all values <= 0x07.
 *                - STRIP_FLAG_STRIP_HIGH     - Strip all values >= 0x80.
 *                - STRIP_FLAG_UNSAFE_QUOTES  - Leave apos and quote in place.
 *                - STRIP_FLAG_UNSAFE_SLASHES - Leave backslashes in place.
 *                - STRIP_FLAG_UNSAFE_SPACES  - Leave spaces in place.
 * @param[in]     charset   NULL or the charset used to treat the input
 *
 * @return
 *    IV_RESULT_OK(0) on success.  Non-zero on failure.
 *    See the complete list above.
 *
 * @verbatim
 * EXAMPLES
 *
 *     Input                                 Output
 *     -----------------------------         -----------------------------
 *     <b>Bob & Mary</b>                     Bob & Mary
 *     "a phrase"                            a phrase
 *     Alice and Bob's house                 Alice and Bob s house
 * @endverbatim
 *
 *
 * DESIGN NOTES
 *
 *     The function assumes ISO-8859-1 encoding.
 *
 *   - the caller is responsible for managing memory; the code
 *     does not allocate memory
 *
 *   - The function requires the input length to be specified
 *     and place the required and/or used length in the
 *     out_len parameter; this allows them to be efficiently
 *     used in environments that store something other than
 *     null terminated strings and also allows you to never
 *     call a function more than once (if the output buffer is
 *     too small on the first call, the value of out_len tells
 *     you how long the buffer needs to be)
 *
 *   - all length parameters specify the length of the
 *     corresponding string in bytes, not characters
 *
 *   - the input buffer does not need to be null-terminated
 *
 *   - the output is always null-terminated (except when it is
 *     not possible -- out==NULL || *out_len==0)
 *
 *   - no context is retained between calls
 *
 *     NOTES
 *
 *       - leading and trailing whitespace is stripped by default;
 *         pass STRIP_FLAG_LEAVE_WHITESP to leave leading and
 *         trailing whitespace in place
 *
 *       - when called with out_len less than the required
 *         length, IV_RESULT_OUTLEN_SMALL is returned with the
 *         required length in out_len and out set to the empty
 *         string
 *
 *       - can be called with out_len == 0 and out == NULL to
 *         compute sufficient storage size, which is returned
 *         in out_len
 *
 *       - when called with out_len == 0, the output is not
 *         NUL-terminated
 */
int get_stripped(const char *in, ssize_t in_len, char *out, int *out_len, unsigned int flags);

#ifdef __cplusplus
}
#endif
