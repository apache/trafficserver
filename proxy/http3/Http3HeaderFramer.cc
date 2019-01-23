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
#include "Http3ClientTransaction.h"

Http3HeaderFramer::Http3HeaderFramer(Http3ClientTransaction *transaction, VIO *source, QPACK *qpack, uint64_t stream_id)
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

const char *HTTP3_VALUE_STATUS           = ":status";
const unsigned HTTP3_LEN_STATUS          = countof(":status") - 1;
static size_t HTTP3_LEN_STATUS_VALUE_STR = 3;

// Copy code from http2_generate_h2_header_from_1_1(h1_hdrs, h3_hdrs);
void
Http3HeaderFramer::_convert_header_from_1_1_to_3(HTTPHdr *h3_hdrs, HTTPHdr *h1_hdrs)
{
  // Add ':status' header field
  char status_str[HTTP3_LEN_STATUS_VALUE_STR + 1];
  snprintf(status_str, sizeof(status_str), "%d", h1_hdrs->status_get());
  MIMEField *status_field = h3_hdrs->field_create(HTTP3_VALUE_STATUS, HTTP3_LEN_STATUS);
  status_field->value_set(h3_hdrs->m_heap, h3_hdrs->m_mime, status_str, HTTP3_LEN_STATUS_VALUE_STR);
  h3_hdrs->field_attach(status_field);

  // Copy headers
  // Intermediaries SHOULD remove connection-specific header fields.
  MIMEFieldIter field_iter;
  for (MIMEField *field = h1_hdrs->iter_get_first(&field_iter); field != nullptr; field = h1_hdrs->iter_get_next(&field_iter)) {
    const char *name;
    int name_len;
    const char *value;
    int value_len;
    name = field->name_get(&name_len);
    if ((name_len == MIME_LEN_CONNECTION && strncasecmp(name, MIME_FIELD_CONNECTION, name_len) == 0) ||
        (name_len == MIME_LEN_KEEP_ALIVE && strncasecmp(name, MIME_FIELD_KEEP_ALIVE, name_len) == 0) ||
        (name_len == MIME_LEN_PROXY_CONNECTION && strncasecmp(name, MIME_FIELD_PROXY_CONNECTION, name_len) == 0) ||
        (name_len == MIME_LEN_TRANSFER_ENCODING && strncasecmp(name, MIME_FIELD_TRANSFER_ENCODING, name_len) == 0) ||
        (name_len == MIME_LEN_UPGRADE && strncasecmp(name, MIME_FIELD_UPGRADE, name_len) == 0)) {
      continue;
    }
    MIMEField *newfield;
    name     = field->name_get(&name_len);
    newfield = h3_hdrs->field_create(name, name_len);
    value    = field->value_get(&value_len);
    newfield->value_set(h3_hdrs->m_heap, h3_hdrs->m_mime, value, value_len);
    h3_hdrs->field_attach(newfield);
  }
}

void
Http3HeaderFramer::_generate_header_block()
{
  // Prase response header and generate header block
  int bytes_used = 0;
  // TODO Use HTTP_TYPE_REQUEST if this is for requests
  this->_header.create(HTTP_TYPE_RESPONSE);
  int parse_result = this->_header.parse_resp(&this->_http_parser, this->_source_vio->get_reader(), &bytes_used, false);
  this->_source_vio->ndone += this->_header.length_get();

  switch (parse_result) {
  case PARSE_RESULT_DONE: {
    HTTPHdr h3_hdr;
    h3_hdr.create(HTTP_TYPE_RESPONSE);
    this->_convert_header_from_1_1_to_3(&h3_hdr, &this->_header);

    this->_header_block        = new_MIOBuffer();
    this->_header_block_reader = this->_header_block->alloc_reader();

    this->_qpack->encode(this->_stream_id, h3_hdr, this->_header_block, this->_header_block_len);
    break;
  }
  case PARSE_RESULT_CONT:
    break;
  default:
    break;
  }
}
