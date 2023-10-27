/** @file

  Catch based unit tests for EventSystem

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
#define CATCH_CONFIG_MAIN
#include <atomic>
#include "catch.hpp"

#include "swoc/swoc_file.h"

#include "iocore/io_uring/IO_URING.h"

#include <functional>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "tscore/ink_hrtime.h"

#include "api/Metrics.h"
using ts::Metrics::Counter;

swoc::file::path
temp_prefix(const char *basename)
{
  char buffer[PATH_MAX];
  std::error_code err;
  auto tmpdir = swoc::file::temp_directory_path();
  snprintf(buffer, sizeof(buffer), "%s/%s.XXXXXX", tmpdir.c_str(), basename);
  auto prefix = swoc::file::path(mkdtemp(buffer));
  bool result = swoc::file::create_directories(prefix, err, 0755);
  if (!result) {
    throw std::runtime_error("Failed to create directory");
  }
  ink_assert(result);

  return prefix;
}

int
open_path(const swoc::file::path &path, int oflags = O_CREAT | O_RDWR, int mode = 0644)
{
  return open(path.c_str(), oflags, mode);
}

template <typename F> class FunctionHolderHandler : public IOUringCompletionHandler
{
public:
  FunctionHolderHandler(F &&f) : f(std::move(f)) {}

  void
  handle_complete(io_uring_cqe *c) override
  {
    f(c->res);
  }

private:
  F f;
};

IOUringCompletionHandler *
handle(const std::function<void(int)> &f)
{
  return new FunctionHolderHandler(std::move(f));
}

void
io_uring_write(IOUringContext &ur, int fd, const char *data, size_t len, const std::function<void(int)> &f)
{
  io_uring_sqe *s = ur.next_sqe(handle(f));

  io_uring_prep_write(s, fd, data, len, 0);
}
void
io_uring_read(IOUringContext &ur, int fd, char *data, size_t len, const std::function<void(int)> &f)
{
  io_uring_sqe *s = ur.next_sqe(handle(f));

  io_uring_prep_read(s, fd, data, len, 0);
}

void
io_uring_close(IOUringContext &ur, int fd, const std::function<void(int)> &f)
{
  io_uring_sqe *s = ur.next_sqe(handle(f));

  io_uring_prep_close(s, fd);
}

void
io_uring_accept(IOUringContext &ur, int sock, sockaddr *addr, socklen_t *addrlen, const std::function<void(int)> &f)
{
  io_uring_sqe *s = ur.next_sqe(handle(f));

  io_uring_prep_accept(s, sock, addr, addrlen, 0);
}

void
io_uring_connect(IOUringContext &ur, int sock, sockaddr *addr, socklen_t addrlen, const std::function<void(int)> &f)
{
  io_uring_sqe *s = ur.next_sqe(handle(f));

  io_uring_prep_connect(s, sock, addr, addrlen);
}

TEST_CASE("disk_io", "[io_uring]")
{
  IOUringConfig cfg = {
    .queue_entries = 32,
  };
  IOUringContext::set_config(cfg);
  IOUringContext ctx;

  auto tmp = temp_prefix("disk_io");

  REQUIRE(swoc::file::exists(tmp));

  auto apath = tmp / "a";

  int fd = open_path(apath);

  std::printf("%s\n", apath.c_str());

  REQUIRE(fd != -1);

  io_uring_write(ctx, fd, "hello", 5, [](int result) { REQUIRE(result == 5); });
  ctx.submit_and_wait(100 * HRTIME_MSECOND);
  io_uring_close(ctx, fd, [&fd](int result) {
    REQUIRE(result == 0);
    fd = -1;
  });

  ctx.submit_and_wait(100 * HRTIME_MSECOND);

  REQUIRE(fd == -1);

  fd             = open_path(apath, O_RDONLY);
  char buffer[6] = {0};
  io_uring_read(ctx, fd, buffer, sizeof(buffer), [&](int result) {
    using namespace std::literals;

    REQUIRE(result == 5);
    REQUIRE("hello"sv == std::string_view(buffer, result));
  });

  ctx.submit_and_wait(100 * HRTIME_MSECOND);
}

void
set_reuseport(int s)
{
  int optval = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
}

void
set_reuseaddr(int s)
{
  int optval = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
}

sockaddr_in
any_addr(int port)
{
  return {.sin_family = AF_INET, .sin_port = htons(port), .sin_addr = {.s_addr = htonl(INADDR_ANY)}, .sin_zero = {}};
}

sockaddr_in
make_addr(const std::string &ip, int port)
{
  sockaddr_in addr = {};

  ::inet_aton(ip.c_str(), &addr.sin_addr);
  addr.sin_port   = htons(port);
  addr.sin_family = AF_INET;

  return addr;
}

int
make_listen_socket(int port)
{
  int s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  set_reuseaddr(s);
  set_reuseport(s);

  const sockaddr_in addr = any_addr(port);
  int rc                 = ::bind(s, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr));
  if (rc == -1) {
    throw std::runtime_error("failed to bind");
  }
  rc = ::listen(s, 10000);
  if (rc == -1) {
    throw std::runtime_error("failed to listen");
  }

  return s;
}

int
make_client_socket()
{
  int s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  return s;
}

class SimpleTestServer
{
public:
  SimpleTestServer(int port) : s(make_listen_socket(port)), port(port), clients(0) {}

  void
  start(IOUringContext &ctx)
  {
    client_len = sizeof(client);
    io_uring_accept(ctx, s, reinterpret_cast<sockaddr *>(&client), &client_len, [&](int result) {
      REQUIRE(result > 0);
      clients++;
    });
  }

  int s;
  int port;
  int clients;
  sockaddr_in client;
  socklen_t client_len;
};

TEST_CASE("net_io", "[io_uring]")
{
  IOUringConfig cfg = {
    .queue_entries = 32,
  };
  IOUringContext::set_config(cfg);
  IOUringContext ctx;

  SimpleTestServer server(4321);

  server.start(ctx);

  auto client_addr = make_addr("127.0.0.1", 4321);
  int s            = make_client_socket();
  std::atomic<bool> connected{false};
  io_uring_connect(ctx, s, reinterpret_cast<sockaddr *>(&client_addr), sizeof(client_addr), [&](int result) {
    REQUIRE(result == 0);
    connected = true;
  });

  auto &m = Metrics::getInstance();

  Counter::AtomicType *completed = m.lookup(m.lookup("proxy.process.io_uring.completed"));

  uint64_t completions_before = Counter::read(completed);
  uint64_t needed             = 2;

  while ((Counter::read(completed) - completions_before) < needed) {
    ctx.submit_and_wait(1 * HRTIME_SECOND);
  }

  REQUIRE(server.clients == 1);
  REQUIRE(connected.load());
}
