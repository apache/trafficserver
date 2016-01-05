/** @file

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

#ifndef IOCORE_NET_P_SSLCLIENTUTILS_H_
#define IOCORE_NET_P_SSLCLIENTUTILS_H_

#include "ts/ink_config.h"
#include "ts/Diags.h"
#include "P_SSLUtils.h"
#include "P_SSLConfig.h"

#include <openssl/ssl.h>

// BoringSSL does not have this include file
#ifndef OPENSSL_IS_BORINGSSL
#include <openssl/opensslconf.h>
#endif

// Create and initialize a SSL client context.
SSL_CTX *SSLInitClientContext(const struct SSLConfigParams *param);

// Returns the index used to store our data on the SSL
int get_ssl_client_data_index();

#endif /* IOCORE_NET_P_SSLCLIENTUTILS_H_ */
