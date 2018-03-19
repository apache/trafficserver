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

#include "HQFrame.h"
#include "HQHeaderFramer.h"
#include "HQClientTransaction.h"
#include "HTTP.h"
#include "I_VIO.h"

HQHeaderFramer::HQHeaderFramer(HQClientTransaction *transaction, VIO *source) : _transaction(transaction), _source_vio(source)
{
  http_parser_init(&this->_http_parser);
}

HQFrameUPtr
HQHeaderFramer::generate_frame(uint16_t max_size)
{
  ink_assert(!this->_transaction->is_response_header_sent());

  if (!this->_header_block) {
    // this->_header_block will be filled if it is ready
    this->_generate_header_block();
  }

  if (this->_header_block) {
    // Create frames on demand base on max_size since we don't know how much we can write now
    const uint8_t *start = this->_header_block + this->_header_block_wrote;
    size_t len           = std::min(this->_header_block_len - this->_header_block_wrote, static_cast<size_t>(max_size));
    HQFrameUPtr frame    = HQFrameFactory::create_headers_frame(start, len);
    this->_header_block_wrote += len;
    if (this->_header_block_len == this->_header_block_wrote) {
      this->_sent_all_data = true;
    }
    return frame;
  } else {
    return HQFrameFactory::create_null_frame();
  }
}

bool
HQHeaderFramer::is_done() const
{
  return this->_sent_all_data;
}

void
HQHeaderFramer::_generate_header_block()
{
  // Prase response header and generate header block
  int bytes_used = 0;
  // TODO Use HTTP_TYPE_REQUEST if this is for requests
  this->_header.create(HTTP_TYPE_RESPONSE);
  int parse_result = this->_header.parse_resp(&this->_http_parser, this->_source_vio->get_reader(), &bytes_used, false);
  this->_source_vio->ndone += this->_header.length_get();

  switch (parse_result) {
  case PARSE_RESULT_DONE:
    this->_compress_header();
    break;
  case PARSE_RESULT_CONT:
    break;
  default:
    break;
  }
}

void
HQHeaderFramer::_compress_header()
{
  // TODO Compress the header data
  // Just copy the header data for now.
  int written         = 0;
  int tmp             = 0;
  int len             = this->_header.length_get();
  this->_header_block = static_cast<uint8_t *>(ats_malloc(len));
  this->_header.print(reinterpret_cast<char *>(this->_header_block), len, &written, &tmp);
  this->_header_block_len = written;
}
