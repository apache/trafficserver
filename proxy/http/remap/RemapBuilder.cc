/** @file
 *
 *  YAML configuration for URL rewriting.
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one or more contributor license
 *  agreements.  See the NOTICE file distributed with this work for additional information regarding
 *  copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
 *  (the "License"); you may not use this file except in compliance with the License.  You may
 *  obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software distributed under the
 *  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 *  express or implied. See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "RemapBuilder.h"
#include "tscpp/util/PostScript.h"

swoc::Rv<UrlRewrite::RegexMapping *>
RemapBuilder::parse_regex_rewrite(url_mapping *mapping, TextView target_host)
{
  static constexpr TextView ERROR_PREFIX{"URL rewrite regex mapping -"};
  swoc::Rv<UrlRewrite::RegexMapping *> zret{nullptr};

  std::unique_ptr<UrlRewrite::RegexMapping> regex_rewrite{new UrlRewrite::RegexMapping};

  int substitution_id;
  int substitution_count = 0;

  regex_rewrite->url_map = mapping;

  if (regex_rewrite->regular_expression.compile(target_host.data()) == false) {
    zret.errata().error("{} pcre_compile failed on '{}'", ERROR_PREFIX, target_host);
  } else {
    int captures = regex_rewrite->regular_expression.get_capture_count();
    if (captures == -1) {
      zret.errata().error("{} no capture groups found for '{}'", ERROR_PREFIX, target_host);
    } else if (captures >= UrlRewrite::MAX_REGEX_SUBS) { // off by one for $0 (implicit capture)
      zret.errata().error("{} more capture groups [{}] in '{}' then the maximum supported [{}]", ERROR_PREFIX, captures + 1,
                          target_host, UrlRewrite::MAX_REGEX_SUBS);
    } else {
      TextView to_host{mapping->toUrl.host_get()};
      TextView::size_type offset = 0;
      while (offset < to_host.size()) {
        offset = to_host.find('$', offset);
        if (offset != TextView::npos && offset <= to_host.size() - 1 && isdigit(to_host[offset])) {
          if (substitution_count > captures) {
            zret.errata().error("{} more substitutions [{}] than capture groups [{}] in '{}'", ERROR_PREFIX, substitution_count,
                                captures, target_host);
            break;
          }
          substitution_id = to_host[offset + 1] - '0';
          if (substitution_id > captures) {
            zret.errata().error("{} capture group index {} is larger than the number of capture groups [{}] in '{}'", ERROR_PREFIX,
                                substitution_id, captures, target_host);
            break;
          }
          regex_rewrite->substitution_markers[regex_rewrite->n_substitutions] = offset;
          regex_rewrite->substitution_ids[regex_rewrite->n_substitutions]     = substitution_id;
          ++regex_rewrite->n_substitutions;
        }
      }

      // so the regex itself is stored in fromURL.host; string to match will be in the request; string
      // to use for substitutions will be in this buffer. Does this need to be localized, or is @c toUrl
      // sufficiently stable?
      regex_rewrite->to_url_host_template = mapping->toUrl.host_get();
    }
  }
  if (zret.is_ok()) {
    zret = regex_rewrite.release(); // release and return.
  }
  return zret;
}
