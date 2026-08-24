/** @file

  Unit tests for IncludeUrlValidator.

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

#include <catch2/catch_test_macros.hpp>

#include "IncludeUrlValidator.h"

using EsiLib::IncludeUrlValidator;

TEST_CASE("splitUrl parses scheme and host")
{
  std::string_view scheme;
  std::string_view host;

  REQUIRE(IncludeUrlValidator::splitUrl("http://example.com/foo", scheme, host));
  REQUIRE(scheme == "http");
  REQUIRE(host == "example.com");

  REQUIRE(IncludeUrlValidator::splitUrl("https://EXAMPLE.com:8443/path?x=1", scheme, host));
  REQUIRE(host == "EXAMPLE.com");

  REQUIRE(IncludeUrlValidator::splitUrl("http://[::1]:80/", scheme, host));
  REQUIRE(host == "::1");

  REQUIRE(IncludeUrlValidator::splitUrl("http://user:pw@example.com/x", scheme, host));
  REQUIRE(host == "example.com");

  REQUIRE_FALSE(IncludeUrlValidator::splitUrl("no-scheme", scheme, host));
  REQUIRE_FALSE(IncludeUrlValidator::splitUrl("://nohost/", scheme, host));
  REQUIRE_FALSE(IncludeUrlValidator::splitUrl("http://", scheme, host));

  // A bracketed IPv6 authority must be followed only by end-of-authority or a
  // ":port" separator. Trailing junk after ']' (e.g. "[::1]evil.com") would
  // otherwise mis-parse the host as "::1" while the URL points elsewhere.
  REQUIRE_FALSE(IncludeUrlValidator::splitUrl("http://[::1]evil.com/", scheme, host));
  REQUIRE_FALSE(IncludeUrlValidator::splitUrl("http://[fe80::1]x/path", scheme, host));
  REQUIRE_FALSE(IncludeUrlValidator::splitUrl("http://[]/", scheme, host));
  REQUIRE_FALSE(IncludeUrlValidator::splitUrl("http://[::1", scheme, host));
  // Valid bracketed forms still parse.
  REQUIRE(IncludeUrlValidator::splitUrl("http://[2001:db8::1]/p", scheme, host));
  REQUIRE(host == "2001:db8::1");
  REQUIRE(IncludeUrlValidator::splitUrl("http://[::1]", scheme, host));
  REQUIRE(host == "::1");
}

TEST_CASE("isPrivateHost recognizes private ranges")
{
  // IPv4 ranges that must be denied.
  REQUIRE(IncludeUrlValidator::isPrivateHost("127.0.0.1"));
  REQUIRE(IncludeUrlValidator::isPrivateHost("10.0.0.1"));
  REQUIRE(IncludeUrlValidator::isPrivateHost("172.16.0.1"));
  REQUIRE(IncludeUrlValidator::isPrivateHost("192.168.5.5"));
  REQUIRE(IncludeUrlValidator::isPrivateHost("169.254.169.254")); // cloud metadata
  REQUIRE(IncludeUrlValidator::isPrivateHost("0.0.0.0"));
  REQUIRE(IncludeUrlValidator::isPrivateHost("100.64.1.1")); // CGNAT
  // IPv6.
  REQUIRE(IncludeUrlValidator::isPrivateHost("::1"));
  REQUIRE(IncludeUrlValidator::isPrivateHost("fe80::1"));
  REQUIRE(IncludeUrlValidator::isPrivateHost("fc00::abcd"));
  REQUIRE(IncludeUrlValidator::isPrivateHost("::ffff:10.0.0.1"));        // IPv4-mapped private
  REQUIRE(IncludeUrlValidator::isPrivateHost("64:ff9b::a00:1"));         // NAT64 wrapping 10.0.0.1
  REQUIRE(IncludeUrlValidator::isPrivateHost("64:ff9b::a9fe:a9fe"));     // NAT64 wrapping 169.254.169.254
  REQUIRE_FALSE(IncludeUrlValidator::isPrivateHost("64:ff9b::808:808")); // NAT64 wrapping 8.8.8.8
  // RFC 6874 IPv6 scope id: stripped before parsing, and the very
  // presence of a zone id means scoped/local — deny outright.
  REQUIRE(IncludeUrlValidator::isPrivateHost("fe80::1%eth0"));
  REQUIRE(IncludeUrlValidator::isPrivateHost("fe80::1%25eth0"));      // URL-encoded form
  REQUIRE(IncludeUrlValidator::isPrivateHost("2001:db8::1%eth0"));    // global address + zone → still scoped
  REQUIRE(IncludeUrlValidator::isPrivateHost("malformed-ipv6%eth0")); // had zone but didn't parse → fail closed
  // Names.
  REQUIRE(IncludeUrlValidator::isPrivateHost("localhost"));
  REQUIRE(IncludeUrlValidator::isPrivateHost("LocalHost"));
  REQUIRE(IncludeUrlValidator::isPrivateHost("foo.localhost"));
  // Trailing FQDN-root dot must not bypass the loopback check.
  REQUIRE(IncludeUrlValidator::isPrivateHost("localhost."));
  REQUIRE(IncludeUrlValidator::isPrivateHost("foo.localhost."));
  // Non-canonical IPv4 numeric forms must not slip past the denylist as
  // "hostnames". Common evasion forms decoded back to 127.0.0.1:
  REQUIRE(IncludeUrlValidator::isPrivateHost("2130706433"));       // decimal int
  REQUIRE(IncludeUrlValidator::isPrivateHost("017700000001"));     // octal
  REQUIRE(IncludeUrlValidator::isPrivateHost("0x7f000001"));       // hex
  REQUIRE(IncludeUrlValidator::isPrivateHost("0x7f.0x0.0x0.0x1")); // mixed hex octets
  REQUIRE(IncludeUrlValidator::isPrivateHost("127.1"));            // 2-part compact
  REQUIRE(IncludeUrlValidator::isPrivateHost("127.0.1"));          // 3-part compact
  // Even non-canonical numeric forms for "public" IPs are rejected: an
  // include URL written this way is never a legitimate hostname and
  // operators should use the dotted-quad form.
  REQUIRE(IncludeUrlValidator::isPrivateHost("0x08080808")); // hex 8.8.8.8
  REQUIRE(IncludeUrlValidator::isPrivateHost("134744072"));  // decimal 8.8.8.8
  // DNS hostnames that happen to start with a digit are still hostnames.
  REQUIRE_FALSE(IncludeUrlValidator::isPrivateHost("1example.com"));
  // Public.
  REQUIRE_FALSE(IncludeUrlValidator::isPrivateHost("8.8.8.8"));
  REQUIRE_FALSE(IncludeUrlValidator::isPrivateHost("2001:4860:4860::8888"));
  REQUIRE_FALSE(IncludeUrlValidator::isPrivateHost("example.com"));
  REQUIRE_FALSE(IncludeUrlValidator::isPrivateHost("example.com."));
}

TEST_CASE("validate enforces scheme and private-host denylist by default")
{
  IncludeUrlValidator v;
  REQUIRE(v.validate("http://example.com/foo") == IncludeUrlValidator::OK);
  REQUIRE(v.validate("https://example.com/") == IncludeUrlValidator::OK);
  // file:///etc/passwd has an empty authority and is rejected as MALFORMED before the scheme check.
  REQUIRE(v.validate("file:///etc/passwd") == IncludeUrlValidator::MALFORMED);
  REQUIRE(v.validate("file://host/etc/passwd") == IncludeUrlValidator::BAD_SCHEME);
  REQUIRE(v.validate("gopher://example.com/") == IncludeUrlValidator::BAD_SCHEME);
  REQUIRE(v.validate("not-a-url") == IncludeUrlValidator::MALFORMED);
  REQUIRE(v.validate("http://169.254.169.254/latest/meta-data/") == IncludeUrlValidator::PRIVATE_HOST);
  REQUIRE(v.validate("http://localhost:8080/admin") == IncludeUrlValidator::PRIVATE_HOST);
  REQUIRE(v.validate("http://[::1]/") == IncludeUrlValidator::PRIVATE_HOST);
  // Trailing-dot bypass: "localhost." must not slip past the denylist.
  REQUIRE(v.validate("http://localhost./admin") == IncludeUrlValidator::PRIVATE_HOST);
  // RFC 6874 IPv6 zone id: URL-encoded "%25" appears in the host after
  // splitUrl strips the brackets; the scoped form must be rejected.
  REQUIRE(v.validate("http://[fe80::1%25eth0]/x") == IncludeUrlValidator::PRIVATE_HOST);
}

TEST_CASE("validate respects allow-private-hosts escape hatch")
{
  IncludeUrlValidator v;
  v.setAllowPrivateHosts(true);
  REQUIRE(v.validate("http://10.0.0.5/svc") == IncludeUrlValidator::OK);
  REQUIRE(v.validate("http://localhost/x") == IncludeUrlValidator::OK);
  // Canonical dotted-quad loopback is allowed in this mode.
  REQUIRE(v.validate("http://127.0.0.1/x") == IncludeUrlValidator::OK);
  // Scheme is still enforced.
  REQUIRE(v.validate("ftp://10.0.0.5/x") == IncludeUrlValidator::BAD_SCHEME);
  // The escape hatch relaxes the private-range denylist, but non-canonical
  // numeric IPv4 forms are SSRF evasion and stay rejected even here — for
  // private addresses...
  REQUIRE(v.validate("http://0x7f000001/x") == IncludeUrlValidator::PRIVATE_HOST);   // hex 127.0.0.1
  REQUIRE(v.validate("http://2130706433/x") == IncludeUrlValidator::PRIVATE_HOST);   // decimal 127.0.0.1
  REQUIRE(v.validate("http://017700000001/x") == IncludeUrlValidator::PRIVATE_HOST); // octal 127.0.0.1
  REQUIRE(v.validate("http://127.1/x") == IncludeUrlValidator::PRIVATE_HOST);        // 2-part compact
  // ...and for public addresses (never a legitimate include host).
  REQUIRE(v.validate("http://0x08080808/x") == IncludeUrlValidator::PRIVATE_HOST); // hex 8.8.8.8
  REQUIRE(v.validate("http://134744072/x") == IncludeUrlValidator::PRIVATE_HOST);  // decimal 8.8.8.8
  // An RFC 6874 zone id selects a (link-local) interface and is rejected even
  // in this mode, so it can never be enabled by accident through the escape
  // hatch.
  REQUIRE(v.validate("http://[fe80::1%25eth0]/x") == IncludeUrlValidator::PRIVATE_HOST);
}

TEST_CASE("validate enforces optional host allowlist regex")
{
  IncludeUrlValidator v;
  REQUIRE(v.setHostAllowRegex(R"((.+\.)?example\.com)"));
  REQUIRE(v.validate("http://example.com/x") == IncludeUrlValidator::OK);
  REQUIRE(v.validate("http://api.example.com/x") == IncludeUrlValidator::OK);
  REQUIRE(v.validate("http://EXAMPLE.com/x") == IncludeUrlValidator::OK);
  REQUIRE(v.validate("http://evil.com/x") == IncludeUrlValidator::NOT_ALLOWLISTED);
  // Regex doesn't bypass the private-host denylist (private check runs first).
  REQUIRE(v.validate("http://127.0.0.1/x") == IncludeUrlValidator::PRIVATE_HOST);
  // Allowlist must see the normalized host: an operator pattern for
  // "example.com" matches an include URL with the FQDN-root form.
  REQUIRE(v.validate("http://example.com./x") == IncludeUrlValidator::OK);
  REQUIRE(v.validate("http://api.example.com./x") == IncludeUrlValidator::OK);
}

TEST_CASE("setHostAllowRegex rejects invalid pattern")
{
  IncludeUrlValidator v;
  REQUIRE_FALSE(v.setHostAllowRegex("(unclosed"));
  // After a failed compile, validator falls back to no-allowlist behavior.
  // The caller (TSPluginInit) is responsible for failing the plugin so
  // this fallback is never reached in practice; the test pins the
  // per-instance contract.
  REQUIRE(v.validate("http://example.com/x") == IncludeUrlValidator::OK);
}

TEST_CASE("redactUserInfo strips credentials before logging")
{
  // Plain userinfo replaced with "***@".
  REQUIRE(IncludeUrlValidator::redactUserInfo("http://user:pass@example.com/x") == "http://***@example.com/x");
  REQUIRE(IncludeUrlValidator::redactUserInfo("http://user@example.com/x") == "http://***@example.com/x");
  // No userinfo: unchanged.
  REQUIRE(IncludeUrlValidator::redactUserInfo("http://example.com/x") == "http://example.com/x");
  // '@' only in the path/query must not be misidentified as userinfo.
  REQUIRE(IncludeUrlValidator::redactUserInfo("http://example.com/path@notuser") == "http://example.com/path@notuser");
  REQUIRE(IncludeUrlValidator::redactUserInfo("http://example.com/?q=a@b") == "http://example.com/?q=a@b");
  // Unparseable / pre-expansion templates: tolerated.
  REQUIRE(IncludeUrlValidator::redactUserInfo("not-a-url") == "not-a-url");
  // Empty userinfo (just "@") is still redacted so the form is consistent.
  REQUIRE(IncludeUrlValidator::redactUserInfo("http://@example.com/") == "http://***@example.com/");
}
