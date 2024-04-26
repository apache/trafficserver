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

#include "proxy/http3/Http3StreamDataVIOAdaptor.h"
#include "iocore/eventsystem/VIO.h"

Http3StreamDataVIOAdaptor::Http3StreamDataVIOAdaptor(VIO *sink) : _sink_vio(sink), _buffer(new_MIOBuffer(BUFFER_SIZE_INDEX_4K)) {}

Http3StreamDataVIOAdaptor::~Http3StreamDataVIOAdaptor()
{
  free_MIOBuffer(this->_buffer);
}

std::vector<Http3FrameType>
Http3StreamDataVIOAdaptor::interests()
{
  return {Http3FrameType::DATA};
}

Http3ErrorUPtr
Http3StreamDataVIOAdaptor::handle_frame(std::shared_ptr<const Http3Frame> frame, int32_t /* frame_seq */,
                                        Http3StreamType /* s_type */)
{
  ink_assert(frame->type() == Http3FrameType::DATA);
  const Http3DataFrame *dframe = dynamic_cast<const Http3DataFrame *>(frame.get());

  // Need to wait for headers to be written
  this->_buffer->write(dframe->payload(), dframe->payload_length());
  this->_total_data_length += dframe->payload_length();

  return Http3ErrorUPtr(nullptr);
}

void
Http3StreamDataVIOAdaptor::finalize()
{
  SCOPED_MUTEX_LOCK(lock, this->_sink_vio->mutex, this_ethread());
  MIOBuffer      *writer = this->_sink_vio->get_writer();
  IOBufferReader *reader = this->_buffer->alloc_reader();
  IOBufferBlock  *block;
  while (reader->read_avail() > 0 && (block = reader->get_current_block()) != nullptr) {
    writer->append_block(block);
    reader->consume(block->size());
  }

  this->_buffer->dealloc_reader(reader);
  this->_sink_vio->nbytes = this->_total_data_length;
}

bool
Http3StreamDataVIOAdaptor::has_data()
{
  return this->_total_data_length > 0;
}
