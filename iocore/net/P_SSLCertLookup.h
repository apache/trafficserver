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

#ifndef __P_SSLCERTLOOKUP_H__
#define __P_SSLCERTLOOKUP_H__

#include "ProxyConfig.h"
#include "P_SSLUtils.h"

struct SSLConfigParams;
struct SSLContextStorage;

struct SSLCertLookup : public ConfigInfo
{
  SSLContextStorage * ssl_storage;
  SSL_CTX *           ssl_default;

  bool insert(SSL_CTX * ctx, const char * name);
  bool insert(SSL_CTX * ctx, const IpEndpoint& address);
  SSL_CTX * findInfoInHash(const char * address) const;
  SSL_CTX * findInfoInHash(const IpEndpoint& address) const;

  // Return the last-resort default TLS context if there is no name or address match.
  SSL_CTX * defaultContext() const { return ssl_default; }

  unsigned count() const;
  SSL_CTX * get(unsigned i) const;

  SSLCertLookup();
  virtual ~SSLCertLookup();
};

#endif /* __P_SSLCERTLOOKUP_H__ */
