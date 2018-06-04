/** @file

  Definitions for reverse proxy

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

  @section details Details

  Implements code necessary for Reverse Proxy which mostly consists of
  general purpose hostname substitution in URLs.

 */

#include "tscore/ink_platform.h"
#include <dlfcn.h>
#include "P_EventSystem.h"
#include "P_Cache.h"
#include "ProxyConfig.h"
#include "ReverseProxy.h"
#include "tscore/MatcherUtils.h"
#include "tscore/Tokenizer.h"
#include "ts/remap.h"
#include "RemapPluginInfo.h"
#include "RemapProcessor.h"
#include "UrlRewrite.h"
#include "UrlMapping.h"

// Global Ptrs
static Ptr<ProxyMutex> reconfig_mutex;
UrlRewrite *rewrite_table        = nullptr;
remap_plugin_info *remap_pi_list = nullptr; // We never reload the remap plugins, just append to 'em.

// Tokens for the Callback function
#define FILE_CHANGED 0
#define REVERSE_CHANGED 1
#define TSNAME_CHANGED 2
#define TRANS_CHANGED 4
#define URL_REMAP_MODE_CHANGED 8
#define HTTP_DEFAULT_REDIRECT_CHANGED 9

//
// Begin API Functions
//
int
init_reverse_proxy()
{
  ink_assert(rewrite_table == nullptr);
  reconfig_mutex = new_ProxyMutex();
  rewrite_table  = new UrlRewrite();

  Note("remap.config loading ...");
  if (!rewrite_table->is_valid()) {
    Fatal("remap.config failed to load");
  }
  Note("remap.config finished loading");

  REC_RegisterConfigUpdateFunc("proxy.config.url_remap.filename", url_rewrite_CB, (void *)FILE_CHANGED);
  REC_RegisterConfigUpdateFunc("proxy.config.proxy_name", url_rewrite_CB, (void *)TSNAME_CHANGED);
  REC_RegisterConfigUpdateFunc("proxy.config.reverse_proxy.enabled", url_rewrite_CB, (void *)REVERSE_CHANGED);
  REC_RegisterConfigUpdateFunc("proxy.config.http.referer_default_redirect", url_rewrite_CB, (void *)HTTP_DEFAULT_REDIRECT_CHANGED);

  // Hold at least one lease, until we reload the configuration
  rewrite_table->acquire();

  return 0;
}

/**
   This function is used to figure out if a URL needs to be remapped
   according to the rules in remap.config.
*/
mapping_type
request_url_remap_redirect(HTTPHdr *request_header, URL *redirect_url, UrlRewrite *table)
{
  return table ? table->Remap_redirect(request_header, redirect_url) : NONE;
}

bool
response_url_remap(HTTPHdr *response_header, UrlRewrite *table)
{
  return table ? table->ReverseMap(response_header) : false;
}

//
//
//  End API Functions
//

/** Used to read the remap.config file after the manager signals a change. */
struct UR_UpdateContinuation;
using UR_UpdContHandler = int (UR_UpdateContinuation::*)(int, void *);
struct UR_UpdateContinuation : public Continuation {
  int
  file_update_handler(int /* etype ATS_UNUSED */, void * /* data ATS_UNUSED */)
  {
    (void)reloadUrlRewrite();
    delete this;
    return EVENT_DONE;
  }
  UR_UpdateContinuation(Ptr<ProxyMutex> &m) : Continuation(m)
  {
    SET_HANDLER((UR_UpdContHandler)&UR_UpdateContinuation::file_update_handler);
  }
};

bool
urlRewriteVerify()
{
  return UrlRewrite().is_valid();
}

/**
  Called when the remap.config file changes. Since it called infrequently,
  we do the load of new file as blocking I/O and lock aquire is also
  blocking.

*/
bool
reloadUrlRewrite()
{
  UrlRewrite *newTable, *oldTable;

  Note("remap.config loading ...");
  Debug("url_rewrite", "remap.config updated, reloading...");
  newTable = new UrlRewrite();
  if (newTable->is_valid()) {
    static const char *msg = "remap.config finished loading";

    // Hold at least one lease, until we reload the configuration
    newTable->acquire();

    // Swap configurations
    oldTable = ink_atomic_swap(&rewrite_table, newTable);

    ink_assert(oldTable != nullptr);

    // Release the old one
    oldTable->release();

    Debug("url_rewrite", "%s", msg);
    Note("%s", msg);
    return true;
  } else {
    static const char *msg = "remap.config failed to load";

    delete newTable;
    Debug("url_rewrite", "%s", msg);
    Error("%s", msg);
    return false;
  }
}

int
url_rewrite_CB(const char * /* name ATS_UNUSED */, RecDataT /* data_type ATS_UNUSED */, RecData data, void *cookie)
{
  int my_token = (int)(long)cookie;

  switch (my_token) {
  case REVERSE_CHANGED:
    rewrite_table->SetReverseFlag(data.rec_int);
    break;

  case TSNAME_CHANGED:
  case FILE_CHANGED:
  case HTTP_DEFAULT_REDIRECT_CHANGED:
    eventProcessor.schedule_imm(new UR_UpdateContinuation(reconfig_mutex), ET_TASK);
    break;

  case URL_REMAP_MODE_CHANGED:
    // You need to restart TS.
    break;

  default:
    ink_assert(0);
    break;
  }

  return 0;
}
