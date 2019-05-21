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

#include "tscore/ink_config.h"
#include "CacheControl.h"
#include "ControlMatcher.h"
#include "Main.h"
#include "P_EventSystem.h"
#include "ProxyConfig.h"
#include "HTTP.h"
#include "HttpConfig.h"
#include "P_Cache.h"
#include "tscore/Regex.h"

namespace
{
constexpr std::string_view MODULE_PREFIX{"[CacheControl]"};
constexpr std::string_view DEFAULT_TAG{"default"};

// This is handled outside ControlMatcher because it does not have a value.
constexpr std::string_view TWEAK_CACHE_RESPONSES_TO_COOKIES{"cache-responses-to-cookies"};

std::array<char const *, CC_NUM_TYPES> CC_directive_str{{"INVALID", "REVALIDATE_AFTER", "NEVER_CACHE", "STANDARD_CACHE",
                                                         "IGNORE_NO_CACHE", "IGNORE_CLIENT_NO_CACHE", "IGNORE_SERVER_NO_CACHE",
                                                         "PIN_IN_CACHE", "TTL_IN_CACHE"}};

std::array<char const *, 3> CC_TIME_MODE_TAG{{"exactly", "at least", "at most"}};

using CC_Table = ControlMatcher<CacheControlRecord, CacheControlResult>;

// Global Ptrs
Ptr<ProxyMutex> reconfig_mutex;
CC_Table *CacheControlTable = nullptr;

} // namespace

// struct CC_FreerContinuation
// Continuation to free old cache control lists after
//  a timeout
//
struct CC_FreerContinuation;
using CC_FreerContHandler = int (CC_FreerContinuation::*)(int, void *);
struct CC_FreerContinuation : public Continuation {
  CC_Table *p;
  int
  freeEvent(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
  {
    Debug("cache_control", "Deleting old table");
    delete p;
    delete this;
    return EVENT_DONE;
  }
  CC_FreerContinuation(CC_Table *ap) : Continuation(nullptr), p(ap)
  {
    SET_HANDLER((CC_FreerContHandler)&CC_FreerContinuation::freeEvent);
  }
};

// struct CC_UpdateContinuation
//
//   Used to read the cache.conf file after the manager signals
//      a change
//
struct CC_UpdateContinuation : public Continuation {
  int
  file_update_handler(int /* etype ATS_UNUSED */, void * /* data ATS_UNUSED */)
  {
    reloadCacheControl();
    delete this;
    return EVENT_DONE;
  }
  CC_UpdateContinuation(Ptr<ProxyMutex> &m) : Continuation(m) { SET_HANDLER(&CC_UpdateContinuation::file_update_handler); }
};

int
cacheControlFile_CB(const char * /* name ATS_UNUSED */, RecDataT /* data_type ATS_UNUSED */, RecData /* data ATS_UNUSED */,
                    void * /* cookie ATS_UNUSED */)
{
  eventProcessor.schedule_imm(new CC_UpdateContinuation(reconfig_mutex), ET_CACHE);
  return 0;
}

//
//   Begin API functions
//

bool
CacheControl_has_ip_rule()
{
  return CacheControlTable->ipMatch;
}

void
initCacheControl()
{
  ink_assert(CacheControlTable == nullptr);
  reconfig_mutex    = new_ProxyMutex();
  CacheControlTable = new CC_Table("proxy.config.cache.control.filename", MODULE_PREFIX.data(), &http_dest_tags);
  REC_RegisterConfigUpdateFunc("proxy.config.cache.control.filename", cacheControlFile_CB, nullptr);
  if (is_debug_tag_set("cache_control")) {
    CacheControlTable->Print();
  }
}

// void reloadCacheControl()
//
//  Called when the cache.conf file changes.  Since it called
//   infrequently, we do the load of new file as blocking I/O and
//   lock acquire is also blocking
//
void
reloadCacheControl()
{
  Note("cache.config loading ...");

  CC_Table *newTable;

  Debug("cache_control", "cache.config updated, reloading");
  eventProcessor.schedule_in(new CC_FreerContinuation(CacheControlTable), CACHE_CONTROL_TIMEOUT, ET_CACHE);
  newTable = new CC_Table("proxy.config.cache.control.filename", MODULE_PREFIX.data(), &http_dest_tags);
  ink_atomic_swap(&CacheControlTable, newTable);
  if (is_debug_tag_set("cache_control")) {
    CacheControlTable->Print();
  }

  Note("cache.config finished loading");
}

void
getCacheControl(CacheControlResult *result, HttpRequestData *rdata, OverridableHttpConfigParams *h_txn_conf, char *tag)
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
  Debug("cache_control", "reval: %d, never-cache: %d, pin: %d, ignore-c: %d ignore-s: %d, ttl: %d .. %d", result->revalidate_after,
        result->never_cache, result->pin_in_cache_for, result->ignore_client_no_cache, result->ignore_server_no_cache,
        result->ttl_min, result->ttl_max);
}

//
//   End API functions
//

// void CacheControlRecord::Print()
//
//  Debugging Method
//
void
CacheControlRecord::Print()
{
  switch (this->directive) {
  case CC_REVALIDATE_AFTER:
    printf("\t\tDirective: %s : %d\n", CC_directive_str[CC_REVALIDATE_AFTER], this->time_arg);
    break;
  case CC_PIN_IN_CACHE:
    printf("\t\tDirective: %s : %d\n", CC_directive_str[CC_PIN_IN_CACHE], this->time_arg);
    break;
  case CC_TTL_VALUE:
    printf("\t\tDirective: %s : %s %d\n", CC_directive_str[CC_TTL_VALUE], CC_TIME_MODE_TAG[int(this->time_style)], this->time_arg);
    break;
  case CC_IGNORE_CLIENT_NO_CACHE:
  case CC_IGNORE_SERVER_NO_CACHE:
  case CC_NEVER_CACHE:
  case CC_STANDARD_CACHE:
  case CC_IGNORE_NO_CACHE:
    printf("\t\tDirective: %s\n", CC_directive_str[this->directive]);
    break;
  case CC_INVALID:
  case CC_NUM_TYPES:
    printf("\t\tDirective: INVALID\n");
    break;
  }
  if (cache_responses_to_cookies >= 0) {
    printf("\t\t  - %s:%d\n", TWEAK_CACHE_RESPONSES_TO_COOKIES.data(), cache_responses_to_cookies);
  }
  ControlBase::Print();
}

// Result CacheControlRecord::Init(matcher_line* line_info)
//
//    matcher_line* line_info - contains parsed label/value
//      pairs of the current cache.config line
//
//    Returns NULL if everything is OK
//      Otherwise, returns an error string that the caller MUST
//        DEALLOCATE with free()
//
Result
CacheControlRecord::Init(matcher_line *line_info)
{
  int time_in;
  const char *tmp;
  char *label;
  char *val;
  bool d_found = false;

  this->line_num = line_info->line_num;

  // First pass for optional tweaks.
  // This is done because the main loop drops out as soon as a directive is found and anything past
  // that must be a built in modifier. Therefore any non-built in modifier must be handled in this
  // special manner. I beseech you, Great Machine Spirits, bring us YAML real soon now...
  for (int i = 0; i < MATCHER_MAX_TOKENS && line_info->num_el; ++i) {
    bool used = false;
    label     = line_info->line[0][i];
    val       = line_info->line[1][i];
    if (!label) {
      continue;
    }

    if (strcasecmp(label, TWEAK_CACHE_RESPONSES_TO_COOKIES) == 0) {
      ts::TextView tv{val, strlen(val)};
      auto v{ts::svto_radix<10>(tv)};
      if (!tv.empty() || v > 4) {
        return Result::failure("Value for %s must be an integer in the range 0..4", TWEAK_CACHE_RESPONSES_TO_COOKIES.data());
      } else {
        cache_responses_to_cookies = int(v);
      }
      used = true;
    }

    // Clip pair if used.
    if (used) {
      line_info->line[0][i] = nullptr;
      --(line_info->num_el);
    }
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
        directive = CC_NEVER_CACHE;
        d_found   = true;
      } else if (strcasecmp(val, "standard-cache") == 0) {
        directive = CC_STANDARD_CACHE;
        d_found   = true;
      } else if (strcasecmp(val, "ignore-no-cache") == 0) {
        directive = CC_IGNORE_NO_CACHE;
        d_found   = true;
      } else if (strcasecmp(val, "ignore-client-no-cache") == 0) {
        directive = CC_IGNORE_CLIENT_NO_CACHE;
        d_found   = true;
      } else if (strcasecmp(val, "ignore-server-no-cache") == 0) {
        directive = CC_IGNORE_SERVER_NO_CACHE;
        d_found   = true;
      } else {
        return Result::failure("%s Invalid action at line %d in cache.config", MODULE_PREFIX.data(), line_num);
      }
    } else {
      if (strcasecmp(label, "revalidate") == 0) {
        directive = CC_REVALIDATE_AFTER;
        d_found   = true;
      } else if (strcasecmp(label, "pin-in-cache") == 0) {
        directive = CC_PIN_IN_CACHE;
        d_found   = true;
      } else if (strcasecmp(label, "ttl-in-cache") == 0) {
        directive = CC_TTL_VALUE;
        d_found   = true;
        if ('>' == *val) {
          time_style = AT_LEAST;
          ++val;
        } else if ('<' == *val) {
          time_style = AT_MOST;
          ++val;
        } else {
          time_style = EXACTLY;
        }
      }
      // Process the time argument for the remaining directives
      if (d_found == true) {
        if (0 == strcasecmp(DEFAULT_TAG, val)) {
          this->time_arg = CC_UNSET_TIME;
        } else {
          tmp = processDurationString(val, &time_in);
          if (tmp == nullptr) {
            this->time_arg = time_in;

          } else {
            return Result::failure("%s %s at line %d in cache.config", MODULE_PREFIX.data(), tmp, line_num);
          }
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
    return Result::failure("%s No directive in cache.config at line %d", MODULE_PREFIX.data(), line_num);
  }
  // Process any modifiers to the directive, if they exist
  if (line_info->num_el > 0) {
    tmp = ProcessModifiers(line_info);

    if (tmp != nullptr) {
      return Result::failure("%s %s at line %d in cache.config", MODULE_PREFIX.data(), tmp, line_num);
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
  bool match               = false;
  HttpRequestData *h_rdata = (HttpRequestData *)rdata;

  switch (this->directive) {
  case CC_REVALIDATE_AFTER:
    if (this->CheckForMatch(h_rdata, -1) == true) {
      result->revalidate_after = time_arg;
      result->reval_line       = this->line_num;
      match                    = true;
    }
    break;
  case CC_NEVER_CACHE:
    if (this->CheckForMatch(h_rdata, -1) == true) {
      // ttl-in-cache overrides never-cache
      if (!result->has_ttl()) {
        result->never_cache = true;
        result->never_line  = this->line_num;
        match               = true;
      }
    }
    break;
  case CC_STANDARD_CACHE:
    // Standard cache just overrides never-cache
    if (this->CheckForMatch(h_rdata, -1) == true) {
      result->never_cache = false;
      result->never_line  = this->line_num;
      match               = true;
    }
    break;
  case CC_IGNORE_NO_CACHE:
  // We cover both client & server cases for this directive
  //  FALLTHROUGH
  case CC_IGNORE_CLIENT_NO_CACHE:
    if (this->CheckForMatch(h_rdata, -1) == true) {
      result->ignore_client_no_cache = true;
      result->ignore_client_line     = this->line_num;
      match                          = true;
    }
    if (this->directive != CC_IGNORE_NO_CACHE) {
      break;
    }
  // FALLTHROUGH
  case CC_IGNORE_SERVER_NO_CACHE:
    if (this->CheckForMatch(h_rdata, -1) == true) {
      result->ignore_server_no_cache = true;
      result->ignore_server_line     = this->line_num;
      match                          = true;
    }
    break;
  case CC_PIN_IN_CACHE:
    if (this->CheckForMatch(h_rdata, -1) == true) {
      result->pin_in_cache_for = time_arg;
      result->pin_line         = this->line_num;
      match                    = true;
    }
    break;
  case CC_TTL_VALUE:
    if (this->CheckForMatch(h_rdata, -1) == true) {
      if (time_arg == CC_UNSET_TIME) {
        result->ttl_min = result->ttl_max = CC_UNSET_TIME;
      } else {
        // ttl-in-cache overrides never-cache
        result->never_cache = false;
        result->never_line  = this->line_num;
        if (time_style == AT_LEAST || time_style == EXACTLY) {
          result->ttl_min = time_arg;
        }
        if (time_style == AT_MOST || time_style == EXACTLY) {
          result->ttl_max = time_arg;
        }
      }
      result->ttl_line = this->line_num;
      match            = true;
    }
    break;
  case CC_INVALID:
  case CC_NUM_TYPES:
  default:
    // Should not get here
    Warning("Impossible directive in CacheControlRecord::UpdateMatch");
    ink_assert(0);
    break;
  }

  if (cache_responses_to_cookies >= 0) {
    result->cache_responses_to_cookies = cache_responses_to_cookies;
  }

  if (match == true && is_debug_tag_set("cache_control")) {
    ts::LocalBufferWriter<256> bw;
    bw.print("Matched '{}' at line {}", CC_directive_str[this->directive], this->line_num);
    if (result->cache_responses_to_cookies >= 0) {
      bw.print(" [{}={:s}]", TWEAK_CACHE_RESPONSES_TO_COOKIES, result->cache_responses_to_cookies);
    }
    Debug("cache_control", "%.*s", int(bw.size()), bw.data());
  }
}
