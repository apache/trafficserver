/** @file

  Runtime-dispatched SIMD base64 entry points, implemented in
  ink_base64_dispatch.cc against Google Highway and selected at runtime via
  HWY_DYNAMIC_DISPATCH. Only built and used when ENABLE_HIGHWAY_DISPATCH is on
  (TS_HAS_HIGHWAY_DISPATCH at compile time); ink_base64.cc routes large inputs
  here and falls back to the scalar path otherwise.

  Both functions produce output byte-for-byte identical to the scalar
  encode_scalar / count_alphabet_prefix + decode_scalar in ink_base64_scalar.h.
  decode consumes only fully-validated SIMD blocks and hands the remainder
  (including any truncation at a non-alphabet byte) to the scalar tail.

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

#include <cstddef>
#include <cstdint>

namespace ats::base64
{
// Decode `in_len` bytes of base64 input. Output buffer capacity has already
// been validated by the caller (ats_base64_decode). Writes a trailing NUL at
// out[*out_len]. Supports in-place use (out == reinterpret_cast<uint8_t*>(in)).
void decode_dispatch(const char *in, size_t in_len, unsigned char *out, size_t *out_len);

// Encode `in_len` binary bytes. Output buffer capacity already validated.
// Writes a trailing NUL at out[*out_len].
void encode_dispatch(const unsigned char *in, size_t in_len, char *out, size_t *out_len);

} // namespace ats::base64
