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

#include <fstream>

#include "tscore/ink_platform.h"
#include "tscore/ink_defs.h"
#include "tscore/ink_inet.h"
#include "tscore/ParseRules.h"
#include "tscore/CryptoHash.h"
#include "tscore/ink_assert.h"
#include "ts/apidefs.h"
#include "tscpp/util/TextView.h"
#include "tscore/ink_inet.h"

IpAddr const IpAddr::INVALID;

using namespace std::literals;

const std::string_view IP_PROTO_TAG_IPV4("ipv4"sv);
const std::string_view IP_PROTO_TAG_IPV6("ipv6"sv);
const std::string_view IP_PROTO_TAG_UDP("udp"sv);
const std::string_view IP_PROTO_TAG_TCP("tcp"sv);
const std::string_view IP_PROTO_TAG_TLS_1_0("tls/1.0"sv);
const std::string_view IP_PROTO_TAG_TLS_1_1("tls/1.1"sv);
const std::string_view IP_PROTO_TAG_TLS_1_2("tls/1.2"sv);
const std::string_view IP_PROTO_TAG_TLS_1_3("tls/1.3"sv);
const std::string_view IP_PROTO_TAG_HTTP_0_9("http/0.9"sv);
const std::string_view IP_PROTO_TAG_HTTP_1_0("http/1.0"sv);
const std::string_view IP_PROTO_TAG_HTTP_1_1("http/1.1"sv);
const std::string_view IP_PROTO_TAG_HTTP_2_0("h2"sv); // HTTP/2 over TLS

const std::string_view UNIX_PROTO_TAG{"unix"sv};

uint32_t
ink_inet_addr(const char *s)
{
  uint32_t u[4];
  uint8_t *pc   = (uint8_t *)s;
  int n         = 0;
  uint32_t base = 10;

  if (nullptr == s) {
    return htonl((uint32_t)-1);
  }

  while (n < 4) {
    u[n] = 0;
    base = 10;

    // handle hex, octal

    if (*pc == '0') {
      if (*++pc == 'x' || *pc == 'X') {
        base = 16, pc++;
      } else {
        base = 8;
      }
    }
    // handle hex, octal, decimal

    while (*pc) {
      if (ParseRules::is_digit(*pc)) {
        u[n] = u[n] * base + (*pc++ - '0');
        continue;
      }
      if (base == 16 && ParseRules::is_hex(*pc)) {
        u[n] = u[n] * 16 + ParseRules::ink_tolower(*pc++) - 'a' + 10;
        continue;
      }
      break;
    }

    n++;
    if (*pc == '.') {
      pc++;
    } else {
      break;
    }
  }

  if (*pc && !ParseRules::is_wslfcr(*pc)) {
    return htonl((uint32_t)-1);
  }

  switch (n) {
  case 1:
    return htonl(u[0]);
  case 2:
    if (u[0] > 0xff || u[1] > 0xffffff) {
      return htonl((uint32_t)-1);
    }
    return htonl((u[0] << 24) | u[1]);
  case 3:
    if (u[0] > 0xff || u[1] > 0xff || u[2] > 0xffff) {
      return htonl((uint32_t)-1);
    }
    return htonl((u[0] << 24) | (u[1] << 16) | u[2]);
  case 4:
    if (u[0] > 0xff || u[1] > 0xff || u[2] > 0xff || u[3] > 0xff) {
      return htonl((uint32_t)-1);
    }
    return htonl((u[0] << 24) | (u[1] << 16) | (u[2] << 8) | u[3]);
  }
  return htonl((uint32_t)-1);
}

const char *
ats_ip_ntop(const struct sockaddr *addr, char *dst, size_t size)
{
  const char *zret = nullptr;

  switch (addr->sa_family) {
  case AF_INET:
    zret = inet_ntop(AF_INET, &ats_ip4_addr_cast(addr), dst, size);
    break;
  case AF_INET6:
    zret = inet_ntop(AF_INET6, &ats_ip6_addr_cast(addr), dst, size);
    break;
  default:
    zret = dst;
    snprintf(dst, size, "*Not IP address [%u]*", addr->sa_family);
    break;
  }
  return zret;
}

std::string_view
ats_ip_family_name(int family)
{
  switch (family) {
  case AF_INET:
    return IP_PROTO_TAG_IPV4;
  case AF_INET6:
    return IP_PROTO_TAG_IPV6;
  case AF_UNIX:
    return UNIX_PROTO_TAG;
  case AF_UNSPEC:
    return "unspec"sv;
  default:
    return "unknown"sv;
  }
}

const char *
ats_ip_nptop(sockaddr const *addr, char *dst, size_t size)
{
  char buff[INET6_ADDRPORTSTRLEN];
  snprintf(dst, size, "%s:%u", ats_ip_ntop(addr, buff, sizeof(buff)), ats_ip_port_host_order(addr));
  return dst;
}

int
ats_ip_parse(std::string_view str, std::string_view *addr, std::string_view *port, std::string_view *rest)
{
  ts::TextView src(str); /// Easier to work with for parsing.
  // In case the incoming arguments are null, set them here and only check for null once.
  // it doesn't matter if it's all the same, the results will be thrown away.
  std::string_view local;
  if (!addr) {
    addr = &local;
  }
  if (!port) {
    port = &local;
  }
  if (!rest) {
    rest = &local;
  }

  ink_zero(*addr);
  ink_zero(*port);
  ink_zero(*rest);

  // Let's see if we can find out what's in the address string.
  if (src) {
    bool colon_p = false;
    src.ltrim_if(&ParseRules::is_ws);
    // Check for brackets.
    if ('[' == *src) {
      /* Ugly. In a number of places we must use bracket notation
         to support port numbers. Rather than mucking with that
         everywhere, we'll tweak it here. Experimentally we can't
         depend on getaddrinfo to handle it. Note that the text
         buffer size includes space for the nul, so a bracketed
         address is at most that size - 1 + 2 -> size+1.

         It just gets better. In order to bind link local addresses
         the scope_id must be set to the interface index. That's
         most easily done by appending a %intf (where "intf" is the
         name of the interface) to the address. Which makes
         the address potentially larger than the standard maximum.
         So we can't depend on that sizing.
      */
      ++src; // skip bracket.
      *addr = src.take_prefix_at(']');
      if (':' == *src) {
        colon_p = true;
        ++src;
      }
    } else {
      ts::TextView::size_type last = src.rfind(':');
      if (last != ts::TextView::npos && last == src.find(':')) {
        // Exactly one colon - leave post colon stuff in @a src.
        *addr   = src.take_prefix_at(last);
        colon_p = true;
      } else { // presume no port, use everything.
        *addr = src;
        src.clear();
      }
    }
    if (colon_p) {
      ts::TextView tmp{src};
      src.ltrim_if(&ParseRules::is_digit);

      if (tmp.data() == src.data()) {               // no digits at all
        src.assign(tmp.data() - 1, tmp.size() + 1); // back up to include colon
      } else {
        *port = std::string_view(tmp.data(), src.data() - tmp.data());
      }
    }
    *rest = src;
  }
  return addr->empty() ? -1 : 0; // true if we found an address.
}

int
ats_ip_pton(const std::string_view &src, sockaddr *ip)
{
  int zret = -1;
  std::string_view addr, port;

  ats_ip_invalidate(ip);
  if (0 == ats_ip_parse(src, &addr, &port)) {
    // Copy if not terminated.
    if (0 != addr[addr.size() - 1]) {
      char *tmp = static_cast<char *>(alloca(addr.size() + 1));
      memcpy(tmp, addr.data(), addr.size());
      tmp[addr.size()] = 0;
      addr             = std::string_view(tmp, addr.size());
    }
    if (addr.find(':') != std::string_view::npos) { // colon -> IPv6
      in6_addr addr6;
      if (inet_pton(AF_INET6, addr.data(), &addr6)) {
        zret = 0;
        ats_ip6_set(ip, addr6);
      }
    } else { // no colon -> must be IPv4
      in_addr addr4;
      if (inet_aton(addr.data(), &addr4)) {
        zret = 0;
        ats_ip4_set(ip, addr4.s_addr);
      }
    }
    // If we had a successful conversion, set the port.
    if (ats_is_ip(ip)) {
      ats_ip_port_cast(ip) = port.empty() ? 0 : htons(atoi(port.data()));
    }
  }

  return zret;
}

int
ats_ip_range_parse(std::string_view src, IpAddr &lower, IpAddr &upper)
{
  int zret = TS_ERROR;
  IpAddr addr, addr2;
  static const IpAddr ZERO_ADDR4{INADDR_ANY};
  static const IpAddr MAX_ADDR4{INADDR_BROADCAST};
  static const IpAddr ZERO_ADDR6{in6addr_any};
  // I can't find a clean way to static const initialize an IPv6 address to all ones.
  // This is the best I can find that's portable.
  static const uint64_t ones[]{UINT64_MAX, UINT64_MAX};
  static const IpAddr MAX_ADDR6{reinterpret_cast<in6_addr const &>(ones)};

  auto idx = src.find_first_of("/-"sv);
  if (idx != src.npos) {
    if (idx + 1 >= src.size()) { // must have something past the separator or it's bogus.
      zret = TS_ERROR;
    } else if ('/' == src[idx]) {
      if (TS_SUCCESS == addr.load(src.substr(0, idx))) { // load the address
        ts::TextView parsed;
        src.remove_prefix(idx + 1); // drop address and separator.
        int cidr = ts::svtoi(src, &parsed);
        if (parsed.size() && 0 <= cidr) { // a cidr that's a positive integer.
          // Special case the cidr sizes for 0, maximum, and for IPv6 64 bit boundaries.
          if (addr.isIp4()) {
            zret = TS_SUCCESS;
            if (0 == cidr) {
              lower = ZERO_ADDR4;
              upper = MAX_ADDR4;
            } else if (cidr <= 32) {
              lower = upper = addr;
              if (cidr < 32) {
                in_addr_t mask = htonl(INADDR_BROADCAST << (32 - cidr));
                lower._addr._ip4 &= mask;
                upper._addr._ip4 |= ~mask;
              }
            } else {
              zret = TS_ERROR;
            }
          } else if (addr.isIp6()) {
            uint64_t mask;
            zret = TS_SUCCESS;
            if (cidr == 0) {
              lower = ZERO_ADDR6;
              upper = MAX_ADDR6;
            } else if (cidr < 64) { // only upper bytes affected, lower bytes are forced.
              mask          = htobe64(~static_cast<uint64_t>(0) << (64 - cidr));
              lower._family = upper._family = addr._family;
              lower._addr._u64[0]           = addr._addr._u64[0] & mask;
              lower._addr._u64[1]           = 0;
              upper._addr._u64[0]           = addr._addr._u64[0] | ~mask;
              upper._addr._u64[1]           = ~static_cast<uint64_t>(0);
            } else if (cidr == 64) {
              lower._family = upper._family = addr._family;
              lower._addr._u64[0] = upper._addr._u64[0] = addr._addr._u64[0];
              lower._addr._u64[1]                       = 0;
              upper._addr._u64[1]                       = ~static_cast<uint64_t>(0);
            } else if (cidr <= 128) { // lower bytes changed, upper bytes unaffected.
              lower = upper = addr;
              if (cidr < 128) {
                mask = htobe64(~static_cast<uint64_t>(0) << (128 - cidr));
                lower._addr._u64[1] &= mask;
                upper._addr._u64[1] |= ~mask;
              }
            } else {
              zret = TS_ERROR;
            }
          }
        }
      }
    } else if (TS_SUCCESS == addr.load(src.substr(0, idx)) && TS_SUCCESS == addr2.load(src.substr(idx + 1)) &&
               addr.family() == addr2.family()) {
      zret = TS_SUCCESS;
      // not '/' so must be '-'
      lower = addr;
      upper = addr2;
    }
  } else if (TS_SUCCESS == addr.load(src)) {
    zret  = TS_SUCCESS;
    lower = upper = addr;
  }
  return zret;
}

uint32_t
ats_ip_hash(sockaddr const *addr)
{
  if (ats_is_ip4(addr)) {
    return ats_ip4_addr_cast(addr);
  } else if (ats_is_ip6(addr)) {
    CryptoHash hash;
    CryptoContext().hash_immediate(hash, const_cast<uint8_t *>(ats_ip_addr8_cast(addr)), TS_IP6_SIZE);
    return hash.u32[0];
  } else {
    // Bad address type.
    return 0;
  }
}

uint64_t
ats_ip_port_hash(sockaddr const *addr)
{
  if (ats_is_ip4(addr)) {
    return (static_cast<uint64_t>(ats_ip4_addr_cast(addr)) << 16) | (ats_ip_port_cast(addr));
  } else if (ats_is_ip6(addr)) {
    CryptoHash hash;
    CryptoContext hash_context;
    hash_context.update(const_cast<uint8_t *>(ats_ip_addr8_cast(addr)), TS_IP6_SIZE);
    in_port_t port = ats_ip_port_cast(addr);
    hash_context.update((uint8_t *)(&port), sizeof(port));
    hash_context.finalize(hash);
    return hash.u64[0];
  } else {
    // Bad address type.
    return 0;
  }
}

int
ats_ip_to_hex(sockaddr const *src, char *dst, size_t len)
{
  int zret = 0;
  ink_assert(len);
  const char *dst_limit = dst + len - 1; // reserve null space.
  if (ats_is_ip(src)) {
    uint8_t const *data = ats_ip_addr8_cast(src);
    for (uint8_t const *src_limit = data + ats_ip_addr_size(src); data < src_limit && dst + 1 < dst_limit; ++data, zret += 2) {
      uint8_t n1 = (*data >> 4) & 0xF; // high nybble.
      uint8_t n0 = *data & 0xF;        // low nybble.

      *dst++ = n1 > 9 ? n1 + 'A' - 10 : n1 + '0';
      *dst++ = n0 > 9 ? n0 + 'A' - 10 : n0 + '0';
    }
  }
  *dst = 0; // terminate but don't include that in the length.
  return zret;
}

sockaddr *
ats_ip_set(sockaddr *dst, IpAddr const &addr, uint16_t port)
{
  if (AF_INET == addr._family) {
    ats_ip4_set(dst, addr._addr._ip4, port);
  } else if (AF_INET6 == addr._family) {
    ats_ip6_set(dst, addr._addr._ip6, port);
  } else {
    ats_ip_invalidate(dst);
  }
  return dst;
}

int
IpAddr::load(const char *text)
{
  IpEndpoint ip;
  int zret = ats_ip_pton(text, &ip);
  *this    = ip;
  return zret;
}

int
IpAddr::load(std::string_view const &text)
{
  IpEndpoint ip;
  int zret = ats_ip_pton(text, &ip.sa);
  this->assign(&ip.sa);
  return zret;
}

char *
IpAddr::toString(char *dest, size_t len) const
{
  IpEndpoint ip;
  ip.assign(*this);
  ats_ip_ntop(&ip, dest, len);
  return dest;
}

bool
IpAddr::isMulticast() const
{
  return (AF_INET == _family && 0xe == (_addr._byte[0] >> 4)) || (AF_INET6 == _family && IN6_IS_ADDR_MULTICAST(&_addr._ip6));
}

bool
operator==(IpAddr const &lhs, sockaddr const *rhs)
{
  bool zret = false;
  if (lhs._family == rhs->sa_family) {
    if (AF_INET == lhs._family) {
      zret = lhs._addr._ip4 == ats_ip4_addr_cast(rhs);
    } else if (AF_INET6 == lhs._family) {
      zret = 0 == memcmp(&lhs._addr._ip6, &ats_ip6_addr_cast(rhs), sizeof(in6_addr));
    } else { // map all non-IP to the same thing.
      zret = true;
    }
  } // else different families, not equal.
  return zret;
}

/** Compare two IP addresses.
    This is useful for IPv4, IPv6, and the unspecified address type.
    If the addresses are of different types they are ordered

    Non-IP < IPv4 < IPv6

     - all non-IP addresses are the same ( including @c AF_UNSPEC )
     - IPv4 addresses are compared numerically (host order)
     - IPv6 addresses are compared byte wise in network order (MSB to LSB)

    @return
      - -1 if @a lhs is less than @a rhs.
      - 0 if @a lhs is identical to @a rhs.
      - 1 if @a lhs is greater than @a rhs.
*/
int
IpAddr::cmp(self const &that) const
{
  int zret       = 0;
  uint16_t rtype = that._family;
  uint16_t ltype = _family;

  // We lump all non-IP addresses into a single equivalence class
  // that is less than an IP address. This includes AF_UNSPEC.
  if (AF_INET == ltype) {
    if (AF_INET == rtype) {
      in_addr_t la = ntohl(_addr._ip4);
      in_addr_t ra = ntohl(that._addr._ip4);
      if (la < ra) {
        zret = -1;
      } else if (la > ra) {
        zret = 1;
      } else {
        zret = 0;
      }
    } else if (AF_INET6 == rtype) { // IPv4 < IPv6
      zret = -1;
    } else { // IP > not IP
      zret = 1;
    }
  } else if (AF_INET6 == ltype) {
    if (AF_INET6 == rtype) {
      zret = memcmp(&_addr._ip6, &that._addr._ip6, TS_IP6_SIZE);
    } else {
      zret = 1; // IPv6 greater than any other type.
    }
  } else if (AF_INET == rtype || AF_INET6 == rtype) {
    // ltype is non-IP so it's less than either IP type.
    zret = -1;
  } else { // Both types are non-IP so they're equal.
    zret = 0;
  }

  return zret;
}

int
ats_ip_getbestaddrinfo(const char *host, IpEndpoint *ip4, IpEndpoint *ip6)
{
  int zret = -1;
  int port = 0; // port value to assign if we find an address.
  addrinfo ai_hints;
  addrinfo *ai_result;
  std::string_view addr_text, port_text;
  std::string_view src(host, strlen(host) + 1);

  if (ip4) {
    ats_ip_invalidate(ip4);
  }
  if (ip6) {
    ats_ip_invalidate(ip6);
  }

  if (0 == ats_ip_parse(src, &addr_text, &port_text)) {
    // Copy if not terminated.
    if (0 != addr_text[addr_text.size() - 1]) {
      char *tmp = static_cast<char *>(alloca(addr_text.size() + 1));
      memcpy(tmp, addr_text.data(), addr_text.size());
      tmp[addr_text.size()] = 0;
      addr_text             = std::string_view(tmp, addr_text.size());
    }
    ink_zero(ai_hints);
    ai_hints.ai_family = AF_UNSPEC;
    ai_hints.ai_flags  = AI_ADDRCONFIG;
    zret               = getaddrinfo(addr_text.data(), nullptr, &ai_hints, &ai_result);

    if (0 == zret) {
      // Walk the returned addresses and pick the "best".
      enum {
        NA, // Not an (IP) Address.
        LO, // Loopback.
        LL, // Link Local.
        PR, // Private.
        MC, // Multicast.
        GL  // Global.
      } spot_type = NA,
        ip4_type = NA, ip6_type = NA;
      sockaddr const *ip4_src = nullptr;
      sockaddr const *ip6_src = nullptr;

      for (addrinfo *ai_spot = ai_result; ai_spot; ai_spot = ai_spot->ai_next) {
        sockaddr const *ai_ip = ai_spot->ai_addr;
        if (!ats_is_ip(ai_ip)) {
          spot_type = NA;
        } else if (ats_is_ip_loopback(ai_ip)) {
          spot_type = LO;
        } else if (ats_is_ip_linklocal(ai_ip)) {
          spot_type = LL;
        } else if (ats_is_ip_private(ai_ip)) {
          spot_type = PR;
        } else if (ats_is_ip_multicast(ai_ip)) {
          spot_type = MC;
        } else {
          spot_type = GL;
        }

        if (spot_type == NA) {
          continue; // Next!
        }

        if (ats_is_ip4(ai_ip)) {
          if (spot_type > ip4_type) {
            ip4_src  = ai_ip;
            ip4_type = spot_type;
          }
        } else if (ats_is_ip6(ai_ip)) {
          if (spot_type > ip6_type) {
            ip6_src  = ai_ip;
            ip6_type = spot_type;
          }
        }
      }
      if (ip4 && ip4_type > NA) {
        ats_ip_copy(ip4, ip4_src);
      }
      if (ip6 && ip6_type > NA) {
        ats_ip_copy(ip6, ip6_src);
      }
      freeaddrinfo(ai_result); // free *after* the copy.
    }
  }

  // We don't really care if the port is null terminated - the parser
  // would get all the digits so the next character is a non-digit (null or
  // not) and atoi will do the right thing in either case.
  if (port_text.size()) {
    port = htons(atoi(port_text.data()));
  }
  if (ats_is_ip(ip4)) {
    ats_ip_port_cast(ip4) = port;
  }
  if (ats_is_ip(ip6)) {
    ats_ip_port_cast(ip6) = port;
  }

  if (!ats_is_ip(ip4) && !ats_is_ip(ip6)) {
    zret = -1;
  }

  return zret;
}

int
ats_ip_check_characters(std::string_view text)
{
  bool found_colon = false;
  bool found_hex   = false;
  for (char c : text) {
    if (':' == c) {
      found_colon = true;
    } else if ('.' == c || isdigit(c)) { /* empty */
      ;
    } else if (isxdigit(c)) {
      found_hex = true;
    } else {
      return AF_UNSPEC;
    }
  }

  return found_hex && !found_colon ? AF_UNSPEC : found_colon ? AF_INET6 : AF_INET;
}

int
ats_tcp_somaxconn()
{
  int value = 0;

/* Darwin version ... */
#if HAVE_SYSCTLBYNAME
  size_t value_size = sizeof(value);
  if (sysctlbyname("kern.ipc.somaxconn", &value, &value_size, nullptr, 0) < 0) {
    value = 0;
  }
#else
  std::ifstream f("/proc/sys/net/core/somaxconn", std::ifstream::in);
  if (f.good()) {
    f >> value;
  }
#endif

  // Default to the compatible value we used before detection. SOMAXCONN is the right
  // macro to use, but most systems set this to 128, which is just too small.
  if (value <= 0 || value > 65535) {
    value = 1024;
  }

  return value;
}

namespace ts
{
BufferWriter &
bwformat(BufferWriter &w, BWFSpec const &spec, in_addr_t addr)
{
  uint8_t *ptr = reinterpret_cast<uint8_t *>(&addr);
  BWFSpec local_spec{spec}; // Format for address elements.
  bool align_p = false;

  if (spec._ext.size()) {
    if (spec._ext.front() == '=') {
      align_p          = true;
      local_spec._fill = '0';
    } else if (spec._ext.size() > 1 && spec._ext[1] == '=') {
      align_p          = true;
      local_spec._fill = spec._ext[0];
    }
  }

  if (align_p) {
    local_spec._min   = 3;
    local_spec._align = BWFSpec::Align::RIGHT;
  } else {
    local_spec._min = 0;
  }

  bwformat(w, local_spec, ptr[0]);
  w.write('.');
  bwformat(w, local_spec, ptr[1]);
  w.write('.');
  bwformat(w, local_spec, ptr[2]);
  w.write('.');
  bwformat(w, local_spec, ptr[3]);
  return w;
}

BufferWriter &
bwformat(BufferWriter &w, BWFSpec const &spec, in6_addr const &addr)
{
  using QUAD = uint16_t const;
  BWFSpec local_spec{spec}; // Format for address elements.
  uint8_t const *ptr   = addr.s6_addr;
  uint8_t const *limit = ptr + sizeof(addr.s6_addr);
  QUAD *lower          = nullptr; // the best zero range
  QUAD *upper          = nullptr;
  bool align_p         = false;

  if (spec._ext.size()) {
    if (spec._ext.front() == '=') {
      align_p          = true;
      local_spec._fill = '0';
    } else if (spec._ext.size() > 1 && spec._ext[1] == '=') {
      align_p          = true;
      local_spec._fill = spec._ext[0];
    }
  }

  if (align_p) {
    local_spec._min   = 4;
    local_spec._align = BWFSpec::Align::RIGHT;
  } else {
    local_spec._min = 0;
    // do 0 compression if there's no internal fill.
    for (QUAD *spot = reinterpret_cast<QUAD *>(ptr), *last = reinterpret_cast<QUAD *>(limit), *current = nullptr; spot < last;
         ++spot) {
      if (0 == *spot) {
        if (current) {
          // If there's no best, or this is better, remember it.
          if (!lower || (upper - lower < spot - current)) {
            lower = current;
            upper = spot;
          }
        } else {
          current = spot;
        }
      } else {
        current = nullptr;
      }
    }
  }

  if (!local_spec.has_numeric_type()) {
    local_spec._type = 'x';
  }

  for (; ptr < limit; ptr += 2) {
    if (reinterpret_cast<uint8_t const *>(lower) <= ptr && ptr <= reinterpret_cast<uint8_t const *>(upper)) {
      if (ptr == addr.s6_addr) {
        w.write(':'); // only if this is the first quad.
      }
      if (ptr == reinterpret_cast<uint8_t const *>(upper)) {
        w.write(':');
      }
    } else {
      uint16_t f = (ptr[0] << 8) + ptr[1];
      bwformat(w, local_spec, f);
      if (ptr != limit - 2) {
        w.write(':');
      }
    }
  }
  return w;
}

BufferWriter &
bwformat(BufferWriter &w, BWFSpec const &spec, IpAddr const &addr)
{
  BWFSpec local_spec{spec}; // Format for address elements and port.
  bool addr_p{true};
  bool family_p{false};

  if (spec._ext.size()) {
    if (spec._ext.front() == '=') {
      local_spec._ext.remove_prefix(1);
    } else if (spec._ext.size() > 1 && spec._ext[1] == '=') {
      local_spec._ext.remove_prefix(2);
    }
  }
  if (local_spec._ext.size()) {
    addr_p = false;
    for (char c : local_spec._ext) {
      switch (c) {
      case 'a':
      case 'A':
        addr_p = true;
        break;
      case 'f':
      case 'F':
        family_p = true;
        break;
      }
    }
  }

  if (addr_p) {
    if (addr.isIp4()) {
      bwformat(w, spec, addr._addr._ip4);
    } else if (addr.isIp6()) {
      bwformat(w, spec, addr._addr._ip6);
    } else {
      w.print("*Not IP address [{}]*", addr.family());
    }
  }

  if (family_p) {
    local_spec._min = 0;
    if (addr_p) {
      w.write(' ');
    }
    if (spec.has_numeric_type()) {
      bwformat(w, local_spec, static_cast<uintmax_t>(addr.family()));
    } else {
      bwformat(w, local_spec, ats_ip_family_name(addr.family()));
    }
  }
  return w;
}

BufferWriter &
bwformat(BufferWriter &w, BWFSpec const &spec, sockaddr const *addr)
{
  BWFSpec local_spec{spec}; // Format for address elements and port.
  bool port_p{true};
  bool addr_p{true};
  bool family_p{false};
  bool local_numeric_fill_p{false};
  char local_numeric_fill_char{'0'};

  if (spec._type == 'p' || spec._type == 'P') {
    bwformat(w, spec, static_cast<void const *>(addr));
    return w;
  }

  if (spec._ext.size()) {
    if (spec._ext.front() == '=') {
      local_numeric_fill_p = true;
      local_spec._ext.remove_prefix(1);
    } else if (spec._ext.size() > 1 && spec._ext[1] == '=') {
      local_numeric_fill_p    = true;
      local_numeric_fill_char = spec._ext.front();
      local_spec._ext.remove_prefix(2);
    }
  }
  if (local_spec._ext.size()) {
    addr_p = port_p = false;
    for (char c : local_spec._ext) {
      switch (c) {
      case 'a':
      case 'A':
        addr_p = true;
        break;
      case 'p':
      case 'P':
        port_p = true;
        break;
      case 'f':
      case 'F':
        family_p = true;
        break;
      }
    }
  }

  if (addr_p) {
    bool bracket_p = false;
    switch (addr->sa_family) {
    case AF_INET:
      bwformat(w, spec, ats_ip4_addr_cast(addr));
      break;
    case AF_INET6:
      if (port_p) {
        w.write('[');
        bracket_p = true; // take a note - put in the trailing bracket.
      }
      bwformat(w, spec, ats_ip6_addr_cast(addr));
      break;
    default:
      w.print("*Not IP address [{}]*", addr->sa_family);
      break;
    }
    if (bracket_p)
      w.write(']');
    if (port_p)
      w.write(':');
  }
  if (port_p) {
    if (local_numeric_fill_p) {
      local_spec._min   = 5;
      local_spec._fill  = local_numeric_fill_char;
      local_spec._align = BWFSpec::Align::RIGHT;
    } else {
      local_spec._min = 0;
    }
    bwformat(w, local_spec, static_cast<uintmax_t>(ats_ip_port_host_order(addr)));
  }
  if (family_p) {
    local_spec._min = 0;
    if (addr_p || port_p)
      w.write(' ');
    if (spec.has_numeric_type()) {
      bwformat(w, local_spec, static_cast<uintmax_t>(addr->sa_family));
    } else {
      bwformat(w, local_spec, ats_ip_family_name(addr->sa_family));
    }
  }
  return w;
}

namespace bwf
{
  detail::MemDump
  Hex_Dump(IpEndpoint const &addr)
  {
    return detail::MemDump(ats_ip_addr8_cast(&addr), ats_ip_addr_size(&addr));
  }
} // namespace bwf

} // namespace ts
