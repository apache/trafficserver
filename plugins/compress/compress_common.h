/** @file

  Common types and structures for compression plugin

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

#if HAVE_BROTLI_ENCODE_H
#include <brotli/encode.h>
#endif

#include "configuration.h"

enum CompressionType {
  COMPRESSION_TYPE_DEFAULT = 0,
  COMPRESSION_TYPE_DEFLATE = 1,
  COMPRESSION_TYPE_GZIP    = 2,
  COMPRESSION_TYPE_BROTLI  = 4
};

enum transform_state {
  transform_state_initialized,
  transform_state_output,
  transform_state_finished,
};

#if HAVE_BROTLI_ENCODE_H
struct BrotliStream {
  BrotliEncoderState *br;
  uint8_t            *next_in;
  size_t              avail_in;
  uint8_t            *next_out;
  size_t              avail_out;
  size_t              total_in;
  size_t              total_out;
};
#endif

struct Data {
  TSHttpTxn                    txn;
  Compress::HostConfiguration *hc;
  TSVIO                        downstream_vio;
  TSIOBuffer                   downstream_buffer;
  TSIOBufferReader             downstream_reader;
  int64_t                      downstream_length;
  z_stream                     zstrm;
  enum transform_state         state;
  int                          compression_type;
  int                          compression_algorithms;
#if HAVE_BROTLI_ENCODE_H
  BrotliStream bstrm;
#endif
};

void log_compression_ratio(int64_t in, int64_t out);
