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

#include "ats_base_fetch.h"

#include <ts/ts.h>

#include "ats_server_context.h"
#include "ats_resource_intercept.h"

#include "net/instaweb/http/public/cache_url_async_fetcher.h"
#include "net/instaweb/util/public/string_util.h"
#include "net/instaweb/util/public/string_writer.h"
#include "net/instaweb/util/public/google_message_handler.h"

using namespace net_instaweb;

// TODO(oschaaf): rename is_resource_fetch -> write_raw_response_headers
AtsBaseFetch::AtsBaseFetch(AtsServerContext *server_context, const net_instaweb::RequestContextPtr &request_ctx,
                           TSVIO downstream_vio, TSIOBuffer downstream_buffer, bool is_resource_fetch)
  : AsyncFetch(request_ctx),
    server_context_(server_context),
    done_called_(false),
    last_buf_sent_(false),
    references_(2),
    // downstream_vio is NULL for the IPRO lookup
    downstream_vio_(downstream_vio),
    downstream_buffer_(downstream_buffer),
    is_resource_fetch_(is_resource_fetch),
    downstream_length_(0),
    txn_mutex_(downstream_vio ? TSVIOMutexGet(downstream_vio) : NULL),
    // TODO(oschaaf): check and use handle_error_.
    handle_error_(false),
    is_ipro_(false),
    ctx_(NULL),
    ipro_callback_(NULL)
{
  buffer_.reserve(1024 * 32);
}

AtsBaseFetch::~AtsBaseFetch()
{
  CHECK(references_ == 0);
}

// Should be called from the event loop,
// and thus with the txn mutex held by ATS
void
AtsBaseFetch::Release()
{
  DecrefAndDeleteIfUnreferenced();
}

void
AtsBaseFetch::Lock()
{
  TSMutexLock(txn_mutex_);
}

void
AtsBaseFetch::Unlock()
{
  TSMutexUnlock(txn_mutex_);
}

bool
AtsBaseFetch::HandleWrite(const StringPiece &sp, net_instaweb::MessageHandler *handler)
{
  ForwardData(sp, false, false);
  return true;
}

bool
AtsBaseFetch::HandleFlush(net_instaweb::MessageHandler *handler)
{
  ForwardData("", true, false);
  return true;
}

void
AtsBaseFetch::HandleHeadersComplete()
{
  // oschaaf: ATS will currently send its response headers
  // earlier than this will fire. So this has become a no-op.
  // This implies that we can't support convert_meta_tags
  TSDebug("ats-speed", "HeadersComplete()!");
  // For resource fetches, we need to output the headers in raw HTTP format.
  if (is_resource_fetch_ || is_ipro_) {
    GoogleMessageHandler mh;
    GoogleString s;
    StringWriter string_writer(&s);
    response_headers()->Add("Connection", "Close");
    response_headers()->WriteAsHttp(&string_writer, &mh);
    ForwardData(StringPiece(s.data(), s.size()), true, false);
  }
}

void
AtsBaseFetch::ForwardData(const StringPiece &sp, bool reenable, bool last)
{
  if (is_ipro_) {
    TSDebug("ats-speed", "ipro forwarddata: %.*s", (int)sp.size(), sp.data());
    buffer_.append(sp.data(), sp.size());
    return;
  }
  TSIOBufferBlock downstream_blkp;
  char *downstream_buffer;
  int64_t downstream_length;
  int64_t to_write = sp.size();

  Lock();
  if (references_ == 2) {
    while (to_write > 0) {
      downstream_blkp       = TSIOBufferStart(downstream_buffer_);
      downstream_buffer     = TSIOBufferBlockWriteStart(downstream_blkp, &downstream_length);
      int64_t bytes_written = to_write > downstream_length ? downstream_length : to_write;
      memcpy(downstream_buffer, sp.data() + (sp.size() - to_write), bytes_written);
      to_write -= bytes_written;
      downstream_length_ += bytes_written;
      TSIOBufferProduce(downstream_buffer_, bytes_written);
    }
    CHECK(to_write == 0) << "to_write failure";
    if (last) {
      TSVIONBytesSet(downstream_vio_, downstream_length_);
    }
    if (reenable) {
      TSVIOReenable(downstream_vio_);
    }
  }
  Unlock();
}

void
AtsBaseFetch::HandleDone(bool success)
{
  // When this is an IPRO lookup:
  // if we've got a 200 result, store the data and setup an intercept
  // to write it out.
  // Regardless, re-enable the transaction at this point.

  // TODO(oschaaf): what about no success?
  if (is_ipro_) {
    TSDebug("ats-speed", "ipro lookup base fetch done()");
    done_called_ = true;

    int status_code = response_headers()->status_code();
    bool status_ok  = (status_code != 0) && (status_code < 400);
    if (status_code == CacheUrlAsyncFetcher::kNotInCacheStatus) {
      TSDebug("ats-speed", "ipro lookup base fetch -> not found in cache");
      ctx_->record_in_place = true;
      TSHttpTxnReenable(ctx_->txn, TS_EVENT_HTTP_CONTINUE);
      ctx_ = NULL;
      DecrefAndDeleteIfUnreferenced();
      return;
    } else if (!status_ok) {
      TSDebug("ats-speed", "ipro lookup base fetch -> ipro cache entry says not applicable");
      TSHttpTxnReenable(ctx_->txn, TS_EVENT_HTTP_CONTINUE);
      ctx_ = NULL;
      DecrefAndDeleteIfUnreferenced();
      return;
    }
    ctx_->serve_in_place = true;
    TransformCtx *ctx    = ctx_;
    TSHttpTxn txn        = ctx_->txn;
    // TODO(oschaaf): deduplicate with code that hooks the resource intercept
    TSHttpTxnServerRespNoStoreSet(txn, 1);

    TSMBuffer reqp;
    TSMLoc req_hdr_loc;
    if (TSHttpTxnClientReqGet(ctx->txn, &reqp, &req_hdr_loc) != TS_SUCCESS) {
      TSError("[ats_base_fetch] Error TSHttpTxnClientReqGet for resource!");
      ctx_ = NULL;
      DecrefAndDeleteIfUnreferenced();
      TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
      return;
    }

    TSCont interceptCont           = TSContCreate((int (*)(tsapi_cont *, TSEvent, void *))ipro_callback_, TSMutexCreate());
    InterceptCtx *intercept_ctx    = new InterceptCtx();
    intercept_ctx->request_ctx     = ctx;
    intercept_ctx->request_headers = new RequestHeaders();
    intercept_ctx->response->append(buffer_);
    copy_request_headers_to_psol(reqp, req_hdr_loc, intercept_ctx->request_headers);
    TSHandleMLocRelease(reqp, TS_NULL_MLOC, req_hdr_loc);

    TSContDataSet(interceptCont, intercept_ctx);
    // TODO(oschaaf): when we serve an IPRO optimized asset, that will be handled
    // by the resource intercept. Which means we should set TXN_INDEX_OWNED_ARG to
    // unset (the intercept now own the context.
    TSHttpTxnServerIntercept(interceptCont, txn);
    // TODO(oschaaf): I don't think we need to lock here, but double check that
    // to make sure.
    ctx_->base_fetch = NULL;
    ctx_             = NULL;
    DecrefAndDeleteIfUnreferenced();
    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
    return;
  }

  TSDebug("ats-speed", "Done()!");
  CHECK(!done_called_);
  CHECK(downstream_vio_);
  Lock();
  done_called_ = true;
  ForwardData("", true, true);

  DecrefAndDeleteIfUnreferenced();
  // TODO(oschaaf): we aren't safe to touch the associated mutex,
  // right? FIX.
  Unlock();
}

void
AtsBaseFetch::DecrefAndDeleteIfUnreferenced()
{
  if (__sync_add_and_fetch(&references_, -1) == 0) {
    delete this;
  }
}
