/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <ts/ts.h>
#include <ts/remap.h>
#include <string.h>
#include <list>
#include <string>

extern "C" {
typedef struct x509_st X509;
typedef struct bio_st BIO;
}

#define PLUGIN_NAME "sslheaders"

#define SslHdrDebug(fmt, ...) TSDebug(PLUGIN_NAME, "%s: " fmt, __func__, ##__VA_ARGS__)
#define SslHdrError(fmt, ...)  \
  TSError("[" PLUGIN_NAME "] " \
          ": %s: " fmt,        \
          __func__, ##__VA_ARGS__)

enum AttachOptions {
  SSL_HEADERS_ATTACH_CLIENT,
  SSL_HEADERS_ATTACH_SERVER,
  SSL_HEADERS_ATTACH_BOTH,
};

enum ExpansionScope {
  SSL_HEADERS_SCOPE_NONE = 0,
  SSL_HEADERS_SCOPE_CLIENT, // Client certificate
  SSL_HEADERS_SCOPE_SERVER, // Server certificate
  SSL_HEADERS_SCOPE_SSL     // SSL connection
};

enum ExpansionField {
  SSL_HEADERS_FIELD_NONE = 0,
  SSL_HEADERS_FIELD_CERTIFICATE, // Attach whole PEM certificate
  SSL_HEADERS_FIELD_SUBJECT,     // Attach certificate subject
  SSL_HEADERS_FIELD_ISSUER,      // Attach certificate issuer
  SSL_HEADERS_FIELD_SERIAL,      // Attach certificate serial number
  SSL_HEADERS_FIELD_SIGNATURE,   // Attach certificate signature
  SSL_HEADERS_FIELD_NOTBEFORE,   // Attach certificate notBefore date
  SSL_HEADERS_FIELD_NOTAFTER,    // Attach certificate notAfter date

  SSL_HEADERS_FIELD_MAX
};

struct SslHdrExpansion {
  SslHdrExpansion() : name(), scope(SSL_HEADERS_SCOPE_NONE), field(SSL_HEADERS_FIELD_NONE) {}
  std::string name; // HTTP header name
  ExpansionScope scope;
  ExpansionField field;

private:
  SslHdrExpansion &operator=(const SslHdrExpansion &);
};

struct SslHdrInstance {
  typedef std::list<SslHdrExpansion> expansion_list;

  SslHdrInstance();
  ~SslHdrInstance();

  expansion_list expansions;
  AttachOptions attach;
  TSCont cont;

  void register_hooks();

private:
  SslHdrInstance(const SslHdrInstance &);
  SslHdrInstance &operator=(const SslHdrInstance &);
};

bool SslHdrParseExpansion(const char *spec, SslHdrExpansion &exp);
bool SslHdrExpandX509Field(BIO *bio, X509 *ptr, ExpansionField field);
