/** @file

  Brotli compression implementation

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

#include "brotli_compress.h"

#if HAVE_BROTLI_ENCODE_H

#include "debug_macros.h"

#include <brotli/encode.h>
#include <cinttypes>

namespace Brotli
{
static bool
compress_operation(Data *data, const char *upstream_buffer, int64_t upstream_length, BrotliEncoderOperation op)
{
  TSIOBufferBlock downstream_blkp;
  int64_t         downstream_length;

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

void
data_alloc(Data *data)
{
  data->bstrm.br        = nullptr;
  data->bstrm.next_in   = nullptr;
  data->bstrm.avail_in  = 0;
  data->bstrm.total_in  = 0;
  data->bstrm.next_out  = nullptr;
  data->bstrm.avail_out = 0;
  data->bstrm.total_out = 0;
}

bool
transform_init(Data *data)
{
  debug("brotli compression: creating Brotli Encoder Instance");
  data->bstrm.br = BrotliEncoderCreateInstance(nullptr, nullptr, nullptr);
  if (!data->bstrm.br) {
    error("brotli-transform: failed to create Brotli Encoder Instance");
    return false;
  }

  int compression_level = data->hc->brotli_compression_level();
  int lgwin             = data->hc->brotli_lgw_size();

  if (!BrotliEncoderSetParameter(data->bstrm.br, BROTLI_PARAM_QUALITY, compression_level)) {
    error("brotli-transform: failed to set compression level %d", compression_level);
    BrotliEncoderDestroyInstance(data->bstrm.br);
    data->bstrm.br = nullptr;
    return false;
  }

  if (!BrotliEncoderSetParameter(data->bstrm.br, BROTLI_PARAM_LGWIN, lgwin)) {
    error("brotli-transform: failed to set window size %d", lgwin);
    BrotliEncoderDestroyInstance(data->bstrm.br);
    data->bstrm.br = nullptr;
    return false;
  }

  debug("brotli compression context initialized with level %d, lgwin %d", compression_level, lgwin);
  return true;
}

void
data_destroy(Data *data)
{
  if (data->bstrm.br) {
    BrotliEncoderDestroyInstance(data->bstrm.br);
    data->bstrm.br = nullptr;
  }
}

void
transform_one(Data *data, const char *upstream_buffer, int64_t upstream_length)
{
  bool ok = compress_operation(data, upstream_buffer, upstream_length, BROTLI_OPERATION_PROCESS);
  if (!ok) {
    error("BrotliEncoderCompressStream(PROCESS) call failed");
    return;
  }

  data->bstrm.total_in += upstream_length;

  if (!data->hc->flush()) {
    return;
  }

  ok = compress_operation(data, nullptr, 0, BROTLI_OPERATION_FLUSH);
  if (!ok) {
    error("BrotliEncoderCompressStream(FLUSH) call failed");
    return;
  }
}

void
transform_finish(Data *data)
{
  if (data->state != transform_state_output) {
    return;
  }

  data->state = transform_state_finished;

  bool ok = compress_operation(data, nullptr, 0, BROTLI_OPERATION_FINISH);
  if (!ok) {
    error("BrotliEncoderCompressStream(PROCESS) call failed");
    return;
  }

  if (data->downstream_length != static_cast<int64_t>(data->bstrm.total_out)) {
    error("brotli-transform: output lengths don't match (%" PRId64 ", %zu)", data->downstream_length, data->bstrm.total_out);
  }

  debug("brotli-transform: Finished brotli");
  log_compression_ratio(data->bstrm.total_in, data->downstream_length);
}

} // namespace Brotli

#endif // HAVE_BROTLI_ENCODE_H
