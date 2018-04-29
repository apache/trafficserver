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
#include "HQDataFramer.h"
#include "HQClientTransaction.h"

HQDataFramer::HQDataFramer(HQClientTransaction *transaction, VIO *source) : _transaction(transaction), _source_vio(source) {}

HQFrameUPtr
HQDataFramer::generate_frame(uint16_t max_size)
{
  if (!this->_transaction->is_response_header_sent()) {
    return HQFrameFactory::create_null_frame();
  }

  HQFrameUPtr frame      = HQFrameFactory::create_null_frame();
  IOBufferReader *reader = this->_source_vio->get_reader();
  size_t len             = std::min(reader->read_avail(), static_cast<int64_t>(max_size));
  if (len) {
    frame = HQFrameFactory::create_data_frame(reinterpret_cast<uint8_t *>(reader->start()), len);
    reader->consume(len);
    this->_source_vio->ndone += len;
  }

  return frame;
}

bool
HQDataFramer::is_done() const
{
  return this->_source_vio->ntodo() == 0;
}
