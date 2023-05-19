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

#include "HTTP.h"
#include "HttpSM.h"

#include "Http3Frame.h"
#include "Http3Transaction.h"

Http3HeaderFramer::Http3HeaderFramer(Http3Transaction *transaction, QPACK *qpack, uint64_t stream_id)
  : _transaction(transaction), _qpack(qpack), _stream_id(stream_id)
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
  if (this->_transaction->direction() == NET_VCONNECTION_OUT) {
    if (!this->_transaction->get_sm()->get_server_request_header()) {
      return Http3FrameFactory::create_null_frame();
    }
  } else {
    if (!this->_transaction->get_sm()->get_client_response_header()) {
      return Http3FrameFactory::create_null_frame();
    }
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
  const HTTPHdr *base;

  if (this->_transaction->direction() == NET_VCONNECTION_OUT) {
    this->_header.create(HTTP_TYPE_REQUEST, HTTP_3_0);
    base = this->_transaction->get_sm()->get_server_request_header();
  } else {
    this->_header.create(HTTP_TYPE_RESPONSE, HTTP_3_0);
    base = this->_transaction->get_sm()->get_client_response_header();
  }

  if (base != nullptr) {
    // Can't use _hedaer.copy(h) because pseudo headers must be at the beginning
    this->_header.status_set(base->status_get());
    for (auto &&ite = base->begin(); ite != base->end(); ite++) {
      std::string_view name  = ite->name_get();
      std::string_view value = ite->value_get();
      auto *f                = this->_header.field_create(name.data(), name.length());
      f->value_set(this->_header.m_heap, this->_header.m_mime, value.data(), value.length());
      this->_header.field_attach(f);
    }
    this->_hvc.convert(this->_header, 1, 3);
    this->_header_block        = new_MIOBuffer(BUFFER_SIZE_INDEX_32K);
    this->_header_block_reader = this->_header_block->alloc_reader();
    this->_qpack->encode(this->_stream_id, this->_header, this->_header_block, this->_header_block_len);
  }
}
