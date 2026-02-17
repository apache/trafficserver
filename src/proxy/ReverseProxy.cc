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
#include "tscore/Filenames.h"
#include <dlfcn.h>
#include "iocore/eventsystem/ConfigProcessor.h"
#include "proxy/ReverseProxy.h"
#include "tscore/MatcherUtils.h"
#include "tscore/Tokenizer.h"
#include "ts/remap.h"
#include "proxy/http/remap/RemapPluginInfo.h"
#include "proxy/http/remap/RemapProcessor.h"
#include "proxy/http/remap/UrlRewrite.h"
#include "proxy/http/remap/UrlMapping.h"

namespace
{
Ptr<ProxyMutex> reconfig_mutex;

DbgCtl dbg_ctl_url_rewrite{"url_rewrite"};

struct URLRewriteReconfigure {
  static void reconfigure([[maybe_unused]] ConfigContext ctx);
};

std::unique_ptr<ConfigUpdateHandler<URLRewriteReconfigure>> url_rewrite_reconf;
} // end anonymous namespace

// Global Ptrs
UrlRewrite                       *rewrite_table       = nullptr;
thread_local PluginThreadContext *pluginThreadContext = nullptr;

// Tokens for the Callback function
#define FILE_CHANGED                  0
#define REVERSE_CHANGED               1
#define TSNAME_CHANGED                2
#define TRANS_CHANGED                 4
#define URL_REMAP_MODE_CHANGED        8
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

  Note("%s loading ...", ts::filename::REMAP);
  if (!rewrite_table->load()) {
    Emergency("%s failed to load", ts::filename::REMAP);
  } else {
    Note("%s finished loading", ts::filename::REMAP);
  }

  RecRegisterConfigUpdateCb("proxy.config.reverse_proxy.enabled", url_rewrite_CB, (void *)REVERSE_CHANGED);

  // reload hooks
  url_rewrite_reconf.reset(new ConfigUpdateHandler<URLRewriteReconfigure>("Url Rewrite Config"));
  url_rewrite_reconf->attach("proxy.config.url_remap.filename");
  url_rewrite_reconf->attach("proxy.config.proxy_name");
  url_rewrite_reconf->attach("proxy.config.http.referer_default_redirect");

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
  return table ? table->Remap_redirect(request_header, redirect_url) : mapping_type::NONE;
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

bool
urlRewriteVerify()
{
  return UrlRewrite().load();
}

/**
  Called when the remap.config file changes. Since it called infrequently,
  we do the load of new file as blocking I/O and lock acquire is also
  blocking.

*/
bool
reloadUrlRewrite([[maybe_unused]] ConfigContext ctx)
{
  std::string msg_buffer;
  msg_buffer.reserve(1024);
  UrlRewrite *newTable, *oldTable;

  Note("%s loading ...", ts::filename::REMAP);
  Dbg(dbg_ctl_url_rewrite, "%s updated, reloading...", ts::filename::REMAP);
  newTable = new UrlRewrite();
  if (newTable->load()) {
    swoc::bwprint(msg_buffer, "{} finished loading", ts::filename::REMAP);

    // Hold at least one lease, until we reload the configuration
    newTable->acquire();

    // Swap configurations
    oldTable = ink_atomic_swap(&rewrite_table, newTable);

    ink_assert(oldTable != nullptr);

    // Release the old one
    oldTable->release();

    Dbg(dbg_ctl_url_rewrite, "%s", msg_buffer.c_str());
    Note(msg_buffer.c_str());
    return true;
  } else {
    swoc::bwprint(msg_buffer, "{} failed to load", ts::filename::REMAP);

    delete newTable;
    Dbg(dbg_ctl_url_rewrite, "%s", msg_buffer.c_str());
    Error(msg_buffer.c_str());
    return false;
  }
}

int
url_rewrite_CB(const char * /* name ATS_UNUSED */, RecDataT /* data_type ATS_UNUSED */, RecData data,
               void * /* cookie ATS_UNUSED */)
{
  rewrite_table->SetReverseFlag(data.rec_int);
  return 0;
}

void
URLRewriteReconfigure::reconfigure([[maybe_unused]] ConfigContext ctx)
{
  reloadUrlRewrite(ctx);
}
