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

#include <array>
#include "sslheaders.h"

#include <openssl/x509.h>
#include <openssl/pem.h>

using x509_expansion = void (*)(X509 *, BIO *);

static void
x509_expand_none(X509 *, BIO *)
{
  // placeholder
}

static void
x509_expand_certificate(X509 *x509, BIO *bio)
{
  long remain;
  char *ptr;

  PEM_write_bio_X509(bio, x509);

  // The PEM format has newlines in it. mod_ssl replaces those with spaces.
  remain = BIO_get_mem_data(bio, &ptr);
  for (char *nl; (nl = (char *)memchr(ptr, '\n', remain)); ptr = nl) {
    *nl = ' ';
    remain -= nl - ptr;
  }
}

static void
x509_expand_subject(X509 *x509, BIO *bio)
{
  X509_NAME *name = X509_get_subject_name(x509);
  X509_NAME_print_ex(bio, name, 0 /* indent */, XN_FLAG_ONELINE);
}

static void
x509_expand_issuer(X509 *x509, BIO *bio)
{
  X509_NAME *name = X509_get_issuer_name(x509);
  X509_NAME_print_ex(bio, name, 0 /* indent */, XN_FLAG_ONELINE);
}

static void
x509_expand_serial(X509 *x509, BIO *bio)
{
  ASN1_INTEGER *serial = X509_get_serialNumber(x509);
  i2a_ASN1_INTEGER(bio, serial);
}

static void
x509_expand_signature(X509 *x509, BIO *bio)
{
  const ASN1_BIT_STRING *sig;
#if OPENSSL_VERSION_NUMBER >= 0x010100000
  X509_get0_signature(&sig, nullptr, x509);
#elif OPENSSL_VERSION_NUMBER >= 0x010002000
  X509_get0_signature(const_cast<ASN1_BIT_STRING **>(&sig), nullptr, x509);
#else
  sig = x509->signature;
#endif
  const char *ptr = reinterpret_cast<const char *>(sig->data);
  const char *end = ptr + sig->length;

  // The canonical OpenSSL way to format the signature seems to be
  // X509_signature_dump(). However that separates each byte with a ':', which is
  // human readable, but would be annoying to parse out of headers. We format as
  // uppercase hex to match the serial number formatting.

  for (; ptr < end; ++ptr) {
    BIO_printf(bio, "%02X", (unsigned char)(*ptr));
  }
}

static void
x509_expand_notbefore(X509 *x509, BIO *bio)
{
  ASN1_TIME *time = X509_get_notBefore(x509);
  ASN1_TIME_print(bio, time);
}

static void
x509_expand_notafter(X509 *x509, BIO *bio)
{
  ASN1_TIME *time = X509_get_notAfter(x509);
  ASN1_TIME_print(bio, time);
}

static const std::array<x509_expansion, SSL_HEADERS_FIELD_MAX> expansions = {{
  x509_expand_none,        // SSL_HEADERS_FIELD_NONE
  x509_expand_certificate, // SSL_HEADERS_FIELD_CERTIFICATE
  x509_expand_subject,     // SSL_HEADERS_FIELD_SUBJECT
  x509_expand_issuer,      // SSL_HEADERS_FIELD_ISSUER
  x509_expand_serial,      // SSL_HEADERS_FIELD_SERIAL
  x509_expand_signature,   // SSL_HEADERS_FIELD_SIGNATURE
  x509_expand_notbefore,   // SSL_HEADERS_FIELD_NOTBEFORE
  x509_expand_notafter,    // SSL_HEADERS_FIELD_NOTAFTER
}};

bool
SslHdrExpandX509Field(BIO *bio, X509 *x509, ExpansionField field)
{
  // Rewind the BIO.
  (void)BIO_reset(bio);

  if (field < expansions.size()) {
    expansions[field](x509, bio);
  }

#if 0
  if (BIO_pending(bio)) {
    long len;
    char * ptr;
    len = BIO_get_mem_data(bio, &ptr);
    SslHdrDebug("X509 field %d: %.*s", (int)field, (int)len, ptr);
  }
#endif

  return true;
}
