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

#include "inktomi++.h"
#include "P_EventSystem.h"
#include "P_UnixNetVConnection.h"
#include "P_UnixNet.h"

#include <openssl/ssl.h>
#include <openssl/err.h>


//////////////////////////////////////////////////////////////////
//
//  class NetVConnection
//
//  A VConnection for a network socket.
//
//////////////////////////////////////////////////////////////////
#ifdef _IOCORE_WIN32_WINNT
class SSLNetVConnection:public NTNetVConnection
#else
class SSLNetVConnection:public UnixNetVConnection
#endif
{
public:
  int connect_calls;
  int connect_want_write;
  int connect_want_read;
  int connect_want_connect;
  int connect_want_ssl;
  int connect_want_syscal;
  int connect_want_accept;
  int connect_want_x509;
  int connect_error_zero;
  int accept_calls;
  int read_calls;
  int read_want_write;
  int read_want_read;
  int read_want_ssl;
  int read_want_syscal;
  int read_want_x509;
  int read_error_zero;
  int write_calls;
  int write_want_write;
  int write_want_read;
  int write_want_ssl;
  int write_want_syscal;
  int write_want_x509;
  int write_error_zero;

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
  virtual int64 load_buffer_and_write(int64 towrite, int64 &wattempted, int64 &total_wrote, MIOBufferAccessor & buf);
  virtual ~ SSLNetVConnection() { }
  ////////////////////////////////////////////////////////////
  // instances of NetVConnection should be allocated        //
  // only from the free list using NetVConnection::alloc(). //
  // The constructo is public just to avoid compile errors. //
  ////////////////////////////////////////////////////////////
  SSLNetVConnection();
  SSL *ssl;
  X509 *client_cert;
  X509 *server_cert;

private:
  bool sslHandShakeComplete;
  bool sslClientConnection;
  SSLNetVConnection(const SSLNetVConnection &);
  SSLNetVConnection & operator =(const SSLNetVConnection &);
};

typedef int (SSLNetVConnection::*SSLNetVConnHandler) (int, void *);

extern ClassAllocator<SSLNetVConnection> sslNetVCAllocator;

//
// Local functions
//


static inline SSLNetVConnection *
new_SSLNetVConnection(EThread * thread)
{
  ProxyMutex *mutex = thread->mutex;
  NET_INCREMENT_DYN_STAT(net_connections_currently_open_stat);
  SSLNetVConnection *vc = sslNetVCAllocator.alloc();
  vc->connect_calls = 0;
  vc->write_calls = 0;
  vc->read_calls = 0;
  vc->accept_calls = 0;
  vc->connect_want_write = 0;
  vc->connect_want_read = 0;
  vc->connect_want_connect = 0;
  vc->connect_want_ssl = 0;
  vc->connect_want_syscal = 0;
  vc->connect_want_accept = 0;
  vc->connect_want_x509 = 0;
  vc->connect_error_zero = 0;
  vc->read_want_write = 0;
  vc->read_want_read = 0;
  vc->read_want_ssl = 0;
  vc->read_want_syscal = 0;
  vc->read_want_x509 = 0;
  vc->read_error_zero = 0;
  vc->write_want_write = 0;
  vc->write_want_read = 0;
  vc->write_want_ssl = 0;
  vc->write_want_syscal = 0;
  vc->write_want_x509 = 0;
  vc->write_error_zero = 0;

  vc->ssl = NULL;
  vc->setSSLHandShakeComplete(0);
  vc->id = net_next_connection_number();
  return vc;
}


#endif /* _SSLNetVConnection_h_ */
