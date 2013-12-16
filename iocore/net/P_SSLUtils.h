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

#ifndef __P_SSLUTILS_H__
#define __P_SSLUTILS_H__

#include "ink_config.h"
#include "Diags.h"

#define OPENSSL_THREAD_DEFINES
#include <openssl/opensslconf.h>
#include <openssl/ssl.h>

#if !defined(OPENSSL_THREADS)
#error Traffic Server requires a OpenSSL library that support threads
#endif

struct SSLConfigParams;
struct SSLCertLookup;

// Create a default SSL server context.
SSL_CTX * SSLDefaultServerContext();

// Create and initialize a SSL server context.
SSL_CTX *
SSLInitServerContext(
    const SSLConfigParams * param,
    const char * serverCertPtr,
    const char * serverCaCertPtr,
    const char * serverKeyPtr);

// Create and initialize a SSL client context.
SSL_CTX * SSLInitClientContext(const SSLConfigParams * param);

// Initialize the SSL library.
void SSLInitializeLibrary();

// Release SSL_CTX and the associated data
void SSLReleaseContext(SSL_CTX* ctx);

// Log an SSL error.
#define SSLError(fmt, ...) SSLDiagnostic(DiagsMakeLocation(), false, fmt, ##__VA_ARGS__)
// Log a SSL diagnostic using the "ssl" diagnostic tag.
#define SSLDebug(fmt, ...) SSLDiagnostic(DiagsMakeLocation(), true, fmt, ##__VA_ARGS__)

void SSLDiagnostic(const SrcLoc& loc, bool debug, const char * fmt, ...) TS_PRINTFLIKE(3, 4);

// Return a static string name for a SSL_ERROR constant.
const char * SSLErrorName(int ssl_error);

// Log a SSL network buffer.
void SSLDebugBufferPrint(const char * tag, const char * buffer, unsigned buflen, const char * message);

// Load the SSL certificate configuration.
bool SSLParseCertificateConfiguration(const SSLConfigParams * params, SSLCertLookup * lookup);

#endif /* __P_SSLUTILS_H__ */
