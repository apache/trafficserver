/** @file

  Gzip/Deflate compression implementation

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

#include "gzip_compress.h"
#include "debug_macros.h"

#include <zlib.h>
#include <cstring>
#include <cinttypes>

namespace Compress
{
extern const char *dictionary;
}

namespace Gzip
{
voidpf
gzip_alloc(voidpf /* opaque ATS_UNUSED */, uInt items, uInt size)
{
  return static_cast<voidpf>(TSmalloc(items * size));
}

void
gzip_free(voidpf /* opaque ATS_UNUSED */, voidpf address)
{
  TSfree(address);
}

void
data_alloc(Data *data)
{
  data->zstrm.next_in   = Z_NULL;
  data->zstrm.avail_in  = 0;
  data->zstrm.total_in  = 0;
  data->zstrm.next_out  = Z_NULL;
  data->zstrm.avail_out = 0;
  data->zstrm.total_out = 0;
  data->zstrm.zalloc    = Gzip::gzip_alloc;
  data->zstrm.zfree     = Gzip::gzip_free;
  data->zstrm.opaque    = (voidpf) nullptr;
  data->zstrm.data_type = Z_ASCII;
}

bool
transform_init(Data *data)
{
  int window_bits = WINDOW_BITS_GZIP;
  if (data->compression_type & COMPRESSION_TYPE_DEFLATE) {
    window_bits = WINDOW_BITS_DEFLATE;
  }

  int compression_level = data->hc->zlib_compression_level();
  debug("gzip compression context initialized with level %d", compression_level);

  int err = deflateInit2(&data->zstrm, compression_level, Z_DEFLATED, window_bits, ZLIB_MEMLEVEL, Z_DEFAULT_STRATEGY);

  if (err != Z_OK) {
    error("gzip-transform: deflateInit2 failed (%d)", err);
    return false;
  }

  if (Compress::dictionary) {
    err = deflateSetDictionary(&data->zstrm, reinterpret_cast<const Bytef *>(Compress::dictionary), strlen(Compress::dictionary));
    if (err != Z_OK) {
      error("gzip-transform: deflateSetDictionary failed (%d)", err);
      deflateEnd(&data->zstrm);
      return false;
    }
  }

  return true;
}

void
data_destroy(Data *data)
{
  // deflateEnd return value ignore is intentional
  // it would spew log on every client abort
  deflateEnd(&data->zstrm);
}

void
transform_one(Data *data, const char *upstream_buffer, int64_t upstream_length)
{
  TSIOBufferBlock downstream_blkp;
  int64_t         downstream_length;
  int             err;
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

void
transform_finish(Data *data)
{
  if (data->state == transform_state_output) {
    TSIOBufferBlock downstream_blkp;
    int64_t         downstream_length;

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
      error("gzip-transform: output lengths don't match (%" PRId64 ", %lu)", data->downstream_length, data->zstrm.total_out);
    }

    debug("gzip-transform: Finished gzip");
    log_compression_ratio(data->zstrm.total_in, data->downstream_length);
  }
}

} // namespace Gzip
