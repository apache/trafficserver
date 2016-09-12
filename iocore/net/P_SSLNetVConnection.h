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
#if !defined(_SSLNetVConnection_h_)
#define _SSLNetVConnection_h_

#include "ts/ink_platform.h"
#include "P_EventSystem.h"
#include "P_UnixNetVConnection.h"
#include "P_UnixNet.h"
#include "P_SSLCertLookup.h"
#include "ts/apidefs.h"

#include <openssl/ssl.h>
#include <openssl/err.h>

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
  virtual int sslStartHandShake(int event, int &err);
  virtual void free(EThread *t);

  virtual void
  enableRead()
  {
    read.enabled  = 1;
    write.enabled = 1;
  }

  virtual bool
  getSSLHandShakeComplete() const
  {
    return sslHandShakeComplete;
  }

  virtual void
  setSSLHandShakeComplete(bool state)
  {
    sslHandShakeComplete = state;
  }

  virtual bool
  getSSLClientConnection() const
  {
    return sslClientConnection;
  }

  virtual void
  setSSLClientConnection(bool state)
  {
    sslClientConnection = state;
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
  virtual void net_read_io(NetHandler *nh, EThread *lthread);
  virtual int64_t load_buffer_and_write(int64_t towrite, MIOBufferAccessor &buf, int64_t &total_written, int &needs);
  void registerNextProtocolSet(const SSLNextProtocolSet *);
  virtual void do_io_close(int lerrno = -1);

  ////////////////////////////////////////////////////////////
  // Instances of NetVConnection should be allocated        //
  // only from the free list using NetVConnection::alloc(). //
  // The constructor is public just to avoid compile errors.//
  ////////////////////////////////////////////////////////////
  SSLNetVConnection();
  virtual ~SSLNetVConnection() {}
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
    this->handShakeReader    = NULL;
    this->handShakeHolder    = NULL;
    this->handShakeBuffer    = NULL;
    this->handShakeBioStored = 0;
  }

  // Returns true if all the hooks reenabled
  bool callHooks(TSEvent eventId);

  // Returns true if we have already called at
  // least some of the hooks
  bool calledHooks(TSEvent /* eventId */) const { return (this->sslHandshakeHookState != HANDSHAKE_HOOKS_PRE); }
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
    return ssl ? SSL_get_version(ssl) : NULL;
  }

  const char *
  getSSLCipherSuite(void) const
  {
    return ssl ? SSL_get_cipher_name(ssl) : NULL;
  }

  /**
   * Populate the current object based on the socket information in in the
   * con parameter and the ssl object in the arg parameter
   * This is logic is invoked when the NetVC object is created in a new thread context
   */
  virtual int populate(Connection &con, Continuation *c, void *arg);

  SSL *ssl;
  ink_hrtime sslHandshakeBeginTime;
  ink_hrtime sslLastWriteTime;
  int64_t sslTotalBytesSent;

  /// Set by asynchronous hooks to request a specific operation.
  SslVConnOp hookOpRequested;

  HPKP *hpkp;

private:
  SSLNetVConnection(const SSLNetVConnection &);
  SSLNetVConnection &operator=(const SSLNetVConnection &);

  bool sslHandShakeComplete;
  bool sslClientConnection;
  bool sslClientRenegotiationAbort;
  bool sslSessionCacheHit;
  MIOBuffer *handShakeBuffer;
  IOBufferReader *handShakeHolder;
  IOBufferReader *handShakeReader;
  int handShakeBioStored;

  bool transparentPassThrough;

  /// The current hook.
  /// @note For @C SSL_HOOKS_INVOKE, this is the hook to invoke.
  class APIHook *curHook;

  enum {
    SSL_HOOKS_INIT,     ///< Initial state, no hooks called yet.
    SSL_HOOKS_INVOKE,   ///< Waiting to invoke hook.
    SSL_HOOKS_ACTIVE,   ///< Hook invoked, waiting for it to complete.
    SSL_HOOKS_CONTINUE, ///< All hooks have been called and completed
    SSL_HOOKS_DONE      ///< All hooks have been called and completed
  } sslPreAcceptHookState;

  enum SSLHandshakeHookState {
    HANDSHAKE_HOOKS_PRE,
    HANDSHAKE_HOOKS_CERT,
    HANDSHAKE_HOOKS_POST,
    HANDSHAKE_HOOKS_INVOKE,
    HANDSHAKE_HOOKS_DONE
  } sslHandshakeHookState;

  const SSLNextProtocolSet *npnSet;
  Continuation *npnEndpoint;
  SessionAccept *sessionAcceptPtr;
  bool sslTrace;
};

typedef int (SSLNetVConnection::*SSLNetVConnHandler)(int, void *);

extern ClassAllocator<SSLNetVConnection> sslNetVCAllocator;

#endif /* _SSLNetVConnection_h_ */
