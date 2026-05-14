/** @file

  Catch based unit tests for OCSP stapling

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

#ifndef LIBINKNET_UNIT_TEST_DIR
#error please set LIBINKNET_UNIT_TEST_DIR
#endif

#define _STR(s)  #s
#define _XSTR(s) _STR(s)

#include "../P_OCSPStapling.h"

#include <catch2/catch_test_macros.hpp>

#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <memory>
#include <string>

namespace
{
constexpr char OCSP_TEST_SSL_DIR[] = _XSTR(LIBINKNET_UNIT_TEST_DIR) "/../../../../tests/gold_tests/tls/ssl";

struct BioDeleter {
  void
  operator()(BIO *bio) const
  {
    BIO_free(bio);
  }
};

struct SslCtxDeleter {
  void
  operator()(SSL_CTX *ctx) const
  {
    SSL_CTX_free(ctx);
  }
};

struct X509Deleter {
  void
  operator()(X509 *cert) const
  {
    X509_free(cert);
  }
};

using BioPtr    = std::unique_ptr<BIO, BioDeleter>;
using SslCtxPtr = std::unique_ptr<SSL_CTX, SslCtxDeleter>;
using X509Ptr   = std::unique_ptr<X509, X509Deleter>;

X509Ptr
load_cert(std::string const &path)
{
  BioPtr bio{BIO_new_file(path.c_str(), "r")};
  REQUIRE(bio != nullptr);

  X509Ptr cert{PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr)};
  REQUIRE(cert != nullptr);
  return cert;
}

} // end anonymous namespace

TEST_CASE("OCSP stapling keeps SSL_CTX certificate map after later init failure", "[ssl][ocsp]")
{
  ssl_stapling_ex_init();

  SslCtxPtr ctx{SSL_CTX_new(TLS_method())};
  REQUIRE(ctx != nullptr);

  auto issuer = load_cert(std::string{OCSP_TEST_SSL_DIR} + "/ca.ocsp.pem");
  auto good   = load_cert(std::string{OCSP_TEST_SSL_DIR} + "/server.ocsp.pem");
  auto bad    = load_cert(std::string{OCSP_TEST_SSL_DIR} + "/signed-foo.pem");

  REQUIRE(SSL_CTX_use_certificate(ctx.get(), good.get()) == 1);

  REQUIRE(SSL_CTX_add_extra_chain_cert(ctx.get(), issuer.get()) == 1);
  issuer.release();

  REQUIRE(ssl_stapling_init_cert(ctx.get(), good.get(), "server.ocsp.pem", nullptr));
  CHECK_FALSE(ssl_stapling_init_cert(ctx.get(), bad.get(), "signed-foo.pem", nullptr));
}
