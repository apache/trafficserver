/** @file

  Catch based unit tests for QUICStream

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

#include "iocore/net/quic/QUICStream.h"
#include "iocore/net/quic/QUICStreamAdapter.h"

#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <limits>

namespace
{

// Hands out a single fixed-size block of data as one contiguous run, tracking how much
// of it remains -- just enough surface for QUICStream::send_data() to exercise its
// pending-block accounting across multiple calls.
class BudgetTestAdapter : public QUICStreamAdapter
{
public:
  BudgetTestAdapter(QUICStream &stream, size_t total_len) : QUICStreamAdapter(stream), _total_len(total_len), _remaining(total_len)
  {
  }

  int64_t
  write(QUICOffset, const uint8_t *, uint64_t, bool) override
  {
    return 0;
  }
  bool
  is_eos() override
  {
    return true;
  }
  uint64_t
  unread_len() override
  {
    return _remaining;
  }
  uint64_t
  read_len() override
  {
    return 0;
  }
  uint64_t
  total_len() override
  {
    return _total_len;
  }
  void
  encourge_read() override
  {
  }
  void
  encourge_write() override
  {
  }
  void
  notify_eos() override
  {
  }

protected:
  Ptr<IOBufferBlock>
  _read(size_t len) override
  {
    len                      = std::min(len, _remaining);
    Ptr<IOBufferBlock> block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
    block->alloc(iobuffer_size_to_index(std::max<size_t>(len, 1), BUFFER_SIZE_INDEX_128));
    block->fill(len);
    return block;
  }

  void
  _consume(size_t len) override
  {
    _remaining -= std::min(len, _remaining);
  }

private:
  size_t _total_len;
  size_t _remaining;
};

// Records what QUICStream::send_data() actually submits to the wire, and lets a test
// simulate connection-level flow control by capping how much of a write is "accepted".
class BudgetTestStreamIO : public QUICStreamIO
{
public:
  int64_t
  read_stream(QUICStreamId, uint8_t *, size_t, bool &, ErrorCode &) override
  {
    return 0;
  }
  bool
  stream_read_finished(QUICStreamId) override
  {
    return false;
  }
  int64_t
  stream_write_capacity(QUICStreamId) override
  {
    return write_capacity;
  }
  int64_t
  write_stream(QUICStreamId, uint8_t const *, size_t len, bool fin, ErrorCode &) override
  {
    last_requested_len = len;
    last_fin           = fin;
    size_t accepted    = std::min(len, accept_up_to);
    return static_cast<int64_t>(accepted);
  }

  int64_t write_capacity     = std::numeric_limits<int64_t>::max();
  size_t  accept_up_to       = std::numeric_limits<size_t>::max();
  size_t  last_requested_len = 0;
  bool    last_fin           = false;
};

} // namespace

TEST_CASE("QUICStream::send_data caps a carried-over pending block to the current event's budget")
{
  QUICStream         stream(nullptr, 0);
  BudgetTestAdapter  adapter(stream, 200 * 1024);
  BudgetTestStreamIO io;

  stream.set_io_adapter(&adapter);

  // Event 1: generous budget, but the connection only accepts part of the write --
  // leaves a pending block that was sized under this (large) budget.
  io.accept_up_to  = 100 * 1024;
  int64_t written1 = stream.send_data(io, QUICStream::MAX_CONNECTION_SEND_BYTES_PER_EVENT);
  REQUIRE(written1 == 100 * 1024);

  // Event 2: contention spikes and the budget drops to the floor. The connection no
  // longer constrains writes, so if send_data() submitted the whole carried-over
  // pending block (100KB) instead of capping to the new budget, this event would
  // blow past MIN_STREAM_SEND_BYTES_PER_EVENT.
  io.accept_up_to  = std::numeric_limits<size_t>::max();
  int64_t written2 = stream.send_data(io, QUICStream::MIN_STREAM_SEND_BYTES_PER_EVENT);

  CHECK(written2 == static_cast<int64_t>(QUICStream::MIN_STREAM_SEND_BYTES_PER_EVENT));
  CHECK(io.last_requested_len == QUICStream::MIN_STREAM_SEND_BYTES_PER_EVENT);
  CHECK(io.last_fin == false);
}

TEST_CASE("QUICStream::compute_fair_send_budget")
{
  SECTION("No contention (0 or 1 writable streams) returns the max budget")
  {
    CHECK(QUICStream::compute_fair_send_budget(0) == QUICStream::MAX_CONNECTION_SEND_BYTES_PER_EVENT);
    CHECK(QUICStream::compute_fair_send_budget(1) == QUICStream::MAX_CONNECTION_SEND_BYTES_PER_EVENT);
  }

  SECTION("Heavy contention clamps to the min budget")
  {
    CHECK(QUICStream::compute_fair_send_budget(100) == QUICStream::MIN_STREAM_SEND_BYTES_PER_EVENT);
  }

  SECTION("Mid-range contention divides the max budget evenly")
  {
    CHECK(QUICStream::compute_fair_send_budget(8) == QUICStream::MAX_CONNECTION_SEND_BYTES_PER_EVENT / 8);
  }

  SECTION("Floor-transition boundary")
  {
    // MAX / MIN is the exact stream count at which the division result equals the floor.
    const size_t boundary = QUICStream::MAX_CONNECTION_SEND_BYTES_PER_EVENT / QUICStream::MIN_STREAM_SEND_BYTES_PER_EVENT;

    CHECK(QUICStream::compute_fair_send_budget(boundary) == QUICStream::MIN_STREAM_SEND_BYTES_PER_EVENT);
    CHECK(QUICStream::compute_fair_send_budget(boundary + 1) == QUICStream::MIN_STREAM_SEND_BYTES_PER_EVENT);
  }
}
