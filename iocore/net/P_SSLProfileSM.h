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

  P_SSLProfileSM.h
  reference from P_SSLNetVConnection.h

  This file implements an I/O Processor for TCP-SSL network I/O.

 ****************************************************************************/
#ifndef __P_SSLPROFILESM_H__
#define __P_SSLPROFILESM_H__

#include "ts/ink_sock.h"
#include "I_NetVConnection.h"
#include "P_UnixNetProfileSM.h"
#include "I_SSLM.h"
#include "P_UnixNetState.h"
#include "P_Connection.h"
#include "ts/ink_platform.h"
#include "P_EventSystem.h"
#include "P_UnixNetVConnection.h"
#include "P_UnixNet.h"
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
#define SSL_OP_SSLV2_HANDSHAKE 0x80

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

//////////////////////////////////////////////////////////////////
//
//  class SSLProfileSM
//
//  A NetProfileSM to implement SSL/TLS layer on a socket
//
//////////////////////////////////////////////////////////////////
class SSLProfileSM : public UnixNetProfileSM, public SSLM
{
public:
  SSLProfileSM();
  static SSLProfileSM *allocate(EThread *t);
  void free(EThread *t);
  void clear();

  int mainEvent(int event, void *data);

  int64_t read(void *buf, int64_t len, int &err);
  int64_t
  readv(struct iovec *vector, int count)
  {
    return 0;
  }
  int64_t write(void *buf, int64_t len, int &err = DEFAULT);
  int64_t
  writev(struct iovec *vector, int count)
  {
    return 0;
  }
  int64_t
  raw_read(void *buf, int64_t len)
  {
    return low_profileSM->read(buf, len);
  }
  int64_t
  raw_readv(struct iovec *vector, int count)
  {
    return low_profileSM->readv(vector, count);
  }
  int64_t
  raw_write(void *buf, int64_t len)
  {
    return low_profileSM->write(buf, len);
  }
  int64_t
  raw_writev(struct iovec *vector, int count)
  {
    return low_profileSM->writev(vector, count);
  }
  static bool
  check_dep(NetProfileSM *low_profile_sm)
  {
    // The low_profile_sm is current profilesm attached to the vc.
    // It should be a base profileSM (TCP or UDP)
    if (low_profile_sm->low_profileSM == NULL) {
      // Currently, SSLProfileSM only support TcpProfileSM as a low ProfileSM
      if (low_profile_sm->type == PROFILE_SM_TCP) {
        return true;
      }
    }
    return false;
  }
  /// Reenable the VC after a pre-accept or SNI hook is called.
  virtual void reenable();
  virtual const char *get_protocol_tag() const;
  void close();

private:
  SSLProfileSM(const SSLProfileSM &);
  SSLProfileSM &operator=(const SSLProfileSM &);

public:
  int handshakeEvent(int event, void *data);
  void handle_handshake(int event, NetHandler *nh, EThread *lthread);

  int sslStartHandShake(int event, int &err);
  int sslServerHandShakeEvent(int &err);
  int sslClientHandShakeEvent(int &err);

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

  int64_t read_raw_data();
  int64_t read_from_net(int64_t toread, int64_t &rattempted, int64_t &total_read, MIOBufferAccessor &buf);
  int64_t load_buffer_and_write(int64_t towrite, MIOBufferAccessor &buf, int64_t &total_written, int &needs);

  // Returns true if all the hooks reenabled
  bool callHooks(TSEvent eventId);

  // Returns true if we have already called at
  // least some of the hooks
  bool calledHooks(TSEvent /* eventId */) const { return (this->sslHandshakeHookState != HANDSHAKE_HOOKS_PRE); }
  bool computeSSLTrace();

  SSL *make_ssl_connection(SSL_CTX *ctx);

  ink_hrtime sslHandshakeBeginTime;
  ink_hrtime sslLastWriteTime;
  int64_t sslTotalBytesSent;

private:
  MIOBuffer *handShakeBuffer;
  IOBufferReader *handShakeHolder;
  IOBufferReader *handShakeReader;
  int handShakeBioStored;

  /// The current hook.
  /// @note For @C SSL_HOOKS_INVOKE, this is the hook to invoke.
  class APIHook *curHook;

  // PreAcceptHook: Probe starting on a SSLProfileSM attached NetVC.
  // HandshakeDoneHook: SSLProfileSM has finished a SSL Handshake.
  enum {
    SSL_HOOKS_INIT,     ///< Initial state, no hooks called yet.
    SSL_HOOKS_INVOKE,   ///< Waiting to invoke hook.
    SSL_HOOKS_ACTIVE,   ///< Hook invoked, waiting for it to complete.
    SSL_HOOKS_CONTINUE, ///< All hooks have been called and completed
    SSL_HOOKS_DONE      ///< All hooks have been called and completed
  } sslPreAcceptHookState,
    sslHandshakeDoneHookState;

  enum SSLHandshakeHookState {
    HANDSHAKE_HOOKS_PRE,
    HANDSHAKE_HOOKS_CERT,
    HANDSHAKE_HOOKS_POST,
    HANDSHAKE_HOOKS_INVOKE,
    HANDSHAKE_HOOKS_DONE
  } sslHandshakeHookState;
};

extern ClassAllocator<SSLProfileSM> sslProfileSMVCAllocator;

#endif /* __P_SSLPROFILESM_H__ */
