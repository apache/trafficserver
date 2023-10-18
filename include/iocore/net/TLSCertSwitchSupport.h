/** @file

  TLSCertSwitchSupport implements common methods and members to
  support switching certificate

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

#pragma once

#include <openssl/ssl.h>
#include "P_SSLCertLookup.h"

class TLSCertSwitchSupport
{
public:
  virtual ~TLSCertSwitchSupport() = default;

  static void initialize();
  static TLSCertSwitchSupport *getInstance(SSL *ssl);
  static void bind(SSL *ssl, TLSCertSwitchSupport *tcss);
  static void unbind(SSL *ssl);

  int selectCertificate(SSL *ssl, SSLCertContextType ctxType);

protected:
  void _clear();

  virtual bool _isTryingRenegotiation() const                                                            = 0;
  virtual shared_SSL_CTX _lookupContextByName(const std::string &servername, SSLCertContextType ctxType) = 0;
  virtual shared_SSL_CTX _lookupContextByIP()                                                            = 0;

private:
  static int _ex_data_index;
};
