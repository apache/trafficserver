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

#pragma once

#include <string>
#include <string_view>

#include "tsutil/Regex.h"

namespace EsiLib
{
class IncludeUrlValidator
{
public:
  enum Reason {
    OK              = 0,
    MALFORMED       = 1,
    BAD_SCHEME      = 2,
    PRIVATE_HOST    = 3,
    NOT_ALLOWLISTED = 4,
  };

  IncludeUrlValidator()  = default;
  ~IncludeUrlValidator() = default;

  // Compiles a regex that, when set, requires the post-expansion hostname to
  // fully match. Returns false on invalid pattern; the caller is responsible
  // for failing initialization closed (this security control should never be
  // silently disabled by a typo'd pattern).
  bool setHostAllowRegex(const std::string &pattern);

  void
  setAllowPrivateHosts(bool b)
  {
    _allow_private_hosts = b;
  }

  // Validates an already-expanded include URL. Cheap and pure.
  Reason validate(std::string_view url) const;

  static const char *reasonString(Reason r);

  // Returns the URL with any userinfo ("user:pass@") replaced by "***@",
  // so credentials don't leak into log lines. Leaves URLs without
  // userinfo unchanged. Tolerates unparseable templates (raw URLs
  // containing $(...) before expansion).
  static std::string redactUserInfo(std::string_view url);

  // Exposed for unit tests.
  static bool splitUrl(std::string_view url, std::string_view &scheme, std::string_view &host);
  static bool isPrivateHost(std::string_view host);

private:
  bool _allow_private_hosts{false};
  bool _has_allow_regex{false};
  // PCRE2-backed (ts::Regex) instead of std::regex: the allowlist is matched
  // in a hot path against attacker-influenced hostnames, and a backtracking
  // engine is vulnerable to catastrophic-backtracking DoS. _match_context
  // carries a match (backtracking) limit so worst-case CPU time per match is
  // bounded; exceeding it fails closed (treated as "not allowlisted").
  Regex             _allow_regex;
  RegexMatchContext _match_context;
};

} // namespace EsiLib
