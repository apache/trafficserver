/** @file

  fuzzing proxy/http2

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

#include "proxy/http2/HTTP2.h"
#include "proxy/hdrs/HuffmanCodec.h"

#define kMinInputLength 8
#define kMaxInputLength 128

#define INITIAL_TABLE_SIZE      4096
#define MAX_REQUEST_HEADER_SIZE 131072
#define MAX_TABLE_SIZE          4096

extern int cmd_disable_pfreelist;

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t *input_data, size_t size_data)
{
  if (size_data < kMinInputLength || size_data > kMaxInputLength) {
    return 0;
  }

  cmd_disable_pfreelist = true;

  hpack_huffman_init();

  HpackIndexingTable       indexing_table(INITIAL_TABLE_SIZE);
  std::unique_ptr<HTTPHdr> headers(new HTTPHdr);
  headers->create(HTTPType::REQUEST);

  hpack_decode_header_block(indexing_table, headers.get(), input_data, size_data, MAX_REQUEST_HEADER_SIZE, MAX_TABLE_SIZE);

  headers->destroy();

  return 0;
}
