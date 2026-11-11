/** @file

   fuzzing proxy/http/ChunkedHandler

   @section license License

   Licensed to the Apache Software Foundation (ASF) under one or more contributor license agreements.
   See the NOTICE file distributed with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied. See the License for the specific language governing permissions and limitations under
   the License.
*/

#include "proxy/http/HttpTunnel.h"
#include "proxy/hdrs/HTTP.h"

#include "iocore/eventsystem/IOBuffer.h"
#include "records/RecordsConfig.h"
#include "tscore/Layout.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#define kMinInputLength 2
#define kMaxInputLength (64 * 1024)
#define TEST_THREADS    1

extern int cmd_disable_pfreelist;

namespace
{
bool
DoInitialization()
{
  Layout::create();
  RecProcessInit();
  LibRecordsConfigInit();

  ink_event_system_init(EVENT_SYSTEM_MODULE_PUBLIC_VERSION);
  eventProcessor.start(TEST_THREADS);
  static EThread *main_thread = new EThread;
  main_thread->set_specific();
  http_init();

  return true;
}

void
fuzz_chunked_content(const uint8_t *data, size_t size, size_t fragment_size, bool strict)
{
  MIOBuffer      *buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_4K);
  IOBufferReader *reader = buffer->alloc_reader();

  ChunkedHandler handler;
  handler.init_by_action(reader, ChunkedHandler::Action::DECHUNK, false, strict);
  handler.dechunked_reader = handler.dechunked_buffer->alloc_reader();
  handler.state            = ChunkedHandler::ChunkedState::READ_SIZE;

  for (size_t offset = 0; offset < size;) {
    size_t length = std::min(fragment_size, size - offset);
    buffer->write(data + offset, length);
    offset += length;

    if (handler.process_chunked_content().second) {
      break;
    }
  }

  handler.clear();
  free_MIOBuffer(buffer);
}
} // namespace

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t *input_data, size_t size_data)
{
  if (size_data < kMinInputLength || size_data > kMaxInputLength) {
    return 0;
  }

  cmd_disable_pfreelist                    = true;
  [[maybe_unused]] static bool initialized = DoInitialization();

  size_t fragment_size = static_cast<size_t>(1) << (input_data[0] & 7);
  fuzz_chunked_content(input_data + 1, size_data - 1, fragment_size, true);
  fuzz_chunked_content(input_data + 1, size_data - 1, fragment_size, false);

  return 0;
}
