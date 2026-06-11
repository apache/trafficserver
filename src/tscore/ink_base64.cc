/** @file

  Base64 encoding and decoding as according to RFC1521.  Similar to uudecode.

  The public entry points (ats_base64_encode / ats_base64_decode, also exposed
  through TSBase64Encode / TSBase64Decode) dispatch between two implementations
  that share the canonical scalar primitives in ink_base64_scalar.h:

    - A hand-rolled scalar path, always present, used directly and for inputs
      below the SIMD crossover threshold. It avoids the SIMD path's runtime
      dispatch overhead, which would otherwise dominate for tiny inputs (e.g.
      the 8-byte SnowflakeID encode).

    - When ENABLE_HIGHWAY_DISPATCH is on (TS_HAS_HIGHWAY_DISPATCH), a SIMD path
      in ink_base64_dispatch.cc built on Google Highway, used for larger
      inputs. It produces output byte-for-byte identical to the scalar path.

  RFC 1521 requires inserting line breaks for long lines.  The basic web
  authentication scheme does not require them.  This implementation is intended
  for web-related use, and line breaks are not implemented.

  Contract preserved by both paths:

    - encode: standard RFC 1521 alphabet (`+`, `/`), `=` padding, no line
      breaks, trailing NUL at outBuffer[length].

    - decode: accepts standard (`+`, `/`) and URL-safe (`-`, `_`) alphabets in
      the same input; tolerates missing padding; on any non-alphabet byte
      (whitespace, `=`, or garbage) truncates and returns success with whatever
      was decoded; trailing NUL at outBuffer[length]; supports in-place decode
      (dst == src).

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
#include "tscore/ink_platform.h"
#include "tscore/ink_config.h"
#include "tscore/ink_base64.h"
#include "tscore/ink_assert.h"

#include "ink_base64_scalar.h"

#if TS_HAS_HIGHWAY_DISPATCH
#include "ink_base64_dispatch.h"

// Inputs at or below these byte counts stay on the scalar path, where they
// outrun the SIMD path's per-call dispatch overhead. The thresholds are
// conservative; both paths are correct at every size. inBufferSize for encode
// is the binary plaintext length; for decode it is the base64-encoded length.
namespace
{
constexpr size_t BASE64_ENCODE_SIMD_THRESHOLD = 24;
constexpr size_t BASE64_DECODE_SIMD_THRESHOLD = 32;
} // namespace
#endif

bool
ats_base64_encode(const unsigned char *inBuffer, size_t inBufferSize, char *outBuffer, size_t outBufSize, size_t *length)
{
  if (outBufSize < ats_base64_encode_dstlen(inBufferSize)) {
    return false;
  }

#if TS_HAS_HIGHWAY_DISPATCH
  if (inBufferSize > BASE64_ENCODE_SIMD_THRESHOLD) {
    ts::base64::encode_dispatch(inBuffer, inBufferSize, outBuffer, length);
    return true;
  }
#endif

  ts::base64::encode_scalar(inBuffer, inBufferSize, outBuffer, length);
  return true;
}

bool
ats_base64_encode(const char *inBuffer, size_t inBufferSize, char *outBuffer, size_t outBufSize, size_t *length)
{
  return ats_base64_encode(reinterpret_cast<const unsigned char *>(inBuffer), inBufferSize, outBuffer, outBufSize, length);
}

bool
ats_base64_decode(const char *inBuffer, size_t inBufferSize, unsigned char *outBuffer, size_t outBufSize, size_t *length)
{
  if (outBufSize < ats_base64_decode_dstlen(inBufferSize)) {
    return false;
  }

#if TS_HAS_HIGHWAY_DISPATCH
  if (inBufferSize > BASE64_DECODE_SIMD_THRESHOLD) {
    // The SIMD path validates inline and truncates at the first non-alphabet
    // byte, so no separate alphabet pre-scan is needed here.
    ts::base64::decode_dispatch(inBuffer, inBufferSize, outBuffer, length);
    return true;
  }
#endif

  // Ignore any trailing `=`s or other undecodable characters, then decode the
  // valid alphabet prefix.
  ts::base64::decode_scalar_prefix(inBuffer, inBufferSize, outBuffer, length);
  return true;
}
