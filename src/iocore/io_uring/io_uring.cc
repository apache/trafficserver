/** @file

Linux io_uring helper library

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

module;

#include "iocore/io_uring/IO_URING.h"

#include <iocore/net/EventIO.h>

#include <tscore/ink_hrtime.h>

#include <liburing.h>

#include <utility>

export module inkuring;

export struct IOUringConfig {
  int queue_entries = 32;
  int sq_poll_ms    = 0;
  int attach_wq     = 0;
  int wq_bounded    = 0;
  int wq_unbounded  = 0;
};

export class IOUringContext
{
public:
  IOUringContext();
  ~IOUringContext();

  IOUringContext(const IOUringContext &) = delete;

  io_uring_sqe *next_sqe(IOUringCompletionHandler *handler);

  bool supports_op(int op) const;

  int                 set_wq_max_workers(unsigned int bounded, unsigned int unbounded);
  std::pair<int, int> get_wq_max_workers();

  void submit();
  void service();
  void submit_and_wait(ink_hrtime ms);

  int register_eventfd();

  // assigns the global iouring config
  static void            set_config(const IOUringConfig &);
  static IOUringContext *local_context();
  static void            set_main_queue(IOUringContext *);
  static int             get_main_queue_fd();

  bool
  valid()
  {
    return ring.ring_fd > 0;
  }

private:
  io_uring        ring  = {};
  io_uring_probe *probe = nullptr;
  int             evfd  = -1;

  void                 handle_cqe(io_uring_cqe *);
  static IOUringConfig config;
};

export class IOUringEventIO : public EventIO
{
public:
  IOUringEventIO() : EventIO() {}
  int  start(EventLoop l, IOUringContext *h);
  void process_event(int flags) override;

private:
  IOUringContext *_h;
};

int
IOUringEventIO::start(EventLoop l, IOUringContext *h)
{
  _h     = h;
  int fd = _h->register_eventfd();
  return start_common(l, fd, EVENTIO_READ);
}

void
IOUringEventIO::process_event(int /* flags ATS_UNUSED */)
{
  _h->service();
}
