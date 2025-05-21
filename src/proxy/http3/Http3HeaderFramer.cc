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

#include "proxy/http3/Http3HeaderFramer.h"

#include "iocore/eventsystem/VIO.h"

#include "proxy/hdrs/HTTP.h"

#include "proxy/http3/Http3Frame.h"
#include "proxy/http3/Http3Transaction.h"

namespace
{
DbgCtl dbg_ctl_http3_trans{"http3_trans"};

} // end anonymous namespace

Http3HeaderFramer::Http3HeaderFramer(Http3Transaction *transaction, VIO *source, QPACK *qpack, uint64_t stream_id)
  : _transaction(transaction), _source_vio(source), _qpack(qpack), _stream_id(stream_id)
{
  http_parser_init(&this->_http_parser);
}

Http3HeaderFramer::~Http3HeaderFramer()
{
  _header.destroy();
  if (_header_block != nullptr) {
    free_MIOBuffer(_header_block);
  }
}

Http3FrameUPtr
Http3HeaderFramer::generate_frame()
{
  if (!this->_source_vio->get_reader()) {
    return Http3FrameFactory::create_null_frame();
  }

  ink_assert(!this->_transaction->is_response_header_sent());

  if (!this->_header_block) {
    // this->_header_block will be filled if it is ready
    this->_generate_header_block();
  }

  if (this->_header_block) {
    uint64_t len = std::min(this->_header_block_len - this->_header_block_wrote, UINT64_C(64 * 1024));

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
Http3HeaderFramer::_generate_header_block()
{
  // Prase response header and generate header block
  int         bytes_used   = 0;
  ParseResult parse_result = ParseResult::ERROR;

  if (this->_transaction->direction() == NET_VCONNECTION_OUT) {
    this->_header.create(HTTPType::REQUEST, HTTP_3_0);
    parse_result = this->_header.parse_req(&this->_http_parser, this->_source_vio->get_reader(), &bytes_used, false);
  } else {
    this->_header.create(HTTPType::RESPONSE, HTTP_3_0);
    parse_result = this->_header.parse_resp(&this->_http_parser, this->_source_vio->get_reader(), &bytes_used, false);
  }
  this->_source_vio->ndone += bytes_used;

  switch (parse_result) {
  case ParseResult::DONE: {
    this->_hvc.convert(this->_header, 1, 3);

    this->_header_block        = new_MIOBuffer(BUFFER_SIZE_INDEX_32K);
    this->_header_block_reader = this->_header_block->alloc_reader();

    this->_qpack->encode(this->_stream_id, this->_header, this->_header_block, this->_header_block_len);
    break;
  }
  case ParseResult::CONT:
    break;
  default:
    Dbg(dbg_ctl_http3_trans, "Ignore invalid headers");
    break;
  }
}
