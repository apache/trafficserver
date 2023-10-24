/** @file

  fuzzing proxy/hdrs & proxy/http

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

#include "proxy/hdrs/HTTP.h"
#include "proxy/hdrs/HttpCompat.h"
#include "tscore/Diags.h"

#define kMinInputLength 10
#define kMaxInputLength 1024

extern int cmd_disable_pfreelist;

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t *input_data, size_t size_data)
{
  if (size_data < kMinInputLength || size_data > kMaxInputLength) {
    return 0;
  }

  std::string input(reinterpret_cast<const char *>(input_data), size_data);
  char const *start = input.c_str();
  char const *end   = input.c_str() + input.size();

  cmd_disable_pfreelist = true;
  DiagsPtr::set(new Diags("fuzzing", "", "", nullptr));

  http_init();

  HTTPParser parser;
  HTTPHdr req_hdr, rsp_hdr, req_hdr_2;

  req_hdr.create(HTTP_TYPE_REQUEST);
  rsp_hdr.create(HTTP_TYPE_RESPONSE);
  req_hdr_2.create(HTTP_TYPE_REQUEST, HTTP_2_0);

  {
    http_parser_init(&parser);
    req_hdr.parse_req(&parser, &start, end, true);
    http_parser_clear(&parser);
  }
  {
    http_parser_init(&parser);
    rsp_hdr.parse_resp(&parser, &start, end, true);
    http_parser_clear(&parser);
  }
  {
    http_parser_init(&parser);

    req_hdr_2.parse_req(&parser, &start, end, true);
    http_parser_clear(&parser);
  }

  req_hdr.destroy();
  rsp_hdr.destroy();
  req_hdr_2.destroy();

  delete diags();

  return 0;
}
