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

// TODO(oschaaf): remove what isn't used
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <limits.h>
#include <stdint.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <ts/ts.h>

#include <vector>
#include <set>

#include "ats_pagespeed.h"

#include "ats_config.h"
#include "ats_header_utils.h"
#include "ats_rewrite_options.h"
#include "ats_log_message_handler.h"

#include "base/logging.h"
#include "net/instaweb/http/public/response_headers.h"
#include "net/instaweb/util/public/string_util.h"

#include "ats_base_fetch.h"
#include "ats_resource_intercept.h"
#include "ats_beacon_intercept.h"
#include "ats_process_context.h"
#include "ats_rewrite_driver_factory.h"
#include "ats_rewrite_options.h"
#include "ats_server_context.h"

#include "net/instaweb/rewriter/public/rewrite_stats.h"
#include "net/instaweb/system/public/in_place_resource_recorder.h"

#include "net/instaweb/automatic/public/proxy_fetch.h"
#include "net/instaweb/http/public/content_type.h"
#include "net/instaweb/http/public/request_context.h"
#include "net/instaweb/rewriter/public/experiment_matcher.h"
#include "net/instaweb/rewriter/public/experiment_util.h"
#include "net/instaweb/rewriter/public/process_context.h"
#include "net/instaweb/rewriter/public/resource_fetch.h"
#include "net/instaweb/rewriter/public/rewrite_driver.h"
#include "net/instaweb/rewriter/public/rewrite_query.h"
#include "net/instaweb/rewriter/public/static_asset_manager.h"
#include "net/instaweb/public/global_constants.h"
#include "net/instaweb/public/version.h"
#include "net/instaweb/util/public/google_message_handler.h"
#include "net/instaweb/util/public/google_url.h"
#include "net/instaweb/util/public/gzip_inflater.h"
#include "net/instaweb/util/public/query_params.h"
#include "net/instaweb/util/public/statistics_logger.h"
#include "net/instaweb/util/public/stdio_file_system.h"
#include "net/instaweb/util/public/string.h"
#include "net/instaweb/util/public/string_writer.h"
#include "net/instaweb/util/public/time_util.h"
#include "net/instaweb/util/stack_buffer.h"
#include "net/instaweb/system/public/system_request_context.h"

#include <dirent.h>

#ifndef INT64_MIN
#define INT64_MAX (9223372036854775807LL)
#endif

using namespace net_instaweb;

static AtsProcessContext *ats_process_context;
static const char *DEBUG_TAG = "ats_pagespeed_transform";
static int TXN_INDEX_ARG;
static int TXN_INDEX_OWNED_ARG;
static int TXN_INDEX_OWNED_ARG_SET;
static int TXN_INDEX_OWNED_ARG_UNSET;
TSMutex config_mutex = TSMutexCreate();
AtsConfig *config    = NULL;
TransformCtx *
get_transaction_context(TSHttpTxn txnp)
{
  return (TransformCtx *)TSHttpTxnArgGet(txnp, TXN_INDEX_ARG);
}

static TransformCtx *
ats_ctx_alloc()
{
  TransformCtx *ctx;

  ctx                    = (TransformCtx *)TSmalloc(sizeof(TransformCtx));
  ctx->downstream_vio    = NULL;
  ctx->downstream_buffer = NULL;
  ctx->downstream_length = 0;
  ctx->state             = transform_state_initialized;

  ctx->base_fetch  = NULL;
  ctx->proxy_fetch = NULL;

  ctx->inflater              = NULL;
  ctx->url_string            = NULL;
  ctx->gurl                  = NULL;
  ctx->write_pending         = false;
  ctx->fetch_done            = false;
  ctx->resource_request      = false;
  ctx->beacon_request        = false;
  ctx->transform_added       = false;
  ctx->mps_user_agent        = false;
  ctx->user_agent            = NULL;
  ctx->server_context        = NULL;
  ctx->html_rewrite          = false;
  ctx->request_method        = NULL;
  ctx->alive                 = 0xaaaa;
  ctx->options               = NULL;
  ctx->to_host               = NULL;
  ctx->in_place              = false;
  ctx->driver                = NULL;
  ctx->record_in_place       = false;
  ctx->recorder              = NULL;
  ctx->ipro_response_headers = NULL;
  ctx->serve_in_place        = false;
  return ctx;
}

void
ats_ctx_destroy(TransformCtx *ctx)
{
  TSReleaseAssert(ctx);
  CHECK(ctx->alive == 0xaaaa) << "Already dead!";
  ctx->alive = 0xbbbb;

  if (ctx->base_fetch != NULL) {
    ctx->base_fetch->Release();
    ctx->base_fetch = NULL;
  }

  if (ctx->proxy_fetch != NULL) {
    ctx->proxy_fetch->Done(false /* failure */);
    ctx->proxy_fetch = NULL;
  }

  if (ctx->inflater != NULL) {
    delete ctx->inflater;
    ctx->inflater = NULL;
  }

  if (ctx->downstream_buffer) {
    TSIOBufferDestroy(ctx->downstream_buffer);
  }

  if (ctx->url_string != NULL) {
    delete ctx->url_string;
    ctx->url_string = NULL;
  }

  if (ctx->gurl != NULL) {
    delete ctx->gurl;
    ctx->gurl = NULL;
  }
  if (ctx->user_agent != NULL) {
    delete ctx->user_agent;
    ctx->user_agent = NULL;
  }
  ctx->request_method = NULL;
  if (ctx->options != NULL) {
    delete ctx->options;
    ctx->options = NULL;
  }
  if (ctx->to_host != NULL) {
    delete ctx->to_host;
    ctx->to_host = NULL;
  }
  if (ctx->driver != NULL) {
    ctx->driver->Cleanup();
    ctx->driver = NULL;
  }
  if (ctx->recorder != NULL) {
    ctx->recorder->Fail();
    ctx->recorder->DoneAndSetHeaders(NULL); // Deletes recorder.
    ctx->recorder = NULL;
  }
  if (ctx->ipro_response_headers != NULL) {
    delete ctx->ipro_response_headers;
    ctx->ipro_response_headers = NULL;
  }

  TSfree(ctx);
}

// Wrapper around GetQueryOptions()
RewriteOptions *
ps_determine_request_options(const RewriteOptions *domain_options, /* may be null */
                             RequestHeaders *request_headers, ResponseHeaders *response_headers, RequestContextPtr request_context,
                             ServerContext *server_context, GoogleUrl *url, GoogleString *pagespeed_query_params,
                             GoogleString *pagespeed_option_cookies)
{
  // Sets option from request headers and url.
  RewriteQuery rewrite_query;
  if (!server_context->GetQueryOptions(request_context, domain_options, url, request_headers, response_headers, &rewrite_query)) {
    // Failed to parse query params or request headers.  Treat this as if there
    // were no query params given.
    TSError("[ats_pagespeed] ps_route request: parsing headers or query params failed.");
    return NULL;
  }

  *pagespeed_query_params   = rewrite_query.pagespeed_query_params().ToEscapedString();
  *pagespeed_option_cookies = rewrite_query.pagespeed_option_cookies().ToEscapedString();

  // Will be NULL if there aren't any options set with query params or in
  // headers.
  return rewrite_query.ReleaseOptions();
}

// There are many sources of options:
//  - the request (query parameters, headers, and cookies)
//  - location block
//  - global server options
//  - experiment framework
// Consider them all, returning appropriate options for this request, of which
// the caller takes ownership.  If the only applicable options are global,
// set options to NULL so we can use server_context->global_options().
bool
ps_determine_options(ServerContext *server_context, RequestHeaders *request_headers, ResponseHeaders *response_headers,
                     RewriteOptions **options, RequestContextPtr request_context, GoogleUrl *url,
                     GoogleString *pagespeed_query_params, GoogleString *pagespeed_option_cookies, bool html_rewrite)
{
  // Global options for this server.  Never null.
  RewriteOptions *global_options = server_context->global_options();

  // TODO(oschaaf): we don't have directory_options right now. But if we did,
  // we'd need to take them into account here.
  RewriteOptions *directory_options = NULL;

  // Request-specific options, nearly always null.  If set they need to be
  // rebased on the directory options or the global options.
  // TODO(oschaaf): domain options..
  RewriteOptions *request_options =
    ps_determine_request_options(NULL /*domain options*/, request_headers, response_headers, request_context, server_context, url,
                                 pagespeed_query_params, pagespeed_option_cookies);

  // Because the caller takes ownership of any options we return, the only
  // situation in which we can avoid allocating a new RewriteOptions is if the
  // global options are ok as are.
  if (directory_options == NULL && request_options == NULL && !global_options->running_experiment()) {
    return true;
  }

  // Start with directory options if we have them, otherwise request options.
  if (directory_options != NULL) {
    //*options = directory_options->Clone();
    // OS: HACK! TODO!
    *options = global_options->Clone();
    (*options)->Merge(*directory_options);
  } else {
    *options = global_options->Clone();
  }

  // Modify our options in response to request options or experiment settings,
  // if we need to.  If there are request options then ignore the experiment
  // because we don't want experiments to be contaminated with unexpected
  // settings.
  if (request_options != NULL) {
    (*options)->Merge(*request_options);
    delete request_options;
  }
  // TODO(oschaaf): experiments
  /*else if ((*options)->running_experiment()) {
    bool ok = ps_set_experiment_state_and_cookie(
        r, request_headers, *options, url->Host());
    if (!ok) {
      delete *options;
      *options = NULL;
      return false;
      }
  }*/

  return true;
}

void
handle_send_response_headers(TSHttpTxn txnp)
{
  TransformCtx *ctx = get_transaction_context(txnp);
  // TODO(oschaaf): Fix the response headers!!
  bool is_owned = TSHttpTxnArgGet(txnp, TXN_INDEX_OWNED_ARG) == &TXN_INDEX_OWNED_ARG_SET;
  if (!is_owned) {
    return;
  }
  CHECK(ctx->alive == 0xaaaa) << "Already dead !";
  if (ctx->html_rewrite) {
    TSMBuffer bufp = NULL;
    TSMLoc hdr_loc = NULL;
    if (ctx->base_fetch == NULL) {
      // TODO(oschaaf): figure out when this happens.
      return;
    }

    if (TSHttpTxnClientRespGet(txnp, &bufp, &hdr_loc) == TS_SUCCESS) {
      ResponseHeaders *pagespeed_headers = ctx->base_fetch->response_headers();
      for (int i = 0; i < pagespeed_headers->NumAttributes(); i++) {
        const GoogleString &name_gs  = pagespeed_headers->Name(i);
        const GoogleString &value_gs = pagespeed_headers->Value(i);

        // We should avoid touching these fields, as ATS will drop keepalive when we do.
        if (StringCaseEqual(name_gs, "Connection") || StringCaseEqual(name_gs, "Transfer-Encoding")) {
          continue;
        }

        TSMLoc field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, name_gs.data(), name_gs.size());
        if (field_loc != NULL) {
          TSMimeHdrFieldValuesClear(bufp, hdr_loc, field_loc);
          TSMimeHdrFieldValueStringInsert(bufp, hdr_loc, field_loc, -1, value_gs.data(), value_gs.size());
        } else if (TSMimeHdrFieldCreate(bufp, hdr_loc, &field_loc) == TS_SUCCESS) {
          if (TSMimeHdrFieldNameSet(bufp, hdr_loc, field_loc, name_gs.data(), name_gs.size()) == TS_SUCCESS) {
            TSMimeHdrFieldValueStringInsert(bufp, hdr_loc, field_loc, -1, value_gs.data(), value_gs.size());
            TSMimeHdrFieldAppend(bufp, hdr_loc, field_loc);
          } else {
            CHECK(false) << "Field name set failure";
          }
          TSHandleMLocRelease(bufp, hdr_loc, field_loc);
        } else {
          CHECK(false) << "Field create failure";
        }
      }

      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    } else {
      DCHECK(false) << "Could not get response headers?!";
    }
  }
}

static void
copy_response_headers_to_psol(TSMBuffer bufp, TSMLoc hdr_loc, ResponseHeaders *psol_headers)
{
  int n_mime_headers = TSMimeHdrFieldsCount(bufp, hdr_loc);
  TSMLoc field_loc;
  const char *name, *value;
  int name_len, value_len;
  GoogleString header;
  for (int i = 0; i < n_mime_headers; ++i) {
    field_loc = TSMimeHdrFieldGet(bufp, hdr_loc, i);
    if (!field_loc) {
      TSDebug(DEBUG_TAG, "[%s] Error while obtaining header field #%d", __FUNCTION__, i);
      continue;
    }
    name = TSMimeHdrFieldNameGet(bufp, hdr_loc, field_loc, &name_len);
    StringPiece s_name(name, name_len);
    int n_field_values = TSMimeHdrFieldValuesCount(bufp, hdr_loc, field_loc);
    for (int j = 0; j < n_field_values; ++j) {
      value = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, j, &value_len);
      if (NULL == value || !value_len) {
        TSDebug(DEBUG_TAG, "[%s] Error while getting value #%d of header [%.*s]", __FUNCTION__, j, name_len, name);
      } else {
        StringPiece s_value(value, value_len);
        psol_headers->Add(s_name, s_value);
        // TSDebug(DEBUG_TAG, "Add response header [%.*s:%.*s]",name_len, name, value_len, value);
      }
    }
    TSHandleMLocRelease(bufp, hdr_loc, field_loc);
  }
}

void
copy_request_headers_to_psol(TSMBuffer bufp, TSMLoc hdr_loc, RequestHeaders *psol_headers)
{
  int n_mime_headers = TSMimeHdrFieldsCount(bufp, hdr_loc);
  TSMLoc field_loc;
  const char *name, *value;
  int name_len, value_len;
  GoogleString header;
  for (int i = 0; i < n_mime_headers; ++i) {
    field_loc = TSMimeHdrFieldGet(bufp, hdr_loc, i);
    if (!field_loc) {
      TSDebug(DEBUG_TAG, "[%s] Error while obtaining header field #%d", __FUNCTION__, i);
      continue;
    }
    name = TSMimeHdrFieldNameGet(bufp, hdr_loc, field_loc, &name_len);
    StringPiece s_name(name, name_len);
    int n_field_values = TSMimeHdrFieldValuesCount(bufp, hdr_loc, field_loc);
    for (int j = 0; j < n_field_values; ++j) {
      value = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, j, &value_len);
      if (NULL == value || !value_len) {
        TSDebug(DEBUG_TAG, "[%s] Error while getting value #%d of header [%.*s]", __FUNCTION__, j, name_len, name);
      } else {
        StringPiece s_value(value, value_len);
        psol_headers->Add(s_name, s_value);
        // TSDebug(DEBUG_TAG, "Add request header [%.*s:%.*s]",name_len, name, value_len, value);
      }
    }
    TSHandleMLocRelease(bufp, hdr_loc, field_loc);
  }
}

// TODO(oschaaf): this is not sustainable when we get more
// configuration options like this.
bool
get_override_expiry(const StringPiece &host)
{
  TSMutexLock(config_mutex);
  AtsHostConfig *hc = config->Find(host.data(), host.size());
  TSMutexUnlock(config_mutex);
  return hc->override_expiry();
}

AtsRewriteOptions *
get_host_options(const StringPiece &host, ServerContext *server_context)
{
  TSMutexLock(config_mutex);
  AtsRewriteOptions *r = (AtsRewriteOptions *)server_context->global_options()->Clone();
  AtsHostConfig *hc    = config->Find(host.data(), host.size());
  if (hc->options() != NULL) {
    // We return a clone here to avoid having to thing about
    // configuration reloads and outstanding options
    hc->options()->ClearSignatureWithCaution();
    r->Merge(*hc->options());
  }
  TSMutexUnlock(config_mutex);
  return r;
}

std::string
get_remapped_host(TSHttpTxn txn)
{
  TSMBuffer server_req_buf;
  TSMLoc server_req_loc;
  std::string to_host;
  if (TSHttpTxnServerReqGet(txn, &server_req_buf, &server_req_loc) == TS_SUCCESS ||
      TSHttpTxnCachedReqGet(txn, &server_req_buf, &server_req_loc) == TS_SUCCESS) {
    to_host = get_header(server_req_buf, server_req_loc, "Host");
    TSHandleMLocRelease(server_req_buf, TS_NULL_MLOC, server_req_loc);
  } else {
    fprintf(stderr, "@@@@@@@ FAILED \n");
  }
  return to_host;
}

static void
ats_transform_init(TSCont contp, TransformCtx *ctx)
{
  // prepare the downstream for transforming
  TSVConn downstream_conn;
  TSMBuffer bufp;
  TSMLoc hdr_loc;
  TSMBuffer reqp;
  TSMLoc req_hdr_loc;
  ctx->state = transform_state_output;

  // TODO: check cleanup flow
  if (TSHttpTxnTransformRespGet(ctx->txn, &bufp, &hdr_loc) != TS_SUCCESS) {
    TSError("[ats_pagespeed] TSHttpTxnTransformRespGet failed");
    return;
  }
  if (TSHttpTxnClientReqGet(ctx->txn, &reqp, &req_hdr_loc) != TS_SUCCESS) {
    TSError("[ats_pagespeed] TSHttpTxnClientReqGet failed");
    return;
  }

  AtsServerContext *server_context = ats_process_context->server_context();
  if (server_context->IsPagespeedResource(*ctx->gurl)) {
    CHECK(false) << "PageSpeed resource should not get here!";
  }

  downstream_conn        = TSTransformOutputVConnGet(contp);
  ctx->downstream_buffer = TSIOBufferCreate();
  ctx->downstream_vio    = TSVConnWrite(downstream_conn, contp, TSIOBufferReaderAlloc(ctx->downstream_buffer), INT64_MAX);
  if (ctx->recorder != NULL) {
    TSHandleMLocRelease(reqp, TS_NULL_MLOC, req_hdr_loc);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    return;
  }

  // TODO(oschaaf): fix host/ip(?)
  SystemRequestContext *system_request_context =
    new SystemRequestContext(server_context->thread_system()->NewMutex(), server_context->timer(), "www.foo.com", 80, "127.0.0.1");
  RequestContextPtr rptr(system_request_context);
  ctx->base_fetch = new AtsBaseFetch(server_context, rptr, ctx->downstream_vio, ctx->downstream_buffer, false);

  ResponseHeaders response_headers;
  RequestHeaders *request_headers = new RequestHeaders();
  ctx->base_fetch->SetRequestHeadersTakingOwnership(request_headers);
  copy_request_headers_to_psol(reqp, req_hdr_loc, request_headers);

  TSHttpStatus status = TSHttpHdrStatusGet(bufp, hdr_loc);
  copy_response_headers_to_psol(bufp, hdr_loc, &response_headers);

  std::string host        = ctx->gurl->HostAndPort().as_string();
  RewriteOptions *options = NULL;
  if (host.size() > 0) {
    options = get_host_options(host.c_str(), server_context);
    if (options != NULL) {
      server_context->message_handler()->Message(kInfo, "request options found \r\n");
    }
  }
  if (options == NULL) {
    options = server_context->global_options()->Clone();
  }

  server_context->message_handler()->Message(kInfo, "request options:\r\n[%s]", options->OptionsToString().c_str());

  /*
  RewriteOptions* options = NULL;
  GoogleString pagespeed_query_params;
  GoogleString pagespeed_option_cookies;
  bool ok = ps_determine_options(server_context,
                                 ctx->base_fetch->request_headers(),
                                 &response_headers,
                                 &options,
                                 rptr,
                                 ctx->gurl,
                                 &pagespeed_query_params,
                                 &pagespeed_option_cookies,
                                 true);
  */

  // TODO(oschaaf): use the determined option/query params
  // Take ownership of custom_options.
  scoped_ptr<RewriteOptions> custom_options(options);
  /*
  if (!ok) {
    TSError("Failure while determining request options for psol");
    options = server_context->global_options();
  } else {
    // ps_determine_options modified url, removing any ModPagespeedFoo=Bar query
    // parameters.  Keep url_string in sync with url.
    ctx->gurl->Spec().CopyToString(ctx->url_string);
    }*/

  RewriteDriver *driver;
  if (custom_options.get() == NULL) {
    driver = server_context->NewRewriteDriver(ctx->base_fetch->request_context());
  } else {
    driver = server_context->NewCustomRewriteDriver(custom_options.release(), ctx->base_fetch->request_context());
  }
  rptr->set_options(driver->options()->ComputeHttpOptions());
  // TODO(oschaaf): http version
  ctx->base_fetch->response_headers()->set_status_code(status);
  copy_response_headers_to_psol(bufp, hdr_loc, ctx->base_fetch->response_headers());
  ctx->base_fetch->response_headers()->ComputeCaching();

  driver->SetUserAgent(ctx->user_agent->c_str());
  driver->SetRequestHeaders(*request_headers);
  // driver->set_pagespeed_query_params(pagespeed_query_params);
  // driver->set_pagespeed_option_cookies(pagespeed_option_cookies);

  bool page_callback_added = false;
  scoped_ptr<ProxyFetchPropertyCallbackCollector> property_callback(ProxyFetchFactory::InitiatePropertyCacheLookup(
    false /*  is resource fetch?*/, *ctx->gurl, server_context, options, ctx->base_fetch,
    false /* requires_blink_cohort (no longer unused) */, &page_callback_added));

  ctx->proxy_fetch = ats_process_context->proxy_fetch_factory()->CreateNewProxyFetch(
    *(ctx->url_string), ctx->base_fetch, driver, property_callback.release(), NULL /* original_content_fetch */);

  TSHandleMLocRelease(reqp, TS_NULL_MLOC, req_hdr_loc);
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
}

static void
ats_transform_one(TransformCtx *ctx, TSIOBufferReader upstream_reader, int amount)
{
  TSDebug("ats-speed", "transform_one()");
  TSIOBufferBlock downstream_blkp;
  const char *upstream_buffer;
  int64_t upstream_length;

  while (amount > 0) {
    downstream_blkp = TSIOBufferReaderStart(upstream_reader);
    if (!downstream_blkp) {
      TSError("[ats_pagespeed] Couldn't get from IOBufferBlock");
      return;
    }

    upstream_buffer = TSIOBufferBlockReadStart(downstream_blkp, upstream_reader, &upstream_length);
    if (!upstream_buffer) {
      TSError("[ats_pagespeed] Couldn't get from TSIOBufferBlockReadStart");
      return;
    }

    if (upstream_length > amount) {
      upstream_length = amount;
    }

    TSDebug("ats-speed", "transform!");
    // TODO(oschaaf): use at least the message handler from the server conrtext here?
    if (ctx->inflater == NULL) {
      if (ctx->recorder != NULL) {
        ctx->recorder->Write(StringPiece((char *)upstream_buffer, upstream_length), ats_process_context->message_handler());
      } else {
        ctx->proxy_fetch->Write(StringPiece((char *)upstream_buffer, upstream_length), ats_process_context->message_handler());
      }
    } else {
      char buf[net_instaweb::kStackBufferSize];

      ctx->inflater->SetInput((char *)upstream_buffer, upstream_length);

      while (ctx->inflater->HasUnconsumedInput()) {
        int num_inflated_bytes = ctx->inflater->InflateBytes(buf, net_instaweb::kStackBufferSize);
        if (num_inflated_bytes < 0) {
          TSError("[ats_pagespeed] Corrupted inflation");
        } else if (num_inflated_bytes > 0) {
          if (ctx->recorder != NULL) {
            ctx->recorder->Write(StringPiece(buf, num_inflated_bytes), ats_process_context->message_handler());
          } else {
            ctx->proxy_fetch->Write(StringPiece(buf, num_inflated_bytes), ats_process_context->message_handler());
          }
        }
      }
    }
    // ctx->proxy_fetch->Flush(NULL);
    TSIOBufferReaderConsume(upstream_reader, upstream_length);
    amount -= upstream_length;
  }
  // TODO(oschaaf): get the output from the base fetch, and send it downstream.
  // This would require proper locking around the base fetch buffer
  // We could also have a look at directly writing to the traffic server buffers
}

static void
ats_transform_finish(TransformCtx *ctx)
{
  if (ctx->state == transform_state_output) {
    ctx->state = transform_state_finished;
    if (ctx->recorder != NULL) {
      TSDebug("ats-speed", "ipro recording finished");
      ctx->recorder->DoneAndSetHeaders(ctx->ipro_response_headers);
      ctx->recorder = NULL;
    } else {
      TSDebug("ats-speed", "proxy fetch finished");
      ctx->proxy_fetch->Done(true);
      ctx->proxy_fetch = NULL;
    }
  }
}

static void
ats_transform_do(TSCont contp)
{
  TSVIO upstream_vio;
  TransformCtx *ctx;
  int64_t upstream_todo;
  int64_t upstream_avail;
  int64_t downstream_bytes_written;

  ctx = (TransformCtx *)TSContDataGet(contp);

  if (ctx->state == transform_state_initialized) {
    ats_transform_init(contp, ctx);
  }

  upstream_vio             = TSVConnWriteVIOGet(contp);
  downstream_bytes_written = ctx->downstream_length;

  if (!TSVIOBufferGet(upstream_vio)) {
    ats_transform_finish(ctx);
    return;
  }

  upstream_todo = TSVIONTodoGet(upstream_vio);

  if (upstream_todo > 0) {
    upstream_avail = TSIOBufferReaderAvail(TSVIOReaderGet(upstream_vio));

    if (upstream_todo > upstream_avail) {
      upstream_todo = upstream_avail;
    }

    if (upstream_todo > 0) {
      if (ctx->recorder != NULL) {
        ctx->downstream_length += upstream_todo;
        TSIOBufferCopy(TSVIOBufferGet(ctx->downstream_vio), TSVIOReaderGet(upstream_vio), upstream_todo, 0);
      }
      ats_transform_one(ctx, TSVIOReaderGet(upstream_vio), upstream_todo);
      TSVIONDoneSet(upstream_vio, TSVIONDoneGet(upstream_vio) + upstream_todo);
    }
  }

  if (TSVIONTodoGet(upstream_vio) > 0) {
    if (upstream_todo > 0) {
      if (ctx->downstream_length > downstream_bytes_written) {
        TSVIOReenable(ctx->downstream_vio);
      }
      TSContCall(TSVIOContGet(upstream_vio), TS_EVENT_VCONN_WRITE_READY, upstream_vio);
    }
  } else {
    // When not recording, the base fetch will re-enable from the PSOL callback.
    if (ctx->recorder != NULL) {
      TSVIONBytesSet(ctx->downstream_vio, ctx->downstream_length);
      TSVIOReenable(ctx->downstream_vio);
    }
    ats_transform_finish(ctx);
    TSContCall(TSVIOContGet(upstream_vio), TS_EVENT_VCONN_WRITE_COMPLETE, upstream_vio);
  }
}

static int
ats_pagespeed_transform(TSCont contp, TSEvent event, void * /* edata ATS_UNUSED */)
{
  TSDebug("ats-speed", "ats_pagespeed_transform()");
  if (TSVConnClosedGet(contp)) {
    // ats_ctx_destroy((TransformCtx*)TSContDataGet(contp));
    TSContDestroy(contp);
    return 0;
  } else {
    switch (event) {
    case TS_EVENT_ERROR: {
      fprintf(stderr, "ats speed transform event: [%d] TS EVENT ERROR?!\n", event);
      TSVIO upstream_vio = TSVConnWriteVIOGet(contp);
      TSContCall(TSVIOContGet(upstream_vio), TS_EVENT_ERROR, upstream_vio);
    } break;
    case TS_EVENT_VCONN_WRITE_COMPLETE:
      TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
      break;
    case TS_EVENT_VCONN_WRITE_READY:
      ats_transform_do(contp);
      break;
    case TS_EVENT_IMMEDIATE:
      ats_transform_do(contp);
      break;
    default:
      DCHECK(false) << "unknown event: " << event;
      ats_transform_do(contp);
      break;
    }
  }

  return 0;
}

static void
ats_pagespeed_transform_add(TSHttpTxn txnp)
{
  TransformCtx *ctx = get_transaction_context(txnp);
  CHECK(ctx);
  if (ctx->transform_added) { // Happens with a stale cache hit
    TSDebug("ats-speed", "transform not added due to already being added");
    return;
  } else {
    TSDebug("ats-speed", "transform added");
    ctx->transform_added = true;
  }

  TSHttpTxnUntransformedRespCache(txnp, ctx->recorder == NULL ? 1 : 0);
  TSHttpTxnTransformedRespCache(txnp, 0);

  TSVConn connp;

  connp = TSTransformCreate(ats_pagespeed_transform, txnp);
  TSContDataSet(connp, ctx);
  TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, connp);
}

void
handle_read_request_header(TSHttpTxn txnp)
{
  TSMBuffer reqp = NULL;
  TSMLoc hdr_loc = NULL;
  char *url      = NULL;
  int url_length = -1;

  TransformCtx *ctx = ats_ctx_alloc();
  ctx->txn          = txnp;
  TSHttpTxnArgSet(txnp, TXN_INDEX_ARG, (void *)ctx);
  TSHttpTxnArgSet(txnp, TXN_INDEX_OWNED_ARG, &TXN_INDEX_OWNED_ARG_SET);

  if (TSHttpTxnClientReqGet(txnp, &reqp, &hdr_loc) == TS_SUCCESS) {
    url = TSHttpTxnEffectiveUrlStringGet(txnp, &url_length);
    if (!url || url_length <= 0) {
      DCHECK(false) << "Could not get url!";
    } else {
      std::string s_url = std::string(url, url_length);
      GoogleUrl gurl(s_url);

      ctx->url_string = new GoogleString(url, url_length);
      ctx->gurl       = new GoogleUrl(*(ctx->url_string));

      if (!ctx->gurl->IsWebValid()) {
        TSDebug("ats-speed", "URL != WebValid(): %s", ctx->url_string->c_str());
      } else {
        const char *method;
        int method_len;
        method                  = TSHttpHdrMethodGet(reqp, hdr_loc, &method_len);
        bool head_or_get        = method == TS_HTTP_METHOD_GET || method == TS_HTTP_METHOD_HEAD;
        ctx->request_method     = method;
        GoogleString user_agent = get_header(reqp, hdr_loc, "User-Agent");
        ctx->user_agent         = new GoogleString(user_agent);
        ctx->server_context     = ats_process_context->server_context();
        TSDebug("ats-speed", "static asset prefix: %s",
                ((AtsRewriteDriverFactory *)ctx->server_context->factory())->static_asset_prefix().c_str());
        if (user_agent.find(kModPagespeedSubrequestUserAgent) != user_agent.npos) {
          ctx->mps_user_agent = true;
        }
        if (ats_process_context->server_context()->IsPagespeedResource(gurl)) {
          if (head_or_get && !ctx->mps_user_agent) {
            ctx->resource_request = true;
            TSHttpTxnArgSet(txnp, TXN_INDEX_OWNED_ARG, &TXN_INDEX_OWNED_ARG_UNSET);
          }
        } else if (ctx->gurl->PathSansLeaf() ==
                   ((AtsRewriteDriverFactory *)ctx->server_context->factory())->static_asset_prefix()) {
          ctx->resource_request = true;
          TSHttpTxnArgSet(txnp, TXN_INDEX_OWNED_ARG, &TXN_INDEX_OWNED_ARG_UNSET);
        } else if (StringCaseEqual(gurl.PathSansQuery(), "/ats_pagespeed_beacon")) {
          ctx->beacon_request = true;
          TSHttpTxnArgSet(txnp, TXN_INDEX_OWNED_ARG, &TXN_INDEX_OWNED_ARG_UNSET);
          hook_beacon_intercept(txnp);
        } else {
          AtsServerContext *server_context = ctx->server_context;
          // TODO(oschaaf): fix host/ip(?)
          SystemRequestContext *system_request_context = new SystemRequestContext(
            server_context->thread_system()->NewMutex(), server_context->timer(), "www.foo.com", 80, "127.0.0.1");
          RequestContextPtr rptr(system_request_context);

          ctx->base_fetch = new AtsBaseFetch(server_context, rptr, ctx->downstream_vio, ctx->downstream_buffer, false);

          RequestHeaders *request_headers = new RequestHeaders();
          ctx->base_fetch->SetRequestHeadersTakingOwnership(request_headers);
          copy_request_headers_to_psol(reqp, hdr_loc, request_headers);

          // TSHttpStatus status = TSHttpHdrStatusGet(bufp, hdr_loc);
          // TODO(oschaaf): http version
          // ctx->base_fetch->response_headers()->set_status_code(status);
          // copy_response_headers_to_psol(bufp, hdr_loc, ctx->base_fetch->response_headers());
          // ctx->base_fetch->response_headers()->ComputeCaching();
          std::string host = ctx->gurl->HostAndPort().as_string();
          // request_headers->Lookup1(HttpAttributes::kHost);
          RewriteOptions *options = NULL;
          if (host.size() > 0) {
            options = get_host_options(host.c_str(), server_context);
          }
          if (options == NULL) {
            options = server_context->global_options()->Clone();
          }

          // GoogleString pagespeed_query_params;
          // GoogleString pagespeed_option_cookies;
          // bool ok = ps_determine_options(server_context,
          //                               ctx->base_fetch->request_headers(),
          //                               NULL /*ResponseHeaders* */,
          //                               &options,
          //                               rptr,
          //                              ctx->gurl,
          //                               &pagespeed_query_params,
          //                               &pagespeed_option_cookies,
          //                               false /*html rewrite*/);
          // Take ownership of custom_options.
          scoped_ptr<RewriteOptions> custom_options(options);

          // ps_determine_options modified url, removing any ModPagespeedFoo=Bar query
          // parameters.  Keep url_string in sync with url.
          // ctx->gurl->Spec().CopyToString(ctx->url_string);

          rptr->set_options(options->ComputeHttpOptions());
          if (options->in_place_rewriting_enabled() && options->enabled() && options->IsAllowed(ctx->gurl->Spec())) {
            RewriteDriver *driver;
            if (custom_options.get() == NULL) {
              driver = server_context->NewRewriteDriver(ctx->base_fetch->request_context());
            } else {
              driver = server_context->NewCustomRewriteDriver(custom_options.release(), ctx->base_fetch->request_context());
            }

            if (!user_agent.empty()) {
              driver->SetUserAgent(ctx->user_agent->c_str());
            }
            driver->SetRequestHeaders(*ctx->base_fetch->request_headers());
            // driver->set_pagespeed_query_params(pagespeed_query_params);
            // driver->set_pagespeed_option_cookies(pagespeed_option_cookies);
            ctx->driver = driver;
            ctx->server_context->message_handler()->Message(kInfo, "Trying to serve rewritten resource in-place: %s",
                                                            ctx->url_string->c_str());

            ctx->in_place = true;
            ctx->base_fetch->set_handle_error(false);
            ctx->base_fetch->set_is_ipro(true);

            // ctx->driver->FetchInPlaceResource(
            //    *ctx->gurl, false /* proxy_mode */, ctx->base_fetch);
          }
        }
      }
      TSfree((void *)url);
    } // gurl->IsWebValid() == true
    TSHandleMLocRelease(reqp, TS_NULL_MLOC, hdr_loc);
  } else {
    DCHECK(false) << "Could not get client request header\n";
  }
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
}

bool
cache_hit(TSHttpTxn txnp)
{
  int obj_status;
  if (TSHttpTxnCacheLookupStatusGet(txnp, &obj_status) == TS_ERROR) {
    // TODO(oschaaf): log warning
    return false;
  }
  return obj_status == TS_CACHE_LOOKUP_HIT_FRESH;
}

static int
transform_plugin(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txn = (TSHttpTxn)edata;

  CHECK(event == TS_EVENT_HTTP_READ_RESPONSE_HDR || event == TS_EVENT_HTTP_READ_CACHE_HDR ||
        event == TS_EVENT_HTTP_SEND_REQUEST_HDR || event == TS_EVENT_HTTP_READ_REQUEST_HDR || event == TS_EVENT_HTTP_TXN_CLOSE ||
        event == TS_EVENT_HTTP_SEND_RESPONSE_HDR)
    << "Invalid transform event";

  if (event != TS_EVENT_HTTP_READ_REQUEST_HDR) {
    // Bail if an intercept is running
    bool is_owned = TSHttpTxnArgGet(txn, TXN_INDEX_OWNED_ARG) == &TXN_INDEX_OWNED_ARG_SET;
    if (!is_owned) {
      TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
      return 0;
    }
  }

  if (event == TS_EVENT_HTTP_SEND_RESPONSE_HDR) {
    handle_send_response_headers(txn);
    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
    return 0;
  }
  if (event == TS_EVENT_HTTP_TXN_CLOSE) {
    TransformCtx *ctx = get_transaction_context(txn);
    // if (ctx != NULL && !ctx->resource_request && !ctx->beacon_request && !ctx->html_rewrite) {
    // For intercepted requests like beacons and resource requests, we don't own the
    // ctx here - the interceptor does.

    if (ctx != NULL) {
      bool is_owned = TSHttpTxnArgGet(txn, TXN_INDEX_OWNED_ARG) == &TXN_INDEX_OWNED_ARG_SET
                      // TODO(oschaaf): rewrite this.
                      && !ctx->serve_in_place;
      if (is_owned) {
        ats_ctx_destroy(ctx);
      }
    }
    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
    return 0;
  }
  if (event == TS_EVENT_HTTP_READ_REQUEST_HDR) {
    handle_read_request_header(txn);
    return 0;
  } else if (event == TS_EVENT_HTTP_SEND_REQUEST_HDR) {
    TSMBuffer request_header_buf = NULL;
    TSMLoc request_header_loc    = NULL;

    if (TSHttpTxnServerReqGet(txn, &request_header_buf, &request_header_loc) == TS_SUCCESS) {
      hide_accept_encoding(request_header_buf, request_header_loc, "@xxAccept-Encoding");
      // Turn off pagespeed optimization at the origin
      set_header(request_header_buf, request_header_loc, "PageSpeed", "off");
      TSHandleMLocRelease(request_header_buf, TS_NULL_MLOC, request_header_loc);
    } else {
      CHECK(false) << "Could not find server request header";
    }
    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
    return 0;
  } else if (event == TS_EVENT_HTTP_READ_RESPONSE_HDR) {
    TSMBuffer request_header_buf = NULL;
    TSMLoc request_header_loc    = NULL;

    if (TSHttpTxnServerReqGet(txn, &request_header_buf, &request_header_loc) == TS_SUCCESS) {
      restore_accept_encoding(request_header_buf, request_header_loc, "@xxAccept-Encoding");
      TSHandleMLocRelease(request_header_buf, TS_NULL_MLOC, request_header_loc);
    } else {
      CHECK(false) << "Could not find server request header";
    }
  }

  CHECK(event == TS_EVENT_HTTP_READ_RESPONSE_HDR || event == TS_EVENT_HTTP_READ_CACHE_HDR);

  TransformCtx *ctx = get_transaction_context(txn);
  if (ctx == NULL) {
    // TODO(oschaaf): document how and when this happens.
    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
    return 0;
  }
  if (ctx->serve_in_place) {
    TSHttpTxnArgSet(txn, TXN_INDEX_OWNED_ARG, &TXN_INDEX_OWNED_ARG_UNSET);
    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
    return 0;
  }
  std::string *to_host = new std::string();
  to_host->append(get_remapped_host(ctx->txn));
  ctx->to_host                  = to_host;
  TSMBuffer response_header_buf = NULL;
  TSMLoc response_header_loc    = NULL;

  // TODO(oschaaf): from configuration!
  bool override_expiry = false;

  const char *host = ctx->gurl->HostAndPort().as_string().c_str();
  // request_headers->Lookup1(HttpAttributes::kHost);
  if (host != NULL && strlen(host) > 0) {
    override_expiry = get_override_expiry(host);
  }

  if (ctx->mps_user_agent && override_expiry) {
    if (TSHttpTxnServerRespGet(txn, &response_header_buf, &response_header_loc) == TS_SUCCESS) {
      // TODO => set cacheable.
      unset_header(response_header_buf, response_header_loc, "Cache-Control");
      unset_header(response_header_buf, response_header_loc, "Expires");
      unset_header(response_header_buf, response_header_loc, "Age");
      set_header(response_header_buf, response_header_loc, "Cache-Control", "public, max-age=3600");
      TSHandleMLocRelease(response_header_buf, TS_NULL_MLOC, response_header_loc);
    }
  }
  bool ok = ctx->gurl->IsWebValid() && !(ctx->resource_request || ctx->beacon_request || ctx->mps_user_agent);
  if (!ok) {
    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
    return 0;
  }

  bool have_response_header = false;

  if (TSHttpTxnServerRespGet(txn, &response_header_buf, &response_header_loc) == TS_SUCCESS) {
    have_response_header = true;
    if (override_expiry) {
      unset_header(response_header_buf, response_header_loc, "Cache-Control");
      unset_header(response_header_buf, response_header_loc, "Expires");
      unset_header(response_header_buf, response_header_loc, "Age");
      set_header(response_header_buf, response_header_loc, "Cache-Control", "public, max-age=3600");
    }
  } else if (TSHttpTxnCachedRespGet(txn, &response_header_buf, &response_header_loc) == TS_SUCCESS) {
    have_response_header = true;
  }
  if (!have_response_header) {
    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
    return 0;
  }

  if (ok) {
    if (ctx->request_method != TS_HTTP_METHOD_GET && ctx->request_method != TS_HTTP_METHOD_HEAD &&
        ctx->request_method != TS_HTTP_METHOD_POST) {
      ok = false;
      TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
      return 0;
    }
  }

  TSHttpStatus status = TSHttpHdrStatusGet(response_header_buf, response_header_loc);
  if (ok) {
    if (!(status == TS_HTTP_STATUS_OK || status == TS_HTTP_STATUS_NOT_FOUND)) {
      ok = false;
      TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
      return 0;
    }
  }

  if (ok) {
    StringPiece s_content_type                    = get_header(response_header_buf, response_header_loc, "Content-Type");
    const net_instaweb::ContentType *content_type = net_instaweb::MimeTypeToContentType(s_content_type);

    if (ctx->record_in_place && content_type != NULL) {
      GoogleString cache_url = *ctx->url_string;
      ctx->server_context->rewrite_stats()->ipro_not_in_cache()->Add(1);
      ctx->server_context->message_handler()->Message(kInfo, "Could not rewrite resource in-place "
                                                             "because URL is not in cache: %s",
                                                      cache_url.c_str());
      const SystemRewriteOptions *options = SystemRewriteOptions::DynamicCast(ctx->driver->options());
      RequestHeaders request_headers;
      // copy_request_headers_from_ngx(r, &request_headers);
      // This URL was not found in cache (neither the input resource nor
      // a ResourceNotCacheable entry) so we need to get it into cache
      // (or at least a note that it cannot be cached stored there).
      // We do that using an Apache output filter.
      // TODO(oschaaf): fix host/ip(?)
      RequestContextPtr system_request_context(new SystemRequestContext(
        ctx->server_context->thread_system()->NewMutex(), ctx->server_context->timer(), "www.foo.com", 80, "127.0.0.1"));

      system_request_context->set_options(options->ComputeHttpOptions());

      ctx->recorder = new InPlaceResourceRecorder(system_request_context, cache_url, ctx->driver->CacheFragment(),
                                                  request_headers.GetProperties(), options->ipro_max_response_bytes(),
                                                  options->ipro_max_concurrent_recordings(), ctx->server_context->http_cache(),
                                                  ctx->server_context->statistics(), ctx->server_context->message_handler());
      // TODO(oschaaf): does this make sense for ats? perhaps we don't need it.
      ctx->ipro_response_headers = new ResponseHeaders();
      ctx->ipro_response_headers->set_status_code(status);
      copy_response_headers_to_psol(response_header_buf, response_header_loc, ctx->ipro_response_headers);
      ctx->ipro_response_headers->ComputeCaching();

      ctx->recorder->ConsiderResponseHeaders(InPlaceResourceRecorder::kPreliminaryHeaders, ctx->ipro_response_headers);
    } else if ((content_type == NULL || !content_type->IsHtmlLike())) {
      ok = false;
      TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
      return 0;
    }
  }

  if (ok) {
    StringPiece content_encoding = get_header(response_header_buf, response_header_loc, "Content-Encoding");
    net_instaweb::GzipInflater::InflateType inflate_type;
    bool is_encoded = false;

    if (StringCaseEqual(content_encoding, "deflate")) {
      is_encoded   = true;
      inflate_type = GzipInflater::kDeflate;
    } else if (StringCaseEqual(content_encoding, "gzip")) {
      is_encoded   = true;
      inflate_type = GzipInflater::kGzip;
    }

    if (is_encoded) {
      ctx->inflater = new GzipInflater(inflate_type);
      ctx->inflater->Init();
    }
    ctx->html_rewrite = ctx->recorder == NULL;
    if (ctx->html_rewrite) {
      TSDebug(DEBUG_TAG, "Will optimize [%s]", ctx->url_string->c_str());
    } else if (ctx->recorder != NULL) {
      TSDebug(DEBUG_TAG, "Will record in place: [%s]", ctx->url_string->c_str());
    } else {
      CHECK(false) << "At this point, adding a transform makes no sense";
    }

    set_header(response_header_buf, response_header_loc, "@gzip_nocache", "0");
    ats_pagespeed_transform_add(txn);
  }

  TSHandleMLocRelease(response_header_buf, TS_NULL_MLOC, response_header_loc);
  TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);

  return 0;
}

bool
RegisterPlugin()
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = (char *)"ats_pagespeed";
  info.vendor_name   = (char *)"Apache Software Foundation";
  info.support_email = (char *)"dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[ats_pagespeed] Failed to register");
    return false;
  }

  return true;
}

void
cleanup_process()
{
  delete ats_process_context;
  AtsRewriteDriverFactory::Terminate();
  AtsRewriteOptions::Terminate();
}

static void
process_configuration()
{
  AtsConfig *new_config = new AtsConfig((AtsThreadSystem *)ats_process_context->server_context()->thread_system());
  DIR *dir;
  struct dirent *ent;

  if ((dir = opendir("/usr/local/etc/trafficserver/psol/")) != NULL) {
    while ((ent = readdir(dir)) != NULL) {
      size_t len = strlen(ent->d_name);
      if (len <= 0)
        continue;
      if (ent->d_name[0] == '.')
        continue;
      if (ent->d_name[len - 1] == '~')
        continue;
      if (ent->d_name[0] == '#')
        continue;
      GoogleString s("/usr/local/etc/trafficserver/psol/");
      s.append(ent->d_name);
      fprintf(stderr, "parse [%s]\n", s.c_str());
      if (!new_config->Parse(s.c_str())) {
        TSError("[ats_pagespeed] Error parsing %s", s.c_str());
      }
    }
    closedir(dir);
  }

  AtsConfig *old_config;
  TSMutexLock(config_mutex);
  fprintf(stderr, "Update configuration\n");
  old_config = config;
  config     = new_config;
  TSMutexUnlock(config_mutex);
  if (old_config != NULL) {
    delete old_config;
  }
}

static void *
config_notification_callback(void *data)
{
  int BUF_MAX = 1024 * (sizeof(struct inotify_event) + 16);
  char buf[BUF_MAX];
  int fd, wd;

  fd = inotify_init();

  if (fd < 0) {
    perror("inotify_init");
    CHECK(false) << "Failed to initialize inotify";
  }

  wd = inotify_add_watch(fd, "/usr/local/etc/trafficserver/psol/", IN_MODIFY | IN_CREATE | IN_DELETE);

  while (1) {
    int len        = read(fd, buf, BUF_MAX);
    int i          = 0;
    bool do_update = false;
    while (i < len) {
      struct inotify_event *event = (struct inotify_event *)&buf[i];
      if (event->len) {
        if (!(event->mask & IN_ISDIR)) {
          const char *name = event->name;
          size_t name_len  = strlen(event->name);
          if (name_len > 0 && name[0] != '.' && name[0] != '#' && name[name_len - 1] != '~') {
            do_update = true;
          }
        }
      }
      i += (sizeof(struct inotify_event)) + event->len;
    }
    if (do_update) {
      process_configuration();
    }
  }

  inotify_rm_watch(fd, wd);
  close(fd);

  return NULL;
}

void
TSPluginInit(int argc, const char *argv[])
{
  if (RegisterPlugin() == true) {
    if (TSHttpArgIndexReserve("ats_pagespeed", "Stores the transaction context", &TXN_INDEX_ARG) != TS_SUCCESS) {
      CHECK(false) << "failed to reserve an argument index";
    }
    if (TSHttpArgIndexReserve("ats_pagespeed", "Stores the transaction context", &TXN_INDEX_OWNED_ARG) != TS_SUCCESS) {
      CHECK(false) << "failed to reserve an argument index";
    }

    AtsRewriteOptions::Initialize();
    AtsRewriteDriverFactory::Initialize();
    net_instaweb::log_message_handler::Install();
    atexit(cleanup_process);
    ats_process_context = new AtsProcessContext();
    process_configuration();
    TSCont transform_contp = TSContCreate(transform_plugin, NULL);
    TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, transform_contp);
    TSHttpHookAdd(TS_HTTP_READ_CACHE_HDR_HOOK, transform_contp);
    TSHttpHookAdd(TS_HTTP_SEND_REQUEST_HDR_HOOK, transform_contp);
    TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, transform_contp);
    TSHttpHookAdd(TS_HTTP_TXN_CLOSE_HOOK, transform_contp);
    TSHttpHookAdd(TS_HTTP_SEND_RESPONSE_HDR_HOOK, transform_contp);

    setup_resource_intercept();
    CHECK(TSThreadCreate(config_notification_callback, NULL)) << "";
    ats_process_context->message_handler()->Message(kInfo, "TSPluginInit OK");
  }
}
