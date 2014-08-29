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
 *  CacheControl.cc - Implementation to Cache Control systtem
 *
 *
 ****************************************************************************/

#include "ink_config.h"

#include <sys/types.h>

#ifdef HAVE_PCRE_PCRE_H
#include <pcre/pcre.h>
#else
#include <pcre.h>
#endif

#include "CacheControl.h"
#include "ControlMatcher.h"
#include "Main.h"
#include "Error.h"
#include "P_EventSystem.h"
#include "ProxyConfig.h"
#include "HTTP.h"
#include "HttpConfig.h"
#include "StatSystem.h"
#include "P_Cache.h"

static const char modulePrefix[] = "[CacheControl]";

# define TWEAK_CACHE_RESPONSES_TO_COOKIES "cache-responses-to-cookies"

static const char *CC_directive_str[CC_NUM_TYPES] = {
  "INVALID",
  "REVALIDATE_AFTER",
  "NEVER_CACHE",
  "STANDARD_CACHE",
  "IGNORE_NO_CACHE",
  "CLUSTER_CACHE_LOCAL",
  "IGNORE_CLIENT_NO_CACHE",
  "IGNORE_SERVER_NO_CACHE",
  "PIN_IN_CACHE",
  "TTL_IN_CACHE"
 // "CACHE_AUTH_CONTENT"
};

typedef ControlMatcher<CacheControlRecord, CacheControlResult> CC_table;

// Global Ptrs
static Ptr<ProxyMutex> reconfig_mutex;
CC_table *CacheControlTable = NULL;

void
CC_delete_table()
{
  delete CacheControlTable;
}

// struct CC_FreerContinuation
// Continuation to free old cache control lists after
//  a timeout
//
struct CC_FreerContinuation;
typedef int (CC_FreerContinuation::*CC_FreerContHandler) (int, void *);
struct CC_FreerContinuation: public Continuation
{
  CC_table *p;
  int freeEvent(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
  {
    Debug("cache_control", "Deleting old table");
    delete p;
    delete this;
    return EVENT_DONE;
  }
  CC_FreerContinuation(CC_table * ap):Continuation(NULL), p(ap)
  {
    SET_HANDLER((CC_FreerContHandler) & CC_FreerContinuation::freeEvent);
  }
};


// struct CC_UpdateContinuation
//
//   Used to read the cache.conf file after the manager signals
//      a change
//
struct CC_UpdateContinuation: public Continuation
{
  int file_update_handler(int /* etype ATS_UNUSED */, void * /* data ATS_UNUSED */)
  {
    reloadCacheControl();
    delete this;
    return EVENT_DONE;
  }
  CC_UpdateContinuation(ProxyMutex * m):Continuation(m)
  {
    SET_HANDLER(&CC_UpdateContinuation::file_update_handler);
  }
};

int
cacheControlFile_CB(const char * /* name ATS_UNUSED */, RecDataT /* data_type ATS_UNUSED */,
                    RecData /* data ATS_UNUSED */, void * /* cookie ATS_UNUSED */)
{
  eventProcessor.schedule_imm(new CC_UpdateContinuation(reconfig_mutex), ET_CACHE);
  return 0;
}

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
  ink_assert(CacheControlTable == NULL);
  reconfig_mutex = new_ProxyMutex();
  CacheControlTable = new CC_table("proxy.config.cache.control.filename", modulePrefix, &http_dest_tags);
  REC_RegisterConfigUpdateFunc("proxy.config.cache.control.filename", cacheControlFile_CB, NULL);
}

// void reloadCacheControl()
//
//  Called when the cache.conf file changes.  Since it called
//   infrequently, we do the load of new file as blocking I/O and
//   lock aquire is also blocking
//
void
reloadCacheControl()
{
  CC_table *newTable;

  Debug("cache_control", "cache.config updated, reloading");
  eventProcessor.schedule_in(new CC_FreerContinuation(CacheControlTable), CACHE_CONTROL_TIMEOUT, ET_CACHE);
  newTable = new CC_table("proxy.config.cache.control.filename", modulePrefix, &http_dest_tags);
  ink_atomic_swap(&CacheControlTable, newTable);
}

void
getCacheControl(CacheControlResult *result, HttpRequestData *rdata, OverridableHttpConfigParams *h_txn_conf, char *tag)
{
  rdata->tag = tag;
  CacheControlTable->Match(rdata, result);

  if (h_txn_conf->cache_cluster_cache_local) {
    result->cluster_cache_local = true;
  }

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

bool 
getClusterCacheLocal(URL *url, char * /* hostname ATS_UNUSED */)
{
  HttpRequestData rdata;
  CacheControlResult result;
  HTTPHdr req_hdr;  

  req_hdr.create(HTTP_TYPE_REQUEST, NULL);
  req_hdr.url_set(url);
  rdata.hdr = &req_hdr;
  CacheControlTable->Match(&rdata, &result);
  req_hdr.clear();
  return result.cluster_cache_local;
}

//
//   End API functions
//


// void CacheControlResult::Print()
//
//  Debugging Method
//
void
CacheControlResult::Print()
{
  printf("\t reval: %d, never-cache: %d, pin: %d, cluster-cache-c: %d ignore-c: %d ignore-s: %d\n",
         revalidate_after, never_cache, pin_in_cache_for, cluster_cache_local, ignore_client_no_cache,
         ignore_server_no_cache);
}

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
  case CC_TTL_IN_CACHE:
    printf("\t\tDirective: %s : %d\n", CC_directive_str[CC_TTL_IN_CACHE], this->time_arg);
    break;
  case CC_CLUSTER_CACHE_LOCAL:
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
  if (cache_responses_to_cookies >= 0)
    printf("\t\t  - " TWEAK_CACHE_RESPONSES_TO_COOKIES ":%d\n",
      cache_responses_to_cookies
    );
  ControlBase::Print();
}

// bool CacheControlRecord::Init(matcher_line* line_info)
//
//    matcher_line* line_info - contains parsed label/value
//      pairs of the current cache.config line
//
//    Returns NULL if everything is OK
//      Otherwise, returns an error string that the caller MUST
//        DEALLOCATE with free()
//
char *
CacheControlRecord::Init(matcher_line * line_info)
{
  int time_in;
  char *errBuf;
  const int errBufLen = 1024;
  const char *tmp;
  char *label;
  char *val;
  bool d_found = false;

  this->line_num = line_info->line_num;

  // First pass for optional tweaks.
  for (int i = 0; i < MATCHER_MAX_TOKENS && line_info->num_el ; ++i) {
    bool used = false;
    label = line_info->line[0][i];
    val = line_info->line[1][i];
    if (!label) continue;

    if (strcasecmp(label, TWEAK_CACHE_RESPONSES_TO_COOKIES) == 0) {
      char* ptr = 0;
      int v = strtol(val, &ptr, 0);
      if (!ptr || v < 0 || v > 4) {
        errBuf = static_cast<char*>(ats_malloc(errBufLen * sizeof(char)));
        snprintf(errBuf, errBufLen, "Value for " TWEAK_CACHE_RESPONSES_TO_COOKIES
                 " must be an integer in the range 0..4");
        return errBuf;
      } else {
        cache_responses_to_cookies = v;
      }
      used = true;
    }

    // Clip pair if used.
    if (used) {
      line_info->line[0][i] = 0;
      --(line_info->num_el);
    }
  }

  // Now look for the directive.
  for (int i = 0; i < MATCHER_MAX_TOKENS; i++) {
    label = line_info->line[0][i];
    val = line_info->line[1][i];

    if (label == NULL) {
      continue;
    }

    if (strcasecmp(label, "action") == 0) {
      if (strcasecmp(val, "never-cache") == 0) {
        directive = CC_NEVER_CACHE;
        d_found = true;
      } else if (strcasecmp(val, "standard-cache") == 0) {
        directive = CC_STANDARD_CACHE;
        d_found = true;
      } else if (strcasecmp(val, "ignore-no-cache") == 0) {
        directive = CC_IGNORE_NO_CACHE;
        d_found = true;
      } else if (strcasecmp(val, "cluster-cache-local") == 0) {
        directive = CC_CLUSTER_CACHE_LOCAL;;
        d_found = true;
      } else if (strcasecmp(val, "ignore-client-no-cache") == 0) {
        directive = CC_IGNORE_CLIENT_NO_CACHE;
        d_found = true;
      } else if (strcasecmp(val, "ignore-server-no-cache") == 0) {
        directive = CC_IGNORE_SERVER_NO_CACHE;
        d_found = true;
      } else {
        errBuf = (char *)ats_malloc(errBufLen * sizeof(char));
        snprintf(errBuf, errBufLen, "%s Invalid action at line %d in cache.config", modulePrefix, line_num);
        return errBuf;
      }
    } else {

      if (strcasecmp(label, "revalidate") == 0) {
        directive = CC_REVALIDATE_AFTER;
        d_found = true;
      } else if (strcasecmp(label, "pin-in-cache") == 0) {
        directive = CC_PIN_IN_CACHE;
        d_found = true;
      } else if (strcasecmp(label, "ttl-in-cache") == 0) {
        directive = CC_TTL_IN_CACHE;
        d_found = true;
      }
      // Process the time argument for the remaining directives
      if (d_found == true) {
        tmp = processDurationString(val, &time_in);
        if (tmp == NULL) {
          this->time_arg = time_in;

        } else {
          errBuf = (char *)ats_malloc(errBufLen * sizeof(char));
          snprintf(errBuf, errBufLen, "%s %s at line %d in cache.config", modulePrefix, tmp, line_num);
          return errBuf;
        }
      }
    }

    if (d_found == true) {
      // Consume the label/value pair we used
      line_info->line[0][i] = NULL;
      line_info->num_el--;
      break;
    }
  }

  if (d_found == false) {
    errBuf = (char *)ats_malloc(errBufLen * sizeof(char));
    snprintf(errBuf, errBufLen, "%s No directive in cache.config at line %d", modulePrefix, line_num);
    return errBuf;
  }
  // Process any modifiers to the directive, if they exist
  if (line_info->num_el > 0) {
    tmp = ProcessModifiers(line_info);

    if (tmp != NULL) {
      errBuf = (char *)ats_malloc(errBufLen * sizeof(char));
      snprintf(errBuf, errBufLen, "%s %s at line %d in cache.config", modulePrefix, tmp, line_num);
      return errBuf;
    }
  }

  return NULL;
}

// void CacheControlRecord::UpdateMatch(CacheControlResult* result, RequestData* rdata)
//
//    Updates the parameters in result if the this element
//     appears later in the file
//
void
CacheControlRecord::UpdateMatch(CacheControlResult * result, RequestData * rdata)
{
  bool match = false;
  HttpRequestData *h_rdata = (HttpRequestData *) rdata;

  switch (this->directive) {
  case CC_REVALIDATE_AFTER:
    if (this->CheckForMatch(h_rdata, result->reval_line) == true) {
      result->revalidate_after = time_arg;
      result->reval_line = this->line_num;
      match = true;
    }
    break;
  case CC_NEVER_CACHE:
    if (this->CheckForMatch(h_rdata, result->never_line) == true) {
      result->never_cache = true;
      result->never_line = this->line_num;
      match = true;
    }
    break;
  case CC_STANDARD_CACHE:
    // Standard cache just overrides never-cache
    if (this->CheckForMatch(h_rdata, result->never_line) == true) {
      result->never_cache = false;
      result->never_line = this->line_num;
      match = true;
    }
    break;
  case CC_IGNORE_NO_CACHE:
    // We cover both client & server cases for this directive
    //  FALLTHROUGH
  case CC_IGNORE_CLIENT_NO_CACHE:
    if (this->CheckForMatch(h_rdata, result->ignore_client_line) == true) {
      result->ignore_client_no_cache = true;
      result->ignore_client_line = this->line_num;
      match = true;
    }
    if (this->directive != CC_IGNORE_NO_CACHE) {
      break;
    }
    // FALLTHROUGH
  case CC_IGNORE_SERVER_NO_CACHE:
    if (this->CheckForMatch(h_rdata, result->ignore_server_line) == true) {
      result->ignore_server_no_cache = true;
      result->ignore_server_line = this->line_num;
      match = true;
    }
    break;
  case CC_CLUSTER_CACHE_LOCAL:
    if (this->CheckForMatch(h_rdata, result->cluster_cache_local_line) == true) {
      result->cluster_cache_local = true;
      result->cluster_cache_local_line = this->line_num;
      match = true;
    }
    break;
  case CC_PIN_IN_CACHE:
    if (this->CheckForMatch(h_rdata, result->pin_line) == true) {
      result->pin_in_cache_for = time_arg;
      result->pin_line = this->line_num;
      match = true;
    }
    break;
  case CC_TTL_IN_CACHE:
    if (this->CheckForMatch(h_rdata, result->ttl_line) == true) {
      result->ttl_in_cache = time_arg;
      result->ttl_line = this->line_num;
      // ttl-in-cache overrides never-cache
      result->never_cache = false;
      result->never_line = this->line_num;
      match = true;
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

  if (cache_responses_to_cookies >= 0)
    result->cache_responses_to_cookies = cache_responses_to_cookies;

  if (match == true) {
    char crtc_debug[80];
    if (result->cache_responses_to_cookies >= 0)
      snprintf(crtc_debug, sizeof(crtc_debug), " [" TWEAK_CACHE_RESPONSES_TO_COOKIES "=%d]",
               result->cache_responses_to_cookies);
    else
      crtc_debug[0] = 0;
      
    Debug("cache_control", "Matched with for %s at line %d%s", CC_directive_str[this->directive],
          this->line_num, crtc_debug);
  }
}
