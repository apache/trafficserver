/** @file

  Transforms content using gzip or deflate

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

#ifndef _GZIP_MISC_H_
#define _GZIP_MISC_H_

#include <stdint.h>
#include <zlib.h>
#include <ts/ts.h>
#include <stdlib.h> //exit()
#include <stdio.h>

// zlib stuff, see [deflateInit2] at http://www.zlib.net/manual.html
static const int ZLIB_MEMLEVEL       = 9; // min=1 (optimize for memory),max=9 (optimized for speed)
static const int WINDOW_BITS_DEFLATE = -15;
static const int WINDOW_BITS_GZIP    = 31;

// misc
static const int COMPRESSION_TYPE_DEFLATE = 1;
static const int COMPRESSION_TYPE_GZIP    = 2;
// this one is just for txnargset/get to point to
static const int GZIP_ONE       = 1;
static const int DICT_PATH_MAX  = 512;
static const int DICT_ENTRY_MAX = 2048;

// this one is used to rename the accept encoding header
// it will be restored later on
// to make it work, the name must be different then downstream proxies though
// otherwise the downstream will restore the accept encoding header

enum transform_state { transform_state_initialized, transform_state_output, transform_state_finished };

typedef struct {
  TSHttpTxn txn;
  TSVIO downstream_vio;
  TSIOBuffer downstream_buffer;
  TSIOBufferReader downstream_reader;
  int downstream_length;
  z_stream zstrm;
  enum transform_state state;
  int compression_type;
} GzipData;

voidpf gzip_alloc(voidpf opaque, uInt items, uInt size);
void gzip_free(voidpf opaque, voidpf address);
void normalize_accept_encoding(TSHttpTxn txnp, TSMBuffer reqp, TSMLoc hdr_loc);
void hide_accept_encoding(TSHttpTxn txnp, TSMBuffer reqp, TSMLoc hdr_loc, const char *hidden_header_name);
void restore_accept_encoding(TSHttpTxn txnp, TSMBuffer reqp, TSMLoc hdr_loc, const char *hidden_header_name);
const char *init_hidden_header_name();
int check_ts_version();
int register_plugin();
const char *load_dictionary(const char *preload_file);
void gzip_log_ratio(int64_t in, int64_t out);

#endif
