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

  I_SSLM.h
  reference from P_SSLNetVConnection.h

  This file implements an interface for SSL Module

 ****************************************************************************/
#ifndef __I_SSLM_H__
#define __I_SSLM_H__

#include "I_SessionAccept.h"
#include "I_SSLNextProtocolSet.h"
#include <openssl/ssl.h>
#include <openssl/err.h>

class SSLNextProtocolSet;

typedef enum {
  SSL_HOOK_OP_DEFAULT,                     ///< Null / initialization value. Do normal processing.
  SSL_HOOK_OP_TUNNEL,                      ///< Switch to blind tunnel
  SSL_HOOK_OP_TERMINATE,                   ///< Termination connection / transaction.
  SSL_HOOK_OP_LAST = SSL_HOOK_OP_TERMINATE ///< End marker value.
} SslVConnOp;

//////////////////////////////////////////////////////////////////
//
//  class SSLM
//
//  A Interface for SSL Module
//
//////////////////////////////////////////////////////////////////
class SSLM
{
public:
  SSLM();
  virtual ~SSLM();

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

  void registerNextProtocolSet(const SSLNextProtocolSet *);

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

  void clear();

  SSL *ssl;

  /// Set by asynchronous hooks to request a specific operation.
  SslVConnOp hookOpRequested;

  bool transparentPassThrough;
  bool sslHandShakeComplete;
  bool sslClientRenegotiationAbort;
  bool sslSessionCacheHit;

  const SSLNextProtocolSet *npnSet;
  Continuation *npnEndpoint;
  SessionAccept *sessionAcceptPtr;
  unsigned long error_code;
};

#endif /* __I_SSLM_H__ */
