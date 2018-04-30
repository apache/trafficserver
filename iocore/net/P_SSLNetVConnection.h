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

/****************************************************************************

  SSLNetVConnection.h

  This file implements an I/O Processor for network I/O.


 ****************************************************************************/
#pragma once

#include <ts/ink_platform.h>
#include <ts/apidefs.h>
#include <ts/string_view.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "P_EventSystem.h"
#include "P_UnixNetVConnection.h"
#include "P_UnixNet.h"

// These are included here because older OpenSSL libraries don't have them.
// Don't copy these defines, or use their values directly, they are merely
// here to avoid compiler errors.
#ifndef SSL_TLSEXT_ERR_OK
#define SSL_TLSEXT_ERR_OK 0
#endif

#ifndef SSL_TLSEXT_ERR_NOACK
#define SSL_TLSEXT_ERR_NOACK 3
#endif

#define SSL_OP_HANDSHAKE 0x16

// TS-2503: dynamic TLS record sizing
// For smaller records, we should also reserve space for various TCP options
// (timestamps, SACKs.. up to 40 bytes [1]), and account for TLS record overhead
// (another 20-60 bytes on average, depending on the negotiated ciphersuite [2]).
// All in all: 1500 - 40 (IP) - 20 (TCP) - 40 (TCP options) - TLS overhead (60-100)
// For larger records, the size is determined by TLS protocol record size
#define SSL_DEF_TLS_RECORD_SIZE 1300  // 1500 - 40 (IP) - 20 (TCP) - 40 (TCP options) - TLS overhead (60-100)
#define SSL_MAX_TLS_RECORD_SIZE 16383 // 2^14 - 1
#define SSL_DEF_TLS_RECORD_BYTE_THRESHOLD 1000000
#define SSL_DEF_TLS_RECORD_MSEC_THRESHOLD 1000

class SSLNextProtocolSet;
class SSLNextProtocolAccept;
struct SSLCertLookup;

typedef enum {
  SSL_HOOK_OP_DEFAULT,                     ///< Null / initialization value. Do normal processing.
  SSL_HOOK_OP_TUNNEL,                      ///< Switch to blind tunnel
  SSL_HOOK_OP_TERMINATE,                   ///< Termination connection / transaction.
  SSL_HOOK_OP_LAST = SSL_HOOK_OP_TERMINATE ///< End marker value.
} SslVConnOp;

//////////////////////////////////////////////////////////////////
//
//  class NetVConnection
//
//  A VConnection for a network socket.
//
//////////////////////////////////////////////////////////////////
class SSLNetVConnection : public UnixNetVConnection
{
  typedef UnixNetVConnection super; ///< Parent type.

public:
  int sslStartHandShake(int event, int &err) override;
  void clear() override;
  void free(EThread *t) override;

  virtual void
  enableRead()
  {
    read.enabled  = 1;
    write.enabled = 1;
  }

  bool
  getSSLHandShakeComplete() const override
  {
    return sslHandShakeComplete;
  }

  virtual void
  setSSLHandShakeComplete(bool state)
  {
    sslHandShakeComplete = state;
  }

  void
  setSSLSessionCacheHit(bool state)
  {
    sslSessionCacheHit = state;
  }

  bool
  getSSLSessionCacheHit() const
  {
    return sslSessionCacheHit;
  }

  int sslServerHandShakeEvent(int &err);
  int sslClientHandShakeEvent(int &err);
  void net_read_io(NetHandler *nh, EThread *lthread) override;
  int64_t load_buffer_and_write(int64_t towrite, MIOBufferAccessor &buf, int64_t &total_written, int &needs) override;
  void registerNextProtocolSet(SSLNextProtocolSet *);
  void do_io_close(int lerrno = -1) override;

  ////////////////////////////////////////////////////////////
  // Instances of NetVConnection should be allocated        //
  // only from the free list using NetVConnection::alloc(). //
  // The constructor is public just to avoid compile errors.//
  ////////////////////////////////////////////////////////////
  SSLNetVConnection();
  ~SSLNetVConnection() override {}
  static int advertise_next_protocol(SSL *ssl, const unsigned char **out, unsigned *outlen, void *);
  static int select_next_protocol(SSL *ssl, const unsigned char **out, unsigned char *outlen, const unsigned char *in,
                                  unsigned inlen, void *);

  Continuation *
  endpoint() const
  {
    return npnEndpoint;
  }

  bool
  getSSLClientRenegotiationAbort() const
  {
    return sslClientRenegotiationAbort;
  }

  void
  setSSLClientRenegotiationAbort(bool state)
  {
    sslClientRenegotiationAbort = state;
  }

  bool
  getTransparentPassThrough() const
  {
    return transparentPassThrough;
  }

  bool
  GetSNIMapping()
  {
    return SNIMapping;
  }

  void
  setTransparentPassThrough(bool val)
  {
    transparentPassThrough = val;
  }

  // Copy up here so we overload but don't override
  using super::reenable;

  /// Reenable the VC after a pre-accept or SNI hook is called.
  virtual void reenable(NetHandler *nh);

  /// Set the SSL context.
  /// @note This must be called after the SSL endpoint has been created.
  virtual bool sslContextSet(void *ctx);

  int64_t read_raw_data();

  void
  initialize_handshake_buffers()
  {
    this->handShakeBuffer    = new_MIOBuffer();
    this->handShakeReader    = this->handShakeBuffer->alloc_reader();
    this->handShakeHolder    = this->handShakeReader->clone();
    this->handShakeBioStored = 0;
  }

  void
  free_handshake_buffers()
  {
    if (this->handShakeReader) {
      this->handShakeReader->dealloc();
    }
    if (this->handShakeHolder) {
      this->handShakeHolder->dealloc();
    }
    if (this->handShakeBuffer) {
      free_MIOBuffer(this->handShakeBuffer);
    }
    this->handShakeReader    = nullptr;
    this->handShakeHolder    = nullptr;
    this->handShakeBuffer    = nullptr;
    this->handShakeBioStored = 0;
  }

  // Returns true if all the hooks reenabled
  bool callHooks(TSEvent eventId);

  // Returns true if we have already called at
  // least some of the hooks
  bool
  calledHooks(TSEvent eventId) const
  {
    bool retval = false;
    switch (this->sslHandshakeHookState) {
    case HANDSHAKE_HOOKS_PRE:
    case HANDSHAKE_HOOKS_PRE_INVOKE:
      if (eventId == TS_EVENT_VCONN_START) {
        if (curHook) {
          retval = true;
        }
      }
      break;
    case HANDSHAKE_HOOKS_SNI:
      if (eventId == TS_EVENT_VCONN_START) {
        retval = true;
      } else if (eventId == TS_EVENT_SSL_SERVERNAME) {
        if (curHook) {
          retval = true;
        }
      }
      break;
    case HANDSHAKE_HOOKS_CERT:
    case HANDSHAKE_HOOKS_CERT_INVOKE:
      if (eventId == TS_EVENT_VCONN_START || eventId == TS_EVENT_SSL_SERVERNAME) {
        retval = true;
      } else if (eventId == TS_EVENT_SSL_CERT) {
        if (curHook) {
          retval = true;
        }
      }
      break;
    case HANDSHAKE_HOOKS_CLIENT_CERT:
    case HANDSHAKE_HOOKS_CLIENT_CERT_INVOKE:
      if (eventId == TS_EVENT_SSL_VERIFY_CLIENT || eventId == TS_EVENT_VCONN_START) {
        retval = true;
      }
      break;

    case HANDSHAKE_HOOKS_DONE:
      retval = true;
      break;
    }
    return retval;
  }
  bool
  getSSLTrace() const
  {
    return sslTrace || super::origin_trace;
  }

  void
  setSSLTrace(bool state)
  {
    sslTrace = state;
  }

  bool computeSSLTrace();

  const char *
  getSSLProtocol(void) const
  {
    return ssl ? SSL_get_version(ssl) : nullptr;
  }

  const char *
  getSSLCipherSuite(void) const
  {
    return ssl ? SSL_get_cipher_name(ssl) : nullptr;
  }

  int populate_protocol(ts::string_view *results, int n) const override;
  const char *protocol_contains(ts::string_view tag) const override;

  /**
   * Populate the current object based on the socket information in in the
   * con parameter and the ssl object in the arg parameter
   * This is logic is invoked when the NetVC object is created in a new thread context
   */
  int populate(Connection &con, Continuation *c, void *arg) override;

  SSL *ssl                         = nullptr;
  ink_hrtime sslHandshakeBeginTime = 0;
  ink_hrtime sslHandshakeEndTime   = 0;
  ink_hrtime sslLastWriteTime      = 0;
  int64_t sslTotalBytesSent        = 0;
  char *serverName                 = nullptr;

  /// Set by asynchronous hooks to request a specific operation.
  SslVConnOp hookOpRequested = SSL_HOOK_OP_DEFAULT;

  // noncopyable
  SSLNetVConnection(const SSLNetVConnection &) = delete;
  SSLNetVConnection &operator=(const SSLNetVConnection &) = delete;

private:
  ts::string_view map_tls_protocol_to_tag(const char *proto_string) const;
  bool update_rbio(bool move_to_socket);

  bool sslHandShakeComplete        = false;
  bool sslClientRenegotiationAbort = false;
  bool sslSessionCacheHit          = false;
  MIOBuffer *handShakeBuffer       = nullptr;
  IOBufferReader *handShakeHolder  = nullptr;
  IOBufferReader *handShakeReader  = nullptr;
  int handShakeBioStored           = 0;

  bool transparentPassThrough = false;

  /// The current hook.
  /// @note For @C SSL_HOOKS_INVOKE, this is the hook to invoke.
  class APIHook *curHook = nullptr;

  enum SSLHandshakeHookState {
    HANDSHAKE_HOOKS_PRE,
    HANDSHAKE_HOOKS_PRE_INVOKE,
    HANDSHAKE_HOOKS_SNI,
    HANDSHAKE_HOOKS_CERT,
    HANDSHAKE_HOOKS_CERT_INVOKE,
    HANDSHAKE_HOOKS_CLIENT_CERT,
    HANDSHAKE_HOOKS_CLIENT_CERT_INVOKE,
    HANDSHAKE_HOOKS_DONE
  } sslHandshakeHookState = HANDSHAKE_HOOKS_PRE;

  const SSLNextProtocolSet *npnSet = nullptr;
  Continuation *npnEndpoint        = nullptr;
  SessionAccept *sessionAcceptPtr  = nullptr;
  bool sslTrace                    = false;
  bool SNIMapping                  = false;
#ifdef SSL_MODE_ASYNC
  EventIO signalep;
#endif
};

typedef int (SSLNetVConnection::*SSLNetVConnHandler)(int, void *);

extern ClassAllocator<SSLNetVConnection> sslNetVCAllocator;
