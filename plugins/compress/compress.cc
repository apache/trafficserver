/** @file

  Transforms content using gzip, deflate, brotli or zstd

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

#include <cstring>
#include <cinttypes>

#include "ts/apidefs.h"
#include "tscore/ink_config.h"

#include <tsutil/PostScript.h>

#include "ts/ts.h"
#include "tscore/ink_defs.h"

#include "debug_macros.h"
#include "compress_common.h"
#include "misc.h"
#include "configuration.h"
#include "gzip_compress.h"
#include "brotli_compress.h"
#include "zstd_compress.h"
#include "ts/remap.h"
#include "ts/remap_version.h"

using namespace std;

// FIXME: custom dictionaries would be nice. configurable/content-type?
// a GPRS device might benefit from a higher compression ratio, whereas a desktop w. high bandwidth
// might be served better with little or no compression at all
// FIXME: look into compressing from the task thread pool
// FIXME: make normalizing accept encoding configurable

// from mod_deflate:
// ZLIB's compression algorithm uses a
// 0-9 based scale that GZIP does where '1' is 'Best speed'
// and '9' is 'Best compression'. Testing has proved level '6'
// to be about the best level to use in an HTTP Server.

namespace compress_ns
{
DbgCtl dbg_ctl{TAG};
}

namespace Compress
{

const char *dictionary = nullptr;

static const char *global_hidden_header_name = nullptr;

static TSMutex compress_config_mutex = nullptr;

// Current global configuration, and the previous one (for cleanup)
Configuration *cur_config  = nullptr;
Configuration *prev_config = nullptr;

namespace
{
  /**
    If client request has both of Range and Accept-Encoding header, follow range-request config.
    */
  void
  handle_range_request(TSMBuffer req_buf, TSMLoc req_loc, HostConfiguration *hc)
  {
    TSMLoc accept_encoding_hdr_field =
      TSMimeHdrFieldFind(req_buf, req_loc, TS_MIME_FIELD_ACCEPT_ENCODING, TS_MIME_LEN_ACCEPT_ENCODING);
    ts::PostScript accept_encoding_defer([&]() -> void { TSHandleMLocRelease(req_buf, req_loc, accept_encoding_hdr_field); });
    if (accept_encoding_hdr_field == TS_NULL_MLOC) {
      return;
    }

    TSMLoc         range_hdr_field = TSMimeHdrFieldFind(req_buf, req_loc, TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE);
    ts::PostScript range_defer([&]() -> void { TSHandleMLocRelease(req_buf, req_loc, range_hdr_field); });
    if (range_hdr_field == TS_NULL_MLOC) {
      return;
    }

    debug("Both of Accept-Encoding and Range header are found in the request");

    switch (hc->range_request_ctl()) {
    case RangeRequestCtrl::REMOVE_RANGE: {
      debug("Remove the Range header by remove-range config");
      while (range_hdr_field) {
        TSMLoc next_dup = TSMimeHdrFieldNextDup(req_buf, req_loc, range_hdr_field);
        TSMimeHdrFieldDestroy(req_buf, req_loc, range_hdr_field);
        TSHandleMLocRelease(req_buf, req_loc, range_hdr_field);
        range_hdr_field = next_dup;
      }
      break;
    }
    case RangeRequestCtrl::REMOVE_ACCEPT_ENCODING: {
      debug("Remove the Accept-Encoding header by remove-accept-encoding config");
      while (accept_encoding_hdr_field) {
        TSMLoc next_dup = TSMimeHdrFieldNextDup(req_buf, req_loc, accept_encoding_hdr_field);
        TSMimeHdrFieldDestroy(req_buf, req_loc, accept_encoding_hdr_field);
        TSHandleMLocRelease(req_buf, req_loc, accept_encoding_hdr_field);
        accept_encoding_hdr_field = next_dup;
      }
      break;
    }
    case RangeRequestCtrl::NO_COMPRESSION:
      // Do NOT touch header - this config is referred by `transformable()` function
      debug("no header modification by no-compression config");
      break;
    case RangeRequestCtrl::NONE:
      [[fallthrough]];
    default:
      debug("Do nothing by none config");
      break;
    }
  }
} // namespace

static Data *
data_alloc(int compression_type, int compression_algorithms, HostConfiguration *hc)
{
  Data *data = static_cast<Data *>(TSmalloc(sizeof(Data)));

  data->downstream_vio         = nullptr;
  data->downstream_buffer      = nullptr;
  data->downstream_reader      = nullptr;
  data->downstream_length      = 0;
  data->state                  = transform_state_initialized;
  data->compression_type       = compression_type;
  data->compression_algorithms = compression_algorithms;
  data->hc                     = hc;

  // Initialize algorithm-specific compression contexts
  if ((compression_type & (COMPRESSION_TYPE_GZIP | COMPRESSION_TYPE_DEFLATE)) &&
      (compression_algorithms & (ALGORITHM_GZIP | ALGORITHM_DEFLATE))) {
    Gzip::data_alloc(data);
  }

#if HAVE_BROTLI_ENCODE_H
  if (compression_type & COMPRESSION_TYPE_BROTLI && compression_algorithms & ALGORITHM_BROTLI) {
    Brotli::data_alloc(data);
  }
#endif
#if HAVE_ZSTD_H
  if ((compression_type & COMPRESSION_TYPE_ZSTD) && (compression_algorithms & ALGORITHM_ZSTD)) {
    Zstd::data_alloc(data);
  }
#endif

  return data;
}

static void
data_destroy(Data *data)
{
  TSReleaseAssert(data);

  if (data->downstream_buffer) {
    TSIOBufferDestroy(data->downstream_buffer);
  }

  // Destroy algorithm-specific compression contexts
  if ((data->compression_type & (COMPRESSION_TYPE_GZIP | COMPRESSION_TYPE_DEFLATE)) &&
      (data->compression_algorithms & (ALGORITHM_GZIP | ALGORITHM_DEFLATE))) {
    Gzip::data_destroy(data);
  }

#if HAVE_BROTLI_ENCODE_H
  if (data->compression_type & COMPRESSION_TYPE_BROTLI && data->compression_algorithms & ALGORITHM_BROTLI) {
    Brotli::data_destroy(data);
  }
#endif
#if HAVE_ZSTD_H
  if (data->compression_type & COMPRESSION_TYPE_ZSTD && data->compression_algorithms & ALGORITHM_ZSTD) {
    Zstd::data_destroy(data);
  }
#endif

  TSfree(data);
}

static TSReturnCode
content_encoding_header(TSMBuffer bufp, TSMLoc hdr_loc, const int compression_type, int algorithm)
{
  TSReturnCode ret;
  TSMLoc       ce_loc;
  const char  *value     = nullptr;
  int          value_len = 0;
  // Delete Content-Encoding if present???
  if (compression_type & COMPRESSION_TYPE_ZSTD && (algorithm & ALGORITHM_ZSTD)) {
    value     = TS_HTTP_VALUE_ZSTD;
    value_len = TS_HTTP_LEN_ZSTD;
  } else if (compression_type & COMPRESSION_TYPE_BROTLI && (algorithm & ALGORITHM_BROTLI)) {
    value     = TS_HTTP_VALUE_BROTLI;
    value_len = TS_HTTP_LEN_BROTLI;
  } else if (compression_type & COMPRESSION_TYPE_GZIP && (algorithm & ALGORITHM_GZIP)) {
    value     = TS_HTTP_VALUE_GZIP;
    value_len = TS_HTTP_LEN_GZIP;
  } else if (compression_type & COMPRESSION_TYPE_DEFLATE && (algorithm & ALGORITHM_DEFLATE)) {
    value     = TS_HTTP_VALUE_DEFLATE;
    value_len = TS_HTTP_LEN_DEFLATE;
  }

  if (value_len == 0) {
    return TS_SUCCESS;
  }

  if ((ret = TSMimeHdrFieldCreateNamed(bufp, hdr_loc, TS_MIME_FIELD_CONTENT_ENCODING, TS_MIME_LEN_CONTENT_ENCODING, &ce_loc)) ==
      TS_SUCCESS) {
    ret = TSMimeHdrFieldValueStringInsert(bufp, hdr_loc, ce_loc, -1, value, value_len);
    if (ret == TS_SUCCESS) {
      ret = TSMimeHdrFieldAppend(bufp, hdr_loc, ce_loc);
    }
    TSHandleMLocRelease(bufp, hdr_loc, ce_loc);
  }

  if (ret != TS_SUCCESS) {
    error("cannot add the Content-Encoding header");
  }

  return ret;
}

static TSReturnCode
vary_header(TSMBuffer bufp, TSMLoc hdr_loc)
{
  TSReturnCode ret;
  TSMLoc       ce_loc;

  ce_loc = TSMimeHdrFieldFind(bufp, hdr_loc, TS_MIME_FIELD_VARY, TS_MIME_LEN_VARY);
  if (ce_loc) {
    int idx, count, len;

    count = TSMimeHdrFieldValuesCount(bufp, hdr_loc, ce_loc);
    for (idx = 0; idx < count; idx++) {
      const char *value = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, ce_loc, idx, &len);
      if (len && strncasecmp("Accept-Encoding", value, len) == 0) {
        // Bail, Vary: Accept-Encoding already sent from origin
        TSHandleMLocRelease(bufp, hdr_loc, ce_loc);
        return TS_SUCCESS;
      }
    }

    ret = TSMimeHdrFieldValueStringInsert(bufp, hdr_loc, ce_loc, -1, TS_MIME_FIELD_ACCEPT_ENCODING, TS_MIME_LEN_ACCEPT_ENCODING);
    TSHandleMLocRelease(bufp, hdr_loc, ce_loc);
  } else {
    if ((ret = TSMimeHdrFieldCreateNamed(bufp, hdr_loc, TS_MIME_FIELD_VARY, TS_MIME_LEN_VARY, &ce_loc)) == TS_SUCCESS) {
      if ((ret = TSMimeHdrFieldValueStringInsert(bufp, hdr_loc, ce_loc, -1, TS_MIME_FIELD_ACCEPT_ENCODING,
                                                 TS_MIME_LEN_ACCEPT_ENCODING)) == TS_SUCCESS) {
        ret = TSMimeHdrFieldAppend(bufp, hdr_loc, ce_loc);
      }

      TSHandleMLocRelease(bufp, hdr_loc, ce_loc);
    }
  }

  if (ret != TS_SUCCESS) {
    error("cannot add/update the Vary header");
  }

  return ret;
}

// FIXME: the etag alteration isn't proper. it should modify the value inside quotes
//       specify a very header..
static TSReturnCode
etag_header(TSMBuffer bufp, TSMLoc hdr_loc)
{
  TSReturnCode ret = TS_SUCCESS;
  TSMLoc       ce_loc;

  ce_loc = TSMimeHdrFieldFind(bufp, hdr_loc, TS_MIME_FIELD_ETAG, TS_MIME_LEN_ETAG);

  if (ce_loc) {
    int         strl;
    const char *strv = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, ce_loc, -1, &strl);

    // do not alter weak etags.
    // FIXME: consider just making the etag weak for compressed content
    if (strl >= 2) {
      int changetag = 1;
      if ((strv[0] == 'w' || strv[0] == 'W') && strv[1] == '/') {
        changetag = 0;
      }
      if (changetag) {
        ret = TSMimeHdrFieldValueAppend(bufp, hdr_loc, ce_loc, 0, "-df", 3);
      }
    }
    TSHandleMLocRelease(bufp, hdr_loc, ce_loc);
  }

  if (ret != TS_SUCCESS) {
    error("cannot handle the %s header", TS_MIME_FIELD_ETAG);
  }

  return ret;
}

// FIXME: some things are potentially compressible. those responses
static void
compress_transform_init(TSCont contp, Data *data)
{
  // update the vary, content-encoding, and etag response headers
  // prepare the downstream for transforming

  TSVConn   downstream_conn;
  TSMBuffer bufp;
  TSMLoc    hdr_loc;

  data->state = transform_state_output;

  if (TSHttpTxnTransformRespGet(data->txn, &bufp, &hdr_loc) != TS_SUCCESS) {
    error("Error TSHttpTxnTransformRespGet");
    return;
  }

  if (content_encoding_header(bufp, hdr_loc, data->compression_type, data->compression_algorithms) == TS_SUCCESS &&
      vary_header(bufp, hdr_loc) == TS_SUCCESS && etag_header(bufp, hdr_loc) == TS_SUCCESS) {
    downstream_conn         = TSTransformOutputVConnGet(contp);
    data->downstream_buffer = TSIOBufferCreate();
    data->downstream_reader = TSIOBufferReaderAlloc(data->downstream_buffer);
    data->downstream_vio    = TSVConnWrite(downstream_conn, contp, data->downstream_reader, INT64_MAX);
  }

#if HAVE_ZSTD_H
  if (data->compression_type & COMPRESSION_TYPE_ZSTD && (data->compression_algorithms & ALGORITHM_ZSTD)) {
    if (!Zstd::transform_init(data)) {
      TSError("Failed to configure Zstandard compression context");
      return;
    }
  }
#endif

  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
}

static void
compress_transform_one(Data *data, TSIOBufferReader upstream_reader, int amount)
{
  TSIOBufferBlock downstream_blkp;
  int64_t         upstream_length;
  while (amount > 0) {
    downstream_blkp = TSIOBufferReaderStart(upstream_reader);
    if (!downstream_blkp) {
      error("couldn't get from IOBufferBlock");
      return;
    }

    const char *upstream_buffer = TSIOBufferBlockReadStart(downstream_blkp, upstream_reader, &upstream_length);
    if (!upstream_buffer) {
      error("couldn't get from TSIOBufferBlockReadStart");
      return;
    }

    if (upstream_length > amount) {
      upstream_length = amount;
    }

#if HAVE_ZSTD_H
    if (data->compression_type & COMPRESSION_TYPE_ZSTD && (data->compression_algorithms & ALGORITHM_ZSTD)) {
      Zstd::transform_one(data, upstream_buffer, upstream_length);
    } else
#endif
#if HAVE_BROTLI_ENCODE_H
      if (data->compression_type & COMPRESSION_TYPE_BROTLI && (data->compression_algorithms & ALGORITHM_BROTLI)) {
      Brotli::transform_one(data, upstream_buffer, upstream_length);
    } else
#endif
      if ((data->compression_type & (COMPRESSION_TYPE_GZIP | COMPRESSION_TYPE_DEFLATE)) &&
          (data->compression_algorithms & (ALGORITHM_GZIP | ALGORITHM_DEFLATE))) {
      Gzip::transform_one(data, upstream_buffer, upstream_length);
    } else {
      warning("No compression supported. Passing data through without transformation.");
      int64_t written = TSIOBufferWrite(data->downstream_buffer, upstream_buffer, upstream_length);
      if (written == TS_ERROR || written != upstream_length) {
        error("Failed to copy upstream data to downstream buffer");
        return;
      }
      data->downstream_length += written;
    }

    TSIOBufferReaderConsume(upstream_reader, upstream_length);
    amount -= upstream_length;
  }
}

static void
compress_transform_finish(Data *data)
{
#if HAVE_ZSTD_H
  if (data->compression_type & COMPRESSION_TYPE_ZSTD && data->compression_algorithms & ALGORITHM_ZSTD) {
    Zstd::transform_finish(data);
    debug("compress_transform_finish: zstd compression finish");
  } else
#endif
#if HAVE_BROTLI_ENCODE_H
    if (data->compression_type & COMPRESSION_TYPE_BROTLI && data->compression_algorithms & ALGORITHM_BROTLI) {
    Brotli::transform_finish(data);
    debug("compress_transform_finish: brotli compression finish");
  } else
#endif
    if ((data->compression_type & (COMPRESSION_TYPE_GZIP | COMPRESSION_TYPE_DEFLATE)) &&
        (data->compression_algorithms & (ALGORITHM_GZIP | ALGORITHM_DEFLATE))) {
    Gzip::transform_finish(data);
    debug("compress_transform_finish: gzip compression finish");
  } else {
    debug("compress_transform_finish: no compression active, passthrough mode");
  }
}

static void
compress_transform_do(TSCont contp)
{
  TSVIO   upstream_vio;
  Data   *data;
  int64_t upstream_todo;
  int64_t downstream_bytes_written;

  data = static_cast<Data *>(TSContDataGet(contp));
  if (data->state == transform_state_initialized) {
    compress_transform_init(contp, data);
  }

  upstream_vio             = TSVConnWriteVIOGet(contp);
  downstream_bytes_written = data->downstream_length;

  if (!TSVIOBufferGet(upstream_vio)) {
    compress_transform_finish(data);

    TSVIONBytesSet(data->downstream_vio, data->downstream_length);

    if (data->downstream_length > downstream_bytes_written) {
      TSVIOReenable(data->downstream_vio);
    }
    return;
  }

  upstream_todo = TSVIONTodoGet(upstream_vio);

  if (upstream_todo > 0) {
    int64_t upstream_avail = TSIOBufferReaderAvail(TSVIOReaderGet(upstream_vio));

    if (upstream_todo > upstream_avail) {
      upstream_todo = upstream_avail;
    }

    if (upstream_todo > 0) {
      compress_transform_one(data, TSVIOReaderGet(upstream_vio), upstream_todo);
      TSVIONDoneSet(upstream_vio, TSVIONDoneGet(upstream_vio) + upstream_todo);
    }
  }

  if (TSVIONTodoGet(upstream_vio) > 0) {
    if (upstream_todo > 0) {
      if (data->downstream_length > downstream_bytes_written) {
        TSVIOReenable(data->downstream_vio);
      }
      TSContCall(TSVIOContGet(upstream_vio), TS_EVENT_VCONN_WRITE_READY, upstream_vio);
    }
  } else {
    compress_transform_finish(data);
    TSVIONBytesSet(data->downstream_vio, data->downstream_length);

    if (data->downstream_length > downstream_bytes_written) {
      TSVIOReenable(data->downstream_vio);
    }

    TSContCall(TSVIOContGet(upstream_vio), TS_EVENT_VCONN_WRITE_COMPLETE, upstream_vio);
  }
}

static int
compress_transform(TSCont contp, TSEvent event, void * /* edata ATS_UNUSED */)
{
  if (TSVConnClosedGet(contp)) {
    data_destroy(static_cast<Data *>(TSContDataGet(contp)));
    TSContDestroy(contp);
    return 0;
  } else {
    switch (event) {
    case TS_EVENT_ERROR: {
      debug("compress_transform: TS_EVENT_ERROR starts");
      TSVIO upstream_vio = TSVConnWriteVIOGet(contp);
      TSContCall(TSVIOContGet(upstream_vio), TS_EVENT_ERROR, upstream_vio);
    } break;
    case TS_EVENT_VCONN_WRITE_COMPLETE:
      TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
      break;
    case TS_EVENT_VCONN_WRITE_READY:
      compress_transform_do(contp);
      break;
    case TS_EVENT_IMMEDIATE:
      compress_transform_do(contp);
      break;
    default:
      warning("unknown event [%d]", event);
      compress_transform_do(contp);
      break;
    }
  }

  return 0;
}

static int
transformable(TSHttpTxn txnp, bool server, HostConfiguration *host_configuration, int *compress_type, int *algorithms)
{
  /* Server response header */
  TSMBuffer bufp;
  TSMLoc    hdr_loc;
  TSMLoc    field_loc;

  /* Client request header */
  TSMBuffer cbuf;
  TSMLoc    chdr;
  TSMLoc    cfield;

  const char  *value;
  int          len;
  TSHttpStatus resp_status;

  if (server) {
    if (TS_SUCCESS != TSHttpTxnServerRespGet(txnp, &bufp, &hdr_loc)) {
      return 0;
    }
  } else {
    if (TS_SUCCESS != TSHttpTxnCachedRespGet(txnp, &bufp, &hdr_loc)) {
      return 0;
    }
  }

  resp_status = TSHttpHdrStatusGet(bufp, hdr_loc);

  // NOTE: error responses can mess up plugins like the escalate.so plugin,
  // and possibly the escalation feature of parent.config. See #2913.
  if (!host_configuration->is_status_code_compressible(resp_status)) {
    info("http response status [%d] is not compressible", resp_status);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    return 0;
  }

  // We got a server response but it was a 304
  // we need to update our data to come from cache instead of
  // the 304 response which does not need to include all headers
  if ((server) && (resp_status == TS_HTTP_STATUS_NOT_MODIFIED)) {
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    if (TS_SUCCESS != TSHttpTxnCachedRespGet(txnp, &bufp, &hdr_loc)) {
      return 0;
    }
  }

  if (TS_SUCCESS != TSHttpTxnClientReqGet(txnp, &cbuf, &chdr)) {
    info("cound not get client request");
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    return 0;
  }

  // check Partial Object is transformable
  if (host_configuration->range_request_ctl() == RangeRequestCtrl::NO_COMPRESSION) {
    // check Range header in client request
    // CAVETE: some plugin (- e.g. cache_range_request) tweaks client headers
    TSMLoc range_hdr_field = TSMimeHdrFieldFind(cbuf, chdr, TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE);
    if (range_hdr_field != TS_NULL_MLOC) {
      debug("Range header found in the request and range_request is configured as no_compression");
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
      TSHandleMLocRelease(cbuf, chdr, range_hdr_field);
      TSHandleMLocRelease(cbuf, TS_NULL_MLOC, chdr);
      return 0;
    }

    // check Content-Range header in (cached) server response
    TSMLoc content_range_hdr_field = TSMimeHdrFieldFind(bufp, hdr_loc, TS_MIME_FIELD_CONTENT_RANGE, TS_MIME_LEN_CONTENT_RANGE);
    if (content_range_hdr_field != TS_NULL_MLOC) {
      debug("Content-Range header found in the response and range_request is configured as no_compression");
      TSHandleMLocRelease(bufp, hdr_loc, content_range_hdr_field);
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
      TSHandleMLocRelease(cbuf, TS_NULL_MLOC, chdr);
      return 0;
    }

    TSHandleMLocRelease(bufp, hdr_loc, content_range_hdr_field);
    TSHandleMLocRelease(cbuf, chdr, range_hdr_field);
  }

  // the only compressible method is currently GET.
  int         method_length;
  const char *method = TSHttpHdrMethodGet(cbuf, chdr, &method_length);

  if (!((method_length == TS_HTTP_LEN_GET && memcmp(method, TS_HTTP_METHOD_GET, TS_HTTP_LEN_GET) == 0) ||
        (method_length == TS_HTTP_LEN_POST && memcmp(method, TS_HTTP_METHOD_POST, TS_HTTP_LEN_POST) == 0))) {
    debug("method is not GET or POST, not compressible");
    TSHandleMLocRelease(cbuf, TS_NULL_MLOC, chdr);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    return 0;
  }

  *algorithms = host_configuration->compression_algorithms();
  cfield      = TSMimeHdrFieldFind(cbuf, chdr, TS_MIME_FIELD_ACCEPT_ENCODING, TS_MIME_LEN_ACCEPT_ENCODING);
  if (cfield != TS_NULL_MLOC) {
    int compression_acceptable = 0;
    int nvalues                = TSMimeHdrFieldValuesCount(cbuf, chdr, cfield);
    for (int i = 0; i < nvalues; i++) {
      value = TSMimeHdrFieldValueStringGet(cbuf, chdr, cfield, i, &len);
      if (!value) {
        continue;
      }

      info("Accept-Encoding value [%.*s]", len, value);

      if (strncasecmp(value, "zstd", sizeof("zstd") - 1) == 0) {
        if (*algorithms & ALGORITHM_ZSTD) {
          compression_acceptable = 1;
        }
        *compress_type |= COMPRESSION_TYPE_ZSTD;
      } else if (strncasecmp(value, "br", sizeof("br") - 1) == 0) {
        if (*algorithms & ALGORITHM_BROTLI) {
          compression_acceptable = 1;
        }
        *compress_type |= COMPRESSION_TYPE_BROTLI;
      } else if (strncasecmp(value, "deflate", sizeof("deflate") - 1) == 0) {
        if (*algorithms & ALGORITHM_DEFLATE) {
          compression_acceptable = 1;
        }
        *compress_type |= COMPRESSION_TYPE_DEFLATE;
      } else if (strncasecmp(value, "gzip", sizeof("gzip") - 1) == 0) {
        if (*algorithms & ALGORITHM_GZIP) {
          compression_acceptable = 1;
        }
        *compress_type |= COMPRESSION_TYPE_GZIP;
      }
    }

    TSHandleMLocRelease(cbuf, chdr, cfield);
    TSHandleMLocRelease(cbuf, TS_NULL_MLOC, chdr);

    if (!compression_acceptable) {
      info("no acceptable encoding match found in request header, not compressible");
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
      return 0;
    }
  } else {
    info("no acceptable encoding found in request header, not compressible");
    TSHandleMLocRelease(cbuf, chdr, cfield);
    TSHandleMLocRelease(cbuf, TS_NULL_MLOC, chdr);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    return 0;
  }

  /* If there already exists a content encoding then we don't want
     to do anything. */
  field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, TS_MIME_FIELD_CONTENT_ENCODING, -1);
  if (field_loc) {
    info("response is already content encoded, not compressible");
    TSHandleMLocRelease(bufp, hdr_loc, field_loc);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    return 0;
  }

  field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, TS_MIME_FIELD_CONTENT_LENGTH, TS_MIME_LEN_CONTENT_LENGTH);
  if (field_loc != TS_NULL_MLOC) {
    unsigned int hdr_value = TSMimeHdrFieldValueUintGet(bufp, hdr_loc, field_loc, -1);
    TSHandleMLocRelease(bufp, hdr_loc, field_loc);
    if (hdr_value == 0) {
      info("response is 0-length, not compressible");
      return 0;
    }

    if (hdr_value < host_configuration->minimum_content_length()) {
      info("response is smaller than minimum content length, not compressing");
      return 0;
    }
  }

  /* We only want to do gzip compression on documents that have a
     content type of "text/" or "application/x-javascript". */
  field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, TS_MIME_FIELD_CONTENT_TYPE, -1);
  if (!field_loc) {
    info("no content type header found, not compressible");
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    return 0;
  }

  value = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, -1, &len);

  int rv = host_configuration->is_content_type_compressible(value, len);

  if (!rv) {
    info("content-type [%.*s] not compressible", len, value);
  }

  TSHandleMLocRelease(bufp, hdr_loc, field_loc);
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);

  return rv;
}

static void
compress_transform_add(TSHttpTxn txnp, HostConfiguration *hc, int compress_type, int algorithms)
{
  TSVConn connp;
  Data   *data;

  TSHttpTxnUntransformedRespCache(txnp, 1);

  if (!hc->cache()) {
    debug("TransformedRespCache  not enabled");
    TSHttpTxnTransformedRespCache(txnp, 0);
  } else {
    debug("TransformedRespCache  enabled");
    TSHttpTxnUntransformedRespCache(txnp, 0);
    TSHttpTxnTransformedRespCache(txnp, 1);
  }

  connp     = TSTransformCreate(compress_transform, txnp);
  data      = data_alloc(compress_type, algorithms, hc);
  data->txn = txnp;

  TSContDataSet(connp, data);
  TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, connp);
}

HostConfiguration *
find_host_configuration(TSHttpTxn /* txnp ATS_UNUSED */, TSMBuffer bufp, TSMLoc locp, Configuration *config)
{
  TSMLoc             fieldp = TSMimeHdrFieldFind(bufp, locp, TS_MIME_FIELD_HOST, TS_MIME_LEN_HOST);
  int                strl   = 0;
  const char        *strv   = nullptr;
  HostConfiguration *host_configuration;

  if (fieldp) {
    strv = TSMimeHdrFieldValueStringGet(bufp, locp, fieldp, -1, &strl);
    TSHandleMLocRelease(bufp, locp, fieldp);
  }
  if (config == nullptr) {
    host_configuration = cur_config->find(strv, strl);
  } else {
    host_configuration = config->find(strv, strl);
  }
  return host_configuration;
}

static int
transform_plugin(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn          txnp          = static_cast<TSHttpTxn>(edata);
  int                compress_type = COMPRESSION_TYPE_DEFAULT;
  int                algorithms    = ALGORITHM_DEFAULT;
  HostConfiguration *hc            = static_cast<HostConfiguration *>(TSContDataGet(contp));

  switch (event) {
  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    // os: the accept encoding header needs to be restored..
    // otherwise the next request won't get a cache hit on this
    if (hc != nullptr) {
      info("reading response headers");
      if (hc->remove_accept_encoding()) {
        TSMBuffer req_buf;
        TSMLoc    req_loc;

        if (TSHttpTxnServerReqGet(txnp, &req_buf, &req_loc) == TS_SUCCESS) {
          restore_accept_encoding(txnp, req_buf, req_loc, global_hidden_header_name);
          TSHandleMLocRelease(req_buf, TS_NULL_MLOC, req_loc);
        }
      }

      if (transformable(txnp, true, hc, &compress_type, &algorithms)) {
        compress_transform_add(txnp, hc, compress_type, algorithms);
      }
    }
    break;

  case TS_EVENT_HTTP_SEND_REQUEST_HDR:
    if (hc != nullptr) {
      info("preparing send request headers");
      if (hc->remove_accept_encoding()) {
        TSMBuffer req_buf;
        TSMLoc    req_loc;

        if (TSHttpTxnServerReqGet(txnp, &req_buf, &req_loc) == TS_SUCCESS) {
          hide_accept_encoding(txnp, req_buf, req_loc, global_hidden_header_name);
          TSHandleMLocRelease(req_buf, TS_NULL_MLOC, req_loc);
        }
      }
      TSHttpTxnHookAdd(txnp, TS_HTTP_READ_RESPONSE_HDR_HOOK, contp);
    }
    break;

  case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE: {
    int obj_status;

    if (TS_ERROR != TSHttpTxnCacheLookupStatusGet(txnp, &obj_status) && (TS_CACHE_LOOKUP_HIT_FRESH == obj_status)) {
      if (hc != nullptr) {
        info("handling compression of cached object");
        if (transformable(txnp, false, hc, &compress_type, &algorithms)) {
          compress_transform_add(txnp, hc, compress_type, algorithms);
        }
      }
    } else {
      // Prepare for going to origin
      info("preparing to go to origin");
      TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_REQUEST_HDR_HOOK, contp);
    }
  } break;

  case TS_EVENT_HTTP_TXN_CLOSE:
    // Release the ocnif lease, and destroy this continuation
    TSContDestroy(contp);
    break;

  default:
    fatal("compress transform unknown event");
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);

  return 0;
}

/**
 * This handles a compress request
 * 1. Reads the client request header
 * 2. For global plugin, get host configuration from global config
 *    For remap plugin, get host configuration from configs populated through remap
 * 3. Check for Accept-Encoding header
 * 4. Check for Range header
 * 5. Schedules TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK and TS_HTTP_TXN_CLOSE_HOOK for
 *    further processing
 */
static void
handle_request(TSHttpTxn txnp, Configuration *config)
{
  TSMBuffer          req_buf;
  TSMLoc             req_loc;
  HostConfiguration *hc;

  if (TSHttpTxnClientReqGet(txnp, &req_buf, &req_loc) == TS_SUCCESS) {
    if (config == nullptr) {
      hc = find_host_configuration(txnp, req_buf, req_loc, nullptr);
    } else {
      hc = find_host_configuration(txnp, req_buf, req_loc, config);
    }
    bool allowed = false;

    if (hc->enabled()) {
      if (hc->has_allows()) {
        int   url_len;
        char *url = TSHttpTxnEffectiveUrlStringGet(txnp, &url_len);
        allowed   = hc->is_url_allowed(url, url_len);
        TSfree(url);
      } else {
        allowed = true;
      }
    }
    if (allowed) {
      TSCont transform_contp = TSContCreate(transform_plugin, nullptr);

      TSContDataSet(transform_contp, (void *)hc);

      info("Kicking off compress plugin for request");
      normalize_accept_encoding(txnp, req_buf, req_loc);
      handle_range_request(req_buf, req_loc, hc);
      TSHttpTxnHookAdd(txnp, TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, transform_contp);
      TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, transform_contp); // To release the config
    }
    TSHandleMLocRelease(req_buf, TS_NULL_MLOC, req_loc);
  }
}

static int
transform_global_plugin(TSCont /* contp ATS_UNUSED */, TSEvent event, void *edata)
{
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    // Handle compress request and use the global configs
    handle_request(txnp, nullptr);
    break;

  default:
    fatal("compress global transform unknown event");
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);

  return 0;
}

static void
load_global_configuration(TSCont contp)
{
  const char    *path      = static_cast<const char *>(TSContDataGet(contp));
  Configuration *newconfig = Configuration::Parse(path);
  Configuration *oldconfig = __sync_lock_test_and_set(&cur_config, newconfig);

  debug("config swapped, old config %p", oldconfig);

  // need a mutex for when there are multiple reloads going on
  TSMutexLock(compress_config_mutex);
  if (prev_config) {
    debug("deleting previous configuration container, %p", prev_config);
    delete prev_config;
  }
  prev_config = oldconfig;
  TSMutexUnlock(compress_config_mutex);
}

static int
management_update(TSCont contp, TSEvent event, void * /* edata ATS_UNUSED */)
{
  TSReleaseAssert(event == TS_EVENT_MGMT_UPDATE);
  info("management update event received");
  load_global_configuration(contp);

  return 0;
}
} // namespace Compress

void
TSPluginInit(int argc, const char *argv[])
{
  const char *config_path         = nullptr;
  Compress::compress_config_mutex = TSMutexCreate();

  if (argc > 2) {
    fatal("the compress plugin does not accept more than 1 plugin argument");
  } else {
    config_path = TSstrdup(2 == argc ? argv[1] : "");
  }

  if (!register_plugin()) {
    fatal("the compress plugin failed to register");
  }

  info("TSPluginInit %s", argv[0]);

  if (!Compress::global_hidden_header_name) {
    Compress::global_hidden_header_name = init_hidden_header_name();
  }

  TSCont management_contp = TSContCreate(Compress::management_update, nullptr);

  // Make sure the global configuration is properly loaded and reloaded on changes
  TSContDataSet(management_contp, (void *)config_path);
  TSMgmtUpdateRegister(management_contp, TAG);
  Compress::load_global_configuration(management_contp);

  // Setup the global hook, main entry point for kicking off the plugin
  TSCont transform_global_contp = TSContCreate(Compress::transform_global_plugin, nullptr);

  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, transform_global_contp);
  info("loaded");
}

//////////////////////////////////////////////////////////////////////////////
// Initialize the plugin as a remap plugin.
//
TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  CHECK_REMAP_API_COMPATIBILITY(api_info, errbuf, errbuf_size);
  info("The compress plugin is successfully initialized");
  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **instance, char * /* errbuf ATS_UNUSED */, int /* errbuf_size ATS_UNUSED */)
{
  info("Instantiating a new compress plugin remap rule");
  info("Reading config from file = %s", argv[2]);

  const char *config_path = nullptr;

  if (argc > 4) {
    fatal("The compress plugin does not accept more than one plugin argument");
  } else {
    config_path = TSstrdup(3 == argc ? argv[2] : "");
  }
  if (!Compress::global_hidden_header_name) {
    Compress::global_hidden_header_name = init_hidden_header_name();
  }

  Compress::Configuration *config = Compress::Configuration::Parse(config_path);
  *instance                       = config;

  free((void *)config_path);
  info("Configuration loaded");
  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *instance)
{
  debug("Cleanup configs read from remap");
  auto c = static_cast<Compress::Configuration *>(instance);
  delete c;
}

TSRemapStatus
TSRemapDoRemap(void *instance, TSHttpTxn txnp, TSRemapRequestInfo * /* rri ATS_UNUSED */)
{
  if (nullptr == instance) {
    info("No Rules configured, falling back to default");
  } else {
    info("Remap Rules configured for compress");
    Compress::Configuration *config = static_cast<Compress::Configuration *>(instance);
    // Handle compress request and use the configs populated from remap instance
    handle_request(txnp, config);
  }
  return TSREMAP_NO_REMAP;
}
