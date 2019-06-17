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

#include "tscore/ink_defs.h"
#include "tscore/ink_thread.h"
#include "tscore/ink_inet.h"

#include "tscore/TestBox.h"
#include <MgmtMarshall.h>
#include <MgmtSocket.h>

#define CHECK_EQ(expr, len)                                                                                 \
  do {                                                                                                      \
    MgmtMarshallInt rcvd = static_cast<MgmtMarshallInt>(expr);                                              \
    box.check(rcvd == static_cast<MgmtMarshallInt>(len), "%s returned length %d, expected %d", #expr, rcvd, \
              static_cast<MgmtMarshallInt>(len));                                                           \
  } while (0)

#define CHECK_VALUE(value, expect, fmt)                                                                       \
  do {                                                                                                        \
    box.check((value) == (expect), "received marshalled value " fmt ", expected " fmt "", (value), (expect)); \
  } while (0)

// The NULL string is marshalled the same as the empty string.
#define CHECK_STRING(value, expect)                                                                                  \
  do {                                                                                                               \
    if (value) {                                                                                                     \
      box.check(strcmp((value), (expect)) == 0, "received marshalled value '%s', expected '%s'", (value), (expect)); \
    } else {                                                                                                         \
      box.check(strcmp((expect), "") == 0, "received marshalled value '%s', expected ''", (expect));                 \
    }                                                                                                                \
  } while (0)

const MgmtMarshallType inval[] = {(MgmtMarshallType)1568};

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

static bool
errno_is_continue()
{
  return errno == EALREADY || errno == EWOULDBLOCK || errno == EINPROGRESS || errno == EAGAIN || mgmt_transient_error();
}

static int
message_connect_channel(RegressionTest *t, int listenfd, int clientfd, int serverport)
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

  connect(clientfd, (const struct sockaddr *)&in, sizeof(in));

  while (need_accept) {
    serverfd = accept(listenfd, nullptr, nullptr);
    if (serverfd == -1) {
      rprintf(t, "accepting, %d %s\n", errno, strerror(errno));
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

static int
message_listen(int &port)
{
  IpEndpoint sa;
  socklen_t slen;
  int fd;

  fd = mgmt_socket(AF_INET, SOCK_STREAM, 0);
  if (fd == -1) {
    goto fail;
  }

  sa.setToAnyAddr(AF_INET);
  if (bind(fd, &sa.sa, sizeof(sa.sin)) == -1) {
    goto fail;
  }

  slen = sizeof(sa);
  if (getsockname(fd, &sa.sa, &slen) == -1) {
    goto fail;
  }

  port = ntohs(ats_ip_port_cast(&sa.sa));

  listen(fd, 5);
  return fd;

fail:
  close(fd);
  return -1;
}

REGRESSION_TEST(MessageReadWriteA)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus, REGRESSION_TEST_PASSED);

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
  serverfd = message_connect_channel(t, listenfd, clientfd, serverport);
  rprintf(t, "listenfd=%d clientfd=%d, serverfd=%d, port=%d\n", listenfd, clientfd, serverfd, serverport);

  fcntl(clientfd, F_SETFL, O_NDELAY);
  fcntl(serverfd, F_SETFL, O_NDELAY);

  mint  = 99;
  mlong = (MgmtMarshallLong)(&listenfd);

  // Check invalid Fd write. ToDo: Commented out, see TS-3052.
  // CHECK_EQ(mgmt_message_write(FD_SETSIZE - 1, ifields, countof(ifields), &mint, &mlong), -1);

  CHECK_EQ(mgmt_message_write(clientfd, ifields, countof(ifields), &mint, &mlong), 12);

  mint  = 0;
  mlong = 0;
  CHECK_EQ(mgmt_message_read(serverfd, ifields, countof(ifields), &mint, &mlong), 12);
  CHECK_VALUE(mint, 99, "%" PRId32);
  CHECK_VALUE(mlong, (MgmtMarshallLong)(&listenfd), "%" PRId64);

  // Marshall a string.
  for (unsigned i = 0; i < countof(stringvals); ++i) {
    const char *s = stringvals[i];
    size_t len    = 4 /* length */ + (s ? strlen(s) : 0) /* bytes */ + 1 /* NULL */;

    mstring = s ? ats_strdup(s) : nullptr;
    CHECK_EQ(mgmt_message_write(clientfd, sfields, countof(sfields), &mstring), len);
    ats_free(mstring);
    mstring = nullptr;

    CHECK_EQ(mgmt_message_read(serverfd, sfields, countof(sfields), &mstring), len);
    CHECK_STRING(s, mstring);
    ats_free(mstring);
    mstring = nullptr;
  }

  // Marshall data.
  mdata.ptr = ats_strdup(alpha);
  mdata.len = strlen(alpha);
  CHECK_EQ(mgmt_message_write(clientfd, dfields, countof(dfields), &mdata), 4 + strlen(alpha));
  ats_free(mdata.ptr);
  ink_zero(mdata);

  CHECK_EQ(mgmt_message_read(serverfd, dfields, countof(dfields), &mdata), 4 + strlen(alpha));
  CHECK_VALUE(mdata.len, strlen(alpha), "%zu");
  box.check(memcmp(mdata.ptr, alpha, strlen(alpha)) == 0, "unexpected mdata contents");
  ats_free(mdata.ptr);
  ink_zero(mdata);

  close(clientfd);
  close(listenfd);
  close(serverfd);
}

REGRESSION_TEST(MessageMarshall)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus, REGRESSION_TEST_PASSED);

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
  CHECK_VALUE(mint, -156, "%" PRId32);
  CHECK_VALUE(mlong, static_cast<MgmtMarshallLong>(UINT32_MAX), "%" PRId64);

  // Marshall a string.
  for (unsigned i = 0; i < countof(stringvals); ++i) {
    const char *s = stringvals[i];
    size_t len    = 4 /* length */ + (s ? strlen(s) : 0) /* bytes */ + 1 /* NULL */;

    mstring = s ? ats_strdup(s) : nullptr;
    CHECK_EQ(mgmt_message_marshall(msgbuf, 1, sfields, countof(sfields), &mstring), -1);
    CHECK_EQ(mgmt_message_marshall(msgbuf, sizeof(msgbuf), sfields, countof(sfields), &mstring), len);
    ats_free(mstring);
    mstring = nullptr;

    CHECK_EQ(mgmt_message_parse(msgbuf, 1, sfields, countof(sfields), &mstring), -1);
    CHECK_EQ(mgmt_message_parse(msgbuf, sizeof(msgbuf), sfields, countof(sfields), &mstring), len);
    CHECK_STRING(s, mstring);
    ats_free(mstring);
    mstring = nullptr;
  }

  // Marshall data.
  mdata.ptr = ats_strdup(alpha);
  mdata.len = strlen(alpha);
  CHECK_EQ(mgmt_message_marshall(msgbuf, 10, dfields, countof(dfields), &mdata), -1);
  CHECK_EQ(mgmt_message_marshall(msgbuf, sizeof(msgbuf), dfields, countof(dfields), &mdata), 4 + strlen(alpha));
  ats_free(mdata.ptr);
  ink_zero(mdata);

  CHECK_EQ(mgmt_message_parse(msgbuf, strlen(alpha), dfields, countof(dfields), &mdata), -1);
  CHECK_EQ(mgmt_message_parse(msgbuf, strlen(alpha) + 4, dfields, countof(dfields), &mdata), 4 + strlen(alpha));
  CHECK_VALUE(mdata.len, strlen(alpha), "%zu");
  box.check(memcmp(mdata.ptr, alpha, strlen(alpha)) == 0, "unexpected mdata contents");
  ats_free(mdata.ptr);
  ink_zero(mdata);

  // Marshall empty data.
  CHECK_EQ(mgmt_message_marshall(msgbuf, sizeof(msgbuf), dfields, countof(dfields), &mdata), 4);

  mdata.ptr = (void *)99;
  mdata.len = 1000;
  CHECK_EQ(mgmt_message_parse(msgbuf, sizeof(msgbuf), dfields, countof(dfields), &mdata), 4);
  CHECK_VALUE(mdata.ptr, (void *)nullptr, "%p");
  CHECK_VALUE(mdata.len, (size_t)0, "%zu");
}

REGRESSION_TEST(MessageLength)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus, REGRESSION_TEST_PASSED);

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
  mstring = (char *)"foo";
  CHECK_EQ(mgmt_message_length(sfields, countof(sfields), &mstring), sizeof("foo") + 4);

  // NULL strings are the same as empty strings ...
  mstring = nullptr;
  CHECK_EQ(mgmt_message_length(sfields, countof(sfields), &mstring), 4 + 1);
  mstring = (char *)"";
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

int
main(int argc, const char **argv)
{
  return RegressionTest::main(argc, argv, REGRESSION_TEST_QUICK);
}
