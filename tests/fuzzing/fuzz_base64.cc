/** @file

  fuzzing tscore base64 (ats_base64_encode / ats_base64_decode)

  Treats the fuzz input as untrusted base64 and decodes it, then round-trips
  arbitrary bytes through encode/decode. Every operation is cross-checked
  against the scalar reference primitives, so any divergence of the public
  (SIMD, when ENABLE_HIGHWAY_DISPATCH is on) path from the scalar path aborts.
  Run under AddressSanitizer to catch any out-of-bounds access on the
  untrusted-input decode path.

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

#include <cstddef> // size_t, used by ink_base64.h

#include <tscore/ink_base64.h>

#include "ink_base64_scalar.h" // scalar reference (added to include path by CMake)

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define kMaxInputLength 65536

namespace
{
[[noreturn]] void
fail(const char *what)
{
  std::fprintf(stderr, "base64 fuzz mismatch: %s\n", what);
  std::abort();
}

// Decode `in` two ways and require identical results (length, bytes, NUL).
void
check_decode(const char *in, size_t len)
{
  const size_t cap = ats_base64_decode_dstlen(len);

  std::vector<unsigned char> out_pub(cap + 1, 0xAB);
  size_t                     n_pub = 0;
  if (!ats_base64_decode(in, len, out_pub.data(), out_pub.size(), &n_pub)) {
    fail("decode returned false with sufficient buffer");
  }

  const size_t               valid = ts::base64::count_alphabet_prefix(in, len);
  std::vector<unsigned char> out_ref(cap + 1, 0xCD);
  size_t                     n_ref = 0;
  ts::base64::decode_scalar(in, valid, out_ref.data(), &n_ref);

  if (n_pub != n_ref || std::memcmp(out_pub.data(), out_ref.data(), n_ref) != 0 || out_pub[n_ref] != '\0') {
    fail("decode parity");
  }
}
} // namespace

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  if (size > kMaxInputLength) {
    return 0;
  }

  // 1. Decode the untrusted input directly (any byte values).
  check_decode(reinterpret_cast<const char *>(data), size);

  // 2. Encode the input bytes; cross-check encode, then decode back and
  //    confirm the original bytes are recovered.
  const size_t      ecap = ats_base64_encode_dstlen(size);
  std::vector<char> enc_pub(ecap + 1, 1);
  size_t            ne_pub = 0;
  if (!ats_base64_encode(reinterpret_cast<const char *>(data), size, enc_pub.data(), enc_pub.size(), &ne_pub)) {
    fail("encode returned false with sufficient buffer");
  }

  std::vector<char> enc_ref(ecap + 1, 2);
  size_t            ne_ref = 0;
  ts::base64::encode_scalar(data, size, enc_ref.data(), &ne_ref);
  if (ne_pub != ne_ref || std::memcmp(enc_pub.data(), enc_ref.data(), ne_ref + 1) != 0) {
    fail("encode parity");
  }

  // Encoded output is pure alphabet (+ padding); decoding it must recover the
  // original bytes, and the two decode paths must agree.
  check_decode(enc_pub.data(), ne_pub);

  std::vector<unsigned char> back(ats_base64_decode_dstlen(ne_pub) + 1, 0);
  size_t                     nb = 0;
  ats_base64_decode(enc_pub.data(), ne_pub, back.data(), back.size(), &nb);
  if (nb != size || std::memcmp(back.data(), data, size) != 0) {
    fail("encode/decode round-trip");
  }

  return 0;
}
