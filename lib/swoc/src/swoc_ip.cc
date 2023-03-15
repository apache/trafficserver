// SPDX-License-Identifier: Apache-2.0
// Copyright Network Geographics 2014
/** @file

  IP address support.
 */

#include "swoc/swoc_ip.h"
#include "swoc/swoc_meta.h"

using swoc::TextView;
using swoc::svtoi;
using swoc::svtou;
using namespace swoc::literals;

namespace {
// Handle the @c sin_len member, the presence of which varies across compilation environments.
template <typename T>
auto
Set_Sockaddr_Len_Case(T *, swoc::meta::CaseTag<0>) -> decltype(swoc::meta::TypeFunc<void>()) {}

template <typename T>
auto
Set_Sockaddr_Len_Case(T *addr, swoc::meta::CaseTag<1>) -> decltype(T::sin_len, swoc::meta::TypeFunc<void>()) {
  addr->sin_len = sizeof(T);
}

template <typename T>
void
Set_Sockaddr_Len(T *addr) {
  Set_Sockaddr_Len_Case(addr, swoc::meta::CaseArg);
}

} // namespace

namespace swoc { inline namespace SWOC_VERSION_NS {

IPAddr const IPAddr::INVALID;
IP4Addr const IP4Addr::MIN{INADDR_ANY};
IP4Addr const IP4Addr::MAX{INADDR_BROADCAST};
IP6Addr const IP6Addr::MIN{0, 0};
IP6Addr const IP6Addr::MAX{std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::max()};

// The Intel compiler won't let me directly use @c nullptr to construct a reference, so I need
// to fake it out by using this psuedo-nullptr.
void *const detail::pseudo_nullptr = nullptr;

bool
IPEndpoint::assign(sockaddr *dst, sockaddr const *src) {
  size_t n = 0;
  if (dst != src) {
    self_type::invalidate(dst);
    switch (src->sa_family) {
    case AF_INET:
      n = sizeof(sockaddr_in);
      break;
    case AF_INET6:
      n = sizeof(sockaddr_in6);
      break;
    }
    if (n) {
      memcpy(dst, src, n);
    }
  }
  return n != 0;
}

IPEndpoint &
IPEndpoint::assign(IP4Addr const &addr) {
  memset(&sa4, 0, sizeof sa4);
  sa4.sin_family      = AF_INET;
  sa4.sin_addr.s_addr = addr.network_order();
  Set_Sockaddr_Len(&sa4);
  return *this;
}

IPEndpoint &
IPEndpoint::assign(IP6Addr const &addr) {
  memset(&sa6, 0, sizeof sa6);
  sa6.sin6_family = AF_INET6;
  addr.network_order(sa6.sin6_addr);
  Set_Sockaddr_Len(&sa6);
  return *this;
}

IPEndpoint &
IPEndpoint::assign(IPAddr const &src) {
  switch (src.family()) {
  case AF_INET: {
    memset(&sa4, 0, sizeof sa4);
    sa4.sin_family      = AF_INET;
    sa4.sin_addr.s_addr = src.ip4().network_order();
    Set_Sockaddr_Len(&sa4);
  } break;
  case AF_INET6: {
    memset(&sa6, 0, sizeof sa6);
    sa6.sin6_family = AF_INET6;
    sa6.sin6_addr   = src.ip6().network_order();
    Set_Sockaddr_Len(&sa6);
  } break;
  }
  return *this;
}

IPEndpoint &
IPEndpoint::assign(IPSrv const &src) {
  switch (src.family()) {
  case AF_INET:
    memset(&sa4, 0, sizeof sa4);
    sa4.sin_family      = AF_INET;
    sa4.sin_addr.s_addr = src.ip4().addr().network_order();
    sa4.sin_port        = src.network_order_port();
    Set_Sockaddr_Len(&sa4);
    break;
  case AF_INET6:
    memset(&sa6, 0, sizeof sa6);
    sa6.sin6_family = AF_INET6;
    sa6.sin6_addr   = src.ip6().addr().network_order();
    sa6.sin6_port   = src.network_order_port();
    Set_Sockaddr_Len(&sa6);
    break;
  default:
    memset(&sa, 0, sizeof sa);
    sa.sa_family = AF_UNSPEC;
  }
  return *this;
}

bool
IPEndpoint::tokenize(std::string_view str, std::string_view *addr, std::string_view *port, std::string_view *rest) {
  TextView src(str); /// Easier to work with for parsing.
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

  *addr = std::string_view{};
  *port = std::string_view{};
  *rest = std::string_view{};

  // Let's see if we can find out what's in the address string.
  if (src) {
    bool colon_p = false;
    src.ltrim_if(&isspace);
    // Check for brackets.
    if ('[' == *src) {
      ++src; // skip bracket.
      *addr = src.take_prefix_at(']');
      if (':' == *src) {
        colon_p = true;
        ++src;
      }
    } else {
      TextView::size_type last = src.rfind(':');
      if (last != TextView::npos && last == src.find(':')) {
        // Exactly one colon - leave post colon stuff in @a src.
        *addr   = src.take_prefix(last);
        colon_p = true;
      } else { // presume no port, use everything.
        *addr = src;
        src.clear();
      }
    }
    if (colon_p) {
      TextView tmp{src};
      src.ltrim_if(&isdigit);

      if (tmp.data() == src.data()) {               // no digits at all
        src.assign(tmp.data() - 1, tmp.size() + 1); // back up to include colon
      } else {
        *port = std::string_view(tmp.data(), src.data() - tmp.data());
      }
    }
    *rest = src;
  }
  return !addr->empty(); // true if we found an address.
}

bool
IPEndpoint::parse(std::string_view const &str) {
  if (IPSrv srv; srv.load(TextView(str).trim_if(&isspace))) {
    this->assign(srv);
    return true;
  }
  return false;
}

socklen_t
IPEndpoint::size() const {
  switch (sa.sa_family) {
  case AF_INET:
    return sizeof(sockaddr_in);
  case AF_INET6:
    return sizeof(sockaddr_in6);
  default:
    return sizeof(sockaddr);
  }
}

std::string_view
IPEndpoint::family_name(sa_family_t family) {
  switch (family) {
  case AF_INET:
    return "ipv4"_sv;
  case AF_INET6:
    return "ipv6"_sv;
  case AF_UNIX:
    return "unix"_sv;
  case AF_UNSPEC:
    return "unspec"_sv;
  }
  return "unknown"_sv;
}

IPEndpoint &
IPEndpoint::set_to_any(int family) {
  memset(this, 0, sizeof(*this));
  if (AF_INET == family) {
    sa4.sin_family      = family;
    sa4.sin_addr.s_addr = INADDR_ANY;
    Set_Sockaddr_Len(&sa4);
  } else if (AF_INET6 == family) {
    sa6.sin6_family = family;
    sa6.sin6_addr   = in6addr_any;
    Set_Sockaddr_Len(&sa6);
  }
  return *this;
}

bool
IPEndpoint::is_any() const {
  bool zret = false;
  switch (this->family()) {
  case AF_INET:
    zret = sa4.sin_addr.s_addr == INADDR_ANY;
    break;
  case AF_INET6:
    zret = IN6_IS_ADDR_UNSPECIFIED(&sa6.sin6_addr);
    break;
  }
  return zret;
}

IPEndpoint &
IPEndpoint::set_to_loopback(int family) {
  memset(this, 0, sizeof(*this));
  if (AF_INET == family) {
    sa.sa_family        = family;
    sa4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    Set_Sockaddr_Len(&sa4);
  } else if (AF_INET6 == family) {
    sa.sa_family  = family;
    sa6.sin6_addr = in6addr_loopback;
    Set_Sockaddr_Len(&sa6);
  }
  return *this;
}

bool
IPEndpoint::is_loopback() const {
  bool zret = false;
  switch (this->family()) {
  case AF_INET:
    zret = ((ntohl(sa4.sin_addr.s_addr) & IN_CLASSA_NET) >> IN_CLASSA_NSHIFT) == IN_LOOPBACKNET;
    break;
  case AF_INET6:
    zret = IN6_IS_ADDR_LOOPBACK(&sa6.sin6_addr);
    break;
  }
  return zret;
}

bool
IP4Addr::load(std::string_view const &text) {
  TextView src{text};
  int n = SIZE; /// # of octets

  _addr = INADDR_ANY; // clear to zero.

  // empty or trailing dot or empty brackets or unmatched brackets.
  src.trim_if(&isspace);
  if (src.empty() || src.back() == '.' || ('[' == *src && ((++src).empty() || src.back() != ']'))) {
    return false;
  }

  in_addr_t max = std::numeric_limits<in_addr_t>::max();
  while (n > 0) {
    TextView parsed;
    auto token = src.take_prefix_at('.');
    auto v = svtou(token, &parsed);
    if (parsed.size() != token.size()) {
      break;
    }
    if (src.empty()) {
      if (v <= max) {
        _addr += v;
        n = 0; // signal complete.
      }
      break;
    } else if (v <= std::numeric_limits<uint8_t>::max()){
      _addr += v << ( --n * 8);
    } else {
      break; // invalid.
    }
    max >>= 8; // reduce by one octet.
  }

  // If there's text left, or not all the octets were filled, fail.
  if (! src.empty() || n != 0) {
    _addr = INADDR_ANY;
    return false;
  }

  return true;
}

IP4Addr::IP4Addr(sockaddr_in const *sa) : _addr(reorder(sa->sin_addr.s_addr)) {}

auto
IP4Addr::operator=(sockaddr_in const *sa) -> self_type & {
  _addr = reorder(sa->sin_addr.s_addr);
  return *this;
}

sockaddr_in *
IP4Addr::copy_to(sockaddr_in *sin) const {
  sin->sin_family = AF_INET;
  sin->sin_addr.s_addr = this->network_order();
  Set_Sockaddr_Len(sin);
  return sin;
}

// --- IPv6

sockaddr_in6 *
IP6Addr::copy_to(sockaddr_in6 *sin6) const {
  sin6->sin6_family = AF_INET6;
  self_type::reorder(sin6->sin6_addr, _addr._raw);
  Set_Sockaddr_Len(sin6);
  return sin6;
}

int
IP6Addr::cmp(const self_type &that) const {
  return *this < that ? -1 : *this > that ? 1 : 0;
}

IP6Addr &
IP6Addr::operator<<=(unsigned int n) {
  static constexpr auto MASK = ~word_type{0};
  if (n < WORD_WIDTH) {
    _addr._store[MSW] <<= n;
    _addr._store[MSW] |= (_addr._store[LSW] >> (WORD_WIDTH - n)) & ~(MASK << n);
    _addr._store[LSW] <<= n;
  } else {
    n -= WORD_WIDTH;
    _addr._store[MSW] = _addr._store[LSW] << n;
    _addr._store[LSW] = 0;
  }
  return *this;
}

IP6Addr &
IP6Addr::operator>>=(unsigned int n) {
  static constexpr auto MASK = ~word_type{0};
  if (n < WORD_WIDTH) {
    _addr._store[LSW] >>= n;
    _addr._store[LSW] |= (_addr._store[MSW] & ~(MASK << n)) << (WORD_WIDTH - n);
    _addr._store[MSW] >>= n;
  } else {
    n -= WORD_WIDTH;
    _addr._store[LSW] = _addr._store[MSW] >> n;
    _addr._store[MSW] = 0;
  }
  return *this;
}

IP6Addr &
IP6Addr::operator&=(self_type const &that) {
  _addr._store[MSW] &= that._addr._store[MSW];
  _addr._store[LSW] &= that._addr._store[LSW];
  return *this;
}

IP6Addr &
IP6Addr::operator|=(self_type const &that) {
  _addr._store[MSW] |= that._addr._store[MSW];
  _addr._store[LSW] |= that._addr._store[LSW];
  return *this;
}

bool
IP6Addr::load(std::string_view const &str) {
  TextView src{str};
  int n         = 0;
  int empty_idx = -1;
  auto quad     = _addr._quad.data();

  src.trim_if(&isspace);
  if (src && '[' == *src) {
    ++src;
    if (src.empty() || src.back() != ']') {
      return false;
    }
    src.remove_suffix(1);
  }

  if (src.size() < 2) {
    return false;
  }

  // If the first character is ':' then it is an error for it not to be followed by another ':'.
  // Special case the empty address and loopback, otherwise just make a note of the leading '::'.
  if (src[0] == ':') {
    if (src[1] == ':') {
      if (src.size() == 2) {
        this->clear();
        return true;
      } else if (src.size() == 3 && src[2] == '1') {
        _addr._store[0] = 0;
        _addr._store[1] = 1;
        return true;
      } else {
        empty_idx = n;
        src.remove_prefix(2);
      }
    } else {
      return false;
    }
  }

  // Sadly the empty quads can't be done in line because it's not possible to know the correct index
  // of the next present quad until the entire address has been parsed.
  while (n < static_cast<int>(N_QUADS) && !src.empty()) {
    TextView token{src.take_prefix_at(':')};
    if (token.empty()) {
      if (empty_idx >= 0) { // two instances of "::", fail.
        return false;
      }
      empty_idx = n;
    } else {
      TextView r;
      auto x = svtoi(token, &r, 16);
      if (r.size() == token.size()) {
        quad[QUAD_IDX[n++]] = x;
      } else {
        break;
      }
    }
  }

  // Handle empty quads - invalid if empty and still had a full set of quads
  if (empty_idx >= 0) {
    if (n < static_cast<int>(N_QUADS)) {
      int nil_idx = N_QUADS - (n - empty_idx);
      int delta   = N_QUADS - n;
      for (int k = N_QUADS - 1; k >= empty_idx; --k) {
        quad[QUAD_IDX[k]] = (k >= nil_idx ? quad[QUAD_IDX[k - delta]] : 0);
      }
      n = N_QUADS; // Mark success.
    } else {
      return false; // too many quads - full set plus empty.
    }
  }

  if (n == N_QUADS && src.empty()) {
    return true;
  }

  this->clear();
  return false;
}

// These are Intel correct, at some point will need to be architecture dependent.

void
IP6Addr::reorder(in6_addr &dst, raw_type const &src) {
  self_type::reorder(dst.s6_addr, src.data());
  self_type::reorder(dst.s6_addr + WORD_SIZE, src.data() + WORD_SIZE);
}

void
IP6Addr::reorder(raw_type &dst, in6_addr const &src) {
  self_type::reorder(dst.data(), src.s6_addr);
  self_type::reorder(dst.data() + WORD_SIZE, src.s6_addr + WORD_SIZE);
}

IPAddr::IPAddr(IPEndpoint const &addr) {
  this->assign(&addr.sa);
}

IPAddr &
IPAddr::operator=(IPEndpoint const &addr) {
  return this->assign(&addr.sa);
}

sockaddr *
IPAddr::copy_to(sockaddr *sa) {
  if (this->is_ip4()) {
    _addr._ip4.copy_to(sa);
  } else if (this->is_ip6()) {
    _addr._ip6.copy_to(sa);
  }
  return sa;
}

bool
IPAddr::load(const std::string_view &text) {
  TextView src{text};
  src.ltrim_if(&isspace);

  if (TextView::npos != src.prefix(5).find_first_of('.')) {
    _family = AF_INET;
  } else if (TextView::npos != src.prefix(6).find_first_of(':')) {
    _family = AF_INET6;
  } else {
    _family = AF_UNSPEC;
  }

  // Do the real parse now
  switch (_family) {
  case AF_INET:
    if (!_addr._ip4.load(src)) {
      _family = AF_UNSPEC;
    }
    break;
  case AF_INET6:
    if (!_addr._ip6.load(src)) {
      _family = AF_UNSPEC;
    }
    break;
  }
  return this->is_valid();
}

IPAddr &
IPAddr::assign(sockaddr const *addr) {
  if (addr) {
    switch (addr->sa_family) {
    case AF_INET:
      return this->assign(reinterpret_cast<sockaddr_in const *>(addr));
    case AF_INET6:
      return this->assign(reinterpret_cast<sockaddr_in6 const *>(addr));
    default:
      break;
    }
  }
  _family = AF_UNSPEC;
  return *this;
}

bool
IPAddr::operator<(self_type const &that) const {
  if (AF_INET == _family) {
    switch (that._family) {
    case AF_INET:
      return _addr._ip4 < that._addr._ip4;
    case AF_INET6:
      return true;
    default:
      return false;
    }
  } else if (AF_INET6 == _family) {
    switch (that._family) {
    case AF_INET:
      return true;
    case AF_INET6:
      return _addr._ip6 < that._addr._ip6;
    default:
      return false;
    }
  }
  return that.is_valid();
}

int
IPAddr::cmp(self_type const &that) const {
  if (AF_INET == _family) {
    switch (that._family) {
    case AF_INET:
      return _addr._ip4.cmp(that._addr._ip4);
    case AF_INET6:
      return -1;
    default:
      return 1;
    }
  } else if (AF_INET6 == _family) {
    switch (that._family) {
    case AF_INET:
      return 1;
    case AF_INET6:
      return _addr._ip6.cmp(that._addr._ip6);
    default:
      return 1;
    }
  }
  return that.is_valid() ? -1 : 0;
}

IPAddr::self_type &
IPAddr::operator&=(IPMask const &mask) {
  if (_family == AF_INET) {
    _addr._ip4 &= mask;
  } else if (_family == AF_INET6) {
    _addr._ip6 &= mask;
  }
  return *this;
}

IPAddr::self_type &
IPAddr::operator|=(IPMask const &mask) {
  if (_family == AF_INET) {
    _addr._ip4 |= mask;
  } else if (_family == AF_INET6) {
    _addr._ip6 |= mask;
  }
  return *this;
}

bool
IPAddr::is_multicast() const {
  return (AF_INET == _family && _addr._ip4.is_multicast()) || (AF_INET6 == _family && _addr._ip6.is_multicast());
}

IPEndpoint::IPEndpoint(string_view const &text) {
  this->invalidate();
  this->parse(text);
}

bool
IPMask::load(string_view const &text) {
  TextView parsed;
  _cidr = swoc::svtou(text, &parsed);
  if (parsed.size() != text.size()) {
    _cidr = 0;
    return false;
  }
  return true;
}

IPMask
IPMask::mask_for(IPAddr const &addr) {
  if (addr.is_ip4()) {
    return self_type::mask_for(addr.ip4());
  } else if (addr.is_ip6()) {
    return self_type::mask_for(addr.ip6());
  }
  return {};
}

auto
IPMask::mask_for_quad(IP6Addr::quad_type q) -> raw_type {
  raw_type cidr = IP6Addr::QUAD_WIDTH;
  if (q != 0) {
    auto mask = IP6Addr::QUAD_MASK;
    do {
      mask <<= 1;
      --cidr;
    } while ((q | ~mask) == q);
    ++cidr; // loop goes exactly 1 too far.
  }
  return cidr;
}

IPMask
IPMask::mask_for(IP4Addr const &addr) {
  auto n        = addr.host_order();
  raw_type cidr = 0;
  if (auto q = (n & IP6Addr::QUAD_MASK); q != 0) {
    cidr = IP6Addr::QUAD_WIDTH + self_type::mask_for_quad(q);
  } else if (q = ((n >> IP6Addr::QUAD_WIDTH) & IP6Addr::QUAD_MASK); q != 0) {
    cidr = self_type::mask_for_quad(q);
  }
  return self_type(cidr);
}

IPMask
IPMask::mask_for(IP6Addr const &addr) {
  auto cidr = IP6Addr::WIDTH;
  for (unsigned idx = IP6Addr::N_QUADS; idx > 0;) {
    auto q = addr._addr._quad[IP6Addr::QUAD_IDX[--idx]];
    cidr -= IP6Addr::QUAD_WIDTH;
    if (q != 0) {
      cidr += self_type::mask_for_quad(q);
      break;
    }
  }
  return self_type(cidr);
}

IP6Addr
IPMask::as_ip6() const {
  static constexpr auto MASK = ~IP6Addr::word_type{0};
  if (_cidr <= IP6Addr::WORD_WIDTH) {
    return {MASK << (IP6Addr::WORD_WIDTH - _cidr), 0};
  } else if (_cidr < 2 * IP6Addr::WORD_WIDTH) {
    return {MASK, MASK << (2 * IP6Addr::WORD_WIDTH - _cidr)};
  }
  return {MASK, MASK};
}

// --- SRV ---

IP4Srv::IP4Srv(swoc::TextView text) {
  this->load(text);
}

bool
IP4Srv::load(swoc::TextView text) {
  TextView addr_text, port_text, rest;
  if (IPEndpoint::tokenize(text, &addr_text, &port_text, &rest)) {
    if (rest.empty()) {
      uintmax_t n = 0;
      if (!port_text.empty()) {
        n = swoc::svtou(port_text, &rest);
        if (rest.size() != port_text.size() || n > std::numeric_limits<in_port_t>::max()) {
          return false; // bad port.
        }
      }
      IP4Addr addr;
      if (addr.load(addr_text)) {
        this->assign(addr, n);
        return true;
      }
    }
  }
  return false;
}

IP6Srv::IP6Srv(swoc::TextView text) {
  this->load(text);
}

bool
IP6Srv::load(swoc::TextView text) {
  TextView addr_text, port_text, rest;
  if (IPEndpoint::tokenize(text, &addr_text, &port_text, &rest)) {
    if (rest.empty()) {
      uintmax_t n = 0;
      if (!port_text.empty()) {
        n = swoc::svtou(port_text, &rest);
        if (rest.size() != port_text.size() || n > std::numeric_limits<in_port_t>::max()) {
          return false; // bad port.
        }
      }
      IP6Addr addr;
      if (addr.load(addr_text)) {
        this->assign(addr, n);
        return true;
      }
    }
  }
  return false;
}

IPSrv::IPSrv(swoc::TextView text) {
  this->load(text);
}

bool
IPSrv::load(swoc::TextView text) {
  TextView addr_text, port_text, rest;
  if (IPEndpoint::tokenize(text, &addr_text, &port_text, &rest)) {
    if (rest.empty()) {
      uintmax_t n = 0;
      if (!port_text.empty()) {
        n = swoc::svtou(port_text, &rest);
        if (rest.size() != port_text.size() || n > std::numeric_limits<in_port_t>::max()) {
          return false; // bad port.
        }
      }
      IPAddr addr;
      if (addr.load(addr_text)) {
        this->assign(addr, n);
        return true;
      }
    }
  }
  return false;
}


IPSrv::IPSrv(IPAddr addr, in_port_t port) {
  _family = addr.family();
  if (addr.is_ip4()) {
    _srv._ip4.assign(addr.ip4(), port);
  } else if (addr.is_ip6()) {
    _srv._ip6.assign(addr.ip6(), port);
  } else {
    _family = AF_UNSPEC;
  }
}

IPSrv::IPSrv(IPEndpoint const& ep) {
  if (ep.is_ip4()) {
    _family = _srv._ip4.family();
    _srv._ip4.assign(&ep.sa4);
  } else if (ep.is_ip6()) {
    _family = _srv._ip6.family();
    _srv._ip6.assign(&ep.sa6);
  }
}

auto IPSrv::assign(const sockaddr *sa) -> self_type & {
  if (AF_INET == sa->sa_family) {
    _family = AF_INET;
    _srv._ip4.assign(reinterpret_cast<sockaddr_in const *>(sa));
  } else if (AF_INET6 == sa->sa_family) {
    _family = AF_INET6;
    _srv._ip6.assign(reinterpret_cast<sockaddr_in6 const *>(sa));
  }
  return *this;
}

// ++ IPNet ++

bool
IP4Net::load(TextView text) {
  auto idx = text.find('/');
  if (idx != text.npos) {
    if (idx + 1 < text.size()) { // must have something past the separator or it's bogus.
      IP4Addr addr;
      if (addr.load(text.substr(0, idx))) { // load the address
        IPMask mask;
        text.remove_prefix(idx + 1); // drop address and separator.
        if (mask.load(text)) {
          this->assign(addr, mask);
          return true;
        }
      }
    }
  }

  this->clear();
  return false;
}

bool
IP6Net::load(TextView text) {
  auto idx = text.find('/');
  if (idx != text.npos) {
    if (idx + 1 < text.size()) { // must have something past the separator or it's bogus.
      IP6Addr addr;
      if (addr.load(text.substr(0, idx))) { // load the address
        IPMask mask;
        text.remove_prefix(idx + 1); // drop address and separator.
        if (mask.load(text)) {
          this->assign(addr, mask);
          return true;
        }
      }
    }
  }

  this->clear();
  return false;
}

bool
IPNet::load(TextView text) {
  if ( auto mask_text = text.split_suffix_at('/') ; !mask_text.empty() ) {
    IPMask mask;
    if (mask.load(mask_text)) {
      if (IP6Addr a6; a6.load(text)) { // load the address
        this->assign(a6, mask);
        return true;
      } else if (IP4Addr a4; a4.load(text)) {
        this->assign(a4, mask);
        return true;
      }
    }
  }
  this->clear();
  return false;
}

// +++ IP4Range +++

IP4Range::IP4Range(swoc::IP4Addr const &addr, swoc::IPMask const &mask) {
  this->assign(addr, mask);
}

IP4Range &
IP4Range::assign(swoc::IP4Addr const &addr, swoc::IPMask const &mask) {
  // Special case the cidr sizes for 0, maximum
  if (0 == mask.width()) {
    _min = metric_type::MIN;
    _max = metric_type::MAX;
  } else {
    _min = _max = addr;
    if (mask.width() < 32) {
      in_addr_t bits = INADDR_BROADCAST << (32 - mask.width());
      _min._addr &= bits;
      _max._addr |= ~bits;
    }
  }
  return *this;
}

bool
IP4Range::load(string_view text) {
  static const string_view SEPARATORS("/-");
  auto idx = text.find_first_of(SEPARATORS);
  if (idx != text.npos) {
    if (idx + 1 < text.size()) { // must have something past the separator or it's bogus.
      if ('/' == text[idx]) {
        metric_type addr;
        if (addr.load(text.substr(0, idx))) { // load the address
          IPMask mask;
          text.remove_prefix(idx + 1); // drop address and separator.
          if (mask.load(text)) {
            this->assign(addr, mask);
            return true;
          }
        }
      } else if (_min.load(text.substr(0, idx)) && _max.load(text.substr(idx + 1))) {
        return true;
      }
    }
  } else if (_min.load(text)) {
    _max = _min;
    return true;
  }

  this->clear();
  return false;
}

IPMask
IP4Range::network_mask() const {
  NetSource nets{*this};
  if (!nets.empty() && (*nets).as_range() == *this) {
    return nets->mask();
  }
  return {}; // default constructed (invalid) mask.
}

IP4Range::NetSource::NetSource(IP4Range::NetSource::range_type const &range) : _range(range) {
  if (!_range.empty()) {
    this->search_wider();
  }
}

auto
IP4Range::NetSource::operator++() -> self_type & {
  auto upper(IP4Addr{_range._min._addr | ~_mask._addr});
  if (upper >= _range.max()) {
    _range.clear();
  } else {
    _range.assign_min(++upper);
    // @a _range is not empty, because there's at least one address still not covered.
    if (this->is_valid(_mask)) {
      this->search_wider();
    } else {
      this->search_narrower();
    }
  }
  return *this;
}

auto IP4Range::NetSource::operator++(int) -> self_type {
  auto zret { *this };
  ++*this;
  return zret;
}

void
IP4Range::NetSource::search_wider() {
  while (_cidr > 0) {
    auto m = _mask;
    m <<= 1;
    if (this->is_valid(m)) {
      _mask = m;
      --_cidr;
    } else {
      break;
    }
  }
}

void
IP4Range::NetSource::search_narrower() {
  while (!this->is_valid(_mask)) {
    _mask._addr >>= 1;
    _mask._addr |= 1U << (IP4Addr::WIDTH - 1); // put top bit back.
    ++_cidr;
  }
}

// +++ IP6Range +++

IP6Range &
IP6Range::assign(IP6Addr const &addr, IPMask const &mask) {
  static constexpr auto FULL_MASK{std::numeric_limits<uint64_t>::max()};
  auto cidr = mask.width();
  if (cidr == 0) {
    _min = metric_type::MIN;
    _max = metric_type::MAX;
  } else if (cidr < 64) { // only upper bytes affected, lower bytes are forced.
    auto bits            = FULL_MASK << (64 - cidr);
    _min._addr._store[0] = addr._addr._store[0] & bits;
    _min._addr._store[1] = 0;
    _max._addr._store[0] = addr._addr._store[0] | ~bits;
    _max._addr._store[1] = FULL_MASK;
  } else if (cidr == 64) {
    _min._addr._store[0] = _max._addr._store[0] = addr._addr._store[0];
    _min._addr._store[1]                        = 0;
    _max._addr._store[1]                        = FULL_MASK;
  } else if (cidr <= 128) { // _min bytes changed, _max bytes unaffected.
    _min = _max = addr;
    if (cidr < 128) {
      auto bits = FULL_MASK << (128 - cidr);
      _min._addr._store[1] &= bits;
      _max._addr._store[1] |= ~bits;
    }
  }
  return *this;
}

bool
IP6Range::load(std::string_view text) {
  static const string_view SEPARATORS("/-");
  auto idx = text.find_first_of(SEPARATORS);
  if (idx != text.npos) {
    if (idx + 1 < text.size()) { // must have something past the separator or it's bogus.
      if ('/' == text[idx]) {
        metric_type addr;
        if (addr.load(text.substr(0, idx))) { // load the address
          IPMask mask;
          text.remove_prefix(idx + 1); // drop address and separator.
          if (mask.load(text)) {
            this->assign(addr, mask);
            return true;
          }
        }
      } else if (_min.load(text.substr(0, idx)) && _max.load(text.substr(idx + 1))) {
        return true;
      }
    }
  } else if (_min.load(text)) {
    _max = _min;
    return true;
  }
  this->clear();
  return false;
}

IPRange::IPRange(IPAddr const &min, IPAddr const &max) {
  if (min.is_ip4() && max.is_ip4()) {
    _range._ip4.assign(min.ip4(), max.ip4());
    _family = AF_INET;
  } else if (min.is_ip6() && max.is_ip6()) {
    _range._ip6.assign(min.ip6(), max.ip6());
    _family = AF_INET6;
  }
}

bool
IPRange::load(std::string_view const &text) {
  if ( auto idx = text.find_first_of(':') ; idx != text.npos ) {
    if (_range._ip6.load(text)) {
      _family = AF_INET6;
      return true;
    }
  } else if (_range._ip4.load(text)) {
    _family = AF_INET;
    return true;
  }

  return false;
}

IPAddr
IPRange::min() const {
  switch (_family) {
  case AF_INET:
    return _range._ip4.min();
  case AF_INET6:
    return _range._ip6.min();
  default:
    break;
  }
  return {};
}

IPAddr
IPRange::max() const {
  switch (_family) {
  case AF_INET:
    return _range._ip4.max();
  case AF_INET6:
    return _range._ip6.max();
  default:
    break;
  }
  return {};
}

bool
IPRange::empty() const {
  switch (_family) {
  case AF_INET:
    return _range._ip4.empty();
  case AF_INET6:
    return _range._ip6.empty();
  default:
    break;
  }
  return true;
}

IPMask
IPRange::network_mask() const {
  switch (_family) {
  case AF_INET:
    return _range._ip4.network_mask();
  case AF_INET6:
    return _range._ip6.network_mask();
  default:
    break;
  }
  return {};
}

bool
IPRange::operator==(self_type const &that) const {
  if (_family != that._family) {
    return false;
  }
  if (this->is_ip4()) {
    return _range._ip4 == that._range._ip4;
  }
  if (this->is_ip6()) {
    return _range._ip6 == that._range._ip6;
  }
  return true;
}

IPMask
IP6Range::network_mask() const {
  NetSource nets{*this};
  if (!nets.empty() && (*nets).as_range() == *this) {
    return nets->mask();
  }
  return {}; // default constructed (invalid) mask.
}

IP6Range::NetSource::NetSource(IP6Range::NetSource::range_type const &range) : _range(range) {
  if (!_range.empty()) {
    this->search_wider();
  }
}

auto
IP6Range::NetSource::operator++() -> self_type & {
  auto upper = _range.min() | _mask;
  if (upper >= _range.max()) {
    _range.clear();
  } else {
    _range.assign_min(++upper);
    // @a _range is not empty, because there's at least one address still not covered.
    if (this->is_valid(_mask)) {
      this->search_wider();
    } else {
      this->search_narrower();
    }
  }
  return *this;
}

void
IP6Range::NetSource::search_wider() {
  while (_mask.width() > 0) {
    auto m = _mask;
    m <<= 1;
    if (this->is_valid(m)) {
      _mask = m;
    } else {
      break;
    }
  }
}

void
IP6Range::NetSource::search_narrower() {
  while (!this->is_valid(_mask)) {
    _mask >>= 1;
  }
}

}} // namespace swoc::SWOC_VERSION_NS
