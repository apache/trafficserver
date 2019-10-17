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

#include "Http3FrameCollector.h"

#include "Http3DebugNames.h"

Http3ErrorUPtr
Http3FrameCollector::on_write_ready(QUICStreamIO *stream_io, size_t &nwritten)
{
  bool all_done = true;
  uint8_t tmp[32768];
  nwritten = 0;

  for (auto g : this->_generators) {
    if (g->is_done()) {
      continue;
    }
    size_t len           = 0;
    Http3FrameUPtr frame = g->generate_frame(sizeof(tmp) - nwritten);
    if (frame) {
      frame->store(tmp + nwritten, &len);
      nwritten += len;

      Debug("http3", "[TX] [%d] | %s size=%zu", stream_io->stream_id(), Http3DebugNames::frame_type(frame->type()), len);
    }

    all_done &= g->is_done();
  }

  if (nwritten) {
    int64_t len = stream_io->write(tmp, nwritten);
    ink_assert(len > 0 && (uint64_t)len == nwritten);
  }

  if (all_done) {
    stream_io->write_done();
  }

  return Http3ErrorUPtr(new Http3NoError());
}

void
Http3FrameCollector::add_generator(Http3FrameGenerator *generator)
{
  this->_generators.push_back(generator);
}
