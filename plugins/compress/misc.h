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

#pragma once

#include <zlib.h>
#include <ts/ts.h>
#include <cstdlib>
#include <cstdio>

#if HAVE_BROTLI_ENCODE_H
#include <brotli/encode.h>
#endif

#include "configuration.h"

using namespace Gzip;

// zlib stuff, see [deflateInit2] at http://www.zlib.net/manual.html
static const int ZLIB_MEMLEVEL       = 9; // min=1 (optimize for memory),max=9 (optimized for speed)
static const int WINDOW_BITS_DEFLATE = -15;
static const int WINDOW_BITS_GZIP    = 31;

// misc
enum CompressionType {
  COMPRESSION_TYPE_DEFAULT = 0,
  COMPRESSION_TYPE_DEFLATE = 1,
  COMPRESSION_TYPE_GZIP    = 2,
  COMPRESSION_TYPE_BROTLI  = 4
};

// this one is used to rename the accept encoding header
// it will be restored later on
// to make it work, the name must be different then downstream proxies though
// otherwise the downstream will restore the accept encoding header

enum transform_state {
  transform_state_initialized,
  transform_state_output,
  transform_state_finished,
};

#if HAVE_BROTLI_ENCODE_H
typedef struct {
  BrotliEncoderState *br;
  uint8_t *next_in;
  size_t avail_in;
  uint8_t *next_out;
  size_t avail_out;
  size_t total_in;
  size_t total_out;
} b_stream;
#endif

typedef struct {
  TSHttpTxn txn;
  HostConfiguration *hc;
  TSVIO downstream_vio;
  TSIOBuffer downstream_buffer;
  TSIOBufferReader downstream_reader;
  int downstream_length;
  z_stream zstrm;
  enum transform_state state;
  int compression_type;
  int compression_algorithms;
#if HAVE_BROTLI_ENCODE_H
  b_stream bstrm;
#endif
} Data;

voidpf gzip_alloc(voidpf opaque, uInt items, uInt size);
void gzip_free(voidpf opaque, voidpf address);
void normalize_accept_encoding(TSHttpTxn txnp, TSMBuffer reqp, TSMLoc hdr_loc);
void hide_accept_encoding(TSHttpTxn txnp, TSMBuffer reqp, TSMLoc hdr_loc, const char *hidden_header_name);
void restore_accept_encoding(TSHttpTxn txnp, TSMBuffer reqp, TSMLoc hdr_loc, const char *hidden_header_name);
const char *init_hidden_header_name();
int check_ts_version();
int register_plugin();
void log_compression_ratio(int64_t in, int64_t out);
