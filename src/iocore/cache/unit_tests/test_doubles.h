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

#pragma once

#include "main.h"

#include "tscore/EventNotify.h"

#include <cstdint>
#include <cstring>
#include <stdexcept>

class FakeVC : public CacheVC
{
public:
  FakeVC()
  {
    this->buf    = new_IOBufferData(iobuffer_size_to_index(1024, MAX_BUFFER_SIZE_INDEX), MEMALIGNED);
    this->blocks = new_IOBufferBlock();
    this->blocks->set(this->buf.get());
    SET_HANDLER(&FakeVC::handle_call);
  }

  void
  set_test_data(char const *source, int len)
  {
    this->blocks->reset();
    if (len > 1024) {
      throw std::runtime_error{"data length exceeds internal buffer"};
    }
    std::memcpy(this->buf->data(), source, len);
    this->blocks->fill(len);
  }

  void
  set_agg_len(int agg_len)
  {
    this->agg_len = agg_len;
  }

  void
  set_header_len(int header_len)
  {
    this->header_len = header_len;
  }

  void
  set_write_len(int write_len)
  {
    this->write_len = write_len;
    this->total_len = static_cast<std::uint64_t>(write_len);
  }

  void
  set_readers(int readers)
  {
    this->f.readers = readers;
  }

  void
  mark_as_evacuator()
  {
    this->f.evacuator = true;
  }

  int
  handle_call(int /* event ATS_UNUSED */, void * /* e ATS_UNUSED */)
  {
    return EVENT_CONT;
  }
};

class WaitingVC final : public FakeVC
{
public:
  WaitingVC(StripeSM *stripe)
  {
    SET_HANDLER(&WaitingVC::handle_call);
    this->stripe = stripe;
    this->dir    = *stripe->dir;
  }

  void
  wait_for_callback()
  {
    this->_notifier.lock();
    while (!this->_got_callback) {
      this->_notifier.wait();
    }
    this->_notifier.unlock();
  }

  int
  handle_call(int /* event ATS_UNUSED */, void * /* e ATS_UNUSED */)
  {
    this->_got_callback = true;
    this->_notifier.signal();
    return EVENT_CONT;
  }

private:
  EventNotify _notifier;
  bool        _got_callback{false};
};
