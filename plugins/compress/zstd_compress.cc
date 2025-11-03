/** @file

  Zstd compression implementation

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information regarding copyright ownership.  The ASF licenses this file to you under
  the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License.  You may
  obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS
  IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the License for the specific
  language governing permissions and limitations under the License.
 */

#include "zstd_compress.h"

#if HAVE_ZSTD_H

#include "debug_macros.h"

#include <cstring>

namespace
{
bool
compress_operation(Data *data, const char *upstream_buffer, int64_t upstream_length, ZSTD_EndDirective mode)
{
  TSIOBufferBlock downstream_blkp;
  int64_t         downstream_length;

  ZSTD_inBuffer input = {upstream_buffer, static_cast<size_t>(upstream_length), 0};

  for (;;) {
    downstream_blkp         = TSIOBufferStart(data->downstream_buffer);
    char *downstream_buffer = TSIOBufferBlockWriteStart(downstream_blkp, &downstream_length);

    ZSTD_outBuffer output = {downstream_buffer, static_cast<size_t>(downstream_length), 0};

    size_t result = ZSTD_compressStream2(data->zstrm_zstd.cctx, &output, &input, mode);

    if (ZSTD_isError(result)) {
      error("Zstd compression failed (%d): %s", mode, ZSTD_getErrorName(result));
      return false;
    }

    if (output.pos > 0) {
      TSIOBufferProduce(data->downstream_buffer, output.pos);
      data->downstream_length    += output.pos;
      data->zstrm_zstd.total_out += output.pos;
    }

    if (mode == ZSTD_e_continue) {
      if (input.pos >= input.size) {
        break;
      }
      if (output.pos == 0 && input.pos < input.size) {
        error("zstd-transform: no progress made in compression");
        return false;
      }
    } else if (result == 0) {
      break;
    }
  }

  return true;
}
} // namespace

namespace Zstd
{
void
data_alloc(Data *data)
{
  std::memset(&data->zstrm_zstd, 0, sizeof(data->zstrm_zstd));

  data->zstrm_zstd.cctx = ZSTD_createCCtx();
  if (!data->zstrm_zstd.cctx) {
    fatal("Zstd Compression Context Creation Failed");
  }
}

void
data_destroy(Data *data)
{
  if (data->zstrm_zstd.cctx) {
    ZSTD_freeCCtx(data->zstrm_zstd.cctx);
    data->zstrm_zstd.cctx = nullptr;
  }
}

void
transform_init(Data *data)
{
  if (!data->zstrm_zstd.cctx) {
    error("Failed to initialize Zstd compression context");
    return;
  }

  size_t result = ZSTD_CCtx_setParameter(data->zstrm_zstd.cctx, ZSTD_c_compressionLevel, data->hc->zstd_compression_level());
  if (ZSTD_isError(result)) {
    error("Failed to set Zstd compression level: %s", ZSTD_getErrorName(result));
    return;
  }

  result = ZSTD_CCtx_setParameter(data->zstrm_zstd.cctx, ZSTD_c_checksumFlag, 1);
  if (ZSTD_isError(result)) {
    error("Failed to enable Zstd checksum: %s", ZSTD_getErrorName(result));
    return;
  }

  debug("zstd compression context initialized with level %d", data->hc->zstd_compression_level());
}

void
transform_one(Data *data, const char *upstream_buffer, int64_t upstream_length)
{
  if (!compress_operation(data, upstream_buffer, upstream_length, ZSTD_e_continue)) {
    error("Zstd compression (CONTINUE) failed");
    return;
  }

  data->zstrm_zstd.total_in += upstream_length;

  if (!data->hc->flush()) {
    return;
  }

  if (!compress_operation(data, nullptr, 0, ZSTD_e_flush)) {
    error("Zstd compression (FLUSH) failed");
  }
}

void
transform_finish(Data *data)
{
  if (data->state != transform_state_output) {
    return;
  }

  TSIOBufferBlock downstream_blkp;
  int64_t         downstream_length;

  data->state = transform_state_finished;

  for (;;) {
    downstream_blkp         = TSIOBufferStart(data->downstream_buffer);
    char *downstream_buffer = TSIOBufferBlockWriteStart(downstream_blkp, &downstream_length);

    ZSTD_outBuffer output = {downstream_buffer, static_cast<size_t>(downstream_length), 0};

    size_t remaining = ZSTD_endStream(data->zstrm_zstd.cctx, &output);

    if (ZSTD_isError(remaining)) {
      error("zstd compression finish failed: %s", ZSTD_getErrorName(remaining));
      break;
    }

    if (output.pos > 0) {
      TSIOBufferProduce(data->downstream_buffer, output.pos);
      data->downstream_length    += output.pos;
      data->zstrm_zstd.total_out += output.pos;
    }

    if (remaining == 0) {
      break;
    }
  }

  debug("zstd-transform: Finished zstd compression");
  log_compression_ratio(data->zstrm_zstd.total_in, data->downstream_length);
}
} // namespace Zstd

#endif // HAVE_ZSTD_H
