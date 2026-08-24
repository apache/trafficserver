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

#pragma once

#include <cstdint>

/** Decode a Huffman-encoded string per RFC 7541 section 5.2.

    @return The decoded length, or a negative value on invalid input or
      insufficient destination space.

    @note dst_len must be strictly greater than the decoded length; with an
      exactly-sized destination the decoder may report insufficient space.
      Huffman expands to at most 8/5 of the encoded length, so sizing dst at
      2x src_len always suffices (see xpack_decode_string).
 */
int64_t huffman_decode(char *dst, uint32_t dst_len, uint8_t const *src, uint32_t src_len);
int64_t huffman_encode(uint8_t *dst, uint32_t dst_len, uint8_t const *src, uint32_t src_len);
