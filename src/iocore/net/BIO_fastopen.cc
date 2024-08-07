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

#include <openssl/opensslv.h>

#include "P_Net.h"

#include "iocore/eventsystem/UnixSocket.h"

#include "tscore/ink_assert.h"
#include "tscore/ink_config.h"

#include "BIO_fastopen.h"

namespace
{

#if defined(HAVE_BIO_GET_EX_NEW_INDEX) && defined(HAVE_BIO_GET_EX_DATA) && defined(HAVE_BIO_SET_EX_DATA)

class ExData
{
public:
  // Pseudo-namespace
  ExData() = delete;

  static int
  idx()
  {
    static int idx_ = []() -> int {
      int i = BIO_get_ex_new_index(0, nullptr, _new, _dup, _free);
      ink_release_assert(i >= 0);
      return i;
    }();
    return idx_;
  }

private:
#if HAVE_CRYPTO_EX_UNUSED
  static constexpr CRYPTO_EX_unused *_new{nullptr};
#else
  static void
  _new(void * /* parent */, void * /* ptr */, CRYPTO_EX_DATA *ad, int idx_, long /* argl */, void * /* argp */)
  {
    ink_release_assert(CRYPTO_set_ex_data(ad, idx_, nullptr) == 1);
  }
#endif

#if !HAVE_CRYPTO_SET_EX_DATA
  static void
  _free(void * /* parent */, void * /* ptr */, CRYPTO_EX_DATA * /* ad */, int /* idx_ */, long /* argl */, void * /* argp */)
  {
  }
#else
  static void
  _free(void * /* parent */, void * /* ptr */, CRYPTO_EX_DATA *ad, int idx_, long /* argl */, void * /* argp */)
  {
    ink_release_assert(CRYPTO_set_ex_data(ad, idx_, nullptr) == 1);
  }
#endif

#if HAVE_CRYPTO_EX_DUP_TYPE1
  using _Type_from_d = void **;
#else
  using _Type_from_d = void *;
#endif

  static int
  _dup(CRYPTO_EX_DATA * /* to */, const CRYPTO_EX_DATA * /* from */, _Type_from_d /* from_d */, int /* idx */, long /* argl */,
       void * /* argp */)
  {
    ink_assert(false);
    return 0;
  }
};

inline void
set_dest_addr_for_bio(BIO *b, void *dest_addr)
{
  ink_assert(BIO_set_ex_data(b, ExData::idx(), dest_addr) == 1);
}

inline void *
get_dest_addr_for_bio(BIO *b)
{
  return BIO_get_ex_data(b, ExData::idx());
}

#else // no BIO ex data in SSL library

// Fall back on the krufty way this was done using older SSL libraries.

inline void
set_dest_addr_for_bio(BIO *b, void *dest_addr)
{
  BIO_set_data(b, dest_addr);
}

inline void *
get_dest_addr_for_bio(BIO *b)
{
  return BIO_get_data(b);
}

#endif

} // end anonymous namespace

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
  int        fd = BIO_get_fd(bio, nullptr);
  UnixSocket sock{fd};
  ink_assert(sock.is_ok());

  void *dst_void = get_dest_addr_for_bio(bio);
  if (dst_void) {
    auto dst = static_cast<sockaddr *>(dst_void);
    // On the first write only, make a TFO request if TFO is enabled.
    // The best documentation on the behavior of the Linux API is in
    // RFC 7413. If we get EINPROGRESS it means that the SYN has been
    // sent without data and we should retry.
    Metrics::Counter::increment(net_rsb.fastopen_attempts);
    err = sock.sendto((void *)in, insz, MSG_FASTOPEN, dst, ats_ip_size(dst));
    if (err >= 0) {
      Metrics::Counter::increment(net_rsb.fastopen_successes);
    }

    set_dest_addr_for_bio(bio, nullptr);
  } else {
    err = sock.write((void *)in, insz);
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
  int        fd = BIO_get_fd(bio, nullptr);
  UnixSocket sock{fd};
  ink_assert(sock.is_ok());

  // TODO: If we haven't done the fastopen, ink_abort().

  err = sock.read(out, outsz);
  if (err < 0) {
    errno = -err;
    if (BIO_sock_non_fatal_error(errno)) {
      BIO_set_retry_read(bio);
    }
  }

  return err < 0 ? -1 : err;
}

#ifndef HAVE_BIO_METH_NEW

static long
fastopen_ctrl(BIO *bio, int cmd, long larg, void *ptr)
{
  return BIO_meth_get_ctrl(const_cast<BIO_METHOD *>(BIO_s_socket()))(bio, cmd, larg, ptr);
}

static const BIO_METHOD fastopen_methods[] = {
  {
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
   }
};
#else // defined(HAVE_BIO_METH_NEW)
static const BIO_METHOD *fastopen_methods = [] {
  BIO_METHOD *methods = BIO_meth_new(BIO_TYPE_SOCKET, "fastopen");
  BIO_meth_set_write(methods, fastopen_bwrite);
  BIO_meth_set_read(methods, fastopen_bread);
  BIO_meth_set_ctrl(methods, BIO_meth_get_ctrl(const_cast<BIO_METHOD *>(BIO_s_socket())));
  BIO_meth_set_create(methods, fastopen_create);
  BIO_meth_set_destroy(methods, fastopen_destroy);
  return methods;
}();
#endif

const BIO_METHOD *
BIO_s_fastopen()
{
  return fastopen_methods;
}

void
BIO_fastopen_set_dest_addr(BIO *bio, const sockaddr *dest_addr)
{
  set_dest_addr_for_bio(bio, const_cast<sockaddr *>(dest_addr));
}
