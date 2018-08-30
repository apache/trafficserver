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
#include "tscore/ink_assert.h"

#include "BIO_fastopen.h"

// For BoringSSL, which for some reason doesn't have this function.
// (In BoringSSL, sock_read() and sock_write() use the internal
// bio_fd_non_fatal_error() instead.) #1437
//
// The following is copied from
// https://github.com/openssl/openssl/blob/3befffa39dbaf2688d823fcf2bdfc07d2487be48/crypto/bio/bss_sock.c
// Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
#ifndef HAVE_BIO_SOCK_NON_FATAL_ERROR
int
BIO_sock_non_fatal_error(int err)
{
  switch (err) {
#if defined(OPENSSL_SYS_WINDOWS)
#if defined(WSAEWOULDBLOCK)
  case WSAEWOULDBLOCK:
#endif
#endif

#ifdef EWOULDBLOCK
#ifdef WSAEWOULDBLOCK
#if WSAEWOULDBLOCK != EWOULDBLOCK
  case EWOULDBLOCK:
#endif
#else
  case EWOULDBLOCK:
#endif
#endif

#if defined(ENOTCONN)
  case ENOTCONN:
#endif

#ifdef EINTR
  case EINTR:
#endif

#ifdef EAGAIN
#if EWOULDBLOCK != EAGAIN
  case EAGAIN:
#endif
#endif

#ifdef EPROTO
  case EPROTO:
#endif

#ifdef EINPROGRESS
  case EINPROGRESS:
#endif

#ifdef EALREADY
  case EALREADY:
#endif
    return (1);
  /* break; */
  default:
    break;
  }
  return (0);
}
#endif

static int (*fastopen_create)(BIO *) = BIO_meth_get_create(const_cast<BIO_METHOD *>(BIO_s_socket()));

static int
fastopen_destroy(BIO *bio)
{
  if (bio) {
    // We expect this BIO to not own the socket, so we must always
    // be in NOCLOSE mode.
    ink_assert(BIO_get_shutdown(bio) == BIO_NOCLOSE);
  }

  return BIO_meth_get_destroy(const_cast<BIO_METHOD *>(BIO_s_socket()))(bio);
}

static int
fastopen_bwrite(BIO *bio, const char *in, int insz)
{
  int64_t err;

  errno = 0;
  BIO_clear_retry_flags(bio);
  int fd = BIO_get_fd(bio, nullptr);
  ink_assert(fd != NO_FD);

  if (BIO_get_data(bio)) {
    // On the first write only, make a TFO request if TFO is enabled.
    // The best documentation on the behavior of the Linux API is in
    // RFC 7413. If we get EINPROGRESS it means that the SYN has been
    // sent without data and we should retry.
    const sockaddr *dst = reinterpret_cast<const sockaddr *>(BIO_get_data(bio));
    ProxyMutex *mutex   = this_ethread()->mutex.get();

    NET_INCREMENT_DYN_STAT(net_fastopen_attempts_stat);

    err = socketManager.sendto(fd, (void *)in, insz, MSG_FASTOPEN, dst, ats_ip_size(dst));
    if (err >= 0) {
      NET_INCREMENT_DYN_STAT(net_fastopen_successes_stat);
    }

    BIO_set_data(bio, nullptr);
  } else {
    err = socketManager.write(fd, (void *)in, insz);
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
  int fd = BIO_get_fd(bio, nullptr);
  ink_assert(fd != NO_FD);

  // TODO: If we haven't done the fastopen, ink_abort().

  err = socketManager.read(fd, out, outsz);
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
  case BIO_C_SET_CONNECT:
    // We only support BIO_set_conn_address(), which sets a sockaddr.
    ink_assert(larg == 2);
    BIO_set_data(bio, ptr);
    return 0;
  }

  return BIO_meth_get_ctrl(const_cast<BIO_METHOD *>(BIO_s_socket()))(bio, cmd, larg, ptr);
}

#ifndef HAVE_BIO_METH_NEW
static const BIO_METHOD fastopen_methods[] = {{
  .type          = BIO_TYPE_SOCKET,
  .name          = "fastopen",
  .bwrite        = fastopen_bwrite,
  .bread         = fastopen_bread,
  .bputs         = nullptr,
  .bgets         = nullptr,
  .ctrl          = fastopen_ctrl,
  .create        = fastopen_create,
  .destroy       = fastopen_destroy,
  .callback_ctrl = nullptr,
}};
#else
static const BIO_METHOD *fastopen_methods = [] {
  BIO_METHOD *fastopen_methods = BIO_meth_new(BIO_TYPE_SOCKET, "fastopen");
  BIO_meth_set_write(fastopen_methods, fastopen_bwrite);
  BIO_meth_set_read(fastopen_methods, fastopen_bread);
  BIO_meth_set_ctrl(fastopen_methods, fastopen_ctrl);
  BIO_meth_set_create(fastopen_methods, fastopen_create);
  BIO_meth_set_destroy(fastopen_methods, fastopen_destroy);
  return fastopen_methods;
}();
#endif

const BIO_METHOD *
BIO_s_fastopen()
{
  return fastopen_methods;
}
