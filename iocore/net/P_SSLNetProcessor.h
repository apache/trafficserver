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

  Net.h

  This file implements an I/O Processor for network I/O.


 ****************************************************************************/
#ifndef __P_SSLNETPROCESSOR_H
#define __P_SSLNETPROCESSOR_H

#include "inktomi++.h"
#include "P_Net.h"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/e_os2.h>


#include "P_SSLConfig.h"

class UnixNetVConnection;
struct NetAccept;

//////////////////////////////////////////////////////////////////
//
//  class SSLNetProcessor
//
//////////////////////////////////////////////////////////////////
struct SSLNetProcessor:public
#ifndef _IOCORE_WIN32_WINNT
  UnixNetProcessor
#else
  NTNetProcessor
#endif
{
public:

  virtual int start(int no_of_ssl_threads);

  void cleanup(void);
  int reconfigure();
  int initSSL(SslConfigParams * param);
  int initSSLClient(SslConfigParams * param);
  int initSSLServerCTX(SslConfigParams * param,
                       SSL_CTX * ctx, char *serverCertPtr, char *serverKeyPtr, bool defaultEnabled);

  SSL_CTX *getSSL_CTX(void)
  {
    return (ctx);
  }
  SSL_CTX *getClientSSL_CTX(void)
  {
    return (client_ctx);
  }
  int getAcceptPort()
  {
    return (accept_port_number);
  }
  static void logSSLError(const char *errStr = "", int critical = 1);

SSLNetProcessor():UnixNetProcessor(), verify_depth(0), ctx(NULL), client_ctx(NULL), sslMutexArray(NULL),
    accept_port_number(-1) {
  };
  virtual ~ SSLNetProcessor();

  int verify_depth;
  SSL_CTX *ctx;
  SSL_CTX *client_ctx;
  ProxyMutex **sslMutexArray;

  //
  // Private
  //
#if !defined (_IOCORE_WIN32_WINNT)

  // Virtual function allows etype
  // to be set to ET_SSL for SSLNetProcessor.
  virtual void setEtype(EventType & etype);
  // Functions all THREAD_FREE and THREAD_ALLOC to be performed
  // for both SSL and regular NetVConnection transparent to
  // netProcessor connect functions.
  virtual UnixNetVConnection *allocateThread(EThread * t);
  virtual void freeThread(UnixNetVConnection * vc, EThread * t);
  virtual NetAccept *createNetAccept();
#else // #if defined (_IOCORE_WIN32)

public:
  virtual NTNetVConnection * newNetVConnection(void);
  virtual NTNetVConnection *newClientNetVConnection(void);
#endif // #if defined (_IOCORE_WIN32)

private:
  void initSSLLocks(void);
  SSLNetProcessor(const SSLNetProcessor &);
  SSLNetProcessor & operator =(const SSLNetProcessor &);

  int accept_port_number;
  static bool open_ssl_initialized;
};


extern inkcoreapi SSLNetProcessor ssl_NetProcessor;


#endif
