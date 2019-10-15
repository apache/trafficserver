/** @file

  Transforms content using gzip, deflate or brotli

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
#include <zlib.h>

#if HAVE_BROTLI_ENCODE_H
#include <brotli/encode.h>
#endif

#include "ts/ts.h"
#include "tscore/ink_defs.h"

#include "debug_macros.h"
#include "misc.h"
#include "configuration.h"
#include "ts/remap.h"

using namespace std;
using namespace Gzip;

// FIXME: custom dictionaries would be nice. configurable/content-type?
// a GPRS device might benefit from a higher compression ratio, whereas a desktop w. high bandwith
// might be served better with little or no compression at all
// FIXME: look into compressing from the task thread pool
// FIXME: make normalizing accept encoding configurable

// from mod_deflate:
// ZLIB's compression algorithm uses a
// 0-9 based scale that GZIP does where '1' is 'Best speed'
// and '9' is 'Best compression'. Testing has proved level '6'
// to be about the best level to use in an HTTP Server.

const int ZLIB_COMPRESSION_LEVEL = 6;
const char *dictionary           = nullptr;
const char *TS_HTTP_VALUE_BROTLI = "br";
const int TS_HTTP_LEN_BROTLI     = 2;

// brotli compression quality 1-11. Testing proved level '6'
#if HAVE_BROTLI_ENCODE_H
const int BROTLI_COMPRESSION_LEVEL = 6;
const int BROTLI_LGW               = 16;
#endif

static const char *global_hidden_header_name = nullptr;

// Current global configuration, and the previous one (for cleanup)
Configuration *cur_config  = nullptr;
Configuration *prev_config = nullptr;

static Data *
data_alloc(int compression_type, int compression_algorithms)
{
  Data *data;
  int err;

  data                         = static_cast<Data *>(TSmalloc(sizeof(Data)));
  data->downstream_vio         = nullptr;
  data->downstream_buffer      = nullptr;
  data->downstream_reader      = nullptr;
  data->downstream_length      = 0;
  data->state                  = transform_state_initialized;
  data->compression_type       = compression_type;
  data->compression_algorithms = compression_algorithms;
  data->zstrm.next_in          = Z_NULL;
  data->zstrm.avail_in         = 0;
  data->zstrm.total_in         = 0;
  data->zstrm.next_out         = Z_NULL;
  data->zstrm.avail_out        = 0;
  data->zstrm.total_out        = 0;
  data->zstrm.zalloc           = gzip_alloc;
  data->zstrm.zfree            = gzip_free;
  data->zstrm.opaque           = (voidpf) nullptr;
  data->zstrm.data_type        = Z_ASCII;

  int window_bits = WINDOW_BITS_GZIP;
  if (compression_type & COMPRESSION_TYPE_DEFLATE) {
    window_bits = WINDOW_BITS_DEFLATE;
  }

  err = deflateInit2(&data->zstrm, ZLIB_COMPRESSION_LEVEL, Z_DEFLATED, window_bits, ZLIB_MEMLEVEL, Z_DEFAULT_STRATEGY);

  if (err != Z_OK) {
    fatal("gzip-transform: ERROR: deflateInit (%d)!", err);
  }

  if (dictionary) {
    err = deflateSetDictionary(&data->zstrm, reinterpret_cast<const Bytef *>(dictionary), strlen(dictionary));
    if (err != Z_OK) {
      fatal("gzip-transform: ERROR: deflateSetDictionary (%d)!", err);
    }
  }
#if HAVE_BROTLI_ENCODE_H
  data->bstrm.br = nullptr;
  if (compression_type & COMPRESSION_TYPE_BROTLI) {
    debug("brotli compression. Create Brotli Encoder Instance.");
    data->bstrm.br = BrotliEncoderCreateInstance(nullptr, nullptr, nullptr);
    if (!data->bstrm.br) {
      fatal("Brotli Encoder Instance Failed");
    }
    BrotliEncoderSetParameter(data->bstrm.br, BROTLI_PARAM_QUALITY, BROTLI_COMPRESSION_LEVEL);
    BrotliEncoderSetParameter(data->bstrm.br, BROTLI_PARAM_LGWIN, BROTLI_LGW);
    data->bstrm.next_in   = nullptr;
    data->bstrm.avail_in  = 0;
    data->bstrm.total_in  = 0;
    data->bstrm.next_out  = nullptr;
    data->bstrm.avail_out = 0;
    data->bstrm.total_out = 0;
  }
#endif
  return data;
}

static void
data_destroy(Data *data)
{
  TSReleaseAssert(data);

  // deflateEnd return value ignore is intentional
  // it would spew log on every client abort
  deflateEnd(&data->zstrm);

  if (data->downstream_buffer) {
    TSIOBufferDestroy(data->downstream_buffer);
  }

// brotlidestory
#if HAVE_BROTLI_ENCODE_H
  BrotliEncoderDestroyInstance(data->bstrm.br);
#endif

  TSfree(data);
}

static TSReturnCode
content_encoding_header(TSMBuffer bufp, TSMLoc hdr_loc, const int compression_type, int algorithm)
{
  TSReturnCode ret;
  TSMLoc ce_loc;
  const char *value = nullptr;
  int value_len     = 0;
  // Delete Content-Encoding if present???
  if (compression_type & COMPRESSION_TYPE_BROTLI && (algorithm & ALGORITHM_BROTLI)) {
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
    error("no need to add Content-Encoding header");
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
  TSMLoc ce_loc;

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
  TSMLoc ce_loc;

  ce_loc = TSMimeHdrFieldFind(bufp, hdr_loc, TS_MIME_FIELD_ETAG, TS_MIME_LEN_ETAG);

  if (ce_loc) {
    int strl;
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

  TSVConn downstream_conn;
  TSMBuffer bufp;
  TSMLoc hdr_loc;

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

  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
}

static void
gzip_transform_one(Data *data, const char *upstream_buffer, int64_t upstream_length)
{
  TSIOBufferBlock downstream_blkp;
  int64_t downstream_length;
  int err;
  data->zstrm.next_in  = (unsigned char *)upstream_buffer;
  data->zstrm.avail_in = upstream_length;

  while (data->zstrm.avail_in > 0) {
    downstream_blkp         = TSIOBufferStart(data->downstream_buffer);
    char *downstream_buffer = TSIOBufferBlockWriteStart(downstream_blkp, &downstream_length);

    data->zstrm.next_out  = reinterpret_cast<unsigned char *>(downstream_buffer);
    data->zstrm.avail_out = downstream_length;

    if (!data->hc->flush()) {
      err = deflate(&data->zstrm, Z_NO_FLUSH);
    } else {
      err = deflate(&data->zstrm, Z_SYNC_FLUSH);
    }

    if (err != Z_OK) {
      warning("deflate() call failed: %d", err);
    }

    if (downstream_length > data->zstrm.avail_out) {
      TSIOBufferProduce(data->downstream_buffer, downstream_length - data->zstrm.avail_out);
      data->downstream_length += (downstream_length - data->zstrm.avail_out);
    }

    if (data->zstrm.avail_out > 0) {
      if (data->zstrm.avail_in != 0) {
        error("gzip-transform: avail_in is (%d): should be 0", data->zstrm.avail_in);
      }
    }
  }
}

#if HAVE_BROTLI_ENCODE_H
static bool
brotli_compress_operation(Data *data, const char *upstream_buffer, int64_t upstream_length, BrotliEncoderOperation op)
{
  TSIOBufferBlock downstream_blkp;
  int64_t downstream_length;

  data->bstrm.next_in  = (uint8_t *)upstream_buffer;
  data->bstrm.avail_in = upstream_length;

  bool ok = true;
  while (ok) {
    downstream_blkp         = TSIOBufferStart(data->downstream_buffer);
    char *downstream_buffer = TSIOBufferBlockWriteStart(downstream_blkp, &downstream_length);

    data->bstrm.next_out  = reinterpret_cast<unsigned char *>(downstream_buffer);
    data->bstrm.avail_out = downstream_length;
    data->bstrm.total_out = 0;

    ok =
      !!BrotliEncoderCompressStream(data->bstrm.br, op, &data->bstrm.avail_in, &const_cast<const uint8_t *&>(data->bstrm.next_in),
                                    &data->bstrm.avail_out, &data->bstrm.next_out, &data->bstrm.total_out);

    if (!ok) {
      error("BrotliEncoderCompressStream(%d) call failed", op);
      return false;
    }

    TSIOBufferProduce(data->downstream_buffer, downstream_length - data->bstrm.avail_out);
    data->downstream_length += (downstream_length - data->bstrm.avail_out);
    if (data->bstrm.avail_in || BrotliEncoderHasMoreOutput(data->bstrm.br)) {
      continue;
    }

    break;
  }

  return ok;
}

static void
brotli_transform_one(Data *data, const char *upstream_buffer, int64_t upstream_length)
{
  bool ok = brotli_compress_operation(data, upstream_buffer, upstream_length, BROTLI_OPERATION_PROCESS);
  if (!ok) {
    error("BrotliEncoderCompressStream(PROCESS) call failed");
    return;
  }

  data->bstrm.total_in += upstream_length;

  if (!data->hc->flush()) {
    return;
  }

  ok = brotli_compress_operation(data, nullptr, 0, BROTLI_OPERATION_FLUSH);
  if (!ok) {
    error("BrotliEncoderCompressStream(FLUSH) call failed");
    return;
  }
}
#endif

static void
compress_transform_one(Data *data, TSIOBufferReader upstream_reader, int amount)
{
  TSIOBufferBlock downstream_blkp;
  int64_t upstream_length;
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

#if HAVE_BROTLI_ENCODE_H
    if (data->compression_type & COMPRESSION_TYPE_BROTLI && (data->compression_algorithms & ALGORITHM_BROTLI)) {
      brotli_transform_one(data, upstream_buffer, upstream_length);
    } else
#endif
      if ((data->compression_type & (COMPRESSION_TYPE_GZIP | COMPRESSION_TYPE_DEFLATE)) &&
          (data->compression_algorithms & (ALGORITHM_GZIP | ALGORITHM_DEFLATE))) {
      gzip_transform_one(data, upstream_buffer, upstream_length);
    } else {
      warning("No compression supported. Shouldn't come here.");
    }

    TSIOBufferReaderConsume(upstream_reader, upstream_length);
    amount -= upstream_length;
  }
}

static void
gzip_transform_finish(Data *data)
{
  if (data->state == transform_state_output) {
    TSIOBufferBlock downstream_blkp;
    int64_t downstream_length;

    data->state = transform_state_finished;

    for (;;) {
      downstream_blkp = TSIOBufferStart(data->downstream_buffer);

      char *downstream_buffer = TSIOBufferBlockWriteStart(downstream_blkp, &downstream_length);
      data->zstrm.next_out    = reinterpret_cast<unsigned char *>(downstream_buffer);
      data->zstrm.avail_out   = downstream_length;

      int err = deflate(&data->zstrm, Z_FINISH);

      if (downstream_length > static_cast<int64_t>(data->zstrm.avail_out)) {
        TSIOBufferProduce(data->downstream_buffer, downstream_length - data->zstrm.avail_out);
        data->downstream_length += (downstream_length - data->zstrm.avail_out);
      }

      if (err == Z_OK) { /* some more data to encode */
        continue;
      }

      if (err != Z_STREAM_END) {
        warning("deflate should report Z_STREAM_END");
      }
      break;
    }

    if (data->downstream_length != static_cast<int64_t>(data->zstrm.total_out)) {
      error("gzip-transform: output lengths don't match (%d, %ld)", data->downstream_length, data->zstrm.total_out);
    }

    debug("gzip-transform: Finished gzip");
    log_compression_ratio(data->zstrm.total_in, data->downstream_length);
  }
}

#if HAVE_BROTLI_ENCODE_H
static void
brotli_transform_finish(Data *data)
{
  if (data->state != transform_state_output) {
    return;
  }

  data->state = transform_state_finished;

  bool ok = brotli_compress_operation(data, nullptr, 0, BROTLI_OPERATION_FINISH);
  if (!ok) {
    error("BrotliEncoderCompressStream(PROCESS) call failed");
    return;
  }

  if (data->downstream_length != static_cast<int64_t>(data->bstrm.total_out)) {
    error("brotli-transform: output lengths don't match (%d, %ld)", data->downstream_length, data->bstrm.total_out);
  }

  debug("brotli-transform: Finished brotli");
  log_compression_ratio(data->bstrm.total_in, data->downstream_length);
}
#endif

static void
compress_transform_finish(Data *data)
{
#if HAVE_BROTLI_ENCODE_H
  if (data->compression_type & COMPRESSION_TYPE_BROTLI && data->compression_algorithms & ALGORITHM_BROTLI) {
    brotli_transform_finish(data);
    debug("compress_transform_finish: brotli compression finish");
  } else
#endif
    if ((data->compression_type & (COMPRESSION_TYPE_GZIP | COMPRESSION_TYPE_DEFLATE)) &&
        (data->compression_algorithms & (ALGORITHM_GZIP | ALGORITHM_DEFLATE))) {
    gzip_transform_finish(data);
    debug("compress_transform_finish: gzip compression finish");
  } else {
    error("No Compression matched, shouldn't come here");
  }
}

static void
compress_transform_do(TSCont contp)
{
  TSVIO upstream_vio;
  Data *data;
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
  TSMLoc hdr_loc;
  TSMLoc field_loc;

  /* Client request header */
  TSMBuffer cbuf;
  TSMLoc chdr;
  TSMLoc cfield;

  const char *value;
  int len;
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

  if (TS_SUCCESS != TSHttpTxnClientReqGet(txnp, &cbuf, &chdr)) {
    info("cound not get client request");
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    return 0;
  }

  // the only compressible method is currently GET.
  int method_length;
  const char *method = TSHttpHdrMethodGet(cbuf, chdr, &method_length);

  if (!(method_length == TS_HTTP_LEN_GET && memcmp(method, TS_HTTP_METHOD_GET, TS_HTTP_LEN_GET) == 0)) {
    debug("method is not GET, not compressible");
    TSHandleMLocRelease(cbuf, TS_NULL_MLOC, chdr);
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

      if (strncasecmp(value, "br", sizeof("br") - 1) == 0) {
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
      info("response is is smaller than minimum content length, not compressing");
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
  Data *data;

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
  data      = data_alloc(compress_type, algorithms);
  data->txn = txnp;
  data->hc  = hc;

  TSContDataSet(connp, data);
  TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, connp);
}

HostConfiguration *
find_host_configuration(TSHttpTxn /* txnp ATS_UNUSED */, TSMBuffer bufp, TSMLoc locp, Configuration *config)
{
  TSMLoc fieldp    = TSMimeHdrFieldFind(bufp, locp, TS_MIME_FIELD_HOST, TS_MIME_LEN_HOST);
  int strl         = 0;
  const char *strv = nullptr;
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
  TSHttpTxn txnp        = static_cast<TSHttpTxn>(edata);
  int compress_type     = COMPRESSION_TYPE_DEFAULT;
  int algorithms        = ALGORITHM_DEFAULT;
  HostConfiguration *hc = static_cast<HostConfiguration *>(TSContDataGet(contp));

  switch (event) {
  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    // os: the accept encoding header needs to be restored..
    // otherwise the next request won't get a cache hit on this
    if (hc != nullptr) {
      info("reading response headers");
      if (hc->remove_accept_encoding()) {
        TSMBuffer req_buf;
        TSMLoc req_loc;

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
        TSMLoc req_loc;

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
    hc->release();
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
 * 3. Check for Accept encoding
 * 4. Schedules TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK and TS_HTTP_TXN_CLOSE_HOOK for
 *    further processing
 */
static void
handle_request(TSHttpTxn txnp, Configuration *config)
{
  TSMBuffer req_buf;
  TSMLoc req_loc;
  HostConfiguration *hc;

  if (TSHttpTxnClientReqGet(txnp, &req_buf, &req_loc) == TS_SUCCESS) {
    if (config == nullptr) {
      hc = find_host_configuration(txnp, req_buf, req_loc, nullptr); // Get a lease on the global config
    } else {
      hc = find_host_configuration(txnp, req_buf, req_loc, config); // Get a lease on the local config passed through doRemap
    }
    bool allowed = false;

    if (hc->enabled()) {
      if (hc->has_allows()) {
        int url_len;
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
      TSHttpTxnHookAdd(txnp, TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, transform_contp);
      TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, transform_contp); // To release the config
    } else {
      hc->release(); // No longer need this configuration, release it.
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
  const char *path         = static_cast<const char *>(TSContDataGet(contp));
  Configuration *newconfig = Configuration::Parse(path);
  Configuration *oldconfig = __sync_lock_test_and_set(&cur_config, newconfig);

  debug("config swapped, old config %p", oldconfig);

  // First, if there was a previous configuration, clean that one out. This avoids the
  // small race condition tht exist between doing a find() and calling hold() on a
  // HostConfig object.
  if (prev_config) {
    prev_config->release_all();
    debug("deleting previous configuration container, %p", prev_config);
    delete prev_config;
  }
  prev_config = oldconfig;
}

static int
management_update(TSCont contp, TSEvent event, void * /* edata ATS_UNUSED */)
{
  TSReleaseAssert(event == TS_EVENT_MGMT_UPDATE);
  info("management update event received");
  load_global_configuration(contp);

  return 0;
}

void
TSPluginInit(int argc, const char *argv[])
{
  const char *config_path = nullptr;

  if (argc > 2) {
    fatal("the compress plugin does not accept more than 1 plugin argument");
  } else {
    config_path = TSstrdup(2 == argc ? argv[1] : "");
  }

  if (!register_plugin()) {
    fatal("the compress plugin failed to register");
  }

  info("TSPluginInit %s", argv[0]);

  if (!global_hidden_header_name) {
    global_hidden_header_name = init_hidden_header_name();
  }

  TSCont management_contp = TSContCreate(management_update, nullptr);

  // Make sure the global configuration is properly loaded and reloaded on changes
  TSContDataSet(management_contp, (void *)config_path);
  TSMgmtUpdateRegister(management_contp, TAG);
  load_global_configuration(management_contp);

  // Setup the global hook, main entry point for kicking off the plugin
  TSCont transform_global_contp = TSContCreate(transform_global_plugin, nullptr);

  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, transform_global_contp);
  info("loaded");
}

//////////////////////////////////////////////////////////////////////////////
// Initialize the plugin as a remap plugin.
//
TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  if (!api_info) {
    strncpy(errbuf, "[tsremap_init] - Invalid TSRemapInterface argument", errbuf_size - 1);
    return TS_ERROR;
  }

  if (api_info->tsremap_version < TSREMAP_VERSION) {
    snprintf(errbuf, errbuf_size, "[TSRemapInit] - Incorrect API version %ld.%ld", api_info->tsremap_version >> 16,
             (api_info->tsremap_version & 0xffff));
    return TS_ERROR;
  }

  info("The compress plugin is successfully initialized");
  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **instance, char *errbuf, int errbuf_size)
{
  info("Instantiating a new compress plugin remap rule");
  info("Reading config from file = %s", argv[2]);

  const char *config_path = nullptr;

  if (argc > 4) {
    fatal("The compress plugin does not accept more than one plugin argument");
  } else {
    config_path = TSstrdup(3 == argc ? argv[2] : "");
  }
  if (!global_hidden_header_name) {
    global_hidden_header_name = init_hidden_header_name();
  }

  Configuration *config = Configuration::Parse(config_path);
  *instance             = config;

  free((void *)config_path);
  info("Configuration loaded");
  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *instance)
{
  debug("Cleanup configs read from remap");
  auto c = static_cast<Configuration *>(instance);
  c->release_all();
  delete c;
}

TSRemapStatus
TSRemapDoRemap(void *instance, TSHttpTxn txnp, TSRemapRequestInfo *rri)
{
  if (nullptr == instance) {
    info("No Rules configured, falling back to default");
  } else {
    info("Remap Rules configured for compress");
    Configuration *config = static_cast<Configuration *>(instance);
    // Handle compress request and use the configs populated from remap instance
    handle_request(txnp, config);
  }
  return TSREMAP_NO_REMAP;
}
