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

#include "Http3HeaderVIOAdaptor.h"

#include "I_VIO.h"
#include "HTTP.h"

Http3HeaderVIOAdaptor::Http3HeaderVIOAdaptor(HTTPHdr *hdr, QPACK *qpack, Continuation *cont, uint64_t stream_id)
  : _request_header(hdr), _qpack(qpack), _cont(cont), _stream_id(stream_id)
{
}

std::vector<Http3FrameType>
Http3HeaderVIOAdaptor::interests()
{
  return {Http3FrameType::HEADERS};
}

Http3ErrorUPtr
Http3HeaderVIOAdaptor::handle_frame(std::shared_ptr<const Http3Frame> frame)
{
  ink_assert(frame->type() == Http3FrameType::HEADERS);
  const Http3HeadersFrame *hframe = dynamic_cast<const Http3HeadersFrame *>(frame.get());

  int res = this->_qpack->decode(this->_stream_id, hframe->header_block(), hframe->header_block_length(), *this->_request_header,
                                 this->_cont);

  if (res == 0) {
    // When decoding is not blocked, continuation should be called directly?
  } else if (res == 1) {
    // Decoding is blocked.
    Debug("http3", "Decoding is blocked. DecodeRequest is scheduled");
  } else if (res < 0) {
    Debug("http3", "Error on decoding header (%d)", res);
  } else {
    ink_abort("should not be here");
  }

  return Http3ErrorUPtr(new Http3NoError());
}
