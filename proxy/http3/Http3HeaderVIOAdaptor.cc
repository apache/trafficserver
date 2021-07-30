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
#include "HTTP2.h"

// Constant strings for pseudo headers
const char *HTTP3_VALUE_SCHEME    = ":scheme";
const char *HTTP3_VALUE_AUTHORITY = ":authority";

const unsigned HTTP3_LEN_SCHEME    = countof(":scheme") - 1;
const unsigned HTTP3_LEN_AUTHORITY = countof(":authority") - 1;

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
  ParseResult res = this->_convert_header_from_3_to_1_1(&this->_header);
  if (res == PARSE_RESULT_ERROR) {
    Debug("http3", "PARSE_RESULT_ERROR");
    return -1;
  }

  // FIXME: response header might be delayed from first response body because of callback from QPACK
  // Workaround fix for mixed response header and body
  if (http_hdr_type_get(this->_header.m_http) == HTTP_TYPE_RESPONSE) {
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
  int bufindex;
  int dumpoffset = 0;
  int done, tmp;
  IOBufferBlock *block;
  do {
    bufindex = 0;
    tmp      = dumpoffset;
    block    = writer->get_current_block();
    if (!block) {
      writer->add_block();
      block = writer->get_current_block();
    }
    done = this->_header.print(block->end(), block->write_avail(), &bufindex, &tmp);
    dumpoffset += bufindex;
    writer->fill(bufindex);
    if (!done) {
      writer->add_block();
    }
  } while (!done);

  this->_is_complete = true;
  return 1;
}

ParseResult
Http3HeaderVIOAdaptor::_convert_header_from_3_to_1_1(HTTPHdr *hdrs)
{
  // TODO: do HTTP/3 specific convert, if there

  if (http_hdr_type_get(hdrs->m_http) == HTTP_TYPE_REQUEST) {
    // Dirty hack to bypass checks
    MIMEField *field;
    if ((field = hdrs->field_find(HTTP3_VALUE_SCHEME, HTTP3_LEN_SCHEME)) == nullptr) {
      char value_s[]          = "https";
      MIMEField *scheme_field = hdrs->field_create(HTTP3_VALUE_SCHEME, HTTP3_LEN_SCHEME);
      scheme_field->value_set(hdrs->m_heap, hdrs->m_mime, value_s, sizeof(value_s) - 1);
      hdrs->field_attach(scheme_field);
    }

    if ((field = hdrs->field_find(HTTP3_VALUE_AUTHORITY, HTTP3_LEN_AUTHORITY)) == nullptr) {
      char value_a[]             = "localhost";
      MIMEField *authority_field = hdrs->field_create(HTTP3_VALUE_AUTHORITY, HTTP3_LEN_AUTHORITY);
      authority_field->value_set(hdrs->m_heap, hdrs->m_mime, value_a, sizeof(value_a) - 1);
      hdrs->field_attach(authority_field);
    }
  }

  return http2_convert_header_from_2_to_1_1(hdrs);
}
