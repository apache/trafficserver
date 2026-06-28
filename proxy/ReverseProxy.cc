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
#include "tscore/TSSystemState.h"
#include <dlfcn.h>
#include "P_EventSystem.h"
#include "P_Cache.h"
#include "P_Freer.h"
#include "ProxyConfig.h"
#include "ReverseProxy.h"
#include "tscore/MatcherUtils.h"
#include "tscore/Tokenizer.h"
#include "ts/remap.h"
#include "RemapPluginInfo.h"
#include "RemapProcessor.h"
#include "UrlRewrite.h"
#include "UrlMapping.h"

namespace
{
// Steers UrlRewriteDeleter to inline-delete; see shutdown_url_rewrite().
std::atomic<bool> rewrite_table_shutdown{false};

// Defer teardown to ET_TASK; UrlRewrite destruction can be slow.
struct UrlRewriteDeleter {
  void
  operator()(UrlRewrite *p) const noexcept
  {
    if (!p) {
      return;
    }
    if (rewrite_table_shutdown.load(std::memory_order_acquire) || TSSystemState::is_event_system_shut_down()) {
      // Leak; plugin teardown is unsafe post-shutdown.
      return;
    }
    // new_Deleter allocates; fall back to inline delete so we don't escape noexcept.
    try {
      new_Deleter(p, 0);
    } catch (...) {
      delete p;
    }
  }
};

} // end anonymous namespace

// Global Ptrs
static Ptr<ProxyMutex> reconfig_mutex;
AtomicSharedPtr<UrlRewrite> rewrite_table;
thread_local PluginThreadContext *pluginThreadContext = nullptr;

void
shutdown_url_rewrite()
{
  // Drain before flag: this ref destructs normally; later drops leak.
  rewrite_table.exchange(nullptr);
  rewrite_table_shutdown.store(true, std::memory_order_release);
}

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
  ink_assert(rewrite_table.load(std::memory_order_acquire) == nullptr);
  reconfig_mutex     = new_ProxyMutex();
  auto initial_table = std::make_unique<UrlRewrite>();

  Note("%s loading ...", ts::filename::REMAP);
  if (!initial_table->load()) {
    Fatal("%s failed to load", ts::filename::REMAP);
  }
  Note("%s finished loading", ts::filename::REMAP);

  REC_RegisterConfigUpdateFunc("proxy.config.url_remap.filename", url_rewrite_CB, (void *)FILE_CHANGED);
  REC_RegisterConfigUpdateFunc("proxy.config.proxy_name", url_rewrite_CB, (void *)TSNAME_CHANGED);
  REC_RegisterConfigUpdateFunc("proxy.config.reverse_proxy.enabled", url_rewrite_CB, (void *)REVERSE_CHANGED);
  REC_RegisterConfigUpdateFunc("proxy.config.http.referer_default_redirect", url_rewrite_CB, (void *)HTTP_DEFAULT_REDIRECT_CHANGED);

  // Publish: shared_ptr semantics replace the prior bespoke acquire()/release() refcount on UrlRewrite.
  rewrite_table.store(std::shared_ptr<UrlRewrite>(initial_table.release(), UrlRewriteDeleter{}), std::memory_order_release);

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
  Note("%s loading ...", ts::filename::REMAP);
  Debug("url_rewrite", "%s updated, reloading...", ts::filename::REMAP);

  auto newTable = std::make_unique<UrlRewrite>();
  if (newTable->load()) {
    static const char *msg_format = "%s finished loading";

    // Atomic publish: an old reader's shared_ptr keeps the prior table alive until its last
    // ref is dropped; new readers see the new table. The prior race between load() and
    // acquire() on the bespoke refcount cannot revive a table whose refcount was driven to
    // zero, because there is no separate refcount.
    rewrite_table.exchange(std::shared_ptr<UrlRewrite>(newTable.release(), UrlRewriteDeleter{}), std::memory_order_acq_rel);

    Debug("url_rewrite", msg_format, ts::filename::REMAP);
    Note(msg_format, ts::filename::REMAP);
    return true;
  } else {
    static const char *msg_format = "%s failed to load";

    // newTable is a unique_ptr; falling out of scope deletes it.
    Debug("url_rewrite", msg_format, ts::filename::REMAP);
    Error(msg_format, ts::filename::REMAP);
    return false;
  }
}

int
url_rewrite_CB(const char * /* name ATS_UNUSED */, RecDataT /* data_type ATS_UNUSED */, RecData data, void *cookie)
{
  int my_token = static_cast<int>((long)cookie);

  switch (my_token) {
  case REVERSE_CHANGED:
    if (auto table = rewrite_table.load(std::memory_order_acquire); table != nullptr) {
      table->SetReverseFlag(data.rec_int);
    }
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
