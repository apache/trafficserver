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

#ifndef IOCORE_NET_SSLINTERNAL_H_
#define IOCORE_NET_SSLINTERNAL_H_

// Defined in SSLInteral.c

#if !TS_USE_SET_RBIO
void SSL_set_rbio(SSL *ssl, BIO *rbio);
#endif

unsigned char *SSL_get_session_ticket(SSLNetVConnection *sslvc);

size_t SSL_get_session_ticket_length(SSLNetVConnection *sslvc);

long SSL_get_session_ticket_lifetime_hint(SSLNetVConnection *sslvc);

void SSL_set_session_ticket(SSLNetVConnection *sslvc, void *ticket, size_t ticketLength);

#endif /*SSLINTERNAL_H_BEEN_INCLUDED_BEFORE */
