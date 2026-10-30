/** @file

   fuzzing swoc IP address parsing and tscore ink_inet

   @section license License

   Licensed to the Apache Software Foundation (ASF) under one or more contributor license agreements.
   See the NOTICE file distributed with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance with the License.  You may obtain
   a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the
   License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
   either express or implied. See the License for the specific language governing permissions and
   limitations under the License.
*/

#include "swoc/IPEndpoint.h"
#include "swoc/IPRange.h"
#include "swoc/IPSrv.h"
#include "tscore/ink_inet.h"

#include <string>
#include <string_view>
#include <vector>

#define kMinInputLength 1
#define kMaxInputLength 4096

namespace
{
bool
is_space(char c)
{
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

} // namespace

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  if (size < kMinInputLength || size > kMaxInputLength) {
    return 0;
  }

  std::string_view text(reinterpret_cast<char const *>(data), size);

  // Parse every whitespace separated token with the swoc IP text parsers
  // (IPAddr, IPEndpoint, IPMask, IPRange, IPSrv) and tscore's ats_ip_pton /
  // ats_ip_range_parse, then cross exercise the range set bookkeeping.
  swoc::IPRangeSet          ranges;
  std::vector<swoc::IPAddr> addrs;

  size_t pos = 0;
  while (pos < text.size()) {
    while (pos < text.size() && is_space(text[pos])) {
      ++pos;
    }
    size_t start = pos;
    while (pos < text.size() && !is_space(text[pos])) {
      ++pos;
    }
    if (pos == start) {
      break;
    }
    std::string_view token = text.substr(start, pos - start);

    swoc::IPAddr addr;
    if (addr.load(token)) {
      addrs.push_back(addr);
      (void)addr.family();
      (void)addr.is_ip4();
      (void)addr.is_ip6();
      (void)addr.is_valid();
      (void)addr.is_loopback();
      (void)addr.is_multicast();
      (void)addr.is_link_local();
      (void)addr.is_private();
      if (addr.is_ip4()) {
        (void)addr.ip4().is_any();
      } else if (addr.is_ip6()) {
        (void)addr.ip6().is_any();
        (void)addr.ip6().is_mapped_ip4();
      }
      (void)(addr == swoc::IPAddr::INVALID);
    }

    swoc::IPEndpoint endpoint;
    if (endpoint.parse(token)) {
      (void)endpoint.is_valid();
      (void)endpoint.host_order_port();
      (void)endpoint.network_order_port();
      (void)endpoint.is_multicast();
    }

    swoc::IPMask mask;
    if (mask.load(token)) {
      (void)mask.width();
    }

    swoc::IPRange range;
    if (range.load(token)) {
      // An inverted range such as "2.2.2.2-1.1.1.1" loads as an empty range;
      // skip marking those so the set's bookkeeping only sees usable ranges.
      if (!range.empty()) {
        ranges.mark(range);
      }
      swoc::IPAddr rmin = range.min();
      swoc::IPAddr rmax = range.max();
      (void)rmin.family();
      (void)rmax.family();
      (void)range.network_mask();
      (void)range.empty();
    }

    swoc::IPSrv srv;
    if (srv.load(token)) {
      (void)srv.addr();
      (void)srv.host_order_port();
      (void)srv.network_order_port();
      (void)srv.family();
      (void)srv.is_valid();
    }

    // Run the tscore parser on the same text and render the result back into
    // a canonical string, which is parsed once more by swoc.
    IpEndpoint ts_endpoint{};
    if (ats_ip_pton(token, &ts_endpoint.sa) == 0) {
      char        buff[INET6_ADDRSTRLEN] = {0};
      char const *rendered               = ats_ip_ntop(&ts_endpoint.sa, buff, sizeof(buff));
      if (rendered != nullptr) {
        swoc::IPAddr roundtrip(rendered);
        (void)roundtrip.family();
      }
    }

    // Exercise tscore's remap ACL style range parser on the same token.
    IpAddr range_lower;
    IpAddr range_upper;
    (void)ats_ip_range_parse(token, range_lower, range_upper);
  }

  // Tie the parsed addresses to the accumulated range set.
  for (auto const &addr : addrs) {
    (void)ranges.contains(addr);
  }
  (void)ranges.count();
  (void)ranges.empty();

  return 0;
}
