/** @file
 *
 *  QUIC Globals
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#pragma once

#include <openssl/ssl.h>

class QUIC
{
public:
  static void init();

  // SSL callbacks
  static int ssl_select_next_protocol(SSL *ssl, const unsigned char **out, unsigned char *outlen, const unsigned char *in,
                                      unsigned inlen, void *);
  static int ssl_client_new_session(SSL *ssl, SSL_SESSION *session);
  static int ssl_cert_cb(SSL *ssl, void *arg);
  static int ssl_sni_cb(SSL *ssl, int *ad, void *arg);

  static int ssl_quic_qc_index;
  static int ssl_quic_tls_index;

private:
  static void _register_stats();
};
