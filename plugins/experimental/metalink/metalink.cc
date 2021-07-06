/** @file
 *
 * Try not to download the same file twice.  Improve cache efficiency
 * and speed up downloads.
 *
 * @section license License
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed
 * with this work for additional information regarding copyright
 * ownership.  The ASF licenses this file to you under the Apache
 * License, Version 2.0 (the "License"); you may not use this file
 * except in compliance with the License.  You may obtain a copy of
 * the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.  See the License for the specific language governing
 * permissions and limitations under the License. */

#include <strings.h>

#include <openssl/sha.h>

#include "ts/ts.h"

/* Implement TS_HTTP_READ_RESPONSE_HDR_HOOK to implement a null
 * transformation.  Compute the SHA-256 digest of the content, write
 * it to the cache and store the request URL at that key.
 *
 * Implement TS_HTTP_SEND_RESPONSE_HDR_HOOK to check the Location and
 * Digest headers.  Use TSCacheRead() to check if the URL in the
 * Location header is already cached.  If not, potentially rewrite
 * that header.  Do this after responses are cached because the cache
 * will change.
 *
 * More details are on the [wiki page] in the Traffic Server wiki.
 *
 *    [wiki page]   https://cwiki.apache.org/confluence/display/TS/Metalink */

/* TSCacheWrite() and TSVConnWrite() data: Write the digest to the
 * cache and store the request URL at that key */

struct WriteData {
  TSHttpTxn txnp;

  TSCacheKey key;

  TSVConn connp;
  TSIOBuffer cache_bufp;
};

/* TSTransformCreate() data: Compute the SHA-256 digest of the content */

struct TransformData {
  TSHttpTxn txnp;

  /* Null transformation */
  TSIOBuffer output_bufp;
  TSVIO output_viop;

  /* Message digest handle */
  SHA256_CTX c;
};

/* TSCacheRead() and TSVConnRead() data: Check the Location and Digest
 * headers */

struct SendData {
  TSHttpTxn txnp;

  TSMBuffer resp_bufp;
  TSMLoc hdr_loc;

  /* Location header */
  TSMLoc location_loc;

  /* Cache key */
  TSMLoc url_loc;
  TSCacheKey key;

  /* Digest header */
  TSMLoc digest_loc;

  /* Digest header field value index */
  int idx;

  TSVConn connp;
  TSIOBuffer cache_bufp;

  const char *value;
  int64_t length;
};

/* Implement TS_HTTP_READ_RESPONSE_HDR_HOOK to implement a null
 * transformation */

/* Write the digest to the cache and store the request URL at that key */

static int
cache_open_write(TSCont contp, void *edata)
{
  TSMBuffer req_bufp;

  TSMLoc hdr_loc;
  TSMLoc url_loc;

  char *value;
  int length;

  WriteData *data = static_cast<WriteData *>(TSContDataGet(contp));
  data->connp     = static_cast<TSVConn>(edata);

  TSCacheKeyDestroy(data->key);

  if (TSHttpTxnClientReqGet(data->txnp, &req_bufp, &hdr_loc) != TS_SUCCESS) {
    TSError("[metalink] Couldn't retrieve client request header");

    TSContDestroy(contp);

    TSfree(data);

    return 0;
  }

  if (TSHttpHdrUrlGet(req_bufp, hdr_loc, &url_loc) != TS_SUCCESS) {
    TSContDestroy(contp);

    TSfree(data);

    TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, hdr_loc);

    return 0;
  }

  /* Allocation!  Must free! */
  value = TSUrlStringGet(req_bufp, url_loc, &length);
  if (!value) {
    TSContDestroy(contp);

    TSfree(data);

    TSHandleMLocRelease(req_bufp, hdr_loc, url_loc);
    TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, hdr_loc);

    return 0;
  }

  TSHandleMLocRelease(req_bufp, hdr_loc, url_loc);
  TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, hdr_loc);

  /* Store the request URL */

  data->cache_bufp         = TSIOBufferCreate();
  TSIOBufferReader readerp = TSIOBufferReaderAlloc(data->cache_bufp);

  int nbytes = TSIOBufferWrite(data->cache_bufp, value, length);

  TSfree(value);

  /* Reentrant!  Reuse the TSCacheWrite() continuation. */
  TSVConnWrite(data->connp, contp, readerp, nbytes);

  return 0;
}

/* Do nothing */

static int
cache_open_write_failed(TSCont contp, void * /* edata ATS_UNUSED */)
{
  WriteData *data = static_cast<WriteData *>(TSContDataGet(contp));
  TSContDestroy(contp);

  TSCacheKeyDestroy(data->key);
  TSfree(data);

  return 0;
}

static int
write_vconn_write_complete(TSCont contp, void * /* edata ATS_UNUSED */)
{
  WriteData *data = static_cast<WriteData *>(TSContDataGet(contp));
  TSContDestroy(contp);

  /* The object is not committed to the cache until the VConnection is
   * closed.  When all the data has been transferred, the user (contp)
   * must do a TSVConnClose() */
  TSVConnClose(data->connp);

  TSIOBufferDestroy(data->cache_bufp);
  TSfree(data);

  return 0;
}

/* TSCacheWrite() and TSVConnWrite() handler: Write the digest to the
 * cache and store the request URL at that key */

static int
write_handler(TSCont contp, TSEvent event, void *edata)
{
  switch (event) {
  case TS_EVENT_CACHE_OPEN_WRITE:
    return cache_open_write(contp, edata);

  case TS_EVENT_CACHE_OPEN_WRITE_FAILED:
    return cache_open_write_failed(contp, edata);

  case TS_EVENT_VCONN_WRITE_COMPLETE:
    return write_vconn_write_complete(contp, edata);

  default:
    TSAssert(!"Unexpected event");
  }

  return 0;
}

/* Copy content from the input buffer to the output buffer without
 * modification and feed it through the message digest at the same
 * time.
 *
 *    1.  Check if we are "closed" before doing anything else to avoid
 *        errors.
 *
 *    2.  Then deal with any input that's available now.
 *
 *    3.  Check if the input is complete after dealing with any
 *        available input in case it was the last of it.  If it is
 *        complete, tell downstream, thank upstream, and finish
 *        computing the digest.  Otherwise either wait for more input
 *        or abort if upstream is "closed".
 *
 * The handler is guaranteed to get called at least once, even if the
 * response is 304 Not Modified, so we are guaranteed an opportunity
 * to clean up e.g. data that we allocated when we called
 * TSTransformCreate().
 *
 * TS_EVENT_VCONN_WRITE_READY and TS_EVENT_VCONN_WRITE_COMPLETE events
 * are sent from downstream, e.g. by
 * TransformTerminus::handle_event().  TS_EVENT_IMMEDIATE events are
 * sent by INKVConnInternal::do_io_write(),
 * INKVConnInternal::do_io_close(), and INKVConnInternal::reenable()
 * which are called from upstream, e.g. by
 * TransformVConnection::do_io_write(),
 * TransformVConnection::do_io_close(), and
 * HttpTunnel::producer_handler().
 *
 * Clean up the output buffer on TS_EVENT_VCONN_WRITE_COMPLETE and not
 * before.  We are guaranteed a TS_EVENT_VCONN_WRITE_COMPLETE event
 * *unless* we are "closed".  In that case we instead get a
 * TS_EVENT_IMMEDIATE event where TSVConnClosedGet() is one.  We'll
 * only ever get one event where TSVConnClosedGet() is one and it will
 * be our last, so we *must* check for this case and clean up then
 * too.  Because we'll only ever get one such event and it will be our
 * last, there's no risk of double freeing.
 *
 * The response headers get sent when TSVConnWrite() gets called and
 * not before.  (We could potentially edit them until then.)
 *
 * The events say nothing about the state of the input.  Gather this
 * instead from TSVConnClosedGet(), TSVIOReaderGet(), and
 * TSVIONTodoGet() and handle the end of the input independently from
 * the TS_EVENT_VCONN_WRITE_COMPLETE event from downstream.
 *
 * When TSVConnClosedGet() is one, *we* are "closed".  We *must* check
 * for this case, if only to clean up allocated data.  Some state is
 * already inconsistent.  (This happens when the response is 304 Not
 * Modified or when the client or origin disconnect before the message
 * is complete.)
 *
 * When TSVIOReaderGet() is NULL, upstream is "closed".  In that case
 * it's clearly an error to call TSIOBufferReaderAvail(), it's also an
 * error at that point to send any events upstream with TSContCall().
 * (This happens when the content length is zero or when we get the
 * final chunk of a chunked response.)
 *
 * The input is complete only when TSVIONTodoGet() is zero.  (Don't
 * update the downstream nbytes otherwise!)  Update the downstream
 * nbytes when the input is complete in case the response is chunked
 * (in which case nbytes is unknown until then).  Downstream will
 * (normally) send the TS_EVENT_VCONN_WRITE_COMPLETE event (and the
 * final chunk if the response is chunked) when ndone equals nbytes
 * and not before.
 *
 * Send our own TS_EVENT_VCONN_WRITE_COMPLETE event upstream when the
 * input is complete otherwise HttpSM::update_stats() won't get called
 * and the transaction won't get logged.  (If there are upstream
 * transformations they won't get a chance to clean up otherwise!)
 *
 * Summary of the cases each event can fall into:
 *
 *    Closed        *We* are "closed".  Clean up allocated data.
 *     │
 *     ├ Start      First (and last) time the handler was called.
 *     │            (This happens when the response is 304 Not
 *     │            Modified.)
 *     │
 *     └ Not start  (This happens when the client or origin disconnect
 *                  before the message is complete.)
 *
 *    Start         First time the handler was called.  Initialize
 *     │            data here because we can't call TSVConnWrite()
 *     │            before TS_HTTP_RESPONSE_TRANSFORM_HOOK.
 *     │
 *     ├ Content length
 *     │
 *     └ Chunked response
 *
 *    Upstream closed
 *                  (This happens when the content length is zero or
 *                  when we get the final chunk of a chunked
 *                  response.)
 *
 *    Available input
 *
 *    Input complete
 *     │
 *     ├ Deja vu    There might be multiple TS_EVENT_IMMEDIATE events
 *     │            between the end of the input and the
 *     │            TS_EVENT_VCONN_WRITE_COMPLETE event from
 *     │            downstream.
 *     │
 *     └ Not deja vu
 *                  Tell downstream and thank upstream.
 *
 *    Downstream complete
 *                  Clean up the output buffer. */

static int
vconn_write_ready(TSCont contp, void * /* edata ATS_UNUSED */)
{
  const char *value;
  int64_t length;

  char digest[32]; /* SHA-256 */

  TransformData *transform_data = static_cast<TransformData *>(TSContDataGet(contp));

  /* Check if we are "closed" before doing anything else to avoid
   * errors.  We *must* check for this case, if only to clean up
   * allocated data.  Some state is already inconsistent.  (This
   * happens if the response is 304 Not Modified or if the client or
   * origin disconnect before the message is complete.) */
  int closed = TSVConnClosedGet(contp);
  if (closed) {
    TSContDestroy(contp);

    /* Avoid failed assert "sdk_sanity_check_iocore_structure(bufp) ==
     * TS_SUCCESS" in TSIOBufferDestroy() if the response is 304 Not
     * Modified */
    if (transform_data->output_bufp) {
      TSIOBufferDestroy(transform_data->output_bufp);
    }

    TSfree(transform_data);

    return 0;
  }

  TSVIO input_viop = TSVConnWriteVIOGet(contp);

  /* Initialize data here because we can't call TSVConnWrite() before
   * TS_HTTP_RESPONSE_TRANSFORM_HOOK */
  if (!transform_data->output_bufp) {
    TSVConn output_connp = TSTransformOutputVConnGet(contp);

    transform_data->output_bufp = TSIOBufferCreate();
    TSIOBufferReader readerp    = TSIOBufferReaderAlloc(transform_data->output_bufp);

    /* Determines the Content-Length header (or a chunked response) */

    /* Reentrant!  Avoid failed assert "nbytes >= 0" if the response
     * is chunked. */
    int nbytes                  = TSVIONBytesGet(input_viop);
    transform_data->output_viop = TSVConnWrite(output_connp, contp, readerp, nbytes < 0 ? INT64_MAX : nbytes);

    SHA256_Init(&transform_data->c);
  }

  /* Then deal with any input that's available now.  Avoid failed
   * assert "sdk_sanity_check_iocore_structure(readerp) == TS_SUCCESS"
   * in TSIOBufferReaderAvail() if the content length is zero or when
   * we get the final chunk of a chunked response. */
  TSIOBufferReader readerp = TSVIOReaderGet(input_viop);
  if (readerp) {
    int avail = TSIOBufferReaderAvail(readerp);
    if (avail) {
      TSIOBufferCopy(transform_data->output_bufp, readerp, avail, 0);

      /* Feed content to the message digest */
      TSIOBufferBlock blockp = TSIOBufferReaderStart(readerp);
      while (blockp) {
        /* No allocation? */
        value = TSIOBufferBlockReadStart(blockp, readerp, &length);
        SHA256_Update(&transform_data->c, value, length);

        blockp = TSIOBufferBlockNext(blockp);
      }

      TSIOBufferReaderConsume(readerp, avail);

      /* Call TSVIONDoneSet() for TSVIONTodoGet() condition */
      int ndone = TSVIONDoneGet(input_viop);
      TSVIONDoneSet(input_viop, ndone + avail);
    }
  }

  /* Check if the input is complete after dealing with any available
   * input in case it was the last of it */
  int ntodo = TSVIONTodoGet(input_viop);
  if (ntodo) {
    TSVIOReenable(transform_data->output_viop);

    TSContCall(TSVIOContGet(input_viop), TS_EVENT_VCONN_WRITE_READY, input_viop);

    /* Don't finish computing the digest (and tell downstream and thank
     * upstream) more than once!  There might be multiple
     * TS_EVENT_IMMEDIATE events between the end of the input and the
     * TS_EVENT_VCONN_WRITE_COMPLETE event from downstream, e.g.
     * INKVConnInternal::reenable() is called by
     * HttpTunnel::producer_handler() when more input is available and
     * TransformVConnection::do_io_shutdown() is called by
     * HttpSM::tunnel_handler_transform_write() when we send our own
     * TS_EVENT_VCONN_WRITE_COMPLETE event upstream. */
  } else if (transform_data->txnp) {
    int ndone = TSVIONDoneGet(input_viop);
    TSVIONBytesSet(transform_data->output_viop, ndone);

    TSVIOReenable(transform_data->output_viop);

    /* Avoid failed assert "c->alive == true" in TSContCall() if the
     * content length is zero or when we get the final chunk of a
     * chunked response */
    if (readerp) {
      TSContCall(TSVIOContGet(input_viop), TS_EVENT_VCONN_WRITE_COMPLETE, input_viop);
    }

    /* Write the digest to the cache */

    SHA256_Final(reinterpret_cast<unsigned char *>(digest), &transform_data->c);

    WriteData *write_data = static_cast<WriteData *>(TSmalloc(sizeof(WriteData)));
    write_data->txnp      = transform_data->txnp;

    /* Don't finish computing the digest more than once! */
    transform_data->txnp = nullptr;

    write_data->key = TSCacheKeyCreate();
    if (TSCacheKeyDigestSet(write_data->key, digest, sizeof(digest)) != TS_SUCCESS) {
      TSCacheKeyDestroy(write_data->key);
      TSfree(write_data);

      return 0;
    }

    /* Can't reuse the TSTransformCreate() continuation because we
     * don't know whether to destroy it in
     * cache_open_write()/cache_open_write_failed() */
    contp = TSContCreate(write_handler, nullptr);
    TSContDataSet(contp, write_data);

    /* Reentrant! */
    TSCacheWrite(contp, write_data->key);
  }

  return 0;
}

/* TSTransformCreate() handler: Compute the SHA-256 digest of the
 * content */

static int
transform_handler(TSCont contp, TSEvent event, void *edata)
{
  switch (event) {
  case TS_EVENT_IMMEDIATE:
  case TS_EVENT_VCONN_WRITE_READY:
    return vconn_write_ready(contp, edata);

  case TS_EVENT_VCONN_WRITE_COMPLETE:
    TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
    break;
  default:
    TSAssert(!"Unexpected event");
  }

  return 0;
}

/* Compute the SHA-256 digest of the content, write it to the cache
 * and store the request URL at that key */

static int
http_read_response_hdr(TSCont /* contp ATS_UNUSED */, void *edata)
{
  TransformData *data = static_cast<TransformData *>(TSmalloc(sizeof(TransformData)));
  data->txnp          = static_cast<TSHttpTxn>(edata);

  /* Can't initialize data here because we can't call TSVConnWrite()
   * before TS_HTTP_RESPONSE_TRANSFORM_HOOK */
  data->output_bufp = nullptr;

  TSVConn connp = TSTransformCreate(transform_handler, data->txnp);
  TSContDataSet(connp, data);

  TSHttpTxnHookAdd(data->txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, connp);

  TSHttpTxnReenable(data->txnp, TS_EVENT_HTTP_CONTINUE);

  return 0;
}

/* Implement TS_HTTP_SEND_RESPONSE_HDR_HOOK to check the Location and
 * Digest headers */

/* Read the URL stored at the digest */

static int
cache_open_read(TSCont contp, void *edata)
{
  SendData *data = static_cast<SendData *>(TSContDataGet(contp));
  data->connp    = static_cast<TSVConn>(edata);

  data->cache_bufp = TSIOBufferCreate();

  /* Reentrant!  Reuse the TSCacheRead() continuation. */
  TSVConnRead(data->connp, contp, data->cache_bufp, INT64_MAX);

  return 0;
}

/* Do nothing, just reenable the response */

static int
cache_open_read_failed(TSCont contp, void * /* edata ATS_UNUSED */)
{
  SendData *data = static_cast<SendData *>(TSContDataGet(contp));
  TSContDestroy(contp);

  TSCacheKeyDestroy(data->key);

  TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->url_loc);
  TSHandleMLocRelease(data->resp_bufp, data->hdr_loc, data->location_loc);
  TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->hdr_loc);

  TSHttpTxnReenable(data->txnp, TS_EVENT_HTTP_CONTINUE);
  TSfree(data);

  return 0;
}

/* TSCacheRead() handler: Check if the URL stored at the digest is
 * cached */

static int
rewrite_handler(TSCont contp, TSEvent event, void * /* edata ATS_UNUSED */)
{
  SendData *data = static_cast<SendData *>(TSContDataGet(contp));
  TSContDestroy(contp);

  TSCacheKeyDestroy(data->key);

  switch (event) {
  /* Yes: Rewrite the Location header and reenable the response */
  case TS_EVENT_CACHE_OPEN_READ:

    TSMimeHdrFieldValuesClear(data->resp_bufp, data->hdr_loc, data->location_loc);
    TSMimeHdrFieldValueStringInsert(data->resp_bufp, data->hdr_loc, data->location_loc, -1, data->value, data->length);

    break;

  /* No: Do nothing, just reenable the response */
  case TS_EVENT_CACHE_OPEN_READ_FAILED:
    break;

  default:
    TSAssert(!"Unexpected event");
  }

  TSIOBufferDestroy(data->cache_bufp);

  TSHandleMLocRelease(data->resp_bufp, data->hdr_loc, data->location_loc);
  TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->hdr_loc);

  TSHttpTxnReenable(data->txnp, TS_EVENT_HTTP_CONTINUE);
  TSfree(data);

  return 0;
}

/* Read the URL stored at the digest */

static int
vconn_read_ready(TSCont contp, void * /* edata ATS_UNUSED */)
{
  SendData *data = static_cast<SendData *>(TSContDataGet(contp));
  TSContDestroy(contp);

  TSVConnClose(data->connp);

  TSIOBufferReader readerp = TSIOBufferReaderAlloc(data->cache_bufp);

  TSIOBufferBlock blockp = TSIOBufferReaderStart(readerp);

  /* No allocation, freed with data->cache_bufp? */
  const char *value = data->value = TSIOBufferBlockReadStart(blockp, readerp, &data->length);

  /* The start pointer is both an input and an output parameter.
   * After a successful parse the start pointer equals the end
   * pointer. */
  if (TSUrlParse(data->resp_bufp, data->url_loc, &value, value + data->length) != TS_PARSE_DONE) {
    TSIOBufferDestroy(data->cache_bufp);

    TSCacheKeyDestroy(data->key);

    TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->url_loc);
    TSHandleMLocRelease(data->resp_bufp, data->hdr_loc, data->location_loc);
    TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->hdr_loc);

    TSHttpTxnReenable(data->txnp, TS_EVENT_HTTP_CONTINUE);
    TSfree(data);

    return 0;
  }

  if (TSCacheKeyDigestFromUrlSet(data->key, data->url_loc) != TS_SUCCESS) {
    TSIOBufferDestroy(data->cache_bufp);

    TSCacheKeyDestroy(data->key);

    TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->url_loc);
    TSHandleMLocRelease(data->resp_bufp, data->hdr_loc, data->location_loc);
    TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->hdr_loc);

    TSHttpTxnReenable(data->txnp, TS_EVENT_HTTP_CONTINUE);
    TSfree(data);

    return 0;
  }

  TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->url_loc);

  /* Check if the URL stored at the digest is cached */

  contp = TSContCreate(rewrite_handler, nullptr);
  TSContDataSet(contp, data);

  /* Reentrant!  (Particularly in case of a cache miss.)
   * rewrite_handler() will clean up the TSVConnRead() buffer so be
   * sure to close this virtual connection or CacheVC::openReadMain()
   * will continue operating on it! */
  TSCacheRead(contp, data->key);

  return 0;
}

/* TSCacheRead() and TSVConnRead() handler: Check if the digest
 * already exists in the cache */

static int
digest_handler(TSCont contp, TSEvent event, void *edata)
{
  switch (event) {
  /* Yes: Read the URL stored at that key */
  case TS_EVENT_CACHE_OPEN_READ:
    return cache_open_read(contp, edata);

  /* No: Do nothing, just reenable the response */
  case TS_EVENT_CACHE_OPEN_READ_FAILED:
    return cache_open_read_failed(contp, edata);

  case TS_EVENT_VCONN_READ_READY:
    return vconn_read_ready(contp, edata);

  default:
    TSAssert(!"Unexpected event");
  }

  return 0;
}

/* TSCacheRead() handler: Check if the Location URL is already cached */

static int
location_handler(TSCont contp, TSEvent event, void * /* edata ATS_UNUSED */)
{
  const char *value;
  int length;

  char digest[33]; /* ATS_BASE64_DECODE_DSTLEN() */

  SendData *data = static_cast<SendData *>(TSContDataGet(contp));
  TSContDestroy(contp);

  switch (event) {
  /* Yes: Do nothing, just reenable the response */
  case TS_EVENT_CACHE_OPEN_READ:
    break;

  /* No: Check if the digest already exists in the cache */
  case TS_EVENT_CACHE_OPEN_READ_FAILED:

    /* No allocation, freed with data->resp_bufp? */
    value = TSMimeHdrFieldValueStringGet(data->resp_bufp, data->hdr_loc, data->digest_loc, data->idx, &length);
    if (TSBase64Decode(value + 8, length - 8, reinterpret_cast<unsigned char *>(digest), sizeof(digest), nullptr) != TS_SUCCESS ||
        TSCacheKeyDigestSet(data->key, digest, 32 /* SHA-256 */) != TS_SUCCESS) {
      break;
    }

    TSHandleMLocRelease(data->resp_bufp, data->hdr_loc, data->digest_loc);

    /* Check if the digest already exists in the cache */

    contp = TSContCreate(digest_handler, nullptr);
    TSContDataSet(contp, data);

    /* Reentrant! */
    TSCacheRead(contp, data->key);

    return 0;

  default:
    TSAssert(!"Unexpected event");
  }

  TSHandleMLocRelease(data->resp_bufp, data->hdr_loc, data->digest_loc);

  TSCacheKeyDestroy(data->key);

  TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->url_loc);
  TSHandleMLocRelease(data->resp_bufp, data->hdr_loc, data->location_loc);
  TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->hdr_loc);

  TSHttpTxnReenable(data->txnp, TS_EVENT_HTTP_CONTINUE);
  TSfree(data);

  return 0;
}

/* Use TSCacheRead() to check if the URL in the Location header is
 * already cached.  If not, potentially rewrite that header.  Do this
 * after responses are cached because the cache will change. */

static int
http_send_response_hdr(TSCont contp, void *edata)
{
  const char *value;
  int length;

  SendData *data = static_cast<SendData *>(TSmalloc(sizeof(SendData)));
  data->txnp     = static_cast<TSHttpTxn>(edata);

  if (TSHttpTxnClientRespGet(data->txnp, &data->resp_bufp, &data->hdr_loc) != TS_SUCCESS) {
    TSError("[metalink] Couldn't retrieve client response header");

    TSHttpTxnReenable(data->txnp, TS_EVENT_HTTP_CONTINUE);
    TSfree(data);

    return 0;
  }

  /* If Instance Digests are not provided by the Metalink servers, the
   * Link header fields pertaining to this specification MUST be
   * ignored */

  /* Metalinks contain whole file hashes as described in Section 6,
   * and MUST include SHA-256, as specified in [FIPS-180-3] */

  /* Assumption: We want to minimize cache reads, so check first that
   *
   *    1.  the response has a Location header and
   *
   *    2.  the response has a Digest header.
   *
   * Then scan if the URL or digest already exist in the cache. */

  /* If the response has a Location header */
  data->location_loc = TSMimeHdrFieldFind(data->resp_bufp, data->hdr_loc, TS_MIME_FIELD_LOCATION, TS_MIME_LEN_LOCATION);
  if (!data->location_loc) {
    TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->hdr_loc);

    TSHttpTxnReenable(data->txnp, TS_EVENT_HTTP_CONTINUE);
    TSfree(data);

    return 0;
  }

  TSUrlCreate(data->resp_bufp, &data->url_loc);

  /* If we can't parse or lookup the Location URL, should we still
   * check if the response has a Digest header?  No: Can't parse or
   * lookup the URL in the Location header is an error. */

  /* No allocation, freed with data->resp_bufp? */
  value = TSMimeHdrFieldValueStringGet(data->resp_bufp, data->hdr_loc, data->location_loc, -1, &length);
  if (TSUrlParse(data->resp_bufp, data->url_loc, &value, value + length) != TS_PARSE_DONE) {
    TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->url_loc);
    TSHandleMLocRelease(data->resp_bufp, data->hdr_loc, data->location_loc);
    TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->hdr_loc);

    TSHttpTxnReenable(data->txnp, TS_EVENT_HTTP_CONTINUE);
    TSfree(data);

    return 0;
  }

  data->key = TSCacheKeyCreate();
  if (TSCacheKeyDigestFromUrlSet(data->key, data->url_loc) != TS_SUCCESS) {
    TSCacheKeyDestroy(data->key);

    TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->url_loc);
    TSHandleMLocRelease(data->resp_bufp, data->hdr_loc, data->location_loc);
    TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->hdr_loc);

    TSHttpTxnReenable(data->txnp, TS_EVENT_HTTP_CONTINUE);
    TSfree(data);

    return 0;
  }

  /* ... and a Digest header */
  data->digest_loc = TSMimeHdrFieldFind(data->resp_bufp, data->hdr_loc, "Digest", 6);
  while (data->digest_loc) {
    int count = TSMimeHdrFieldValuesCount(data->resp_bufp, data->hdr_loc, data->digest_loc);
    for (data->idx = 0; data->idx < count; data->idx += 1) {
      /* No allocation, freed with data->resp_bufp? */
      value = TSMimeHdrFieldValueStringGet(data->resp_bufp, data->hdr_loc, data->digest_loc, data->idx, &length);
      if (length < 8 + 44 /* 32 bytes, Base64 */ || strncasecmp(value, "SHA-256=", 8)) {
        continue;
      }

      /* Check if the Location URL is already cached */

      contp = TSContCreate(location_handler, nullptr);
      TSContDataSet(contp, data);

      /* Reentrant! */
      TSCacheRead(contp, data->key);

      return 0;
    }

    TSMLoc next_loc = TSMimeHdrFieldNextDup(data->resp_bufp, data->hdr_loc, data->digest_loc);

    TSHandleMLocRelease(data->resp_bufp, data->hdr_loc, data->digest_loc);

    data->digest_loc = next_loc;
  }

  /* Didn't find a Digest header, just reenable the response */

  TSCacheKeyDestroy(data->key);

  TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->url_loc);
  TSHandleMLocRelease(data->resp_bufp, data->hdr_loc, data->location_loc);
  TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->hdr_loc);

  TSHttpTxnReenable(data->txnp, TS_EVENT_HTTP_CONTINUE);
  TSfree(data);

  return 0;
}

static int
handler(TSCont contp, TSEvent event, void *edata)
{
  switch (event) {
  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    return http_read_response_hdr(contp, edata);

  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    return http_send_response_hdr(contp, edata);

  default:
    TSAssert(!"Unexpected event");
  }

  return 0;
}

void
TSPluginInit(int /* argc ATS_UNUSED */, const char * /* argv ATS_UNUSED */[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = (char *)"metalink";
  info.vendor_name   = (char *)"Apache Software Foundation";
  info.support_email = (char *)"dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[metalink] Plugin registration failed");
  }

  TSCont contp = TSContCreate(handler, nullptr);

  TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, contp);
  TSHttpHookAdd(TS_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
}
