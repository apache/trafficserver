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

#include <sys/eventfd.h>
#include <atomic>
#include <cstring>
#include <stdexcept>

#include <unistd.h>

#include "I_IO_URING.h"
#include "tscore/ink_hrtime.h"

std::atomic<int> main_wq_fd;
std::atomic<uint64_t> io_uring_submissions = 0;
std::atomic<uint64_t> io_uring_completions = 0;

IOUringConfig IOUringContext::config;

void
IOUringContext::set_config(const IOUringConfig &cfg)
{
  config = cfg;
}

IOUringContext::IOUringContext()
{
  io_uring_params p{};

  if (config.attach_wq > 0) {
    int wq_fd = get_main_queue_fd();
    if (wq_fd > 0) {
      p.flags = IORING_SETUP_ATTACH_WQ;
      p.wq_fd = wq_fd;
    }
  }

  if (config.sq_poll_ms > 0) {
    p.flags          |= IORING_SETUP_SQPOLL;
    p.sq_thread_idle  = config.sq_poll_ms;
  }

  int ret = io_uring_queue_init_params(config.queue_entries, &ring, &p);
  if (ret < 0) {
    throw std::runtime_error(strerror(-ret));
  }

  /* no sharing for non-fixed either */
  if (config.sq_poll_ms && !(p.features & IORING_FEAT_SQPOLL_NONFIXED)) {
    throw std::runtime_error("No SQPOLL sharing with nonfixed");
  }

  // assign this handler to the thread
  // TODO(cmcfarlen): Assign in thread somewhere else
  // this_ethread()->diskHandler = this;
}

IOUringContext::~IOUringContext()
{
  if (evfd != -1) {
    ::close(evfd);
    evfd = -1;
  }
  io_uring_queue_exit(&ring);
}

void
IOUringContext::set_main_queue(IOUringContext *dh)
{
  dh->set_wq_max_workers(config.wq_bounded, config.wq_unbounded);
  main_wq_fd.store(dh->ring.ring_fd);
}

int
IOUringContext::get_main_queue_fd()
{
  return main_wq_fd.load();
}

int
IOUringContext::set_wq_max_workers(unsigned int bounded, unsigned int unbounded)
{
  if (bounded == 0 && unbounded == 0) {
    return 0;
  }
  unsigned int args[2] = {bounded, unbounded};
  int result           = io_uring_register_iowq_max_workers(&ring, args);
  return result;
}

std::pair<int, int>
IOUringContext::get_wq_max_workers()
{
  unsigned int args[2] = {0, 0};
  io_uring_register_iowq_max_workers(&ring, args);
  return std::make_pair(args[0], args[1]);
}

void
IOUringContext::submit()
{
  io_uring_submissions.fetch_add(io_uring_submit(&ring));
}

void
IOUringContext::handle_cqe(io_uring_cqe *cqe)
{
  auto *op = reinterpret_cast<IOUringCompletionHandler *>(io_uring_cqe_get_data(cqe));

  op->handle_complete(cqe);
}

void
IOUringContext::service()
{
  io_uring_cqe *cqe = nullptr;
  io_uring_peek_cqe(&ring, &cqe);
  while (cqe) {
    handle_cqe(cqe);
    io_uring_completions++;
    io_uring_cqe_seen(&ring, cqe);

    cqe = nullptr;
    io_uring_peek_cqe(&ring, &cqe);
  }
}

void
IOUringContext::submit_and_wait(ink_hrtime t)
{
  timespec ts               = ink_hrtime_to_timespec(t);
  __kernel_timespec timeout = {ts.tv_sec, ts.tv_nsec};
  io_uring_cqe *cqe         = nullptr;

  int count = io_uring_submit_and_wait_timeout(&ring, &cqe, 1, &timeout, nullptr);
  io_uring_submissions.fetch_add(count);
  while (cqe) {
    handle_cqe(cqe);
    io_uring_completions++;
    io_uring_cqe_seen(&ring, cqe);

    cqe = nullptr;
    io_uring_peek_cqe(&ring, &cqe);
  }
}

int
IOUringContext::register_eventfd()
{
  if (evfd == -1) {
    evfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);

    io_uring_register_eventfd(&ring, evfd);
  }
  return evfd;
}

IOUringContext *
IOUringContext::local_context()
{
  thread_local IOUringContext threadContext;

  return &threadContext;
}
