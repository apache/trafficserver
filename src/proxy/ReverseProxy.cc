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
#include "proxy/http/remap/UrlMappingPathIndex.h"

namespace
{
Ptr<ProxyMutex> reconfig_mutex;

DbgCtl dbg_ctl_url_rewrite{"url_rewrite"};

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

  RecRegisterConfigUpdateCb("proxy.config.url_remap.filename", url_rewrite_CB, (void *)FILE_CHANGED);
  RecRegisterConfigUpdateCb("proxy.config.proxy_name", url_rewrite_CB, (void *)TSNAME_CHANGED);
  RecRegisterConfigUpdateCb("proxy.config.reverse_proxy.enabled", url_rewrite_CB, (void *)REVERSE_CHANGED);
  RecRegisterConfigUpdateCb("proxy.config.http.referer_default_redirect", url_rewrite_CB, (void *)HTTP_DEFAULT_REDIRECT_CHANGED);

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

/** Used to read the remap.config file after the manager signals a change. */
struct UR_UpdateContinuation : public Continuation {
  int
  file_update_handler(int /* etype ATS_UNUSED */, void * /* data ATS_UNUSED */)
  {
    static_cast<void>(reloadUrlRewrite());
    delete this;
    return EVENT_DONE;
  }
  UR_UpdateContinuation(Ptr<ProxyMutex> &m) : Continuation(m) { SET_HANDLER(&UR_UpdateContinuation::file_update_handler); }
};

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
reloadUrlRewrite()
{
  UrlRewrite *newTable, *oldTable;

  Note("%s loading ...", ts::filename::REMAP);
  Dbg(dbg_ctl_url_rewrite, "%s updated, reloading...", ts::filename::REMAP);
  newTable = new UrlRewrite();
  if (newTable->load()) {
    static const char *msg_format = "%s finished loading";

    // Hold at least one lease, until we reload the configuration
    newTable->acquire();

    // Swap configurations
    oldTable = ink_atomic_swap(&rewrite_table, newTable);

    ink_assert(oldTable != nullptr);

    // Release the old one
    oldTable->release();

    Dbg(dbg_ctl_url_rewrite, msg_format, ts::filename::REMAP);
    Note(msg_format, ts::filename::REMAP);
    return true;
  } else {
    static const char *msg_format = "%s failed to load";

    delete newTable;
    Dbg(dbg_ctl_url_rewrite, msg_format, ts::filename::REMAP);
    Error(msg_format, ts::filename::REMAP);
    return false;
  }
}

/**
 * Helper function to initialize volume_host_rec for a single url_mapping.
 * This is a no-op if the mapping has no volume string or is already initialized.
 */
static void
init_mapping_volume_host_rec(url_mapping &mapping)
{
  char errbuf[256];

  if (!mapping.initVolumeHostRec(errbuf, sizeof(errbuf))) {
    Error("Failed to initialize volume record for @volume=%s: %s", mapping.getVolume().c_str(), errbuf);
  }
}

static void
init_store_volume_host_records(UrlRewrite::MappingsStore &store)
{
  if (store.hash_lookup) {
    for (auto &entry : *store.hash_lookup) {
      UrlMappingPathIndex *path_index = entry.second;

      if (path_index) {
        path_index->foreach_mapping(init_mapping_volume_host_rec);
      }
    }
  }

  for (UrlRewrite::RegexMapping *reg_map = store.regex_list.head; reg_map; reg_map = reg_map->link.next) {
    if (reg_map->url_map) {
      init_mapping_volume_host_rec(*reg_map->url_map);
    }
  }
}

// This is called after the cache is initialized, since we may need the volume_host_records
void
init_remap_volume_host_records()
{
  UrlRewrite *table = rewrite_table;

  if (!table) {
    return;
  }

  Dbg(dbg_ctl_url_rewrite, "Initializing volume_host_rec for all remap rules after cache init");

  // Initialize for all mapping stores
  init_store_volume_host_records(table->forward_mappings);
  init_store_volume_host_records(table->reverse_mappings);
  init_store_volume_host_records(table->permanent_redirects);
  init_store_volume_host_records(table->temporary_redirects);
  init_store_volume_host_records(table->forward_mappings_with_recv_port);
}

int
url_rewrite_CB(const char * /* name ATS_UNUSED */, RecDataT /* data_type ATS_UNUSED */, RecData data, void *cookie)
{
  int my_token = static_cast<int>((long)cookie);

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
