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

#include "proxy/http3/Http3HeaderVIOAdaptor.h"
#include "proxy/hdrs/HeaderValidator.h"

#include "iocore/eventsystem/VIO.h"
#include "proxy/hdrs/HTTP.h"

namespace
{
DbgCtl dbg_ctl_http3{"http3"};
DbgCtl dbg_ctl_v_http3{"v_http3"};

} // end anonymous namespace

Http3HeaderVIOAdaptor::Http3HeaderVIOAdaptor(VIO *sink, HTTPType http_type, QPACK *qpack, uint64_t stream_id)
  : _sink_vio(sink), _qpack(qpack), _stream_id(stream_id)
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
Http3HeaderVIOAdaptor::handle_frame(std::shared_ptr<const Http3Frame> frame, Http3StreamType /* s_type */)
{
  ink_assert(frame->type() == Http3FrameType::HEADERS);
  const Http3HeadersFrame *hframe = dynamic_cast<const Http3HeadersFrame *>(frame.get());

  int res = this->_qpack->decode(this->_stream_id, hframe->header_block(), hframe->header_block_length(), _header, this);

  if (res == 0) {
    // When decoding is not blocked, continuation should be called directly?
  } else if (res == 1) {
    // Decoding is blocked.
    Dbg(dbg_ctl_http3, "Decoding is blocked. DecodeRequest is scheduled");
  } else if (res < 0) {
    Dbg(dbg_ctl_http3, "Error on decoding header (%d)", res);
  } else {
    ink_abort("should not be here");
  }

  return Http3ErrorUPtr(nullptr);
}

bool
Http3HeaderVIOAdaptor::is_complete()
{
  return this->_is_complete;
}

int
Http3HeaderVIOAdaptor::event_handler(int event, Event * /* data ATS_UNUSED */)
{
  switch (event) {
  case QPACK_EVENT_DECODE_COMPLETE:
    Dbg(dbg_ctl_v_http3, "%s (%d)", "QPACK_EVENT_DECODE_COMPLETE", event);
    if (this->_on_qpack_decode_complete()) {
      // If READ_READY event is scheduled, should it be canceled?
    }
    break;
  case QPACK_EVENT_DECODE_FAILED:
    Dbg(dbg_ctl_v_http3, "%s (%d)", "QPACK_EVENT_DECODE_FAILED", event);
    // FIXME: handle error
    break;
  }

  return EVENT_DONE;
}

int
Http3HeaderVIOAdaptor::_on_qpack_decode_complete()
{
  // Currently trailer support for h3 is not implemented.
  constexpr static bool NON_TRAILER = false;
  if (!HeaderValidator::is_h2_h3_header_valid(this->_header, http_hdr_type_get(this->_header.m_http) == HTTPType::RESPONSE,
                                              NON_TRAILER)) {
    Dbg(dbg_ctl_http3, "Header is invalid");
    return -1;
  }
  int res = this->_hvc.convert(this->_header, 3, 1);
  if (res != 0) {
    Dbg(dbg_ctl_http3, "ParseResult::ERROR");
    return -1;
  }

  // FIXME: response header might be delayed from first response body because of callback from QPACK
  // Workaround fix for mixed response header and body
  if (http_hdr_type_get(this->_header.m_http) == HTTPType::RESPONSE) {
    return 0;
  }

  SCOPED_MUTEX_LOCK(lock, this->_sink_vio->mutex, this_ethread());
  MIOBuffer *writer = this->_sink_vio->get_writer();

  // TODO: Http2Stream::send_request has same logic. It originally comes from HttpSM::write_header_into_buffer.
  // a). Make HttpSM::write_header_into_buffer static
  //   or
  // b). Add interface to HTTPHdr to dump data
  //   or
  // c). Add interface to HttpSM to handle HTTPHdr directly
  int            bufindex;
  int            dumpoffset = 0;
  int            done, tmp;
  IOBufferBlock *block;
  do {
    bufindex = 0;
    tmp      = dumpoffset;
    block    = writer->get_current_block();
    if (!block) {
      writer->add_block();
      block = writer->get_current_block();
    }
    done        = this->_header.print(block->end(), block->write_avail(), &bufindex, &tmp);
    dumpoffset += bufindex;
    writer->fill(bufindex);
    if (!done) {
      writer->add_block();
    }
  } while (!done);

  this->_is_complete = true;
  return 1;
}
