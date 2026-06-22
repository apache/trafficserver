/** @file

  Micro-benchmarks for IOBufferReader copy and MIOBuffer
  write-from-reader paths.

  Establishes the SC-007 baseline for the inkevent design cleanup
  (feature 007). The IOBuffer.h three-way split (US2) must show ±2%
  against this benchmark since reader/writer paths must continue
  inlining identically after the header partition.

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

#define CATCH_CONFIG_ENABLE_BENCHMARKING

#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <catch2/interfaces/catch_interfaces_config.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include <cstdint>
#include <cstring>

#include "tscore/Layout.h"

#include "iocore/eventsystem/EventSystem.h"
#include "records/RecordsConfig.h"

#include "iocore/utils/diags.i"

#define TEST_THREADS 1

namespace
{
constexpr int64_t PAYLOAD_BYTES = 32 * 1024;
} // namespace

TEST_CASE("IOBuffer copy paths", "[iocore][bench][iobuffer]")
{
  // Pre-fill a source MIOBuffer with PAYLOAD_BYTES of data; the
  // benchmarks below copy it through the reader/writer surface.
  uint8_t buf[PAYLOAD_BYTES];
  std::memset(buf, 0xCD, sizeof(buf));

  BENCHMARK("MIOBuffer::write(buf, n)")
  {
    MIOBuffer      *dst    = new_MIOBuffer(BUFFER_SIZE_INDEX_4K);
    IOBufferReader *reader = dst->alloc_reader();
    int64_t         total  = 0;
    while (total < PAYLOAD_BYTES) {
      total += dst->write(buf + total, PAYLOAD_BYTES - total);
    }
    int64_t avail = reader->read_avail();
    free_MIOBuffer(dst);
    return avail;
  };

  BENCHMARK("MIOBuffer::write(reader, n) — read-side copy")
  {
    MIOBuffer      *src    = new_MIOBuffer(BUFFER_SIZE_INDEX_4K);
    IOBufferReader *sr     = src->alloc_reader();
    int64_t         filled = 0;
    while (filled < PAYLOAD_BYTES) {
      filled += src->write(buf + filled, PAYLOAD_BYTES - filled);
    }

    MIOBuffer      *dst    = new_MIOBuffer(BUFFER_SIZE_INDEX_4K);
    IOBufferReader *dr     = dst->alloc_reader();
    int64_t         copied = dst->write(sr, PAYLOAD_BYTES);
    int64_t         avail  = dr->read_avail();

    free_MIOBuffer(dst);
    free_MIOBuffer(src);
    return copied + avail;
  };

  BENCHMARK("IOBufferReader::memcpy")
  {
    MIOBuffer      *src    = new_MIOBuffer(BUFFER_SIZE_INDEX_4K);
    IOBufferReader *reader = src->alloc_reader();
    int64_t         filled = 0;
    while (filled < PAYLOAD_BYTES) {
      filled += src->write(buf + filled, PAYLOAD_BYTES - filled);
    }

    uint8_t out[PAYLOAD_BYTES];
    char   *end = reader->memcpy(out, PAYLOAD_BYTES);

    free_MIOBuffer(src);
    return end - reinterpret_cast<char *>(out);
  };
}

struct EventProcessorListener : Catch::EventListenerBase {
  using EventListenerBase::EventListenerBase;

  void
  testRunStarting(Catch::TestRunInfo const & /* testRunInfo ATS_UNUSED */) override
  {
    Layout::create();
    init_diags("", nullptr);
    RecProcessInit();

    LibRecordsConfigInit();

    ink_event_system_init(EVENT_SYSTEM_MODULE_PUBLIC_VERSION);
    eventProcessor.start(TEST_THREADS);

    EThread *main_thread = new EThread;
    main_thread->set_specific();
  }
};

CATCH_REGISTER_LISTENER(EventProcessorListener);
