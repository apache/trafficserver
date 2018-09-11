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

#include "sslheaders.h"
#include <memory>
#include "tscore/ink_defs.h"

// Count of fields (not including SSL_HEADERS_FIELD_NONE).
#define NUMFIELDS (SSL_HEADERS_FIELD_MAX - 1)

static const struct _f {
  const char *name;
  ExpansionField field;
} fields[] = {
  {"certificate", SSL_HEADERS_FIELD_CERTIFICATE}, {"subject", SSL_HEADERS_FIELD_SUBJECT},
  {"issuer", SSL_HEADERS_FIELD_ISSUER},           {"serial", SSL_HEADERS_FIELD_SERIAL},
  {"signature", SSL_HEADERS_FIELD_SIGNATURE},     {"notbefore", SSL_HEADERS_FIELD_NOTBEFORE},
  {"notafter", SSL_HEADERS_FIELD_NOTAFTER},
};

// Static assert to guarantee the fields table is current.
extern char assert_fields_are_populated[((sizeof(fields) / sizeof(fields[0])) - NUMFIELDS) == 0 ? 0 : -1];

bool
SslHdrParseExpansion(const char *spec, SslHdrExpansion &exp)
{
  const char *sep;
  const char *selector;

  // First, split on '=' to separate the header name from the SSL expansion.
  sep = strchr(spec, '=');
  if (sep == nullptr) {
    SslHdrError("%s: missing '=' in SSL header expansion '%s'", PLUGIN_NAME, spec);
    return false;
  }

  exp.name = std::string(spec, sep - spec);
  selector = sep + 1;

  // Next, split on '.' to separate the certificate selector from the field selector.
  sep = strchr(selector, '.');
  if (sep == nullptr) {
    SslHdrError("%s: missing '.' in SSL header expansion '%s'", PLUGIN_NAME, spec);
    return false;
  }

  if (strncmp(selector, "server.", 7) == 0) {
    exp.scope = SSL_HEADERS_SCOPE_SERVER;
  } else if (strncmp(selector, "client.", 7) == 0) {
    exp.scope = SSL_HEADERS_SCOPE_CLIENT;
  } else if (strncmp(selector, "ssl.", 4) == 0) {
    exp.scope = SSL_HEADERS_SCOPE_SSL;
    SslHdrError("%s: the SSL header expansion scope is not implemented: '%s'", PLUGIN_NAME, spec);
    return false;
  } else {
    SslHdrError("%s: invalid SSL header expansion '%s'", PLUGIN_NAME, spec);
    return false;
  }

  // Push sep to point to the field selector.
  selector = sep + 1;
  for (unsigned i = 0; i < countof(fields); ++i) {
    if (strcmp(selector, fields[i].name) == 0) {
      exp.field = fields[i].field;
      return true;
    }
  }

  SslHdrError("%s: invalid SSL certificate field selector '%s'", PLUGIN_NAME, spec);
  return false;
}
