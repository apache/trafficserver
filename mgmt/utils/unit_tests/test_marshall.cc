/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <iostream>
#include <string_view>
#include <cstdint>

#include <tscore/ink_defs.h>
#include <tscore/ink_thread.h>
#include <tscore/ink_inet.h>

#include <MgmtMarshall.h>
#include <MgmtSocket.h>

#include <catch.hpp>

namespace
{
bool
check_eq(char const *expr_as_str, MgmtMarshallInt rcvd, MgmtMarshallInt len)
{
  if (rcvd == len) {
    return true;
  }
  std::cout << expr_as_str << " returned length " << rcvd << ", expected " << len << '\n';
  return false;
}

#define CHECK_EQ(expr, len) CHECK(check_eq(#expr, static_cast<MgmtMarshallInt>(expr), static_cast<MgmtMarshallInt>(len)))

template <typename V_t, typename E_t>
bool
check_value(V_t value, E_t expect)
{
  if (sizeof(V_t) > sizeof(E_t) ? (value == static_cast<V_t>(expect)) : (static_cast<E_t>(value) == expect)) {
    return true;
  }
  std::cout << "received marshalled value " << value << ", expected " << expect << '\n';
  return false;
}

bool
check_value(void *value, void *expect)
{
  if (value == expect) {
    return true;
  }
  std::cout << std::hex << "received marshalled value " << reinterpret_cast<std::uintptr_t>(value) << ", expected "
            << reinterpret_cast<std::uintptr_t>(expect) << '\n'
            << std::dec;
  return false;
}

#define CHECK_VALUE(value, expect) CHECK(check_value((value), (expect)))

bool
check_str(char const *value, char const *expect)
{
  if (!value) {
    value = "";
  }
  if (std::string_view(value) == expect) {
    return true;
  }
  std::cout << "received marshalled value " << value << ", expected " << expect << '\n';
  return false;
}

#define CHECK_STR(value, expect) CHECK(check_str((value), (expect)))

const MgmtMarshallType inval[] = {static_cast<MgmtMarshallType>(1568)};

const MgmtMarshallType ifields[] = {MGMT_MARSHALL_INT, MGMT_MARSHALL_LONG};

const MgmtMarshallType sfields[] = {
  MGMT_MARSHALL_STRING,
};

const MgmtMarshallType dfields[] = {
  MGMT_MARSHALL_DATA,
};

const MgmtMarshallType afields[] = {
  MGMT_MARSHALL_DATA, MGMT_MARSHALL_INT, MGMT_MARSHALL_LONG, MGMT_MARSHALL_STRING, MGMT_MARSHALL_LONG, MGMT_MARSHALL_LONG,
};

const char alpha[]       = "abcdefghijklmnopqrstuvwxyz0123456789";
const char *stringvals[] = {nullptr, "", "randomstring"};

bool
errno_is_continue()
{
  return errno == EALREADY || errno == EWOULDBLOCK || errno == EINPROGRESS || errno == EAGAIN || mgmt_transient_error();
}

int
message_connect_channel(int listenfd, int clientfd, int serverport)
{
  //  bool need_connect = true;
  bool need_accept = true;
  int serverfd     = -1;

  struct sockaddr_in in;

  ink_zero(in);
  in.sin_family      = AF_INET;
  in.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  in.sin_port        = htons(serverport);

  fcntl(clientfd, F_SETFL, O_NONBLOCK);
  fcntl(listenfd, F_SETFL, O_NONBLOCK);

  connect(clientfd, reinterpret_cast<const struct sockaddr *>(&in), sizeof(in));

  while (need_accept) {
    serverfd = accept(listenfd, nullptr, nullptr);
    if (serverfd == -1) {
      std::cout << "accepting, " << errno << ' ' << strerror(errno) << '\n';
      if (!errno_is_continue()) {
        return -1;
      }
      ink_thr_yield();
    } else {
      need_accept = false;
    }
  }

  return serverfd;
}

int
message_listen(int &port)
{
  IpEndpoint sa;
  socklen_t slen;
  int fd;

  fd = mgmt_socket(AF_INET, SOCK_STREAM, 0);
  if (fd >= 0) {
    sa.setToAnyAddr(AF_INET);
    if (bind(fd, &sa.sa, sizeof(sa.sin)) >= 0) {
      slen = sizeof(sa);
      if (getsockname(fd, &sa.sa, &slen) >= -1) {
        port = ntohs(ats_ip_port_cast(&sa.sa));

        listen(fd, 5);

        return fd;
      }
    }
    close(fd);
  }
  return -1;
}

} // end anonymous namespace

TEST_CASE("MessageReadWriteA", "[mgmt_utils][msg_rd_wr_a]")
{
  int listenfd   = -1;
  int serverfd   = -1;
  int clientfd   = -1;
  int serverport = 0;

  MgmtMarshallInt mint       = 0;
  MgmtMarshallLong mlong     = 0;
  MgmtMarshallString mstring = nullptr;
  MgmtMarshallData mdata     = {nullptr, 0};

  clientfd = mgmt_socket(AF_INET, SOCK_STREAM, 0);
  listenfd = message_listen(serverport);
  serverfd = message_connect_channel(listenfd, clientfd, serverport);
  std::cout << "listenfd=" << listenfd << " clientfd=" << clientfd << " serverfd=" << serverfd << " port=" << serverport << '\n';

  fcntl(clientfd, F_SETFL, O_NDELAY);
  fcntl(serverfd, F_SETFL, O_NDELAY);

  mint  = 99;
  mlong = reinterpret_cast<MgmtMarshallLong>(&listenfd);

  // Check invalid Fd write. ToDo: Commented out, see TS-3052.
  // CHECK_EQ(mgmt_message_write(FD_SETSIZE - 1, ifields, countof(ifields), &mint, &mlong), -1);

  CHECK_EQ(mgmt_message_write(clientfd, ifields, countof(ifields), &mint, &mlong), 12);

  mint  = 0;
  mlong = 0;
  CHECK_EQ(mgmt_message_read(serverfd, ifields, countof(ifields), &mint, &mlong), 12);
  CHECK_VALUE(mint, 99);
  CHECK_VALUE(mlong, reinterpret_cast<MgmtMarshallLong>(&listenfd));

  // Marshall a string.
  for (unsigned i = 0; i < countof(stringvals); ++i) {
    const char *s   = stringvals[i];
    std::size_t len = 4 /* length */ + (s ? std::strlen(s) : 0) /* bytes */ + 1 /* NULL */;

    mstring = s ? ats_strdup(s) : nullptr;
    CHECK_EQ(mgmt_message_write(clientfd, sfields, countof(sfields), &mstring), len);
    ats_free(mstring);
    mstring = nullptr;

    CHECK_EQ(mgmt_message_read(serverfd, sfields, countof(sfields), &mstring), len);
    CHECK_STR(s, mstring);
    ats_free(mstring);
    mstring = nullptr;
  }

  // Marshall data.
  mdata.ptr = ats_strdup(alpha);
  mdata.len = std::strlen(alpha);
  CHECK_EQ(mgmt_message_write(clientfd, dfields, countof(dfields), &mdata), 4 + std::strlen(alpha));
  ats_free(mdata.ptr);
  ink_zero(mdata);

  CHECK_EQ(mgmt_message_read(serverfd, dfields, countof(dfields), &mdata), 4 + std::strlen(alpha));
  CHECK_VALUE(mdata.len, std::strlen(alpha));
  if (std::memcmp(mdata.ptr, alpha, std::strlen(alpha)) != 0) {
    std::cout << "unexpected mdata contents\n";
    CHECK(false);
  }
  ats_free(mdata.ptr);
  ink_zero(mdata);

  close(clientfd);
  close(listenfd);
  close(serverfd);
}

TEST_CASE("MessageMarshall", "[mgmt_utils][msg_marshall]")
{
  char msgbuf[4096];

  MgmtMarshallInt mint       = 0;
  MgmtMarshallLong mlong     = 0;
  MgmtMarshallString mstring = nullptr;
  MgmtMarshallData mdata     = {nullptr, 0};

  // Parse empty message.
  CHECK_EQ(mgmt_message_parse(nullptr, 0, nullptr, 0), 0);

  // Marshall empty message.
  CHECK_EQ(mgmt_message_marshall(nullptr, 0, nullptr, 0), 0);

  // Marshall some integral types.
  mint  = -156;
  mlong = UINT32_MAX;
  CHECK_EQ(mgmt_message_marshall(msgbuf, 1, ifields, countof(ifields), &mint, &mlong), -1);
  CHECK_EQ(mgmt_message_marshall(msgbuf, sizeof(msgbuf), ifields, countof(ifields), &mint, &mlong), 12);
  CHECK_EQ(mgmt_message_parse(msgbuf, 1, ifields, countof(ifields), &mint, &mlong), -1);
  CHECK_EQ(mgmt_message_parse(msgbuf, sizeof(msgbuf), ifields, countof(ifields), &mint, &mlong), 12);
  CHECK_VALUE(mint, -156);
  CHECK_VALUE(mlong, static_cast<MgmtMarshallLong>(UINT32_MAX));

  // Marshall a string.
  for (unsigned i = 0; i < countof(stringvals); ++i) {
    const char *s = stringvals[i];
    size_t len    = 4 /* length */ + (s ? std::strlen(s) : 0) /* bytes */ + 1 /* NULL */;

    mstring = s ? ats_strdup(s) : nullptr;
    CHECK_EQ(mgmt_message_marshall(msgbuf, 1, sfields, countof(sfields), &mstring), -1);
    CHECK_EQ(mgmt_message_marshall(msgbuf, sizeof(msgbuf), sfields, countof(sfields), &mstring), len);
    ats_free(mstring);
    mstring = nullptr;

    CHECK_EQ(mgmt_message_parse(msgbuf, 1, sfields, countof(sfields), &mstring), -1);
    CHECK_EQ(mgmt_message_parse(msgbuf, sizeof(msgbuf), sfields, countof(sfields), &mstring), len);
    CHECK_STR(s, mstring);
    ats_free(mstring);
    mstring = nullptr;
  }

  // Marshall data.
  mdata.ptr = ats_strdup(alpha);
  mdata.len = std::strlen(alpha);
  CHECK_EQ(mgmt_message_marshall(msgbuf, 10, dfields, countof(dfields), &mdata), -1);
  CHECK_EQ(mgmt_message_marshall(msgbuf, sizeof(msgbuf), dfields, countof(dfields), &mdata), 4 + std::strlen(alpha));
  ats_free(mdata.ptr);
  ink_zero(mdata);

  CHECK_EQ(mgmt_message_parse(msgbuf, std::strlen(alpha), dfields, countof(dfields), &mdata), -1);
  CHECK_EQ(mgmt_message_parse(msgbuf, std::strlen(alpha) + 4, dfields, countof(dfields), &mdata), 4 + std::strlen(alpha));
  CHECK_VALUE(mdata.len, std::strlen(alpha));
  if (std::memcmp(mdata.ptr, alpha, std::strlen(alpha)) != 0) {
    std::cout << "unexpected mdata contents\n";
    CHECK(false);
  }
  ats_free(mdata.ptr);
  ink_zero(mdata);

  // Marshall empty data.
  CHECK_EQ(mgmt_message_marshall(msgbuf, sizeof(msgbuf), dfields, countof(dfields), &mdata), 4);

  mdata.ptr = reinterpret_cast<void *>(99);
  mdata.len = 1000;
  CHECK_EQ(mgmt_message_parse(msgbuf, sizeof(msgbuf), dfields, countof(dfields), &mdata), 4);
  CHECK_VALUE(mdata.ptr, static_cast<void *>(nullptr));
  CHECK_VALUE(mdata.len, 0);
}

TEST_CASE("MessageLemgth", "[mgmt_utils][msg_len]")
{
  MgmtMarshallInt mint       = 0;
  MgmtMarshallLong mlong     = 0;
  MgmtMarshallString mstring = nullptr;
  MgmtMarshallData mdata     = {nullptr, 0};

  // Check invalid marshall type.
  CHECK_EQ(mgmt_message_length(inval, countof(inval), NULL), -1);

  // Check empty types array.
  CHECK_EQ(mgmt_message_length(nullptr, 0), 0);

  CHECK_EQ(mgmt_message_length(ifields, countof(ifields), &mint, &mlong), 12);

  // string messages include a 4-byte length and the NULL
  mstring = const_cast<char *>("foo");
  CHECK_EQ(mgmt_message_length(sfields, countof(sfields), &mstring), sizeof("foo") + 4);

  // NULL strings are the same as empty strings ...
  mstring = nullptr;
  CHECK_EQ(mgmt_message_length(sfields, countof(sfields), &mstring), 4 + 1);
  mstring = const_cast<char *>("");
  CHECK_EQ(mgmt_message_length(sfields, countof(sfields), &mstring), 4 + 1);

  // data fields include a 4-byte length. We don't go looking at the data in this case.
  mdata.len = 99;
  mdata.ptr = nullptr;
  CHECK_EQ(mgmt_message_length(dfields, countof(dfields), &mdata), 99 + 4);

  mstring   = (char *)"all fields";
  mdata.len = 31;
  CHECK_EQ(mgmt_message_length(afields, countof(afields), &mdata, &mint, &mlong, &mstring, &mlong, &mlong),
           31 + 4 + 4 + 8 + sizeof("all fields") + 4 + 8 + 8);

  mdata.ptr = nullptr;
  mdata.len = 0;
  CHECK_EQ(mgmt_message_length(dfields, countof(dfields), &mdata), 4);
}
