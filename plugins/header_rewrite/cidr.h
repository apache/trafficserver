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

// CIDR masking helpers for %{CIDR:...}, kept header-only for unit testing.
#pragma once

#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstdint>
#include <cstring>

// /0 yields a 0 mask, avoiding the undefined `<< 32`. Out-of-range prefixes
// are clamped into [0, 32] to keep the shift well-defined.
inline in_addr_t
cidr_v4_mask(int prefix)
{
  if (prefix <= 0) {
    return 0;
  }
  if (prefix >= 32) {
    return htonl(UINT32_MAX);
  }
  return htonl(UINT32_MAX << (32 - prefix));
}

// Trailing bytes to clear, plus a high-bit mask for the partial byte (0xff if aligned).
// Out-of-range prefixes are clamped into [0, 128] so the math stays well-defined.
inline void
cidr_v6_params(int prefix, int &zero_bytes, unsigned char &mask)
{
  if (prefix < 0) {
    prefix = 0;
  } else if (prefix > 128) {
    prefix = 128;
  }

  int const rem_bits = prefix % 8;

  zero_bytes = (128 - prefix) / 8;
  mask       = rem_bits ? static_cast<unsigned char>(0xff << (8 - rem_bits)) : 0xff;
}

// Clear the trailing zero_bytes, then keep the high bits of the byte above them.
inline void
cidr_apply_v6(in6_addr &addr, int zero_bytes, unsigned char mask)
{
  if (zero_bytes > 0) {
    memset(&addr.s6_addr[16 - zero_bytes], 0, zero_bytes);
  }
  if (mask != 0xff) {
    addr.s6_addr[16 - zero_bytes - 1] &= mask;
  }
}
