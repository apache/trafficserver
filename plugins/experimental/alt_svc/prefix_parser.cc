/**
  @file
  @brief Interprets an IP address and prefix as a CIDR IP interval.

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

#include "prefix_parser.h"

void
prefix_bit_ops(sockaddr_storage *addr, unsigned int prefix_length, bool set)
{
  if (addr->ss_family == AF_INET) {
    sockaddr_in *as_v4 = (sockaddr_in *)addr;
    // We have a check here to avoid the undefined behavior of shifting an int 32 times.
    in_addr_t prefix       = prefix_length == 0 ? 0x0 : 0xFFFFFFFF >> (32 - prefix_length);
    as_v4->sin_addr.s_addr = htonl(set ? (ntohl(as_v4->sin_addr.s_addr) | prefix) : (ntohl(as_v4->sin_addr.s_addr) & ~prefix));
  } else if (addr->ss_family == AF_INET6) {
    sockaddr_in6 *as_v6 = (sockaddr_in6 *)addr;
    if (prefix_length % 8 != 0) {
      // modify first byte
      uint8_t prefix                = ((uint8_t)(1 << (prefix_length % 8))) + ((uint8_t)-1);
      int idx                       = 15 - (prefix_length) / 8;
      as_v6->sin6_addr.s6_addr[idx] = set ? (as_v6->sin6_addr.s6_addr[idx] | prefix) : (as_v6->sin6_addr.s6_addr[idx] & ~prefix);
    }
    for (int i = 16 - (prefix_length) / 8; i < 16; i++) {
      as_v6->sin6_addr.s6_addr[i] = set ? 0xFF : 0x00;
    }
  }
}

inline void
unset_prefix_bits(sockaddr_storage *lower, unsigned int prefix_length)
{
  prefix_bit_ops(lower, prefix_length, false);
}

inline void
set_prefix_bits(sockaddr_storage *upper, unsigned int prefix_length)
{
  prefix_bit_ops(upper, prefix_length, true);
}

PrefixParseError
parse_addresses(const char *prefixedAddress, int prefix_num, sockaddr_storage *lower, sockaddr_storage *upper)
{
  // Step 1: verify the prefixedAddress is correct, otherwise output an invalid error.
  if (prefix_num < 0 || prefix_num > 128) {
    TSError("Bad IP prefix when parsing: %s/%d", prefixedAddress, prefix_num);
    return PrefixParseError::bad_prefix;
  }
  // Step 2: convert ip address string to ip address
  ats_ip_pton(prefixedAddress, (sockaddr *)lower);

  if (lower->ss_family != AF_INET && lower->ss_family != AF_INET6) {
    TSError("Bad IP address when parsing: %s/%d", prefixedAddress, prefix_num);
    return PrefixParseError::bad_ip;
  }

  if (lower->ss_family == AF_INET && prefix_num > 32) {
    TSError("Bad IP prefix when parsing: %s/%d", prefixedAddress, prefix_num);
    return PrefixParseError::bad_prefix;
  }
  // Step 3: parse out the prefix and the remaining ip address string
  int prefix_bits = (lower->ss_family == AF_INET ? 32 : 128) - prefix_num;
  // Step 4: unset the prefix bits for lower, clone as upper, set the prefix bits for upper
  unset_prefix_bits(lower, prefix_bits);
  memcpy(upper, lower, sizeof(sockaddr_storage));
  set_prefix_bits(upper, prefix_bits);
  return PrefixParseError::ok;
}
