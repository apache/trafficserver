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

#include "libts.h"
#include <dlfcn.h>
#include "Main.h"
#include "Error.h"
#include "P_EventSystem.h"
#include "StatSystem.h"
#include "P_Cache.h"
#include "ProxyConfig.h"
#include "ReverseProxy.h"
#include "MatcherUtils.h"
#include "Tokenizer.h"
#include "api/ts/remap.h"
#include "RemapPluginInfo.h"
#include "RemapProcessor.h"
#include "UrlRewrite.h"
#include "UrlMapping.h"
#include "ink_unused.h"      /* MAGIC_EDITING_TAG */

/** Time till we free the old stuff after a reconfiguration. */
#define URL_REWRITE_TIMEOUT            (HRTIME_SECOND*60)

// Global Ptrs
static Ptr<ProxyMutex> reconfig_mutex = NULL;
UrlRewrite *rewrite_table = NULL;
remap_plugin_info *remap_pi_list; // We never reload the remap plugins, just append to 'em.

// Tokens for the Callback function
#define FILE_CHANGED 0
#define REVERSE_CHANGED 1
#define TSNAME_CHANGED 2
#define AC_PORT_CHANGED 3
#define TRANS_CHANGED 4
#define DEFAULT_TO_PAC_CHANGED 5
#define DEFAULT_TO_PAC_PORT_CHANGED 7
#define URL_REMAP_MODE_CHANGED 8
#define HTTP_DEFAULT_REDIRECT_CHANGED 9

int url_remap_mode;

//
// Begin API Functions
//

int
init_reverse_proxy()
{
  ink_assert(rewrite_table == NULL);
  reconfig_mutex = new_ProxyMutex();
  rewrite_table = NEW(new UrlRewrite("proxy.config.url_remap.filename"));

  if (!rewrite_table->is_valid()) {
    Warning("Can not load the remap table, exiting out!");
    // TODO: For now, I _exit() out of here, because otherwise we'll keep generating
    // core files (if enabled) when starting up with a bad remap.config file.
    _exit(-1);
  }

  REVERSE_RegisterConfigUpdateFunc("proxy.config.url_remap.filename", url_rewrite_CB, (void *) FILE_CHANGED);
  REVERSE_RegisterConfigUpdateFunc("proxy.config.proxy_name", url_rewrite_CB, (void *) TSNAME_CHANGED);
  REVERSE_RegisterConfigUpdateFunc("proxy.config.reverse_proxy.enabled", url_rewrite_CB, (void *) REVERSE_CHANGED);
  REVERSE_RegisterConfigUpdateFunc("proxy.config.admin.autoconf_port", url_rewrite_CB, (void *) AC_PORT_CHANGED);
  REVERSE_RegisterConfigUpdateFunc("proxy.config.url_remap.default_to_server_pac", url_rewrite_CB, (void *) DEFAULT_TO_PAC_CHANGED);
  REVERSE_RegisterConfigUpdateFunc("proxy.config.url_remap.default_to_server_pac_port", url_rewrite_CB, (void *) DEFAULT_TO_PAC_PORT_CHANGED);
  REVERSE_RegisterConfigUpdateFunc("proxy.config.url_remap.url_remap_mode", url_rewrite_CB, (void *) URL_REMAP_MODE_CHANGED);
  REVERSE_RegisterConfigUpdateFunc("proxy.config.http.referer_default_redirect", url_rewrite_CB, (void *) HTTP_DEFAULT_REDIRECT_CHANGED);
  return 0;
}

// TODO: This function needs to be rewritten (or replaced) with something that uses the new
// Remap Processor properly. Right now, we effectively don't support "remap" rules on a few
// odd ball configs, for example if you use the "CONNECT" method, or if you set
// set proxy.config.url_remap.url_remap_mode to "2" (which is a completely undocumented "feature").
bool
request_url_remap(HttpTransact::State * s, HTTPHdr * request_header, char **redirect_url, unsigned int filter_mask)
{
  NOWARN_UNUSED(s);
  NOWARN_UNUSED(request_header);
  NOWARN_UNUSED(redirect_url);
  NOWARN_UNUSED(filter_mask);
  return false;
  // return rewrite_table ? rewrite_table->Remap(s, request_header, redirect_url, orig_url, tag, filter_mask) : false;
}

/**
   This function is used to figure out if a URL needs to be remapped
   according to the rules in remap.config.
*/
mapping_type
request_url_remap_redirect(HTTPHdr *request_header, URL *redirect_url)
{
  return rewrite_table ? rewrite_table->Remap_redirect(request_header, redirect_url) : NONE;
}

bool
response_url_remap(HTTPHdr *response_header)
{
  return rewrite_table ? rewrite_table->ReverseMap(response_header) : false;
}
 

//
//
//  End API Functions
//

/** Used to read the remap.config file after the manager signals a change. */
struct UR_UpdateContinuation;
typedef int (UR_UpdateContinuation::*UR_UpdContHandler) (int, void *);
struct UR_UpdateContinuation: public Continuation
{
  int file_update_handler(int etype, void *data)
  {
    NOWARN_UNUSED(etype);
    NOWARN_UNUSED(data);

    reloadUrlRewrite();
    delete this;
    return EVENT_DONE;
  }
  UR_UpdateContinuation(ProxyMutex * m)
    : Continuation(m)
  {
    SET_HANDLER((UR_UpdContHandler) & UR_UpdateContinuation::file_update_handler);
  }
};

struct UR_FreerContinuation;
typedef int (UR_FreerContinuation::*UR_FreerContHandler) (int, void *);

/** Used to free url rewrite class. */
struct UR_FreerContinuation: public Continuation
{
  UrlRewrite *p;
  int freeEvent(int event, Event * e)
  {
    NOWARN_UNUSED(event);
    NOWARN_UNUSED(e);
    Debug("url_rewrite", "Deleting old remap.config table");
    delete p;
    delete this;
    return EVENT_DONE;
  }
  UR_FreerContinuation(UrlRewrite * ap):Continuation(new_ProxyMutex()), p(ap)
  {
    SET_HANDLER((UR_FreerContHandler) & UR_FreerContinuation::freeEvent);
  }
};

/**
  Called when the remap.config file changes. Since it called infrequently,
  we do the load of new file as blocking I/O and lock aquire is also
  blocking.

*/
void
reloadUrlRewrite()
{
  UrlRewrite *newTable;

  Debug("url_rewrite", "remap.config updated, reloading...");
  newTable = new UrlRewrite("proxy.config.url_remap.filename");
  if (newTable->is_valid()) {
    eventProcessor.schedule_in(new UR_FreerContinuation(rewrite_table), URL_REWRITE_TIMEOUT, ET_TASK);
    Debug("url_rewrite", "remap.config done reloading!");
    ink_atomic_swap(&rewrite_table, newTable);
  } else {
    static const char* msg = "failed to reload remap.config, not replacing!";
    delete newTable;
    Debug("url_rewrite", "%s", msg);
    Warning("%s", msg);
  }
}

int
url_rewrite_CB(const char *name, RecDataT data_type, RecData data, void *cookie)
{
  NOWARN_UNUSED(name);
  NOWARN_UNUSED(data_type);
  int my_token = (int) (long) cookie;

  switch (my_token) {
  case REVERSE_CHANGED:
    rewrite_table->SetReverseFlag(data.rec_int);
    break;

  case TSNAME_CHANGED:
  case DEFAULT_TO_PAC_CHANGED:
  case DEFAULT_TO_PAC_PORT_CHANGED:
  case FILE_CHANGED:
  case HTTP_DEFAULT_REDIRECT_CHANGED:
    eventProcessor.schedule_imm(NEW(new UR_UpdateContinuation(reconfig_mutex)), ET_TASK);
    break;

  case AC_PORT_CHANGED:
    // The AutoConf port does not current change on manager except at restart
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

