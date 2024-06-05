/** @ja3_utils.h
  Plugin JA3 Fingerprint calculates JA3 signatures for incoming SSL traffic.
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

#include <string>

namespace ja3
{

/** Encode a buffer of 8bit values.
 *
 * The values will be converted to their decimal string representations and
 * joined with the '-' character.
 *
 * @param buf The buffer to encode. This should be an SSL buffer of 8bit
 *  values.
 * @param len The length of the buffer. If the length is zero, buf will
 *  not be dereferenced.
 * @return The string-encoded ja3 representation of the buffer.
 */
std::string encode_word_buffer(unsigned char const *buf, int const len);

/** Encode a buffer of big-endian 16bit values.
 *
 * The values will be converted to their decimal string representations and
 * joined with the '-' character. Any GREASE values in the buffer will be
 * ignored.
 *
 * @param buf The buffer to encode. This should be a big-endian SSL buffer
 *  of 16bit values.
 * @param len The length of the buffer. If the length is zero, buf will not
 *  be dereferenced.
 * @return The string-encoded ja3 representation of the buffer.
 */
std::string encode_dword_buffer(unsigned char const *buf, int const len);

/** Encode a buffer of integers.
 *
 * The values will be converted to their decimal string representations and
 * joined with the '-' character. Any GREASE values in the buffer will be
 * ignored.
 *
 * @param buf The buffer to encode. The buffer underlying the span should be
 *  an SSL buffer of ints.
 * @param len The length (number of values) in the buffer. If the length is
 *  zero, buf will not be dereferenced.
 * @return The string-encoded ja3 representation of the buffer.
 */
std::string encode_integer_buffer(int const *buf, int const len);

} // end namespace ja3
