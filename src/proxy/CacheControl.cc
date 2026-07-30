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

/*****************************************************************************
 *
 *  CacheControl.cc - Implementation to Cache Control system
 *
 *
 ****************************************************************************/

#include <sys/types.h>

#include <string>
#include <string_view>

#include "config/cache.h"
#include "tscore/Filenames.h"
#include "proxy/CacheControl.h"
#include "proxy/ControlMatcher.h"
#include "mgmt/config/ConfigContextDiags.h"
#include "mgmt/config/ConfigRegistry.h"
#include "proxy/http/HttpConfig.h"

using CC_table = ControlMatcher<CacheControlRecord, CacheControlResult>;

namespace
{
const char     modulePrefix[]                  = "[CacheControl]";
constexpr char CACHE_CONTROL_FILENAME_RECORD[] = "proxy.config.cache.control.filename";

#define TWEAK_CACHE_RESPONSES_TO_COOKIES "cache-responses-to-cookies"

const char *CC_directive_str[static_cast<int>(CacheControlType::NUM_TYPES)] = {
  "INVALID",
  "REVALIDATE_AFTER",
  "NEVER_CACHE",
  "STANDARD_CACHE",
  "IGNORE_NO_CACHE",
  "IGNORE_CLIENT_NO_CACHE",
  "IGNORE_SERVER_NO_CACHE",
  "PIN_IN_CACHE",
  "TTL_IN_CACHE"
  // "CACHE_AUTH_CONTENT"
};

Ptr<ProxyMutex> reconfig_mutex;

DbgCtl dbg_ctl_v_http3{"v_http3"};
DbgCtl dbg_ctl_http3{"http3"};
DbgCtl dbg_ctl_cache_control{"cache_control"};

std::string
quote_matcher_value(std::string_view value)
{
  std::string result{"\""};

  for (char c : value) {
    if (c == '\\' || c == '"') {
      result.push_back('\\');
    }
    result.push_back(c);
  }
  result.push_back('"');
  return result;
}

void
append_matcher_pair(std::string &line, std::string_view key, std::string_view value)
{
  if (!line.empty()) {
    line.push_back(' ');
  }
  line.append(key);
  line.push_back('=');
  line.append(quote_matcher_value(value));
}

void
append_optional_match(std::string &line, std::string_view key, std::optional<std::string> const &value)
{
  if (value) {
    append_matcher_pair(line, key, *value);
  }
}

std::string
build_matcher_config(config::CacheConfig const &config)
{
  std::string matcher_config;

  for (auto const &rule : config) {
    std::string line;

    append_optional_match(line, "dest_host", rule.match.dest_host);
    append_optional_match(line, "dest_domain", rule.match.dest_domain);
    append_optional_match(line, "dest_ip", rule.match.dest_ip);
    append_optional_match(line, "url_regex", rule.match.url_regex);
    append_optional_match(line, "host_regex", rule.match.host_regex);
    append_optional_match(line, "port", rule.match.port);
    append_optional_match(line, "scheme", rule.match.scheme);
    append_optional_match(line, "prefix", rule.match.prefix);
    append_optional_match(line, "suffix", rule.match.suffix);
    append_optional_match(line, "method", rule.match.method);
    append_optional_match(line, "time", rule.match.time);
    append_optional_match(line, "src_ip", rule.match.src_ip);
    append_optional_match(line, "iport", rule.match.incoming_port);
    append_optional_match(line, "tag", rule.match.tag);
    if (rule.match.internal) {
      append_matcher_pair(line, "internal", *rule.match.internal ? "true" : "false");
    }
    if (rule.match.primary_count() == 0) {
      append_matcher_pair(line, "url_regex", ".*");
    }

    append_matcher_pair(line, "yaml_rule", "true");
    if (rule.action.cache) {
      append_matcher_pair(line, "yaml_cache", *rule.action.cache == config::CacheMode::NEVER ? "never" : "standard");
    }
    append_optional_match(line, "yaml_revalidate", rule.action.revalidate);
    append_optional_match(line, "yaml_pin_in_cache", rule.action.pin_in_cache);
    append_optional_match(line, "yaml_ttl_in_cache", rule.action.ttl_in_cache);
    if (rule.action.ignore_no_cache.value_or(false) || rule.action.ignore_client_no_cache.value_or(false)) {
      append_matcher_pair(line, "yaml_ignore_client_no_cache", "true");
    }
    if (rule.action.ignore_no_cache.value_or(false) || rule.action.ignore_server_no_cache.value_or(false)) {
      append_matcher_pair(line, "yaml_ignore_server_no_cache", "true");
    }
    if (rule.action.cache_responses_to_cookies) {
      append_matcher_pair(line, TWEAK_CACHE_RESPONSES_TO_COOKIES, std::to_string(*rule.action.cache_responses_to_cookies));
    }

    matcher_config.append(line);
    matcher_config.push_back('\n');
  }

  return matcher_config;
}

std::unique_ptr<CC_table>
load_cache_control_table(ConfigContext ctx)
{
  constexpr int match_flags = ALLOW_HOST_TABLE | ALLOW_IP_TABLE | ALLOW_REGEX_TABLE | ALLOW_HOST_REGEX_TABLE | ALLOW_URL_TABLE;

  ats_scoped_str config_path(RecConfigReadConfigPath(CACHE_CONTROL_FILENAME_RECORD));
  auto           table =
    std::make_unique<CC_table>(CACHE_CONTROL_FILENAME_RECORD, modulePrefix, &http_dest_tags, match_flags | DONT_BUILD_TABLE, ctx);

  ink_release_assert(config_path);
  ink_strlcpy(table->config_file_path, config_path.get(), sizeof(table->config_file_path));
  table->flags = match_flags;

  std::string_view path{config_path.get()};
  if (path.ends_with(".yaml") || path.ends_with(".yml")) {
    config::CacheConfigParser parser;
    auto                      result = parser.parse(config_path.get());

    if (result.file_not_found) {
      CfgLoadLog(ctx, DL_Warning, "Cannot open cache configuration %s", config_path.get());
      return table;
    }
    if (!result.ok()) {
      CfgLoadFailWithErrata(ctx, result.errata, "%s failed to load", config_path.get());
      return nullptr;
    }

    std::string matcher_config = build_matcher_config(result.value);
    table->m_numEntries        = table->BuildTableFromString(matcher_config.data(), ctx);
  } else {
    table->m_numEntries = table->BuildTable(ctx);
  }

  return table;
}

} // end anonymous namespace

// Global Ptrs
CC_table *CacheControlTable = nullptr;

// struct CC_FreerContinuation
// Continuation to free old cache control lists after
//  a timeout
//
struct CC_FreerContinuation;
using CC_FreerContHandler = int (CC_FreerContinuation::*)(int, void *);
struct CC_FreerContinuation : public Continuation {
  CC_table *p;
  int
  freeEvent(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
  {
    Dbg(dbg_ctl_cache_control, "Deleting old table");
    delete p;
    delete this;
    return EVENT_DONE;
  }
  CC_FreerContinuation(CC_table *ap) : Continuation(nullptr), p(ap) { SET_HANDLER(&CC_FreerContinuation::freeEvent); }
};

//
//   Begin API functions
//
bool
host_rule_in_CacheControlTable()
{
  return (CacheControlTable->hostMatch ? true : false);
}

bool
ip_rule_in_CacheControlTable()
{
  return (CacheControlTable->ipMatch ? true : false);
}

void
initCacheControl()
{
  ink_assert(CacheControlTable == nullptr);
  reconfig_mutex = new_ProxyMutex();

  auto table = load_cache_control_table({});
  if (table) {
    CacheControlTable = table.release();
  } else {
    CacheControlTable = new CC_table(CACHE_CONTROL_FILENAME_RECORD, modulePrefix, &http_dest_tags,
                                     ALLOW_HOST_TABLE | ALLOW_IP_TABLE | ALLOW_REGEX_TABLE | ALLOW_HOST_REGEX_TABLE |
                                       ALLOW_URL_TABLE | DONT_BUILD_TABLE);
  }

  config::ConfigRegistry::Get_Instance().register_config( // File registration.
    "cache_control",                                      // registry key
    ts::filename::CACHE,                                  // default filename
    "proxy.config.cache.control.filename",                // record holding the filename
    [](ConfigContext ctx) { reloadCacheControl(ctx); },   // reload handler
    config::ConfigSource::FileOnly,                       // no RPC content source
    {"proxy.config.cache.control.filename"});             // trigger records
}

// void reloadCacheControl()
//
//  Called when the cache.conf file changes.  Since it called
//   infrequently, we do the load of new file as blocking I/O and
//   lock acquire is also blocking
//
void
reloadCacheControl(ConfigContext ctx)
{
  CfgLoadLog(ctx, DL_Note, "Cache configuration loading ...");
  Dbg(dbg_ctl_cache_control, "Cache configuration updated, reloading");

  auto new_table = load_cache_control_table(ctx);
  if (!new_table) {
    return;
  }

  CC_table *old_table = CacheControlTable;
  ink_atomic_swap(&CacheControlTable, new_table.release());
  eventProcessor.schedule_in(new CC_FreerContinuation(old_table), CACHE_CONTROL_TIMEOUT, ET_CALL);

  CfgLoadComplete(ctx, "Cache configuration finished loading");
}

void
getCacheControl(CacheControlResult *result, HttpRequestData *rdata, const OverridableHttpConfigParams *h_txn_conf, char *tag)
{
  rdata->tag = tag;
  CacheControlTable->Match(rdata, result);

  if (h_txn_conf->cache_ignore_client_no_cache) {
    result->ignore_client_no_cache = true;
  }

  if (h_txn_conf->cache_ignore_server_no_cache) {
    result->ignore_server_no_cache = true;
  }

  if (!h_txn_conf->cache_ignore_client_cc_max_age) {
    result->ignore_client_cc_max_age = false;
  }
}

//
//   End API functions
//

// void CacheControlResult::Print()
//
//  Debugging Method
//
void
CacheControlResult::Print() const
{
  printf("\t reval: %d, never-cache: %d, pin: %d, ignore-c: %d ignore-s: %d\n", revalidate_after, never_cache, pin_in_cache_for,
         ignore_client_no_cache, ignore_server_no_cache);
}

// void CacheControlRecord::Print()
//
//  Debugging Method
//
void
CacheControlRecord::Print() const
{
  switch (this->directive) {
  case CacheControlType::REVALIDATE_AFTER:
    printf("\t\tDirective: %s : %d\n", CC_directive_str[static_cast<int>(CacheControlType::REVALIDATE_AFTER)], this->time_arg);
    break;
  case CacheControlType::PIN_IN_CACHE:
    printf("\t\tDirective: %s : %d\n", CC_directive_str[static_cast<int>(CacheControlType::PIN_IN_CACHE)], this->time_arg);
    break;
  case CacheControlType::TTL_IN_CACHE:
    printf("\t\tDirective: %s : %d\n", CC_directive_str[static_cast<int>(CacheControlType::TTL_IN_CACHE)], this->time_arg);
    break;
  case CacheControlType::IGNORE_CLIENT_NO_CACHE:
  case CacheControlType::IGNORE_SERVER_NO_CACHE:
  case CacheControlType::NEVER_CACHE:
  case CacheControlType::STANDARD_CACHE:
  case CacheControlType::IGNORE_NO_CACHE:
    printf("\t\tDirective: %s\n", CC_directive_str[static_cast<int>(this->directive)]);
    break;
  case CacheControlType::INVALID:
  case CacheControlType::NUM_TYPES:
    printf("\t\tDirective: INVALID\n");
    break;
  }
  if (cache_responses_to_cookies >= 0) {
    printf("\t\t  - " TWEAK_CACHE_RESPONSES_TO_COOKIES ":%d\n", cache_responses_to_cookies);
  }
  ControlBase::Print();
}

// Result CacheControlRecord::Init(matcher_line* line_info)
//
//    matcher_line* line_info - contains parsed label/value
//      pairs of the current cache rule
//
//    Returns NULL if everything is OK
//      Otherwise, returns an error string that the caller MUST
//        DEALLOCATE with free()
//
Result
CacheControlRecord::Init(matcher_line *line_info)
{
  int         time_in;
  const char *tmp;
  char       *label;
  char       *val;
  bool        d_found = false;

  this->line_num = line_info->line_num;

  // First pass for optional tweaks.
  for (int i = 0; i < MATCHER_MAX_TOKENS && line_info->num_el; ++i) {
    bool used = false;
    label     = line_info->line[0][i];
    val       = line_info->line[1][i];
    if (!label) {
      continue;
    }

    if (strcasecmp(label, "yaml_rule") == 0) {
      if (strcasecmp(val, "true") != 0) {
        return Result::failure("Value for yaml_rule must be true");
      }
      is_yaml_rule = true;
      used         = true;
    } else if (strcasecmp(label, TWEAK_CACHE_RESPONSES_TO_COOKIES) == 0) {
      char *ptr = nullptr;
      int   v   = strtol(val, &ptr, 0);
      if (ptr == val || *ptr != '\0' || v < 0 || v > 4) {
        return Result::failure("Value for " TWEAK_CACHE_RESPONSES_TO_COOKIES " must be an integer in the range 0..4");
      } else {
        cache_responses_to_cookies = v;
      }
      used = true;
    }

    // Clip pair if used.
    if (used) {
      line_info->line[0][i] = nullptr;
      --(line_info->num_el);
    }
  }

  if (is_yaml_rule) {
    int action_count = cache_responses_to_cookies >= 0 ? 1 : 0;

    for (int i = 0; i < MATCHER_MAX_TOKENS && line_info->num_el; ++i) {
      label = line_info->line[0][i];
      val   = line_info->line[1][i];
      if (!label) {
        continue;
      }

      bool used = true;
      if (strcasecmp(label, "yaml_cache") == 0) {
        if (strcasecmp(val, "never") == 0) {
          yaml_cache_action = CacheControlType::NEVER_CACHE;
        } else if (strcasecmp(val, "standard") == 0) {
          yaml_cache_action = CacheControlType::STANDARD_CACHE;
        } else {
          return Result::failure("%s Invalid cache action at line %d in %s", modulePrefix, line_num, ts::filename::CACHE);
        }
      } else if (strcasecmp(label, "yaml_revalidate") == 0) {
        tmp = processDurationString(val, &yaml_revalidate_after);
        if (tmp != nullptr) {
          return Result::failure("%s %s at line %d in %s", modulePrefix, tmp, line_num, ts::filename::CACHE);
        }
      } else if (strcasecmp(label, "yaml_pin_in_cache") == 0) {
        tmp = processDurationString(val, &yaml_pin_in_cache_for);
        if (tmp != nullptr) {
          return Result::failure("%s %s at line %d in %s", modulePrefix, tmp, line_num, ts::filename::CACHE);
        }
      } else if (strcasecmp(label, "yaml_ttl_in_cache") == 0) {
        tmp = processDurationString(val, &yaml_ttl_in_cache);
        if (tmp != nullptr) {
          return Result::failure("%s %s at line %d in %s", modulePrefix, tmp, line_num, ts::filename::CACHE);
        }
      } else if (strcasecmp(label, "yaml_ignore_client_no_cache") == 0) {
        if (strcasecmp(val, "true") != 0) {
          return Result::failure("%s Invalid boolean at line %d in %s", modulePrefix, line_num, ts::filename::CACHE);
        }
        yaml_ignore_client_no_cache = true;
      } else if (strcasecmp(label, "yaml_ignore_server_no_cache") == 0) {
        if (strcasecmp(val, "true") != 0) {
          return Result::failure("%s Invalid boolean at line %d in %s", modulePrefix, line_num, ts::filename::CACHE);
        }
        yaml_ignore_server_no_cache = true;
      } else {
        used = false;
      }

      if (used) {
        line_info->line[0][i] = nullptr;
        --line_info->num_el;
        ++action_count;
      }
    }

    if (action_count == 0) {
      return Result::failure("%s No action in %s at line %d", modulePrefix, ts::filename::CACHE, line_num);
    }
    if (line_info->num_el > 0) {
      tmp = ProcessModifiers(line_info);
      if (tmp != nullptr) {
        return Result::failure("%s %s at line %d in %s", modulePrefix, tmp, line_num, ts::filename::CACHE);
      }
    }
    return Result::ok();
  }

  // Now look for the directive.
  for (int i = 0; i < MATCHER_MAX_TOKENS; i++) {
    label = line_info->line[0][i];
    val   = line_info->line[1][i];

    if (label == nullptr) {
      continue;
    }

    if (strcasecmp(label, "action") == 0) {
      if (strcasecmp(val, "never-cache") == 0) {
        directive = CacheControlType::NEVER_CACHE;
        d_found   = true;
      } else if (strcasecmp(val, "standard-cache") == 0) {
        directive = CacheControlType::STANDARD_CACHE;
        d_found   = true;
      } else if (strcasecmp(val, "ignore-no-cache") == 0) {
        directive = CacheControlType::IGNORE_NO_CACHE;
        d_found   = true;
      } else if (strcasecmp(val, "ignore-client-no-cache") == 0) {
        directive = CacheControlType::IGNORE_CLIENT_NO_CACHE;
        d_found   = true;
      } else if (strcasecmp(val, "ignore-server-no-cache") == 0) {
        directive = CacheControlType::IGNORE_SERVER_NO_CACHE;
        d_found   = true;
      } else {
        return Result::failure("%s Invalid action at line %d in %s", modulePrefix, line_num, ts::filename::CACHE);
      }
    } else {
      if (strcasecmp(label, "revalidate") == 0) {
        directive = CacheControlType::REVALIDATE_AFTER;
        d_found   = true;
      } else if (strcasecmp(label, "pin-in-cache") == 0) {
        directive = CacheControlType::PIN_IN_CACHE;
        d_found   = true;
      } else if (strcasecmp(label, "ttl-in-cache") == 0) {
        directive = CacheControlType::TTL_IN_CACHE;
        d_found   = true;
      }
      // Process the time argument for the remaining directives
      if (d_found == true) {
        tmp = processDurationString(val, &time_in);
        if (tmp == nullptr) {
          this->time_arg = time_in;

        } else {
          return Result::failure("%s %s at line %d in %s", modulePrefix, tmp, line_num, ts::filename::CACHE);
        }
      }
    }

    if (d_found == true) {
      // Consume the label/value pair we used
      line_info->line[0][i] = nullptr;
      line_info->num_el--;
      break;
    }
  }

  if (d_found == false) {
    return Result::failure("%s No directive in %s at line %d", modulePrefix, ts::filename::CACHE, line_num);
  }
  // Process any modifiers to the directive, if they exist
  if (line_info->num_el > 0) {
    tmp = ProcessModifiers(line_info);

    if (tmp != nullptr) {
      return Result::failure("%s %s at line %d in %s", modulePrefix, tmp, line_num, ts::filename::CACHE);
    }
  }

  return Result::ok();
}

// void CacheControlRecord::UpdateMatch(CacheControlResult* result, RequestData* rdata)
//
//    Updates the parameters in result if the this element
//     appears later in the file
//
void
CacheControlRecord::UpdateMatch(CacheControlResult *result, RequestData *rdata)
{
  bool             match   = false;
  HttpRequestData *h_rdata = static_cast<HttpRequestData *>(rdata);

  if (is_yaml_rule) {
    if (!this->CheckForMatch(h_rdata, result->matched_rule_line)) {
      return;
    }

    CacheControlResult rule_result;

    rule_result.matched_rule_line = this->line_num;
    if (yaml_cache_action == CacheControlType::NEVER_CACHE) {
      rule_result.never_cache = true;
      rule_result.never_line  = this->line_num;
    } else if (yaml_cache_action == CacheControlType::STANDARD_CACHE) {
      rule_result.never_cache = false;
      rule_result.never_line  = this->line_num;
    }
    if (yaml_revalidate_after != CC_UNSET_TIME) {
      rule_result.revalidate_after = yaml_revalidate_after;
      rule_result.reval_line       = this->line_num;
    }
    if (yaml_pin_in_cache_for != CC_UNSET_TIME) {
      rule_result.pin_in_cache_for = yaml_pin_in_cache_for;
      rule_result.pin_line         = this->line_num;
    }
    if (yaml_ttl_in_cache != CC_UNSET_TIME) {
      rule_result.ttl_in_cache = yaml_ttl_in_cache;
      rule_result.ttl_line     = this->line_num;
      rule_result.never_cache  = false;
      rule_result.never_line   = this->line_num;
    }
    rule_result.ignore_client_no_cache     = yaml_ignore_client_no_cache;
    rule_result.ignore_server_no_cache     = yaml_ignore_server_no_cache;
    rule_result.cache_responses_to_cookies = cache_responses_to_cookies;
    *result                                = rule_result;

    Dbg(dbg_ctl_cache_control, "Matched cache.yaml rule at line %d", this->line_num);
    return;
  }

  switch (this->directive) {
  case CacheControlType::REVALIDATE_AFTER:
    if (this->CheckForMatch(h_rdata, result->reval_line) == true) {
      result->revalidate_after = time_arg;
      result->reval_line       = this->line_num;
      match                    = true;
    }
    break;
  case CacheControlType::NEVER_CACHE:
    if (this->CheckForMatch(h_rdata, result->never_line) == true) {
      // ttl-in-cache overrides never-cache
      if (result->ttl_line == -1) {
        result->never_cache = true;
        result->never_line  = this->line_num;
        match               = true;
      }
    }
    break;
  case CacheControlType::STANDARD_CACHE:
    // Standard cache just overrides never-cache
    if (this->CheckForMatch(h_rdata, result->never_line) == true) {
      result->never_cache = false;
      result->never_line  = this->line_num;
      match               = true;
    }
    break;
  case CacheControlType::IGNORE_NO_CACHE:
  // We cover both client & server cases for this directive
  //  FALLTHROUGH
  case CacheControlType::IGNORE_CLIENT_NO_CACHE:
    if (this->CheckForMatch(h_rdata, result->ignore_client_line) == true) {
      result->ignore_client_no_cache = true;
      result->ignore_client_line     = this->line_num;
      match                          = true;
    }
    if (this->directive != CacheControlType::IGNORE_NO_CACHE) {
      break;
    }
  // FALLTHROUGH
  case CacheControlType::IGNORE_SERVER_NO_CACHE:
    if (this->CheckForMatch(h_rdata, result->ignore_server_line) == true) {
      result->ignore_server_no_cache = true;
      result->ignore_server_line     = this->line_num;
      match                          = true;
    }
    break;
  case CacheControlType::PIN_IN_CACHE:
    if (this->CheckForMatch(h_rdata, result->pin_line) == true) {
      result->pin_in_cache_for = time_arg;
      result->pin_line         = this->line_num;
      match                    = true;
    }
    break;
  case CacheControlType::TTL_IN_CACHE:
    if (this->CheckForMatch(h_rdata, result->ttl_line) == true) {
      result->ttl_in_cache = time_arg;
      result->ttl_line     = this->line_num;
      // ttl-in-cache overrides never-cache
      result->never_cache = false;
      result->never_line  = this->line_num;
      match               = true;
    }
    break;
  case CacheControlType::INVALID:
  case CacheControlType::NUM_TYPES:
  default:
    // Should not get here
    Warning("Impossible directive in CacheControlRecord::UpdateMatch");
    ink_assert(0);
    break;
  }

  if (cache_responses_to_cookies >= 0) {
    result->cache_responses_to_cookies = cache_responses_to_cookies;
  }

  if (match == true) {
    char crtc_debug[80];
    if (result->cache_responses_to_cookies >= 0) {
      snprintf(crtc_debug, sizeof(crtc_debug), " [" TWEAK_CACHE_RESPONSES_TO_COOKIES "=%d]", result->cache_responses_to_cookies);
    } else {
      crtc_debug[0] = 0;
    }

    Dbg(dbg_ctl_cache_control, "Matched with for %s at line %d%s", CC_directive_str[static_cast<int>(this->directive)],
        this->line_num, crtc_debug);
  }
}
