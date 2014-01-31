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
#if !defined (_SSLNetVConnection_h_)
#define _SSLNetVConnection_h_

#include "libts.h"
#include "P_EventSystem.h"
#include "P_UnixNetVConnection.h"
#include "P_UnixNet.h"

#include <openssl/ssl.h>
#include <openssl/err.h>

// These are included here beacuse older OpenSSL libraries don't have them.
// Don't copy these defines, or use their values directly, they are merely
// here to avoid compiler errors.
#ifndef SSL_TLSEXT_ERR_OK
#define SSL_TLSEXT_ERR_OK 0
#endif

#ifndef SSL_TLSEXT_ERR_NOACK
#define SSL_TLSEXT_ERR_NOACK 3
#endif

class SSLNextProtocolSet;

//////////////////////////////////////////////////////////////////
//
//  class NetVConnection
//
//  A VConnection for a network socket.
//
//////////////////////////////////////////////////////////////////
class SSLNetVConnection:public UnixNetVConnection
{
public:
  virtual int sslStartHandShake(int event, int &err);
  virtual void free(EThread * t);
  virtual void enableRead()
  {
    read.enabled = 1;
    write.enabled = 1;
  };
  virtual bool getSSLHandShakeComplete()
  {
    return sslHandShakeComplete;
  };
  void setSSLHandShakeComplete(bool state)
  {
    sslHandShakeComplete = state;
  };
  virtual bool getSSLClientConnection()
  {
    return sslClientConnection;
  };
  virtual void setSSLClientConnection(bool state)
  {
    sslClientConnection = state;
  };
  int sslServerHandShakeEvent(int &err);
  int sslClientHandShakeEvent(int &err);
  virtual void net_read_io(NetHandler * nh, EThread * lthread);
  virtual int64_t load_buffer_and_write(int64_t towrite, int64_t &wattempted, int64_t &total_wrote, MIOBufferAccessor & buf, int &needs);
  void registerNextProtocolSet(const SSLNextProtocolSet *);

  ////////////////////////////////////////////////////////////
  // Instances of NetVConnection should be allocated        //
  // only from the free list using NetVConnection::alloc(). //
  // The constructor is public just to avoid compile errors.//
  ////////////////////////////////////////////////////////////
  SSLNetVConnection();
  virtual ~SSLNetVConnection() { }

  SSL *ssl;
  X509 *client_cert;
  X509 *server_cert;

  static int advertise_next_protocol(SSL *ssl, const unsigned char **out, unsigned int *outlen, void *arg);

  Continuation * endpoint() const {
    return npnEndpoint;
  }

  bool getSSLClientRenegotiationAbort() const
  {
    return sslClientRenegotiationAbort;
  };

  void setSSLClientRenegotiationAbort(bool state)
  {
    sslClientRenegotiationAbort = state;
  };

private:
  SSLNetVConnection(const SSLNetVConnection &);
  SSLNetVConnection & operator =(const SSLNetVConnection &);

  bool sslHandShakeComplete;
  bool sslClientConnection;
  bool sslClientRenegotiationAbort;
  const SSLNextProtocolSet * npnSet;
  Continuation * npnEndpoint;
};

typedef int (SSLNetVConnection::*SSLNetVConnHandler) (int, void *);

extern ClassAllocator<SSLNetVConnection> sslNetVCAllocator;

#endif /* _SSLNetVConnection_h_ */
