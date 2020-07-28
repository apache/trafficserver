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

#include "Http3HeaderFramer.h"

#include "I_VIO.h"

#include "HTTP.h"
#include "HTTP2.h"

#include "Http3Frame.h"
#include "Http3Transaction.h"

Http3HeaderFramer::Http3HeaderFramer(Http3Transaction *transaction, VIO *source, QPACK *qpack, uint64_t stream_id)
  : _transaction(transaction), _source_vio(source), _qpack(qpack), _stream_id(stream_id)
{
  http_parser_init(&this->_http_parser);
}

Http3FrameUPtr
Http3HeaderFramer::generate_frame(uint16_t max_size)
{
  ink_assert(!this->_transaction->is_response_header_sent());

  if (!this->_header_block) {
    // this->_header_block will be filled if it is ready
    this->_generate_header_block();
  }

  if (this->_header_block) {
    // Create frames on demand base on max_size since we don't know how much we can write now
    uint64_t len = std::min(this->_header_block_len - this->_header_block_wrote, static_cast<uint64_t>(max_size));

    Http3FrameUPtr frame = Http3FrameFactory::create_headers_frame(this->_header_block_reader, len);

    this->_header_block_wrote += len;

    if (this->_header_block_len == this->_header_block_wrote) {
      this->_sent_all_data = true;
    }
    return frame;
  } else {
    return Http3FrameFactory::create_null_frame();
  }
}

bool
Http3HeaderFramer::is_done() const
{
  return this->_sent_all_data;
}

void
Http3HeaderFramer::_convert_header_from_1_1_to_3(HTTPHdr *hdrs)
{
  http2_convert_header_from_1_1_to_2(hdrs);
}

void
Http3HeaderFramer::_generate_header_block()
{
  // Prase response header and generate header block
  int bytes_used           = 0;
  ParseResult parse_result = PARSE_RESULT_ERROR;

  if (this->_transaction->direction() == NET_VCONNECTION_OUT) {
    this->_header.create(HTTP_TYPE_REQUEST);
    http2_init_pseudo_headers(this->_header);
    parse_result = this->_header.parse_req(&this->_http_parser, this->_source_vio->get_reader(), &bytes_used, false);
  } else {
    this->_header.create(HTTP_TYPE_RESPONSE);
    http2_init_pseudo_headers(this->_header);
    parse_result = this->_header.parse_resp(&this->_http_parser, this->_source_vio->get_reader(), &bytes_used, false);
  }
  this->_source_vio->ndone += bytes_used;

  switch (parse_result) {
  case PARSE_RESULT_DONE: {
    this->_convert_header_from_1_1_to_3(&this->_header);

    this->_header_block        = new_MIOBuffer(BUFFER_SIZE_INDEX_32K);
    this->_header_block_reader = this->_header_block->alloc_reader();

    this->_qpack->encode(this->_stream_id, this->_header, this->_header_block, this->_header_block_len);
    break;
  }
  case PARSE_RESULT_CONT:
    break;
  default:
    Debug("http3_trans", "Ignore invalid headers");
    break;
  }
}
