/** @file

  Unit tests for FetchSM.

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

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

#include "iocore/eventsystem/IOBuffer.h"
#include "proxy/FetchSM.h"
#include "proxy/hdrs/HTTP.h"
#include "proxy/hdrs/MIME.h"
#include "proxy/hdrs/URL.h"

extern int                            cmd_disable_pfreelist;
extern ClassAllocator<FetchSM, false> FetchSMAllocator;

namespace
{
void
initialize_fetch_sm_once()
{
  static bool initialized = false;
  if (!initialized) {
    cmd_disable_pfreelist = true;
    init_buffer_allocators(0);
    url_init();
    mime_init();
    http_init();
    initialized = true;
  }
}
} // namespace

TEST_CASE("FetchSM copies response headers across IOBufferBlocks", "[FetchSM]")
{
  initialize_fetch_sm_once();

  constexpr int64_t block_size = BUFFER_SIZE_FOR_INDEX(BUFFER_SIZE_INDEX_128);

  // Make our X-Fill explicitly block_size in length to ensure we exceed
  // block_size to force the parsed response header to span multiple
  // IOBufferBlocks.
  std::string const response =
    "HTTP/1.1 200 OK\r\nX-Fill: " + std::string(static_cast<size_t>(block_size), 'a') + "\r\nContent-Length: 4\r\n\r\nbody";
  size_t const header_length = response.find("\r\n\r\n") + 4;

  MIOBuffer      *response_buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_128);
  IOBufferReader *reader          = response_buffer->alloc_reader();
  response_buffer->write(response.data(), response.size());

  // Verify our precondition: that we did in fact create a situation with more
  // than one IOBufferBlocks.
  REQUIRE(header_length > static_cast<size_t>(block_size));
  REQUIRE(reader->block_count() > 1);
  REQUIRE(reader->block_read_avail() < static_cast<int64_t>(header_length));

  // The heart of the test: verify that FetchSMAllocator can parse the multiple
  // blocks correctly.
  FetchSM *fetch_sm = FetchSMAllocator.alloc();
  fetch_sm->init_comm();
  fetch_sm->get_info_from_buffer(reader);

  int   copied_length = 0;
  char *copied        = fetch_sm->resp_get(&copied_length);

  REQUIRE(copied != nullptr);
  REQUIRE(copied_length == static_cast<int>(response.size()));
  CHECK(std::string_view(copied, copied_length) == response);

  fetch_sm->cleanUp();
  free_MIOBuffer(response_buffer);
}
