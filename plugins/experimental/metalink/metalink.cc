/** @file

    Implement the Metalink protocol to "dedup" cache entries for
    equivalent content. This can for example improve the cache hit
    ratio for content with many different (unique) URLs.

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


/*
  This plugin was originally developed by Jack Bates during his Google
  Summer of Code 2012 project for Metalinker.
*/


#include <stdio.h>
#include <string.h>

#include <openssl/sha.h>

#include "ts/ts.h"
#include "ink_defs.h"

/* Implement TS_HTTP_READ_RESPONSE_HDR_HOOK to implement a null transform.
 * Compute the SHA-256 digest of the content, write it to the cache and store
 * the request URL at that key.
 *
 * Implement TS_HTTP_SEND_RESPONSE_HDR_HOOK to check the "Location: ..." and
 * "Digest: SHA-256=..." headers.  Use TSCacheRead() to check if the URL in the
 * "Location: ..." header is already cached.  If not, potentially rewrite that
 * header.  Do this after responses are cached because the cache will change.
 *
 * More details are on the [wiki page] in the Traffic Server wiki.
 *
 * [wiki page]  https://cwiki.apache.org/confluence/display/TS/Metalink */

/* TSVConnWrite() data: Store the request URL */

typedef struct {
  TSVConn connp;
  TSIOBuffer bufp;

} WriteData;

/* TSTransformCreate() and TSCacheWrite() data: Compute the SHA-256 digest of
 * the content and write it to the cache */

typedef struct {
  TSHttpTxn txnp;

  /* Null transform */
  TSIOBuffer output_bufp;
  TSVIO output_viop;

  /* Message digest handle */
  SHA256_CTX c;

  TSCacheKey key;

} TransformData;

/* TSCacheRead() and TSVConnRead() data: Check the "Location: ..." and
 * "Digest: SHA-256=..." headers */

typedef struct {
  TSHttpTxn txnp;

  TSMBuffer resp_bufp;
  TSMLoc hdr_loc;

  /* "Location: ..." header */
  TSMLoc location_loc;

  /* Cache key */
  TSMLoc url_loc;
  TSCacheKey key;

  /* "Digest: SHA-256=..." header */
  TSMLoc digest_loc;

  /* Digest header field value index */
  int idx;

  TSIOBuffer read_bufp;

} SendData;

/* Implement TS_HTTP_READ_RESPONSE_HDR_HOOK to implement a null transform */

/* Store the request URL */

static int
write_vconn_write_complete(TSCont contp, void * /* edata ATS_UNUSED */)
{
  WriteData *data = (WriteData *) TSContDataGet(contp);
  TSContDestroy(contp);

  /* The object is not committed to the cache until the vconnection is closed.
   * When all the data has been transferred, the user (contp) must do a
   * TSVConnClose() */
  TSVConnClose(data->connp);

  TSIOBufferDestroy(data->bufp);
  TSfree(data);

  return 0;
}

/* TSVConnWrite() handler: Store the request URL */

static int
write_handler(TSCont contp, TSEvent event, void *edata)
{
  switch (event) {
  case TS_EVENT_VCONN_WRITE_COMPLETE:
    return write_vconn_write_complete(contp, edata);

  default:
    TSAssert(!"Unexpected event");
  }

  return 0;
}

/* Compute the SHA-256 digest of the content, write it to the cache and store
 * the request URL at that key */

static int
cache_open_write(TSCont contp, void *edata)
{
  TSMBuffer req_bufp;

  TSMLoc hdr_loc;
  TSMLoc url_loc;

  const char *value;
  int length;

  TransformData *transform_data = (TransformData *) TSContDataGet(contp);
  TSContDestroy(contp);

  TSCacheKeyDestroy(transform_data->key);

  if (TSHttpTxnClientReqGet(transform_data->txnp, &req_bufp, &hdr_loc) != TS_SUCCESS) {
    TSError("Couldn't retrieve client request header");

    TSfree(transform_data);

    return 0;
  }

  TSfree(transform_data);

  if (TSHttpHdrUrlGet(req_bufp, hdr_loc, &url_loc) != TS_SUCCESS) {
    TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, hdr_loc);

    return 0;
  }

  value = TSUrlStringGet(req_bufp, url_loc, &length);
  if (!value) {
    TSHandleMLocRelease(req_bufp, hdr_loc, url_loc);
    TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, hdr_loc);

    return 0;
  }

  TSHandleMLocRelease(req_bufp, hdr_loc, url_loc);
  TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, hdr_loc);

  /* Store the request URL */

  WriteData *write_data = (WriteData *) TSmalloc(sizeof(WriteData));
  write_data->connp = (TSVConn) edata;

  /* Can't reuse TSTransformCreate() continuation because it already implements
   * TS_EVENT_VCONN_WRITE_COMPLETE */
  contp = TSContCreate(write_handler, NULL);
  TSContDataSet(contp, write_data);

  write_data->bufp = TSIOBufferCreate();
  TSIOBufferReader readerp = TSIOBufferReaderAlloc(write_data->bufp);

  int nbytes = TSIOBufferWrite(write_data->bufp, value, length);

  TSVConnWrite(write_data->connp, contp, readerp, nbytes);

  return 0;
}

/* Do nothing */

static int
cache_open_write_failed(TSCont contp, void * /* edata ATS_UNUSED */)
{
  TransformData *data = (TransformData *) TSContDataGet(contp);
  TSContDestroy(contp);

  TSCacheKeyDestroy(data->key);
  TSfree(data);

  return 0;
}

/* Copy content from the input buffer to the output buffer without modification
 * while at the same time feeding it to the message digest */

static int
vconn_write_ready(TSCont contp, void * /* edata ATS_UNUSED */)
{
  const char *value;
  int64_t length;
  TransformData *data = (TransformData *) TSContDataGet(contp);

  TSVIO input_viop = TSVConnWriteVIOGet(contp);

  /* Initialize data here because can't call TSVConnWrite() before
   * TS_HTTP_RESPONSE_TRANSFORM_HOOK */
  if (!data->output_bufp) {

    /* Avoid failed assert "sdk_sanity_check_iocore_structure(connp) ==
     * TS_SUCCESS" in TSVConnWrite() if the response is 304 Not Modified */
    TSVConn output_connp = TSTransformOutputVConnGet(contp);
    if (!output_connp) {
      TSContDestroy(contp);

      TSfree(data);

      return 0;
    }

    data->output_bufp = TSIOBufferCreate();
    TSIOBufferReader readerp = TSIOBufferReaderAlloc(data->output_bufp);

    /* Determines the "Content-Length: ..." header
     * (or "Transfer-Encoding: chunked") */

    /* Avoid failed assert "nbytes >= 0" if "Transfer-Encoding: chunked" */
    int nbytes = TSVIONBytesGet(input_viop);
    data->output_viop = TSVConnWrite(output_connp, contp, readerp, nbytes < 0 ? INT64_MAX : nbytes);

    SHA256_Init(&data->c);
  }

  /* If the response has a "Content-Length: ..." header then ntodo will never
   * be zero because there will instead be a TS_EVENT_VCONN_WRITE_COMPLETE
   * event from downstream after nbytes of content.
   *
   * Otherwise (if the response is "Transfer-Encoding: chunked") ntodo will be
   * zero when the upstream nbytes is known at the end of the content, because
   * there won't be a TS_EVENT_VCONN_WRITE_COMPLETE event while the downstream
   * nbytes is INT64_MAX.
   *
   * In that case to get it to send a TS_EVENT_VCONN_WRITE_COMPLETE event,
   * update the downstream nbytes and reenable it.  Zero the downstream nbytes
   * is a shortcut. */
  int ntodo = TSVIONTodoGet(input_viop);
  if (!ntodo) {
    TSVIONBytesSet(data->output_viop, 0);

    TSVIOReenable(data->output_viop);

    return 0;
  }

  /* Avoid failed assert "sdk_sanity_check_iocore_structure(readerp) ==
   * TS_SUCCESS" in TSIOBufferReaderAvail() if the client or server disconnects
   * or the content length is zero.
   *
   * Don't update the downstream nbytes and reenable it because if not at the
   * end yet and can't read any more content then can't compute the digest.
   *
   * (There hasn't been a TS_EVENT_VCONN_WRITE_COMPLETE event from downstream
   * yet so if the response has a "Content-Length: ..." header, it is greater
   * than the content so far.  ntodo is still greater than zero so if the
   * response is "Transfer-Encoding: chunked", not at the end yet.) */
  TSIOBufferReader readerp = TSVIOReaderGet(input_viop);
  if (!readerp) {
    TSContDestroy(contp);

    TSIOBufferDestroy(data->output_bufp);
    TSfree(data);

    return 0;
  }

  int avail = TSIOBufferReaderAvail(readerp);

  if (avail) {
    TSIOBufferCopy(data->output_bufp, readerp, avail, 0);

    /* Feed content to the message digest */
    TSIOBufferBlock blockp = TSIOBufferReaderStart(readerp);
    while (blockp) {

      value = TSIOBufferBlockReadStart(blockp, readerp, &length);
      SHA256_Update(&data->c, value, length);

      blockp = TSIOBufferBlockNext(blockp);
    }

    TSIOBufferReaderConsume(readerp, avail);

    /* Call TSVIONDoneSet() for TSVIONTodoGet() condition */
    int ndone = TSVIONDoneGet(input_viop);
    TSVIONDoneSet(input_viop, ndone + avail);

    TSVIOReenable(data->output_viop);

    TSContCall(TSVIOContGet(input_viop), TS_EVENT_VCONN_WRITE_READY, input_viop);
  }

  return 0;
}

/* Write the digest to the cache */

static int
transform_vconn_write_complete(TSCont contp, void * /* edata ATS_UNUSED */)
{
  char digest[32];

  TransformData *data = (TransformData *) TSContDataGet(contp);

  TSIOBufferDestroy(data->output_bufp);

  SHA256_Final((unsigned char *) digest, &data->c);

  data->key = TSCacheKeyCreate();
  if (TSCacheKeyDigestSet(data->key, digest, sizeof(digest)) != TS_SUCCESS) {
    TSContDestroy(contp);

    TSfree(data);

    return 0;
  }

  /* Reuse the TSTransformCreate() continuation */
  TSCacheWrite(contp, data->key);

  return 0;
}

/* TSTransformCreate() and TSCacheWrite() handler */

static int
transform_handler(TSCont contp, TSEvent event, void *edata)
{
  switch (event) {
  case TS_EVENT_CACHE_OPEN_WRITE:
    return cache_open_write(contp, edata);

  case TS_EVENT_CACHE_OPEN_WRITE_FAILED:
    return cache_open_write_failed(contp, edata);

  case TS_EVENT_IMMEDIATE:
  case TS_EVENT_VCONN_WRITE_READY:
    return vconn_write_ready(contp, edata);

  case TS_EVENT_VCONN_WRITE_COMPLETE:
    return transform_vconn_write_complete(contp, edata);

  default:
    TSAssert(!"Unexpected event");
  }

  return 0;
}

/* Compute the SHA-256 digest of the content, write it to the cache and store
 * the request URL at that key */

static int
http_read_response_hdr(TSCont /* contp ATS_UNUSED */, void *edata)
{
  TransformData *data = (TransformData *) TSmalloc(sizeof(TransformData));
  data->txnp = (TSHttpTxn) edata;

  /* Can't initialize data here because can't call TSVConnWrite() before
   * TS_HTTP_RESPONSE_TRANSFORM_HOOK */
  data->output_bufp = NULL;

  TSVConn connp = TSTransformCreate(transform_handler, data->txnp);
  TSContDataSet(connp, data);

  TSHttpTxnHookAdd(data->txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, connp);

  TSHttpTxnReenable(data->txnp, TS_EVENT_HTTP_CONTINUE);

  return 0;
}

/* Implement TS_HTTP_SEND_RESPONSE_HDR_HOOK to check the "Location: ..." and
 * "Digest: SHA-256=..." headers */

/* Read the URL stored at the digest */

static int
cache_open_read(TSCont contp, void *edata)
{
  SendData *data = (SendData *) TSContDataGet(contp);
  TSVConn connp = (TSVConn) edata;

  data->read_bufp = TSIOBufferCreate();

  /* Reuse the TSCacheRead() continuation */
  TSVConnRead(connp, contp, data->read_bufp, INT64_MAX);

  return 0;
}

/* Do nothing, just reenable the response */

static int
cache_open_read_failed(TSCont contp, void * /* edata ATS_UNUSED */)
{
  SendData *data = (SendData *) TSContDataGet(contp);
  TSContDestroy(contp);

  TSCacheKeyDestroy(data->key);

  TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->url_loc);
  TSHandleMLocRelease(data->resp_bufp, data->hdr_loc, data->location_loc);
  TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->hdr_loc);

  TSHttpTxnReenable(data->txnp, TS_EVENT_HTTP_CONTINUE);
  TSfree(data);

  return 0;
}

/* TSCacheRead() handler: Check if the URL stored at the digest is cached */

static int
rewrite_handler(TSCont contp, TSEvent event, void * /* edata ATS_UNUSED */)
{
  const char *value;
  int length;

  SendData *data = (SendData *) TSContDataGet(contp);
  TSContDestroy(contp);

  switch (event) {

  /* Yes: Rewrite the "Location: ..." header and reenable the response */
  case TS_EVENT_CACHE_OPEN_READ:
    value = TSUrlStringGet(data->resp_bufp, data->url_loc, &length);
    TSMimeHdrFieldValuesClear(data->resp_bufp, data->hdr_loc, data->location_loc);
    TSMimeHdrFieldValueStringInsert(data->resp_bufp, data->hdr_loc, data->location_loc, -1, value, length);
    break;

  /* No: Do nothing, just reenable the response */
  case TS_EVENT_CACHE_OPEN_READ_FAILED:
    break;

  default:
    TSAssert(!"Unexpected event");
  }

  TSCacheKeyDestroy(data->key);

  TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->url_loc);
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
  const char *value;
  int64_t length;
  SendData *data = (SendData *) TSContDataGet(contp);

  TSContDestroy(contp);

  TSIOBufferReader readerp = TSIOBufferReaderAlloc(data->read_bufp);
  TSIOBufferBlock blockp = TSIOBufferReaderStart(readerp);

  value = TSIOBufferBlockReadStart(blockp, readerp, &length);
  if (TSUrlParse(data->resp_bufp, data->url_loc, &value, value + length) != TS_PARSE_DONE) {
    TSIOBufferDestroy(data->read_bufp);

    TSCacheKeyDestroy(data->key);

    TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->url_loc);
    TSHandleMLocRelease(data->resp_bufp, data->hdr_loc, data->location_loc);
    TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->hdr_loc);

    TSHttpTxnReenable(data->txnp, TS_EVENT_HTTP_CONTINUE);
    TSfree(data);

    return 0;
  }

  TSIOBufferDestroy(data->read_bufp);

  if (TSCacheKeyDigestFromUrlSet(data->key, data->url_loc) != TS_SUCCESS) {
    TSCacheKeyDestroy(data->key);

    TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->url_loc);
    TSHandleMLocRelease(data->resp_bufp, data->hdr_loc, data->location_loc);
    TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->hdr_loc);

    TSHttpTxnReenable(data->txnp, TS_EVENT_HTTP_CONTINUE);
    TSfree(data);

    return 0;
  }

  /* Check if the URL stored at the digest is cached */

  contp = TSContCreate(rewrite_handler, NULL);
  TSContDataSet(contp, data);

  TSCacheRead(contp, data->key);

  return 0;
}

/* TSCacheRead() and TSVConnRead() handler: Check if the "Digest: SHA-256=..."
 * digest already exists in the cache */

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

/* TSCacheRead() handler: Check if the "Location: ..." URL is already cached */

static int
location_handler(TSCont contp, TSEvent event, void * /* edata ATS_UNUSED */)
{
  const char *value;
  int length;

  /* ATS_BASE64_DECODE_DSTLEN() */
  char digest[33];

  SendData *data = (SendData *) TSContDataGet(contp);
  TSContDestroy(contp);

  switch (event) {
  /* Yes: Do nothing, just reenable the response */
  case TS_EVENT_CACHE_OPEN_READ:
    break;

  /* No: Check if the "Digest: SHA-256=..." digest already exists in the cache */
  case TS_EVENT_CACHE_OPEN_READ_FAILED:

    value = TSMimeHdrFieldValueStringGet(data->resp_bufp, data->hdr_loc, data->digest_loc, data->idx, &length);
    if (TSBase64Decode(value + 8, length - 8, (unsigned char *) digest, sizeof(digest), NULL) != TS_SUCCESS
        || TSCacheKeyDigestSet(data->key, digest, 32 /* SHA-256 */ ) != TS_SUCCESS) {
      break;
    }

    contp = TSContCreate(digest_handler, NULL);
    TSContDataSet(contp, data);

    TSCacheRead(contp, data->key);
    TSHandleMLocRelease(data->resp_bufp, data->hdr_loc, data->digest_loc);

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

/* Use TSCacheRead() to check if the URL in the "Location: ..." header is
 * already cached.  If not, potentially rewrite that header.  Do this after
 * responses are cached because the cache will change. */

static int
http_send_response_hdr(TSCont contp, void *edata)
{
  const char *value;
  int length;

  SendData *data = (SendData *) TSmalloc(sizeof(SendData));

  data->txnp = (TSHttpTxn) edata;
  if (TSHttpTxnClientRespGet(data->txnp, &data->resp_bufp, &data->hdr_loc) != TS_SUCCESS) {
    TSError("Couldn't retrieve client response header");

    TSHttpTxnReenable(data->txnp, TS_EVENT_HTTP_CONTINUE);
    TSfree(data);

    return 0;
  }

  /* If Instance Digests are not provided by the Metalink servers, the Link
   * header fields pertaining to this specification MUST be ignored */

  /* Metalinks contain whole file hashes as described in Section 6, and MUST
   * include SHA-256, as specified in [FIPS-180-3] */

  /* Assumption: Want to minimize cache read, so check first that:
   *
   *   1. response has a "Location: ..." header
   *   2. response has a "Digest: SHA-256=..." header
   *
   * Then scan if the URL or digest already exist in the cache */

  /* If the response has a "Location: ..." header */
  data->location_loc = TSMimeHdrFieldFind(data->resp_bufp, data->hdr_loc, TS_MIME_FIELD_LOCATION, TS_MIME_LEN_LOCATION);
  if (!data->location_loc) {
    TSHandleMLocRelease(data->resp_bufp, TS_NULL_MLOC, data->hdr_loc);

    TSHttpTxnReenable(data->txnp, TS_EVENT_HTTP_CONTINUE);
    TSfree(data);

    return 0;
  }

  TSUrlCreate(data->resp_bufp, &data->url_loc);

  /* If can't parse or lookup the "Location: ..." URL, should still check if
   * the response has a "Digest: SHA-256=..." header?  No: Can't parse or
   * lookup the URL in the "Location: ..." header is an error. */
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

  /* ... and a "Digest: SHA-256=..." header */
  data->digest_loc = TSMimeHdrFieldFind(data->resp_bufp, data->hdr_loc, "Digest", 6);
  while (data->digest_loc) {

    int count = TSMimeHdrFieldValuesCount(data->resp_bufp, data->hdr_loc, data->digest_loc);
    for (data->idx = 0; data->idx < count; data->idx += 1) {

      value = TSMimeHdrFieldValueStringGet(data->resp_bufp, data->hdr_loc, data->digest_loc, data->idx, &length);
      if (length < 8 + 44 /* 32 bytes, Base64 */ || strncasecmp(value, "SHA-256=", 8)) {
        continue;
      }

      /* Check if the "Location: ..." URL is already cached */

      contp = TSContCreate(location_handler, NULL);
      TSContDataSet(contp, data);

      TSCacheRead(contp, data->key);

      return 0;
    }

    TSMLoc next_loc = TSMimeHdrFieldNextDup(data->resp_bufp, data->hdr_loc, data->digest_loc);

    TSHandleMLocRelease(data->resp_bufp, data->hdr_loc, data->digest_loc);

    data->digest_loc = next_loc;
  }

  /* Didn't find a "Digest: SHA-256=..." header, just reenable the response */

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
TSPluginInit(int /* argc ATS_UNUSED */, const char */* argv ATS_UNUSED */[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name = const_cast<char*>("metalink");
  info.vendor_name = const_cast<char*>("Apache Software Foundation");
  info.support_email = const_cast<char*>("dev@trafficserver.apache.org");

  if (TSPluginRegister(TS_SDK_VERSION_3_0, &info) != TS_SUCCESS) {
    TSError("Plugin registration failed");
  }

  TSCont contp = TSContCreate(handler, NULL);

  TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, contp);
  TSHttpHookAdd(TS_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
}
