/** @file
 *
 *  OpenSSL socket BIO that does TCP Fast Open.
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "P_Net.h"
#include "I_SocketManager.h"
#include "ts/ink_assert.h"

#include "BIO_fastopen.h"

static int
fastopen_create(BIO *bio)
{
  bio->init  = 0;
  bio->num   = NO_FD;
  bio->flags = 0;
  bio->ptr   = NULL;

  return 1;
}

static int
fastopen_destroy(BIO *bio)
{
  if (bio) {
    // We expect this BIO to not own the socket, so we must always
    // be in NOCLOSE mode.
    ink_assert(bio->shutdown == BIO_NOCLOSE);
    fastopen_create(bio);
  }

  return 1;
}

static int
fastopen_bwrite(BIO *bio, const char *in, int insz)
{
  int64_t err;

  errno = 0;
  BIO_clear_retry_flags(bio);
  ink_assert(bio->num != NO_FD);

  if (bio->ptr) {
    // On the first write only, make a TFO request if TFO is enabled.
    // The best documentation on the behavior of the Linux API is in
    // RFC 7413. If we get EINPROGRESS it means that the SYN has been
    // sent without data and we should retry.
    const sockaddr *dst = reinterpret_cast<const sockaddr *>(bio->ptr);
    ProxyMutex *mutex   = this_ethread()->mutex.get();

    NET_INCREMENT_DYN_STAT(net_fastopen_attempts_stat);

    err = socketManager.sendto(bio->num, (void *)in, insz, MSG_FASTOPEN, dst, ats_ip_size(dst));
    if (err < 0) {
      NET_INCREMENT_DYN_STAT(net_fastopen_failures_stat);
    }

    bio->ptr = NULL;
  } else {
    err = socketManager.write(bio->num, (void *)in, insz);
  }

  if (err < 0) {
    errno = -err;
    if (BIO_sock_non_fatal_error(errno)) {
      BIO_set_retry_write(bio);
    }
  }

  return err < 0 ? -1 : err;
}

static int
fastopen_bread(BIO *bio, char *out, int outsz)
{
  int64_t err;

  errno = 0;
  BIO_clear_retry_flags(bio);
  ink_assert(bio->num != NO_FD);

  // TODO: If we haven't done the fastopen, ink_abort().

  err = socketManager.read(bio->num, out, outsz);
  if (err < 0) {
    errno = -err;
    if (BIO_sock_non_fatal_error(errno)) {
      BIO_set_retry_write(bio);
    }
  }

  return err < 0 ? -1 : err;
}

static long
fastopen_ctrl(BIO *bio, int cmd, long larg, void *ptr)
{
  switch (cmd) {
  case BIO_C_SET_FD:
    ink_assert(larg == BIO_CLOSE || larg == BIO_NOCLOSE);
    ink_assert(bio->num == NO_FD);

    bio->init     = 1;
    bio->shutdown = larg;
    bio->num      = *reinterpret_cast<int *>(ptr);
    return 0;

  case BIO_C_SET_CONNECT:
    // We only support BIO_set_conn_address(), which sets a sockaddr.
    ink_assert(larg == 2);
    bio->ptr = ptr;
    return 0;

  // We are unbuffered so unconditionally succeed on BIO_flush().
  case BIO_CTRL_FLUSH:
    return 1;

  case BIO_CTRL_PUSH:
  case BIO_CTRL_POP:
    return 0;

  default:
#if DEBUG
    ink_abort("unsupported BIO control cmd=%d larg=%ld ptr=%p", cmd, larg, ptr);
#endif

    return 0;
  }
}

static const BIO_METHOD fastopen_methods = {
  .type          = BIO_TYPE_SOCKET,
  .name          = "fastopen",
  .bwrite        = fastopen_bwrite,
  .bread         = fastopen_bread,
  .bputs         = NULL,
  .bgets         = NULL,
  .ctrl          = fastopen_ctrl,
  .create        = fastopen_create,
  .destroy       = fastopen_destroy,
  .callback_ctrl = NULL,
};

const BIO_METHOD *
BIO_s_fastopen()
{
  return &fastopen_methods;
}
