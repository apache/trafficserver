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

#include "mp4_common.h"

static char *ts_arg(const char *param, size_t param_len, const char *key, size_t key_len, size_t *val_len);
static int mp4_handler(TSCont contp, TSEvent event, void *edata);
static void mp4_cache_lookup_complete(Mp4Context *mc, TSHttpTxn txnp);
static void mp4_read_response(Mp4Context *mc, TSHttpTxn txnp);
static void mp4_add_transform(Mp4Context *mc, TSHttpTxn txnp);
static int mp4_transform_entry(TSCont contp, TSEvent event, void *edata);
static int mp4_transform_handler(TSCont contp, Mp4Context *mc);
static int mp4_parse_meta(Mp4TransformContext *mtc, bool body_complete);

TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  if (!api_info) {
    snprintf(errbuf, errbuf_size, "[TSRemapInit] - Invalid TSRemapInterface argument");
    return TS_ERROR;
  }

  if (api_info->size < sizeof(TSRemapInterface)) {
    snprintf(errbuf, errbuf_size, "[TSRemapInit] - Incorrect size of TSRemapInterface structure");
    return TS_ERROR;
  }

  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char ** /* argv ATS_UNUSED */, void **ih, char *errbuf, int errbuf_size)
{
  if (argc > 2) {
    snprintf(errbuf, errbuf_size, "[TSRemapNewInstance] - Argument should be removed");
  }

  *ih = nullptr;
  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void * /* ih ATS_UNUSED */)
{
  return;
}

TSRemapStatus
TSRemapDoRemap(void * /* ih ATS_UNUSED */, TSHttpTxn rh, TSRemapRequestInfo *rri)
{
  const char *method, *query, *path;
  int method_len, query_len, path_len;
  size_t val_len;
  const char *val;
  int ret;
  float start;
  char buf[1024];
  int buf_len;
  int left, right;
  TSMLoc ae_field, range_field;
  TSCont contp;
  Mp4Context *mc;

  method = TSHttpHdrMethodGet(rri->requestBufp, rri->requestHdrp, &method_len);
  if (method != TS_HTTP_METHOD_GET) {
    return TSREMAP_NO_REMAP;
  }

  // check suffix
  path = TSUrlPathGet(rri->requestBufp, rri->requestUrl, &path_len);

  if (path == nullptr || path_len <= 4) {
    return TSREMAP_NO_REMAP;

  } else if (strncasecmp(path + path_len - 4, ".mp4", 4) != 0) {
    return TSREMAP_NO_REMAP;
  }

  start = 0;
  query = TSUrlHttpQueryGet(rri->requestBufp, rri->requestUrl, &query_len);

  val = ts_arg(query, query_len, "start", sizeof("start") - 1, &val_len);
  if (val != nullptr) {
    ret = sscanf(val, "%f", &start);
    if (ret != 1) {
      start = 0;
    }
  }

  if (start == 0) {
    return TSREMAP_NO_REMAP;

  } else if (start < 0) {
    TSHttpTxnStatusSet(rh, TS_HTTP_STATUS_BAD_REQUEST);
    TSHttpTxnErrorBodySet(rh, TSstrdup("Invalid request."), sizeof("Invalid request.") - 1, nullptr);
  }

  // reset args
  left  = val - sizeof("start") - query;
  right = query + query_len - val - val_len;

  if (left > 0) {
    left--;
  }

  if (left == 0 && right > 0) {
    right--;
  }

  buf_len = sprintf(buf, "%.*s%.*s", left, query, right, query + query_len - right);
  TSUrlHttpQuerySet(rri->requestBufp, rri->requestUrl, buf, buf_len);

  // remove Accept-Encoding
  ae_field = TSMimeHdrFieldFind(rri->requestBufp, rri->requestHdrp, TS_MIME_FIELD_ACCEPT_ENCODING, TS_MIME_LEN_ACCEPT_ENCODING);
  if (ae_field) {
    TSMimeHdrFieldDestroy(rri->requestBufp, rri->requestHdrp, ae_field);
    TSHandleMLocRelease(rri->requestBufp, rri->requestHdrp, ae_field);
  }

  // remove Range
  range_field = TSMimeHdrFieldFind(rri->requestBufp, rri->requestHdrp, TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE);
  if (range_field) {
    TSMimeHdrFieldDestroy(rri->requestBufp, rri->requestHdrp, range_field);
    TSHandleMLocRelease(rri->requestBufp, rri->requestHdrp, range_field);
  }

  mc    = new Mp4Context(start);
  contp = TSContCreate(mp4_handler, nullptr);
  TSContDataSet(contp, mc);

  TSHttpTxnHookAdd(rh, TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, contp);
  TSHttpTxnHookAdd(rh, TS_HTTP_READ_RESPONSE_HDR_HOOK, contp);
  TSHttpTxnHookAdd(rh, TS_HTTP_TXN_CLOSE_HOOK, contp);
  return TSREMAP_NO_REMAP;
}

static int
mp4_handler(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp;
  Mp4Context *mc;

  txnp = static_cast<TSHttpTxn>(edata);
  mc   = static_cast<Mp4Context *>(TSContDataGet(contp));

  switch (event) {
  case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
    mp4_cache_lookup_complete(mc, txnp);
    break;

  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    mp4_read_response(mc, txnp);
    break;

  case TS_EVENT_HTTP_TXN_CLOSE:
    delete mc;
    TSContDestroy(contp);
    break;

  default:
    break;
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

static void
mp4_cache_lookup_complete(Mp4Context *mc, TSHttpTxn txnp)
{
  TSMBuffer bufp;
  TSMLoc hdrp;
  TSMLoc cl_field;
  TSHttpStatus code;
  int obj_status;
  int64_t n;

  if (TSHttpTxnCacheLookupStatusGet(txnp, &obj_status) == TS_ERROR) {
    TSError("[%s] Couldn't get cache status of object", __FUNCTION__);
    return;
  }

  if (obj_status != TS_CACHE_LOOKUP_HIT_STALE && obj_status != TS_CACHE_LOOKUP_HIT_FRESH) {
    return;
  }

  if (TSHttpTxnCachedRespGet(txnp, &bufp, &hdrp) != TS_SUCCESS) {
    TSError("[%s] Couldn't get cache resp", __FUNCTION__);
    return;
  }

  code = TSHttpHdrStatusGet(bufp, hdrp);
  if (code != TS_HTTP_STATUS_OK) {
    goto release;
  }

  n = 0;

  cl_field = TSMimeHdrFieldFind(bufp, hdrp, TS_MIME_FIELD_CONTENT_LENGTH, TS_MIME_LEN_CONTENT_LENGTH);
  if (cl_field) {
    n = TSMimeHdrFieldValueInt64Get(bufp, hdrp, cl_field, -1);
    TSHandleMLocRelease(bufp, hdrp, cl_field);
  }

  if (n <= 0) {
    goto release;
  }

  mc->cl = n;
  mp4_add_transform(mc, txnp);

release:

  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdrp);
}

static void
mp4_read_response(Mp4Context *mc, TSHttpTxn txnp)
{
  TSMBuffer bufp;
  TSMLoc hdrp;
  TSMLoc cl_field;
  TSHttpStatus status;
  int64_t n;

  if (TSHttpTxnServerRespGet(txnp, &bufp, &hdrp) != TS_SUCCESS) {
    TSError("[%s] could not get request os data", __FUNCTION__);
    return;
  }

  status = TSHttpHdrStatusGet(bufp, hdrp);
  if (status != TS_HTTP_STATUS_OK) {
    goto release;
  }

  n        = 0;
  cl_field = TSMimeHdrFieldFind(bufp, hdrp, TS_MIME_FIELD_CONTENT_LENGTH, TS_MIME_LEN_CONTENT_LENGTH);
  if (cl_field) {
    n = TSMimeHdrFieldValueInt64Get(bufp, hdrp, cl_field, -1);
    TSHandleMLocRelease(bufp, hdrp, cl_field);
  }

  if (n <= 0) {
    goto release;
  }

  mc->cl = n;
  mp4_add_transform(mc, txnp);

release:

  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdrp);
}

static void
mp4_add_transform(Mp4Context *mc, TSHttpTxn txnp)
{
  TSVConn connp;

  if (mc->transform_added) {
    return;
  }

  mc->mtc = new Mp4TransformContext(mc->start, mc->cl);

  TSHttpTxnUntransformedRespCache(txnp, 1);
  TSHttpTxnTransformedRespCache(txnp, 0);

  connp = TSTransformCreate(mp4_transform_entry, txnp);
  TSContDataSet(connp, mc);
  TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, connp);

  mc->transform_added = true;
}

static int
mp4_transform_entry(TSCont contp, TSEvent event, void * /* edata ATS_UNUSED */)
{
  TSVIO input_vio;
  Mp4Context *mc = static_cast<Mp4Context *>(TSContDataGet(contp));

  if (TSVConnClosedGet(contp)) {
    TSContDestroy(contp);
    return 0;
  }

  switch (event) {
  case TS_EVENT_ERROR:
    input_vio = TSVConnWriteVIOGet(contp);
    TSContCall(TSVIOContGet(input_vio), TS_EVENT_ERROR, input_vio);
    break;

  case TS_EVENT_VCONN_WRITE_COMPLETE:
    TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
    break;

  case TS_EVENT_VCONN_WRITE_READY:
  default:
    mp4_transform_handler(contp, mc);
    break;
  }

  return 0;
}

static int
mp4_transform_handler(TSCont contp, Mp4Context *mc)
{
  TSVConn output_conn;
  TSVIO input_vio;
  TSIOBufferReader input_reader;
  int64_t avail, toread, need, upstream_done;
  int ret;
  bool write_down;
  Mp4TransformContext *mtc;

  mtc = mc->mtc;

  output_conn  = TSTransformOutputVConnGet(contp);
  input_vio    = TSVConnWriteVIOGet(contp);
  input_reader = TSVIOReaderGet(input_vio);

  if (!TSVIOBufferGet(input_vio)) {
    if (mtc->output.buffer) {
      TSVIONBytesSet(mtc->output.vio, mtc->total);
      TSVIOReenable(mtc->output.vio);
    }
    return 1;
  }

  avail         = TSIOBufferReaderAvail(input_reader);
  upstream_done = TSVIONDoneGet(input_vio);

  TSIOBufferCopy(mtc->res_buffer, input_reader, avail, 0);
  TSIOBufferReaderConsume(input_reader, avail);
  TSVIONDoneSet(input_vio, upstream_done + avail);

  toread     = TSVIONTodoGet(input_vio);
  write_down = false;

  if (!mtc->parse_over) {
    ret = mp4_parse_meta(mtc, toread <= 0);
    if (ret == 0) {
      goto trans;
    }

    mtc->parse_over    = true;
    mtc->output.buffer = TSIOBufferCreate();
    mtc->output.reader = TSIOBufferReaderAlloc(mtc->output.buffer);

    if (ret < 0) {
      mtc->output.vio    = TSVConnWrite(output_conn, contp, mtc->output.reader, mc->cl);
      mtc->raw_transform = true;

    } else {
      mtc->output.vio = TSVConnWrite(output_conn, contp, mtc->output.reader, mtc->content_length);
    }
  }

  avail = TSIOBufferReaderAvail(mtc->res_reader);

  if (mtc->raw_transform) {
    if (avail > 0) {
      TSIOBufferCopy(mtc->output.buffer, mtc->res_reader, avail, 0);
      TSIOBufferReaderConsume(mtc->res_reader, avail);
      mtc->total += avail;
      write_down = true;
    }

  } else {
    // copy the new meta data
    if (mtc->total < mtc->meta_length) {
      TSIOBufferCopy(mtc->output.buffer, mtc->mm.out_handle.reader, mtc->meta_length, 0);
      mtc->total += mtc->meta_length;
      write_down = true;
    }

    // ignore useless part
    if (mtc->pos < mtc->tail) {
      avail = TSIOBufferReaderAvail(mtc->res_reader);
      need  = mtc->tail - mtc->pos;
      if (need > avail) {
        need = avail;
      }

      if (need > 0) {
        TSIOBufferReaderConsume(mtc->res_reader, need);
        mtc->pos += need;
      }
    }

    // copy the video & audio data
    if (mtc->pos >= mtc->tail) {
      avail = TSIOBufferReaderAvail(mtc->res_reader);

      if (avail > 0) {
        TSIOBufferCopy(mtc->output.buffer, mtc->res_reader, avail, 0);
        TSIOBufferReaderConsume(mtc->res_reader, avail);

        mtc->pos += avail;
        mtc->total += avail;
        write_down = true;
      }
    }
  }

trans:

  if (write_down) {
    TSVIOReenable(mtc->output.vio);
  }

  if (toread > 0) {
    TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_READY, input_vio);

  } else {
    TSVIONBytesSet(mtc->output.vio, mtc->total);
    TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_COMPLETE, input_vio);
  }

  return 1;
}

static int
mp4_parse_meta(Mp4TransformContext *mtc, bool body_complete)
{
  int ret;
  int64_t avail, bytes;
  TSIOBufferBlock blk;
  const char *data;
  Mp4Meta *mm;

  mm = &mtc->mm;

  avail = TSIOBufferReaderAvail(mtc->dup_reader);
  blk   = TSIOBufferReaderStart(mtc->dup_reader);

  while (blk != nullptr) {
    data = TSIOBufferBlockReadStart(blk, mtc->dup_reader, &bytes);
    if (bytes > 0) {
      TSIOBufferWrite(mm->meta_buffer, data, bytes);
    }

    blk = TSIOBufferBlockNext(blk);
  }

  TSIOBufferReaderConsume(mtc->dup_reader, avail);

  ret = mm->parse_meta(body_complete);

  if (ret > 0) { // meta success
    mtc->tail           = mm->start_pos;
    mtc->content_length = mm->content_length;
    mtc->meta_length    = TSIOBufferReaderAvail(mm->out_handle.reader);
  }

  if (ret != 0) {
    TSIOBufferReaderFree(mtc->dup_reader);
    mtc->dup_reader = nullptr;
  }

  return ret;
}

static char *
ts_arg(const char *param, size_t param_len, const char *key, size_t key_len, size_t *val_len)
{
  const char *p, *last;
  const char *val;

  *val_len = 0;

  if (!param || !param_len) {
    return nullptr;
  }

  p    = param;
  last = p + param_len;

  for (; p < last; p++) {
    p = static_cast<char *>(memmem(p, last - p, key, key_len));

    if (p == nullptr) {
      return nullptr;
    }

    if ((p == param || *(p - 1) == '&') && *(p + key_len) == '=') {
      val = p + key_len + 1;

      p = (char *)memchr(p, '&', last - p);

      if (p == nullptr) {
        p = param + param_len;
      }

      *val_len = p - val;

      return const_cast<char *>(val);
    }
  }

  return nullptr;
}
