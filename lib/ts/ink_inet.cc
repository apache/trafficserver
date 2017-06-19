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

#include "ts/ink_platform.h"
#include "ts/ink_defs.h"
#include "ts/ink_inet.h"
#include "ts/ParseRules.h"
#include "ts/ink_code.h"
#include "ts/ink_assert.h"
#include "ts/TestBox.h"
#include "ts/TextBuffer.h"

IpAddr const IpAddr::INVALID;

const ts::StringView IP_PROTO_TAG_IPV4("ipv4", ts::StringView::literal);
const ts::StringView IP_PROTO_TAG_IPV6("ipv6", ts::StringView::literal);
const ts::StringView IP_PROTO_TAG_UDP("udp", ts::StringView::literal);
const ts::StringView IP_PROTO_TAG_TCP("tcp", ts::StringView::literal);
const ts::StringView IP_PROTO_TAG_TLS_1_0("tls/1.0", ts::StringView::literal);
const ts::StringView IP_PROTO_TAG_TLS_1_1("tls/1.1", ts::StringView::literal);
const ts::StringView IP_PROTO_TAG_TLS_1_2("tls/1.2", ts::StringView::literal);
const ts::StringView IP_PROTO_TAG_TLS_1_3("tls/1.3", ts::StringView::literal);
const ts::StringView IP_PROTO_TAG_HTTP_0_9("http/0.9", ts::StringView::literal);
const ts::StringView IP_PROTO_TAG_HTTP_1_0("http/1.0", ts::StringView::literal);
const ts::StringView IP_PROTO_TAG_HTTP_1_1("http/1.1", ts::StringView::literal);
const ts::StringView IP_PROTO_TAG_HTTP_2_0("h2", ts::StringView::literal); // HTTP/2 over TLS

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

ts::StringView
ats_ip_family_name(int family)
{
  static const ts::StringView UNSPEC("Unspec", ts::StringView::literal);
  return AF_INET == family ? IP_PROTO_TAG_IPV4 : AF_INET6 == family ? IP_PROTO_TAG_IPV6 : UNSPEC;
}

const char *
ats_ip_nptop(sockaddr const *addr, char *dst, size_t size)
{
  char buff[INET6_ADDRPORTSTRLEN];
  snprintf(dst, size, "%s:%u", ats_ip_ntop(addr, buff, sizeof(buff)), ats_ip_port_host_order(addr));
  return dst;
}

int
ats_ip_parse(ts::ConstBuffer src, ts::ConstBuffer *addr, ts::ConstBuffer *port, ts::ConstBuffer *rest)
{
  // In case the incoming arguments are null.
  ts::ConstBuffer localAddr, localPort;
  if (!addr) {
    addr = &localAddr;
  }
  if (!port) {
    port = &localPort;
  }
  addr->reset();
  port->reset();
  if (rest) {
    rest->reset();
  }

  // Let's see if we can find out what's in the address string.
  if (src) {
    bool colon_p = false;
    while (src && ParseRules::is_ws(*src)) {
      ++src;
    }
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
      *addr = src.splitOn(']');
      if (':' == *src) {
        colon_p = true;
        ++src;
      }
    } else {
      ts::ConstBuffer post = src.after(':');
      if (post.data() && !post.find(':')) {
        *addr   = src.splitOn(post.data() - 1);
        colon_p = true;
      } else { // presume no port, use everything.
        *addr = src;
        src.reset();
      }
    }
    if (colon_p) {
      ts::ConstBuffer tmp(src);
      while (src && ParseRules::is_digit(*src)) {
        ++src;
      }

      if (tmp.data() == src.data()) {            // no digits at all
        src.set(tmp.data() - 1, tmp.size() + 1); // back up to include colon
      } else {
        *port = tmp.clip(src.data());
      }
    }
    if (rest) {
      *rest = src;
    }
  }
  return *addr ? 0 : -1; // true if we found an address.
}

int
ats_ip_pton(const ts::ConstBuffer &src, sockaddr *ip)
{
  int zret = -1;
  ts::ConstBuffer addr, port;

  ats_ip_invalidate(ip);
  if (0 == ats_ip_parse(src, &addr, &port)) {
    // Copy if not terminated.
    if (0 != addr[addr.size() - 1]) {
      char *tmp = static_cast<char *>(alloca(addr.size() + 1));
      memcpy(tmp, addr.data(), addr.size());
      tmp[addr.size()] = 0;
      addr.set(tmp, addr.size());
    }
    if (addr.find(':')) { // colon -> IPv6
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
      ats_ip_port_cast(ip) = port ? htons(atoi(port.data())) : 0;
    }
  }

  return zret;
}

uint32_t
ats_ip_hash(sockaddr const *addr)
{
  union md5sum {
    unsigned char c[16];
    uint32_t i;
  } zret;
  zret.i = 0;

  if (ats_is_ip4(addr)) {
    zret.i = ats_ip4_addr_cast(addr);
  } else if (ats_is_ip6(addr)) {
    ink_code_md5(const_cast<uint8_t *>(ats_ip_addr8_cast(addr)), TS_IP6_SIZE, zret.c);
  }
  return zret.i;
}

uint64_t
ats_ip_port_hash(sockaddr const *addr)
{
  union md5sum {
    uint64_t i;
    uint16_t b[4];
    unsigned char c[16];
  } zret;

  zret.i = 0;
  if (ats_is_ip4(addr)) {
    zret.i = (static_cast<uint64_t>(ats_ip4_addr_cast(addr)) << 16) | (ats_ip_port_cast(addr));
  } else if (ats_is_ip6(addr)) {
    ink_code_md5(const_cast<uint8_t *>(ats_ip_addr8_cast(addr)), TS_IP6_SIZE, zret.c);
    // now replace the bottom 16bits so we can account for the port.
    zret.b[3] = ats_ip_port_cast(addr);
  }
  return zret.i;
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
IpAddr::load(ts::ConstBuffer const &text)
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
  ts::ConstBuffer addr_text, port_text;
  ts::ConstBuffer src(host, strlen(host) + 1);

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
      addr_text.set(tmp, addr_text.size());
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
      if (ip4_type > NA) {
        ats_ip_copy(ip4, ip4_src);
      }
      if (ip6_type > NA) {
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
ats_ip_check_characters(ts::ConstBuffer text)
{
  bool found_colon = false;
  bool found_hex   = false;
  for (const char *p = text.data(), *limit = p + text.size(); p < limit; ++p) {
    if (':' == *p) {
      found_colon = true;
    } else if ('.' == *p || isdigit(*p)) { /* empty */
      ;
    } else if (isxdigit(*p)) {
      found_hex = true;
    } else {
      return AF_UNSPEC;
    }
  }

  return found_hex && !found_colon ? AF_UNSPEC : found_colon ? AF_INET6 : AF_INET;
}

// Need to declare this type globally so gcc 4.4 can use it in the countof() template ...
struct ip_parse_spec {
  const char *hostspec;
  const char *host;
  const char *port;
  const char *rest;
};

REGRESSION_TEST(Ink_Inet)(RegressionTest *t, int /* atype */, int *pstatus)
{
  TestBox box(t, pstatus);
  IpEndpoint ep;
  IpAddr addr;

  box = REGRESSION_TEST_PASSED;

  // Test ats_ip_parse() ...
  {
    struct ip_parse_spec names[] = {
      {"::", "::", nullptr, nullptr},
      {"[::1]:99", "::1", "99", nullptr},
      {"127.0.0.1:8080", "127.0.0.1", "8080", nullptr},
      {"127.0.0.1:8080-Bob", "127.0.0.1", "8080", "-Bob"},
      {"127.0.0.1:", "127.0.0.1", nullptr, ":"},
      {"foo.example.com", "foo.example.com", nullptr, nullptr},
      {"foo.example.com:99", "foo.example.com", "99", nullptr},
      {"ffee::24c3:3349:3cee:0143", "ffee::24c3:3349:3cee:0143", nullptr, nullptr},
      {"fe80:88b5:4a:20c:29ff:feae:1c33:8080", "fe80:88b5:4a:20c:29ff:feae:1c33:8080", nullptr, nullptr},
      {"[ffee::24c3:3349:3cee:0143]", "ffee::24c3:3349:3cee:0143", nullptr, nullptr},
      {"[ffee::24c3:3349:3cee:0143]:80", "ffee::24c3:3349:3cee:0143", "80", nullptr},
      {"[ffee::24c3:3349:3cee:0143]:8080x", "ffee::24c3:3349:3cee:0143", "8080", "x"},
    };

    for (unsigned i = 0; i < countof(names); ++i) {
      ip_parse_spec const &s = names[i];
      ts::ConstBuffer host, port, rest;
      size_t len;

      box.check(ats_ip_parse(ts::ConstBuffer(s.hostspec, strlen(s.hostspec)), &host, &port, &rest) == 0, "ats_ip_parse(%s)",
                s.hostspec);
      len = strlen(s.host);
      box.check(len == host.size() && strncmp(host.data(), s.host, host.size()) == 0, "ats_ip_parse(%s) gave addr '%.*s'",
                s.hostspec, static_cast<int>(host.size()), host.data());
      if (s.port) {
        len = strlen(s.port);
        box.check(len == port.size() && strncmp(port.data(), s.port, port.size()) == 0, "ats_ip_parse(%s) gave port '%.*s'",
                  s.hostspec, static_cast<int>(port.size()), port.data());
      } else {
        box.check(port.size() == 0, "ats_ip_parse(%s) gave port '%.*s' instead of empty", s.hostspec, static_cast<int>(port.size()),
                  port.data());
      }

      if (s.rest) {
        len = strlen(s.rest);
        box.check(len == rest.size() && strncmp(rest.data(), s.rest, len) == 0, "ats_ip_parse(%s) gave rest '%.*s' instead of '%s'",
                  s.hostspec, static_cast<int>(rest.size()), rest.data(), s.rest);
      } else {
        box.check(rest.size() == 0, "ats_ip_parse(%s) gave rest '%.*s' instead of empty", s.hostspec, static_cast<int>(rest.size()),
                  rest.data());
      }
    }
  }

  // Test ats_ip_pton() ...
  {
    box.check(ats_ip_pton("76.14.64.156", &ep.sa) == 0, "ats_ip_pton()");
    box.check(addr.load("76.14.64.156") == 0, "IpAddr::load()");
    box.check(addr.family() == ep.family(), "mismatched address family");

    switch (addr.family()) {
    case AF_INET:
      box.check(ep.sin.sin_addr.s_addr == addr._addr._ip4, "IPv4 address mismatch");
      break;
    case AF_INET6:
      box.check(memcmp(&ep.sin6.sin6_addr, &addr._addr._ip6, sizeof(in6_addr)) == 0, "IPv6 address mismatch");
      break;
    default:;
    }
  }
}

int
ats_tcp_somaxconn()
{
  int fd;
  int value = 0;

/* Darwin version ... */
#if HAVE_SYSCTLBYNAME
  size_t value_size = sizeof(value);
  if (sysctlbyname("kern.ipc.somaxconn", &value, &value_size, nullptr, 0) == 0) {
    return value;
  }
#endif

  fd = open("/proc/sys/net/ipv4/tcp_max_syn_backlog", O_RDONLY);
  if (fd != -1) {
    TextBuffer text(0);
    text.slurp(fd);
    if (!text.empty()) {
      value = strtoul(text.bufPtr(), nullptr, 10);
    }
    close(fd);
  }

  // Default to the compatible value we used before detection. SOMAXCONN is the right
  // macro to use, but most systems set this to 128, which is just too small.
  if (value <= 0) {
    return 1024;
  }

  return value;
}
