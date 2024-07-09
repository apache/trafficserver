/*
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
#pragma once

#include "ts/ts.h"
#include "ts/remap.h"

#include "cripts/Bundle.hpp"
#include "cripts/Context.hpp"

// Case hierarchy, this is from ATS ts_meta.h.
template <unsigned N> struct CaseTag : public CaseTag<N - 1> {
  constexpr CaseTag()             = default;
  static constexpr unsigned value = N;
};

template <> struct CaseTag<0> {
  constexpr CaseTag()             = default;
  static constexpr unsigned value = 0;
};

static constexpr CaseTag<9> CaseArg{};

inline void
CaseVoidFunc()
{
}

// See if we should setup the send_response hook.
// ToDo: The execute flag was weird, but necessary for clang++ (at least) to not
// complain about the _do_send_response symbol not being defined for the case
// when no do_send_response() is used.

// ToDo: With ATS 10.x, we have support in the remap plugin init structure to
// probe available plugin symbols through the dlopen/dlsym APIs. We should use
// that instead of this.

// do-remap pseudo hook caller
template <typename T>
auto
wrap_do_remap(T *context, bool execute, CaseTag<1>) -> decltype(_do_remap(context), bool())
{
  if (execute) {
    _do_remap(context);
  }
  return true;
}

template <typename T>
auto
wrap_do_remap(T * /* context ATS_UNUSED */, bool /* execute ATS_UNUSED */, CaseTag<0>) -> bool
{
  return false;
}

// send-response-header hook caller
template <typename T>
auto
wrap_post_remap(T *context, bool execute, CaseTag<1>) -> decltype(_do_post_remap(context), bool())
{
  if (execute) {
    _do_post_remap(context);
  }
  return true;
}

template <typename T>
auto
wrap_post_remap(T * /* context ATS_UNUSED */, bool /* execute ATS_UNUSED */, CaseTag<0>) -> bool
{
  return false;
}

// send-response-header hook caller
template <typename T>
auto
wrap_send_response(T *context, bool execute, CaseTag<1>) -> decltype(_do_send_response(context), bool())
{
  if (execute) {
    _do_send_response(context);
  }
  return true;
}

template <typename T>
auto
wrap_send_response(T * /* context ATS_UNUSED */, bool /* execute ATS_UNUSED */, CaseTag<0>) -> bool
{
  return false;
}

// send-request-header hook caller
template <typename T>
auto
wrap_send_request(T *context, bool execute, CaseTag<1>) -> decltype(_do_send_request(context), bool())
{
  if (execute) {
    _do_send_request(context);
  }
  return true;
}

template <typename T>
auto
wrap_send_request(T * /* context ATS_UNUSED */, bool /* execute ATS_UNUSED */, CaseTag<0>) -> bool
{
  return false;
}

// read-response-header hook caller
template <typename T>
auto
wrap_read_response(T *context, bool execute, CaseTag<1>) -> decltype(_do_read_response(context), bool())
{
  if (execute) {
    _do_read_response(context);
  }
  return true;
}

template <typename T>
auto
wrap_read_response(T * /* context ATS_UNUSED */, bool /* execute ATS_UNUSED */, CaseTag<0>) -> bool
{
  return false;
}

// cache-lookup-complete hook caller
template <typename T>
auto
wrap_cache_lookup(T *context, bool execute, CaseTag<1>) -> decltype(_do_cache_lookup(context), bool())
{
  if (execute) {
    _do_cache_lookup(context);
  }
  return true;
}

template <typename T>
auto
wrap_cache_lookup(T * /* context ATS_UNUSED */, bool /* execute ATS_UNUSED */, CaseTag<0>) -> bool
{
  return false;
}

// txn-close hook caller
template <typename T>
auto
wrap_txn_close(T *context, bool execute, CaseTag<1>) -> decltype(_do_txn_close(context), bool())
{
  if (execute) {
    _do_txn_close(context);
  }
  return true;
}

template <typename T>
auto
wrap_txn_close(T * /* context ATS_UNUSED */, bool /* execute ATS_UNUSED */, CaseTag<0>) -> bool
{
  return false;
}

// do_init caller (not a hook, but called when a Cript is loaded.
// Note that the "context" here is a different context, it's the remap plugin
// data.
template <typename T>
auto
wrap_plugin_init(T *context, bool execute, CaseTag<1>) -> decltype(_do_init(context), bool())
{
  if (execute) {
    _do_init(context);
  }
  return true;
}

template <typename T>
auto
wrap_plugin_init(T * /* context ATS_UNUSED */, bool /* execute ATS_UNUSED */, CaseTag<0>) -> bool
{
  return false;
}

// create/delete instance caller (not a hook, but called when a Cript is used in a
// remap rule). Note that the "context" here is a different context, it's the
// instance data.
template <typename T>
auto
wrap_create_instance(T *context, bool execute, CaseTag<1>) -> decltype(_do_create_instance(context), bool())
{
  if (execute) {
    _do_create_instance(context);
  }
  return true;
}

template <typename T>
auto
wrap_create_instance(T * /* context ATS_UNUSED */, bool /* execute ATS_UNUSED */, CaseTag<0>) -> bool
{
  return false;
}

template <typename T>
auto
wrap_delete_instance(T *context, bool execute, CaseTag<1>) -> decltype(_do_delete_instance(context), bool())
{
  if (execute) {
    _do_delete_instance(context);
  }
  return true;
}

template <typename T>
auto
wrap_delete_instance(T * /* context ATS_UNUSED */, bool /* execute ATS_UNUSED */, CaseTag<0>) -> bool
{
  return false;
}

// This is the default continuation handler, for all do_*() callbacks
int
default_cont(TSCont contp, TSEvent event, void *edata)
{
  auto  txnp    = static_cast<TSHttpTxn>(edata);
  auto *context = static_cast<Cript::Context *>(TSContDataGet(contp));

  context->reset(); // Clears the cached handles to internal ATS data (mloc's etc.)

  switch (event) {
  case TS_EVENT_HTTP_SEND_REQUEST_HDR:
    context->state.hook = TS_HTTP_SEND_REQUEST_HDR_HOOK;
    if (!context->state.error.Failed()) {
      CDebug("Entering do_send_request()");

      // Call any bundle callbacks that are registered for this hook
      if (!context->state.error.Failed() && context->p_instance.Callbacks() & Cript::Callbacks::DO_SEND_REQUEST) {
        for (auto &bundle : context->p_instance.bundles) {
          if (bundle->Callbacks() & Cript::Callbacks::DO_SEND_REQUEST) {
            bundle->doSendRequest(context);
          }
        }
      }
      wrap_send_request(context, true, CaseArg);
      Client::URL::_get(context).Update(); // Make sure any changes to the request URL is updated
    }
    break;
  case TS_EVENT_HTTP_READ_RESPONSE_HDR: // 60006
    context->state.hook = TS_HTTP_READ_RESPONSE_HDR_HOOK;
    if (!context->state.error.Failed()) {
      CDebug("Entering do_read_response()");

      // Call any bundle callbacks that are registered for this hook
      if (!context->state.error.Failed() && context->p_instance.Callbacks() & Cript::Callbacks::DO_READ_RESPONSE) {
        for (auto &bundle : context->p_instance.bundles) {
          if (bundle->Callbacks() & Cript::Callbacks::DO_READ_RESPONSE) {
            bundle->doReadResponse(context);
          }
        }
      }
      wrap_read_response(context, true, CaseArg);
    }
    break;
  case TS_EVENT_HTTP_SEND_RESPONSE_HDR: // 60007
    context->state.hook = TS_HTTP_SEND_RESPONSE_HDR_HOOK;
    if (!context->state.error.Failed()) {
      CDebug("Entering do_send_response()");

      // Call any bundle callbacks that are registered for this hook
      if (!context->state.error.Failed() && context->p_instance.Callbacks() & Cript::Callbacks::DO_SEND_RESPONSE) {
        for (auto &bundle : context->p_instance.bundles) {
          if (bundle->Callbacks() & Cript::Callbacks::DO_SEND_RESPONSE) {
            bundle->doSendResponse(context);
          }
        }
      }
      wrap_send_response(context, true, CaseArg);
    }
    break;
  case TS_EVENT_HTTP_TXN_CLOSE: // 60012
    context->state.hook = TS_HTTP_TXN_CLOSE_HOOK;
    if (context->state.enabled_hooks & Cript::Callbacks::DO_TXN_CLOSE) {
      CDebug("Entering do_txn_close()");

      // Call any bundle callbacks that are registered for this hook
      if (!context->state.error.Failed() && context->p_instance.Callbacks() & Cript::Callbacks::DO_TXN_CLOSE) {
        for (auto &bundle : context->p_instance.bundles) {
          if (bundle->Callbacks() & Cript::Callbacks::DO_TXN_CLOSE) {
            bundle->doTxnClose(context);
          }
        }
      }
      wrap_txn_close(context, true, CaseArg);
    }

    TSContDestroy(contp);
    context->Release();
    break;
  case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE: // 60015
    context->state.hook = TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK;
    if (!context->state.error.Failed()) {
      CDebug("Entering do_cache_lookup()");

      // Call any bundle callbacks that are registered for this hook
      if (!context->state.error.Failed() && context->p_instance.Callbacks() & Cript::Callbacks::DO_CACHE_LOOKUP) {
        for (auto &bundle : context->p_instance.bundles) {
          if (bundle->Callbacks() & Cript::Callbacks::DO_CACHE_LOOKUP) {
            bundle->doCacheLookup(context);
          }
        }
      }
      wrap_cache_lookup(context, true, CaseArg);
    }
    break;
  case TS_EVENT_HTTP_POST_REMAP: // 60017
    context->state.hook = TS_HTTP_POST_REMAP_HOOK;
    CDebug("Entering do_post_remap()");

    // Call any bundle callbacks that are registered for this hook
    if (!context->state.error.Failed() && context->p_instance.Callbacks() & Cript::Callbacks::DO_POST_REMAP) {
      for (auto &bundle : context->p_instance.bundles) {
        if (bundle->Callbacks() & Cript::Callbacks::DO_POST_REMAP) {
          bundle->doPostRemap(context);
        }
      }
    }
    wrap_post_remap(context, true, CaseArg);

    if (!context->state.error.Failed()) {
      Cache::URL::_get(context).Update();  // Make sure the cache-key gets updated, if modified
      Client::URL::_get(context).Update(); // Make sure any changes to the request URL is updated
    }
    break;
    // This is for cleanup, and should always be called / wrapped
  default:
    CFatal("Cripts continuation: Unknown event %d", event);
    break;
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  if (!api_info) {
    strncpy(errbuf, "[TSRemapInit] - Invalid TSRemapInterface argument", errbuf_size - 1);
    return TS_ERROR;
  }

  if (api_info->size < sizeof(TSRemapInterface)) {
    strncpy(errbuf, "[TSRemapInit] - Incorrect size of TSRemapInterface structure", errbuf_size - 1);
    return TS_ERROR;
  }

  if (api_info->tsremap_version < TSREMAP_VERSION) {
    snprintf(errbuf, errbuf_size, "[TSRemapInit] - Incorrect API version %ld.%ld", api_info->tsremap_version >> 16,
             (api_info->tsremap_version & 0xffff));
    return TS_ERROR;
  }

  // This is to check, and call, the Cript's do_init() if provided.
  // Note that the context here is not a Cript context, but rather tha API info
  extern void           global_initialization();
  extern pthread_once_t init_once_control;

  pthread_once(&init_once_control, global_initialization);

  bool needs_plugin_init = wrap_plugin_init(api_info, false, CaseArg);

  if (needs_plugin_init) {
    wrap_plugin_init(api_info, true, CaseArg);
  }

  // TSDebug("Cript", "Remap plugin %s is successfully initialized", __BASE_FILE__);

  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char * /* errbuf ATS_UNUSED */, int /* errbuf_size ATS_UNUSED */)
{
  auto                  *inst = new Cript::Instance(argc, argv);
  Cript::InstanceContext context(*inst);
  bool                   needs_create_instance = wrap_create_instance(&context, false, CaseArg);

  if (needs_create_instance) {
    wrap_create_instance(&context, true, CaseArg);
  }

  if (!inst->Failed()) {
    std::vector<Cript::Bundle::Error> errors;

    for (auto &bundle : inst->bundles) {
      // Collect all the callbacks needed from all bundles, and validate them
      if (bundle->Validate(errors)) {
        inst->NeedCallback(bundle->Callbacks());
      }
    }

    if (!errors.empty()) {
      TSError("[Cript: %s] - Failed to validate callbacks for the following bundles:", inst->plugin_debug_tag.c_str());

      for (auto &err : errors) {
        TSError("[Cript: %s] \tIn Bundle %.*s, option %.*s()", inst->plugin_debug_tag.c_str(),
                static_cast<int>(err.Bundle().size()), err.Bundle().data(), static_cast<int>(err.Option().size()),
                err.Option().data());
        TSError("[Cript: %s] \t\t-> %.*s", inst->plugin_debug_tag.c_str(), static_cast<int>(err.Message().size()),
                err.Message().data());
      }
      delete inst;
      return TS_ERROR;
    }

    *ih = static_cast<void *>(inst);

    inst->debug("Created a new instance for Cript = {}", inst->plugin_debug_tag);
    inst->debug("The context data size = {}", sizeof(Cript::Context));

    return TS_SUCCESS;
  } else {
    delete inst; // Cleanup the instance data, which can't be used now
    return TS_ERROR;
  }
}

void
TSRemapDeleteInstance(void *ih)
{
  auto                   inst = static_cast<Cript::Instance *>(ih);
  Cript::InstanceContext context(*inst);
  bool                   needs_delete_instance = wrap_delete_instance(&context, false, CaseArg);

  if (needs_delete_instance) {
    wrap_delete_instance(&context, true, CaseArg);
  }

  inst->debug("Deleted an instance for Cript = {}", inst->plugin_debug_tag);

  delete inst;
}

TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn txnp, TSRemapRequestInfo *rri)
{
  // Check to see if we need to setup future hooks with a continuation. This
  // should only happen once.
  static bool     cript_needs_do_remap = wrap_do_remap(static_cast<Cript::Context *>(nullptr), false, CaseArg);
  static unsigned cript_enabled_hooks =
    (wrap_post_remap(static_cast<Cript::Context *>(nullptr), false, CaseArg) ? Cript::Callbacks::DO_POST_REMAP : 0) |
    (wrap_send_response(static_cast<Cript::Context *>(nullptr), false, CaseArg) ? Cript::Callbacks::DO_SEND_RESPONSE : 0) |
    (wrap_send_request(static_cast<Cript::Context *>(nullptr), false, CaseArg) ? Cript::Callbacks::DO_SEND_REQUEST : 0) |
    (wrap_read_response(static_cast<Cript::Context *>(nullptr), false, CaseArg) ? Cript::Callbacks::DO_READ_RESPONSE : 0) |
    (wrap_cache_lookup(static_cast<Cript::Context *>(nullptr), false, CaseArg) ? Cript::Callbacks::DO_CACHE_LOOKUP : 0) |
    (wrap_txn_close(static_cast<Cript::Context *>(nullptr), false, CaseArg) ? Cript::Callbacks::DO_TXN_CLOSE : 0);

  TSHttpSsn ssnp         = TSHttpTxnSsnGet(txnp);
  auto     *inst         = static_cast<Cript::Instance *>(ih);
  auto      bundle_cbs   = inst->Callbacks();
  auto     *context      = Cript::Context::Factory(txnp, ssnp, rri, *inst);
  bool      keep_context = false;

  context->state.hook          = TS_HTTP_READ_REQUEST_HDR_HOOK; // Not quite true
  context->state.enabled_hooks = (cript_enabled_hooks | bundle_cbs);

  if (cript_needs_do_remap || bundle_cbs & Cript::Callbacks::DO_REMAP) {
    CDebug("Entering do_remap()");
    for (auto &bundle : context->p_instance.bundles) {
      bundle->doRemap(context);
    }
    if (!context->state.error.Failed()) {
      wrap_do_remap(context, true, CaseArg); // This can fail the context
    }
  }

  // Don't do the callbacks when we are in a failure state.
  if (!context->state.error.Failed()) {
    Cache::URL::_get(context).Update();  // Make sure the cache-key gets updated, if modified
    Client::URL::_get(context).Update(); // Make sure any changes to the request URL is updated

    if (context->state.enabled_hooks >= Cript::Callbacks::DO_POST_REMAP) {
      context->default_cont = TSContCreate(default_cont, nullptr);
      TSContDataSet(context->default_cont, context);

      if (context->state.enabled_hooks & Cript::Callbacks::DO_POST_REMAP) {
        TSHttpTxnHookAdd(txnp, TS_HTTP_POST_REMAP_HOOK, context->default_cont);
      }

      if (context->state.enabled_hooks & Cript::Callbacks::DO_SEND_RESPONSE) {
        TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, context->default_cont);
      }

      if (context->state.enabled_hooks & Cript::Callbacks::DO_SEND_REQUEST) {
        TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_REQUEST_HDR_HOOK, context->default_cont);
      }

      if (context->state.enabled_hooks & Cript::Callbacks::DO_READ_RESPONSE) {
        TSHttpTxnHookAdd(txnp, TS_HTTP_READ_RESPONSE_HDR_HOOK, context->default_cont);
      }

      if (context->state.enabled_hooks & Cript::Callbacks::DO_CACHE_LOOKUP) {
        TSHttpTxnHookAdd(txnp, TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, context->default_cont);
      }

      // This is always needed when we have at least one Txn Hook. It will also
      // call the Cript's callback, if it exists.
      TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, context->default_cont);

      // Make sure we keep the context
      keep_context = true;
    }
  }

  // Check and deal with any failures here. Failures here are considered
  // catastrophic, and should give an error. ToDo: Do we want to have different
  // levels of failure here? Non-fatal vs fatal?
  context->state.error.Execute(context);

  // For now, we always allocate the context, but a possible future optimization
  // could be to use stack allocation when there is only a do_remap() callback.
  if (!keep_context) {
    context->Release();
  }

  // See if the Client URL was modified, which dicates the return code here.
  if (Client::URL::_get(context).Modified()) {
    context->p_instance.debug("Client::URL was modified, returning TSREMAP_DID_REMAP");
    return TSREMAP_DID_REMAP;
  } else {
    context->p_instance.debug("Client::URL was NOT modified, returning TSREMAP_NO_REMAP");
    return TSREMAP_NO_REMAP;
  }
}
