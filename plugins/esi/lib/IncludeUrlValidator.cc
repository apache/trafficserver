/** @file

  Validator for esi:include src URLs to mitigate SSRF.

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

#include "IncludeUrlValidator.h"

#include <arpa/inet.h>
#include <cctype>
#include <cstring>
#include <netinet/in.h>
#include <string>

using std::string_view;

namespace EsiLib
{
namespace
{
  // Backtracking limit for the allowlist match. PCRE2 stops and reports
  // PCRE2_ERROR_MATCHLIMIT once this many match steps are taken, bounding
  // worst-case CPU per validation against attacker-influenced hostnames.
  // Matches the value used by the regex_remap plugin.
  constexpr uint32_t ALLOW_REGEX_MATCH_LIMIT = 1750;

  bool
  iequals(string_view a, string_view b)
  {
    if (a.size() != b.size()) {
      return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
        return false;
      }
    }
    return true;
  }

  bool
  iendswith(string_view s, string_view suffix)
  {
    if (s.size() < suffix.size()) {
      return false;
    }
    return iequals(s.substr(s.size() - suffix.size()), suffix);
  }

  // RFC 1918, loopback, link-local, unspecified, broadcast, and CGNAT.
  bool
  ipv4IsPrivate(const in_addr &a)
  {
    uint32_t h = ntohl(a.s_addr);

    // 0.0.0.0/8 (unspecified / "this network")
    if ((h & 0xFF000000u) == 0x00000000u) {
      return true;
    }
    // 10.0.0.0/8
    if ((h & 0xFF000000u) == 0x0A000000u) {
      return true;
    }
    // 100.64.0.0/10 (CGNAT)
    if ((h & 0xFFC00000u) == 0x64400000u) {
      return true;
    }
    // 127.0.0.0/8
    if ((h & 0xFF000000u) == 0x7F000000u) {
      return true;
    }
    // 169.254.0.0/16 (link-local, includes cloud metadata 169.254.169.254)
    if ((h & 0xFFFF0000u) == 0xA9FE0000u) {
      return true;
    }
    // 172.16.0.0/12
    if ((h & 0xFFF00000u) == 0xAC100000u) {
      return true;
    }
    // 192.0.0.0/24 (IETF protocol assignments) and 192.0.2.0/24 (TEST-NET-1)
    if ((h & 0xFFFFFF00u) == 0xC0000000u || (h & 0xFFFFFF00u) == 0xC0000200u) {
      return true;
    }
    // 192.168.0.0/16
    if ((h & 0xFFFF0000u) == 0xC0A80000u) {
      return true;
    }
    // 198.18.0.0/15 (benchmarking)
    if ((h & 0xFFFE0000u) == 0xC6120000u) {
      return true;
    }
    // 198.51.100.0/24 (TEST-NET-2), 203.0.113.0/24 (TEST-NET-3)
    if ((h & 0xFFFFFF00u) == 0xC6336400u || (h & 0xFFFFFF00u) == 0xCB007100u) {
      return true;
    }
    // 224.0.0.0/4 (multicast), 240.0.0.0/4 (reserved), 255.255.255.255
    if ((h & 0xF0000000u) == 0xE0000000u || (h & 0xF0000000u) == 0xF0000000u) {
      return true;
    }
    return false;
  }

  bool
  ipv6IsPrivate(const in6_addr &a)
  {
    // ::/128 unspecified, ::1/128 loopback
    bool all_zero = true;
    for (int i = 0; i < 15; ++i) {
      if (a.s6_addr[i] != 0) {
        all_zero = false;
        break;
      }
    }
    if (all_zero && (a.s6_addr[15] == 0 || a.s6_addr[15] == 1)) {
      return true;
    }
    // fe80::/10 link-local
    if (a.s6_addr[0] == 0xfe && (a.s6_addr[1] & 0xc0) == 0x80) {
      return true;
    }
    // fc00::/7 unique local
    if ((a.s6_addr[0] & 0xfe) == 0xfc) {
      return true;
    }
    // ff00::/8 multicast
    if (a.s6_addr[0] == 0xff) {
      return true;
    }
    // ::ffff:0:0/96 IPv4-mapped — fall through to IPv4 check
    bool is_v4_mapped = true;
    for (int i = 0; i < 10; ++i) {
      if (a.s6_addr[i] != 0) {
        is_v4_mapped = false;
        break;
      }
    }
    if (is_v4_mapped && a.s6_addr[10] == 0xff && a.s6_addr[11] == 0xff) {
      in_addr v4;
      std::memcpy(&v4.s_addr, &a.s6_addr[12], 4);
      return ipv4IsPrivate(v4);
    }
    // 64:ff9b::/96 NAT64 — the last 32 bits are an embedded IPv4. Recurse
    // through ipv4IsPrivate so private IPv4 destinations (e.g.
    // 64:ff9b::0a00:0001 → 10.0.0.1) cannot bypass the denylist in
    // NAT64-enabled environments.
    if (a.s6_addr[0] == 0x00 && a.s6_addr[1] == 0x64 && a.s6_addr[2] == 0xff && a.s6_addr[3] == 0x9b) {
      bool nat64_zero_middle = true;
      for (int i = 4; i < 12; ++i) {
        if (a.s6_addr[i] != 0) {
          nat64_zero_middle = false;
          break;
        }
      }
      if (nat64_zero_middle) {
        in_addr v4;
        std::memcpy(&v4.s_addr, &a.s6_addr[12], 4);
        return ipv4IsPrivate(v4);
      }
    }
    return false;
  }

  // Non-canonical numeric IPv4 forms — decimal ("2130706433"), octal
  // ("017700000001"), hex ("0x7f000001"), and shortcut forms like "127.1" —
  // are accepted by inet_aton but not inet_pton. They have no legitimate use
  // in an include URL and are a standard SSRF-filter evasion. They must be
  // rejected even when --allow-private-include-hosts is set, since that flag
  // only relaxes the private-range denylist, not these evasion forms.
  // Strips any zone id and trailing FQDN-root dot before checking, matching
  // the normalization in isPrivateHost.
  bool
  isNonCanonicalNumericIPv4(string_view host)
  {
    if (auto pct = host.find('%'); pct != string_view::npos) {
      host = host.substr(0, pct);
    }
    if (!host.empty() && host.back() == '.') {
      host.remove_suffix(1);
    }

    std::string h(host);
    in_addr v4{};
    if (inet_pton(AF_INET, h.c_str(), &v4) == 1) {
      return false; // canonical dotted-quad, handled by the normal denylist
    }
    return inet_aton(h.c_str(), &v4) != 0;
  }

} // namespace

bool
IncludeUrlValidator::splitUrl(string_view url, string_view &scheme, string_view &host)
{
  auto sep = url.find("://");
  if (sep == string_view::npos || sep == 0) {
    return false;
  }
  scheme = url.substr(0, sep);

  string_view rest = url.substr(sep + 3);
  if (rest.empty()) {
    return false;
  }

  // Strip userinfo "user:pass@". Be careful: '@' may appear in path; only look
  // before the authority terminator.
  auto authority_end    = rest.find_first_of("/?#");
  string_view authority = (authority_end == string_view::npos) ? rest : rest.substr(0, authority_end);

  auto at = authority.rfind('@');
  if (at != string_view::npos) {
    authority.remove_prefix(at + 1);
  }

  if (authority.empty()) {
    return false;
  }

  // IPv6 literal in brackets: [....] optionally followed by ":port".
  if (authority.front() == '[') {
    auto rb = authority.find(']');
    if (rb == string_view::npos) {
      return false;
    }
    // The closing ']' must terminate the authority or be immediately followed
    // by the port separator ':'. Anything else (e.g. "[::1]evil.com") is
    // malformed; reject it rather than silently extracting "::1" as the host
    // while the real URL points elsewhere.
    if (rb + 1 != authority.size() && authority[rb + 1] != ':') {
      return false;
    }
    host = authority.substr(1, rb - 1);
  } else {
    auto colon = authority.find(':');
    host       = (colon == string_view::npos) ? authority : authority.substr(0, colon);
  }
  return !host.empty();
}

bool
IncludeUrlValidator::isPrivateHost(string_view host)
{
  // RFC 6874 IPv6 scope id: "fe80::1%eth0" (or URL-encoded
  // "fe80::1%25eth0" after splitUrl strips the brackets). inet_pton
  // rejects the '%', so strip from there before parsing — and treat
  // the very presence of a zone id as private, because scope ids only
  // make sense for link-local / non-global addresses.
  bool had_zone = false;
  if (auto pct = host.find('%'); pct != string_view::npos) {
    had_zone = true;
    host     = host.substr(0, pct);
  }
  // Strip a single trailing FQDN-root dot: "localhost." resolves the
  // same as "localhost"; without this, the hostname checks below would
  // be trivially bypassed.
  if (!host.empty() && host.back() == '.') {
    host.remove_suffix(1);
  }

  std::string h(host);
  in_addr v4{};
  if (inet_pton(AF_INET, h.c_str(), &v4) == 1) {
    return ipv4IsPrivate(v4);
  }
  // Non-canonical IPv4 numeric forms — decimal integer ("2130706433"),
  // octal ("017700000001"), hex ("0x7f000001"), and shortcut forms
  // like "127.1" — are accepted by inet_aton but not by inet_pton.
  // They have no legitimate use in ESI include URLs and are routinely
  // used to evade SSRF filters that only canonicalize the dotted-quad
  // form. Reject outright instead of trying to apply ipv4IsPrivate to
  // the parsed value.
  if (inet_aton(h.c_str(), &v4) != 0) {
    return true;
  }
  in6_addr v6{};
  if (inet_pton(AF_INET6, h.c_str(), &v6) == 1) {
    return had_zone || ipv6IsPrivate(v6);
  }
  // Had a zone id but didn't parse as IPv6 — fail closed.
  if (had_zone) {
    return true;
  }
  // Non-IP hostname: deny obvious loopback aliases. Real hostnames that
  // resolve to private space must be caught by the optional allowlist or
  // upstream remap policy; we don't do DNS here.
  if (iequals(host, "localhost") || iendswith(host, ".localhost")) {
    return true;
  }
  return false;
}

bool
IncludeUrlValidator::setHostAllowRegex(const std::string &pattern)
{
  // RE_ANCHORED forces a match to start at the beginning of the host; validate()
  // additionally requires it to consume the whole host, giving full-match
  // (std::regex_match) semantics without mutating the operator's pattern.
  if (!_allow_regex.compile(pattern.c_str(), RE_CASE_INSENSITIVE | RE_ANCHORED)) {
    _has_allow_regex = false;
    return false;
  }
  // Cap backtracking so a pathological pattern/host can't burn unbounded CPU
  // on this hot path; an exceeded limit surfaces from exec() as no match and is
  // treated as "not allowlisted" (fail closed).
  _allow_regex.set_match_limit(ALLOW_REGEX_MATCH_LIMIT);
  _has_allow_regex = true;
  return true;
}

std::string
IncludeUrlValidator::redactUserInfo(string_view url)
{
  // Locate the authority, then its userinfo. The authority begins after "://"
  // for an absolute URL, or after a leading "//" for a protocol-relative URL
  // ("//user:pass@host/path"). If there is an '@' before the authority
  // terminator ('/', '?', or '#'), everything up to and including the '@' is
  // userinfo and may carry credentials — replace it with "***@". This runs on
  // rejection paths for URLs that fail validation too, so it must not require
  // a valid scheme. Check the leading "//" first: a protocol-relative URL has
  // no scheme, and a later "://" (e.g. inside a query) must not be mistaken
  // for the authority delimiter.
  size_t authority_begin;
  if (url.substr(0, 2) == "//") {
    authority_begin = 2;
  } else if (auto scheme_sep = url.find("://"); scheme_sep != string_view::npos) {
    authority_begin = scheme_sep + 3;
  } else {
    return std::string{url};
  }
  size_t authority_end  = url.find_first_of("/?#", authority_begin);
  string_view authority = (authority_end == string_view::npos) ? url.substr(authority_begin) :
                                                                 url.substr(authority_begin, authority_end - authority_begin);
  auto at_in_authority = authority.rfind('@');
  if (at_in_authority == string_view::npos) {
    return std::string{url};
  }
  std::string redacted;
  redacted.reserve(url.size());
  redacted.append(url.substr(0, authority_begin));
  redacted.append("***@");
  redacted.append(url.substr(authority_begin + at_in_authority + 1));
  return redacted;
}

IncludeUrlValidator::Reason
IncludeUrlValidator::validate(string_view url) const
{
  // Reject ASCII control characters / whitespace to avoid request splitting in TSFetchUrl request construction.
  for (unsigned char c : url) {
    if (c <= 0x20 || c == 0x7f) {
      return MALFORMED;
    }
  }

  string_view scheme;
  string_view host;
  if (!splitUrl(url, scheme, host)) {
    return MALFORMED;
  }
  if (!iequals(scheme, "http") && !iequals(scheme, "https")) {
    return BAD_SCHEME;
  }
  // Non-canonical numeric IPv4 forms are SSRF evasion and are rejected
  // unconditionally — the allow-private-hosts escape hatch only relaxes the
  // private-range denylist, not these forms.
  if (isNonCanonicalNumericIPv4(host)) {
    return PRIVATE_HOST;
  }
  // An RFC 6874 zone id (e.g. "fe80::1%25eth0", with the '%' URL-encoded as
  // "%25") selects a network interface and is only meaningful for link-local
  // / non-global addresses. Reject any host carrying a '%' unconditionally so
  // it can never be enabled by accident through the private-hosts escape
  // hatch.
  if (host.find('%') != string_view::npos) {
    return PRIVATE_HOST;
  }
  if (!_allow_private_hosts && isPrivateHost(host)) {
    return PRIVATE_HOST;
  }
  if (_has_allow_regex) {
    // Normalize the host the same way isPrivateHost does so an
    // allowlist for "example.com" still matches "example.com." and
    // doesn't accidentally match a host carrying a zone id.
    string_view bare = host;
    if (auto pct = bare.find('%'); pct != string_view::npos) {
      bare = bare.substr(0, pct);
    }
    if (!bare.empty() && bare.back() == '.') {
      bare.remove_suffix(1);
    }
    std::string h(bare);
    // Full-match (std::regex_match) semantics on PCRE1: the pattern is compiled
    // RE_ANCHORED so the match must start at offset 0, and we additionally
    // require it to consume the entire host (ovector[1] == length). exec()
    // returns false on no match, error, or an exceeded match limit, so any of
    // those fails closed (treated as "not allowlisted").
    int ovector[Regex::DEFAULT_GROUP_COUNT * 3] = {0};
    if (!_allow_regex.exec(h, ovector, Regex::DEFAULT_GROUP_COUNT * 3) || ovector[1] != static_cast<int>(h.length())) {
      return NOT_ALLOWLISTED;
    }
  }
  return OK;
}

const char *
IncludeUrlValidator::reasonString(Reason r)
{
  switch (r) {
  case OK:
    return "ok";
  case MALFORMED:
    return "malformed-url";
  case BAD_SCHEME:
    return "bad-scheme";
  case PRIVATE_HOST:
    return "private-host";
  case NOT_ALLOWLISTED:
    return "not-allowlisted";
  }
  return "unknown";
}

} // namespace EsiLib
