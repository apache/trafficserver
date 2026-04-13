/** @file
 *
 *  Remap configuration runtime helpers.
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "proxy/http/remap/RemapConfig.h"

#include <dirent.h>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#include "proxy/hdrs/HTTP.h"
#include "proxy/http/remap/UrlRewrite.h"
#include "tscore/Diags.h"

load_remap_file_func load_remap_file_cb = nullptr;

BUILD_TABLE_INFO::BUILD_TABLE_INFO() = default;

BUILD_TABLE_INFO::~BUILD_TABLE_INFO()
{
  clear_acl_rules_list();
}

void
BUILD_TABLE_INFO::reset()
{
}

void
BUILD_TABLE_INFO::clear_acl_rules_list()
{
  auto *rp = rules_list;

  while (rp != nullptr) {
    auto *tmp = rp->next;
    delete rp;
    rp = tmp;
  }

  rules_list = nullptr;
}

bool
is_inkeylist(const char *key, ...)
{
  va_list ap;

  if (key == nullptr || key[0] == '\0') {
    return false;
  }

  va_start(ap, key);

  const char *str = va_arg(ap, const char *);
  while (str) {
    if (!strcasecmp(key, str)) {
      va_end(ap);
      return true;
    }

    str = va_arg(ap, const char *);
  }

  va_end(ap);
  return false;
}

void
free_directory_list(int n_entries, struct dirent **entrylist)
{
  for (int i = 0; i < n_entries; ++i) {
    free(entrylist[i]);
  }

  free(entrylist);
}

const char *
is_valid_scheme(std::string_view fromScheme, std::string_view toScheme)
{
  const char *errStr = nullptr;

  if ((fromScheme != std::string_view{URL_SCHEME_HTTP} && fromScheme != std::string_view{URL_SCHEME_HTTPS} &&
       fromScheme != std::string_view{URL_SCHEME_FILE} && fromScheme != std::string_view{URL_SCHEME_TUNNEL} &&
       fromScheme != std::string_view{URL_SCHEME_WS} && fromScheme != std::string_view{URL_SCHEME_WSS} &&
       fromScheme != std::string_view{URL_SCHEME_HTTP_UDS} && fromScheme != std::string_view{URL_SCHEME_HTTPS_UDS}) ||
      (toScheme != std::string_view{URL_SCHEME_HTTP} && toScheme != std::string_view{URL_SCHEME_HTTPS} &&
       toScheme != std::string_view{URL_SCHEME_TUNNEL} && toScheme != std::string_view{URL_SCHEME_WS} &&
       toScheme != std::string_view{URL_SCHEME_WSS})) {
    errStr = "only http, https, http+unix, https+unix, ws, wss, and tunnel remappings are supported";
    return errStr;
  }

  if ((fromScheme == std::string_view{URL_SCHEME_WSS} || fromScheme == std::string_view{URL_SCHEME_WS}) &&
      (toScheme != std::string_view{URL_SCHEME_WSS} && toScheme != std::string_view{URL_SCHEME_WS})) {
    errStr = "WS or WSS can only be mapped out to WS or WSS.";
  }

  return errStr;
}

bool
process_regex_mapping_config(const char *from_host_lower, url_mapping *new_mapping, UrlRewrite::RegexMapping *reg_map)
{
  std::string_view to_host{};
  int              to_host_len;
  int              substitution_id;
  int32_t          captures;

  reg_map->to_url_host_template     = nullptr;
  reg_map->to_url_host_template_len = 0;
  reg_map->n_substitutions          = 0;

  reg_map->url_map = new_mapping;

  // Use the normalized host buffer because it is NUL-terminated for the regex compiler.
  if (!reg_map->regular_expression.compile(from_host_lower)) {
    Warning("pcre_compile failed! Regex has error starting at %s", from_host_lower);
    goto fail;
  }

  captures = reg_map->regular_expression.get_capture_count();
  if (captures == -1) {
    Warning("pcre_fullinfo failed!");
    goto fail;
  }

  if (captures >= UrlRewrite::MAX_REGEX_SUBS) {
    Warning("regex has %d capturing subpatterns (including entire regex); Max allowed: %d", captures + 1,
            UrlRewrite::MAX_REGEX_SUBS);
    goto fail;
  }

  to_host     = new_mapping->toURL.host_get();
  to_host_len = static_cast<int>(to_host.length());
  for (int i = 0; i < to_host_len - 1; ++i) {
    if (to_host[i] == '$') {
      substitution_id = to_host[i + 1] - '0';
      if ((substitution_id < 0) || (substitution_id > captures)) {
        Warning("Substitution id [%c] has no corresponding capture pattern in regex [%s]", to_host[i + 1], from_host_lower);
        goto fail;
      }
      reg_map->substitution_markers[reg_map->n_substitutions] = i;
      reg_map->substitution_ids[reg_map->n_substitutions]     = substitution_id;
      ++reg_map->n_substitutions;
    }
  }

  reg_map->to_url_host_template_len = to_host_len;
  reg_map->to_url_host_template     = static_cast<char *>(ats_malloc(to_host_len));
  memcpy(reg_map->to_url_host_template, to_host.data(), to_host_len);

  return true;

fail:
  ats_free(reg_map->to_url_host_template);
  reg_map->to_url_host_template     = nullptr;
  reg_map->to_url_host_template_len = 0;

  return false;
}
