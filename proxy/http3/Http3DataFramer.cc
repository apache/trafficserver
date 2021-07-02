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

#include "Http3Frame.h"
#include "Http3DataFramer.h"
#include "Http3Transaction.h"

Http3DataFramer::Http3DataFramer(Http3Transaction *transaction, VIO *source) : _transaction(transaction), _source_vio(source) {}

Http3FrameUPtr
Http3DataFramer::generate_frame()
{
  if (!this->_transaction->is_response_header_sent()) {
    return Http3FrameFactory::create_null_frame();
  }

  Http3FrameUPtr frame   = Http3FrameFactory::create_null_frame();
  IOBufferReader *reader = this->_source_vio->get_reader();

  size_t payload_len = 128 * 1024;
  if (!reader->is_read_avail_more_than(payload_len)) {
    payload_len = reader->read_avail();
  }

  if (payload_len) {
    frame = Http3FrameFactory::create_data_frame(reader, payload_len);
    this->_source_vio->ndone += payload_len;
  }

  return frame;
}

bool
Http3DataFramer::is_done() const
{
  return this->_source_vio->ntodo() == 0;
}
