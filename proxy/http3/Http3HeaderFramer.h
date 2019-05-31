/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#pragma once

#include "hdrs/HTTP.h"

#include "QPACK.h"

#include "Http3FrameGenerator.h"
#include "Http3Frame.h"

class Http3Transaction;
class VIO;

class Http3HeaderFramer : public Http3FrameGenerator
{
public:
  Http3HeaderFramer(Http3Transaction *transaction, VIO *source, QPACK *qpack, uint64_t stream_id);

  // Http3FrameGenerator
  Http3FrameUPtr generate_frame(uint16_t max_size) override;
  bool is_done() const override;

private:
  Http3Transaction *_transaction       = nullptr;
  VIO *_source_vio                     = nullptr;
  QPACK *_qpack                        = nullptr;
  MIOBuffer *_header_block             = nullptr;
  IOBufferReader *_header_block_reader = nullptr;
  uint64_t _header_block_len           = 0;
  uint64_t _header_block_wrote         = 0;
  uint64_t _stream_id                  = 0;
  bool _sent_all_data                  = false;
  HTTPParser _http_parser;
  HTTPHdr _header;

  void _convert_header_from_1_1_to_3(HTTPHdr *h3_hdrs, HTTPHdr *h1_hdrs);
  void _generate_header_block();
};
