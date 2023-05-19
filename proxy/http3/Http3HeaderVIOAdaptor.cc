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

#include "HTTP.h"

Http3HeaderVIOAdaptor::Http3HeaderVIOAdaptor(HTTPType http_type, QPACK *qpack, uint64_t stream_id)
  : _qpack(qpack), _stream_id(stream_id)
{
  SET_HANDLER(&Http3HeaderVIOAdaptor::event_handler);

  this->_header.create(http_type);
}

Http3HeaderVIOAdaptor::~Http3HeaderVIOAdaptor()
{
  this->_header.destroy();
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

  int res = this->_qpack->decode(this->_stream_id, hframe->header_block(), hframe->header_block_length(), _header, this);

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

bool
Http3HeaderVIOAdaptor::is_complete()
{
  return this->_is_complete;
}

const HTTPHdr *
Http3HeaderVIOAdaptor::get_header()
{
  return &this->_header;
}

int
Http3HeaderVIOAdaptor::event_handler(int event, Event *data)
{
  switch (event) {
  case QPACK_EVENT_DECODE_COMPLETE:
    Debug("v_http3", "%s (%d)", "QPACK_EVENT_DECODE_COMPLETE", event);
    if (this->_on_qpack_decode_complete()) {
      // If READ_READY event is scheduled, should it be canceled?
    }
    break;
  case QPACK_EVENT_DECODE_FAILED:
    Debug("v_http3", "%s (%d)", "QPACK_EVENT_DECODE_FAILED", event);
    // FIXME: handle error
    break;
  }

  return EVENT_DONE;
}

int
Http3HeaderVIOAdaptor::_on_qpack_decode_complete()
{
  int res = this->_hvc.convert(this->_header, 3, 1);
  if (res != 0) {
    Debug("http3", "PARSE_RESULT_ERROR");
    return -1;
  }

  this->_is_complete = true;
  return 1;
}
