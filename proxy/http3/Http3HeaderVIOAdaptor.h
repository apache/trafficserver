/** @file

  A brief file description

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

#include "QPACK.h"

#include "Http3FrameHandler.h"

class Http3HeaderVIOAdaptor : public Continuation, public Http3FrameHandler
{
public:
  Http3HeaderVIOAdaptor(VIO *sink, HTTPType http_type, QPACK *qpack, uint64_t stream_id);
  ~Http3HeaderVIOAdaptor();

  // Http3FrameHandler
  std::vector<Http3FrameType> interests() override;
  Http3ErrorUPtr handle_frame(std::shared_ptr<const Http3Frame> frame) override;

  bool is_complete();
  int event_handler(int event, Event *data);

private:
  VIO *_sink_vio      = nullptr;
  QPACK *_qpack       = nullptr;
  uint64_t _stream_id = 0;
  bool _is_complete   = false;

  HTTPHdr _header; ///< HTTP header buffer for decoding

  int _on_qpack_decode_complete();
  ParseResult _convert_header_from_3_to_1_1(HTTPHdr *hdr);
};
