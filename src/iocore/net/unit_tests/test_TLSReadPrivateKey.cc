/** @file

  Catch based unit tests for TLSReadPrivateKey.

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

#include "../P_SSLUtils.h"

#include <catch2/catch_test_macros.hpp>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>

namespace
{
// A P-256 key and a self-signed certificate for it, generated once for these tests. Fixed material keeps the tests
// independent of which key generation functions the TLS library has.
constexpr std::string_view KEY_PEM = "-----BEGIN PRIVATE KEY-----\n"
                                     "MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQgyWd/PfJ+LaCMwC1j\n"
                                     "Fr4mcuibmDCQcPSV68txGMCVPrWhRANCAAQJ6M4pT/pRZ57zijZj1xtGA8CP04Ta\n"
                                     "ko976T8UxiRqTFOrPz5cuUa5LC3ivwQUAEnAYfmSy+0qPO9Zyz5hI7O3\n"
                                     "-----END PRIVATE KEY-----\n";

constexpr std::string_view CERT_PEM = "-----BEGIN CERTIFICATE-----\n"
                                      "MIIBXDCCAQKgAwIBAgIUb8TF11I4tBFu2BtgfeW6P7sB75EwCgYIKoZIzj0EAwIw\n"
                                      "EzERMA8GA1UEAwwIYXRzLXRlc3QwIBcNMjYxMTIwMDE0MjUwWhgPMjEyNjEwMjcw\n"
                                      "MTQyNTBaMBMxETAPBgNVBAMMCGF0cy10ZXN0MFkwEwYHKoZIzj0CAQYIKoZIzj0D\n"
                                      "AQcDQgAECejOKU/6UWee84o2Y9cbRgPAj9OE2pKPe+k/FMYkakxTqz8+XLlGuSwt\n"
                                      "4r8EFABJwGH5ksvtKjzvWcs+YSOzt6MyMDAwHQYDVR0OBBYEFM3Y0nKv0qU+CI0D\n"
                                      "2aIypVpjz6eRMA8GA1UdEwEB/wQFMAMBAf8wCgYIKoZIzj0EAwIDSAAwRQIhAMpy\n"
                                      "7c6e8xoWZI35tqTc+HR9NHhbRJ/iJ1GqCpWHsNwQAiBmzjiiQXxBEZ0clBLL74qL\n"
                                      "K/jdMuSjp6WQ2JZdJhmLAQ==\n"
                                      "-----END CERTIFICATE-----\n";

char passphrase[]{"ats-secret-pass"};

/// Password callback that always supplies @c passphrase.
int
passphrase_cb(char *buf, int size, int /* rwflag */, void * /* u */)
{
  int const length = std::min(static_cast<int>(std::strlen(passphrase)), size);

  std::memcpy(buf, passphrase, length);
  return length;
}

/// Returns KEY_PEM encrypted with AES-256-CBC under @a pass.
std::string
encrypted_key_pem(char *pass)
{
  scoped_BIO const  in{BIO_new_mem_buf(KEY_PEM.data(), KEY_PEM.size())};
  scoped_PKEY const pkey{PEM_read_bio_PrivateKey(in.get(), nullptr, nullptr, nullptr)};
  REQUIRE(pkey != nullptr);

  scoped_BIO const out{BIO_new(BIO_s_mem())};
  REQUIRE(out != nullptr);
  REQUIRE(PEM_write_bio_PrivateKey(out.get(), pkey.get(), EVP_aes_256_cbc(), reinterpret_cast<unsigned char *>(pass),
                                   static_cast<int>(std::strlen(pass)), nullptr, nullptr) == 1);
  BUF_MEM *buffer = nullptr;
  REQUIRE(BIO_get_mem_ptr(out.get(), &buffer) == 1);
  return std::string{buffer->data, buffer->length};
}

/** Reads @a secret with TLSReadPrivateKey on a context that has neither a certificate nor a key.

    Fails the test if the context has a key afterwards, since reading must not set one.

    @param secret What TLSReadPrivateKey reads, as load_certs would pass it.
    @param passwd_cb The context's default password callback, if not null.
    @return Whether a key was read and it is the key of CERT_PEM.
 */
bool
read_key_of_cert(std::string_view secret, pem_password_cb *passwd_cb = nullptr)
{
  SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
  REQUIRE(ctx != nullptr);
  if (passwd_cb != nullptr) {
    SSL_CTX_set_default_passwd_cb(ctx, passwd_cb);
  }

  scoped_PKEY const pkey = TLSReadPrivateKey(ctx, "test.key", secret);
  CHECK(SSL_CTX_get0_privatekey(ctx) == nullptr);
  SSL_CTX_free(ctx);
  if (pkey == nullptr) {
    return false;
  }

  scoped_BIO const  bio{BIO_new_mem_buf(CERT_PEM.data(), CERT_PEM.size())};
  scoped_X509 const cert{PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr)};
  REQUIRE(cert != nullptr);
  return X509_check_private_key(cert.get(), pkey.get()) == 1;
}
} // namespace

TEST_CASE("TLSReadPrivateKey: reads the key without setting it on the context")
{
  CHECK(read_key_of_cert(KEY_PEM));
}

TEST_CASE("TLSReadPrivateKey: reads the key that follows the certificate in one PEM")
{
  CHECK(read_key_of_cert(std::string{CERT_PEM} + std::string{KEY_PEM}));
}

TEST_CASE("TLSReadPrivateKey: decrypts an encrypted key with the context's password callback")
{
  CHECK(read_key_of_cert(encrypted_key_pem(passphrase), passphrase_cb));
}

TEST_CASE("TLSReadPrivateKey: an encrypted key with the wrong passphrase is not read")
{
  char wrong[]{"the-wrong-passphrase"};

  CHECK_FALSE(read_key_of_cert(encrypted_key_pem(wrong), passphrase_cb));
}

TEST_CASE("TLSReadPrivateKey: an unparseable key is not read")
{
  CHECK_FALSE(read_key_of_cert("-----BEGIN PRIVATE KEY-----\nnot base64\n-----END PRIVATE KEY-----\n"));
}

TEST_CASE("TLSReadPrivateKey: a certificate without a key is not read")
{
  CHECK_FALSE(read_key_of_cert(CERT_PEM));
}
