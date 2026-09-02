/** @file

  Catch based unit tests for two pieces of inknet SSL_CTX setup, each
  exercised through its public SSLMultiCertConfigLoader boundary:

    * The DH-parameter handling of init_server_ssl_ctx, which transitively
      invokes ssl_context_enable_dhe and (when a file is configured)
      load_dhparams_file.

    * The private key handling of load_certs, which transitively invokes the
      file-static SSLPrivateKeyHandler.

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

#include <iocore/net/SSLMultiCertConfigLoader.h>
#include "../P_SSLCertLookup.h"
#include "../P_SSLConfig.h"
#include "../P_SSLUtils.h"

#include <tscore/ink_memory.h>
#include <tscore/ink_platform.h>

#include <catch2/catch_test_macros.hpp>

#include <openssl/bio.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace
{

std::string
bio_to_string(BIO *bio)
{
  BUF_MEM *bm = nullptr;
  REQUIRE(1 == BIO_get_mem_ptr(bio, &bm));
  return std::string{bm->data, bm->length};
}

std::string
make_valid_dh_pem()
{
  EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_from_name(nullptr, "DH", nullptr);
  REQUIRE(pctx != nullptr);
  REQUIRE(EVP_PKEY_paramgen_init(pctx) > 0);
  char             prime_group[]{"dh_2048_256"};
  OSSL_PARAM const params[2] = {
    OSSL_PARAM_construct_utf8_string("group", prime_group, 0),
    OSSL_PARAM_construct_end(),
  };
  REQUIRE(EVP_PKEY_CTX_set_params(pctx, params) > 0);
  EVP_PKEY *pkey = nullptr;
  REQUIRE(EVP_PKEY_generate(pctx, &pkey) > 0);

  BIO *bio = BIO_new(BIO_s_mem());
  REQUIRE(bio != nullptr);
  REQUIRE(PEM_write_bio_Parameters(bio, pkey) == 1);
  std::string const out{bio_to_string(bio)};
  BIO_free(bio);
  EVP_PKEY_free(pkey);
  EVP_PKEY_CTX_free(pctx);
  return out;
}

// PEM-encodes pkey as a private key, optionally encrypting it with the given
// cipher and passphrase (cipher==nullptr leaves it unencrypted).
std::string
key_to_pem(EVP_PKEY *pkey, EVP_CIPHER const *cipher, char *pass)
{
  BIO *bio = BIO_new(BIO_s_mem());
  REQUIRE(bio != nullptr);
  int passlen{pass ? static_cast<int>(std::strlen(pass)) : 0};
  REQUIRE(PEM_write_bio_PrivateKey(bio, pkey, cipher, reinterpret_cast<unsigned char *>(pass), passlen, nullptr, nullptr) == 1);
  std::string out{bio_to_string(bio)};
  BIO_free(bio);
  return out;
}

std::string
make_rsa_pem()
{
  EVP_PKEY *pkey = EVP_RSA_gen(2048);
  REQUIRE(pkey != nullptr);
  std::string const out{key_to_pem(pkey, nullptr, nullptr)};
  EVP_PKEY_free(pkey);
  return out;
}

// A self-signed certificate paired with the matching 2048-bit RSA private key,
// both PEM-encoded. Each call produces a fresh, independent key pair. When a
// cipher is given the key PEM is encrypted under the passphrase.
struct CertAndKey {
  std::string cert_pem;
  std::string key_pem;
};

CertAndKey
make_cert_and_key(EVP_CIPHER const *cipher = nullptr, char *pass = nullptr)
{
  EVP_PKEY *pkey = EVP_RSA_gen(2048);
  REQUIRE(pkey != nullptr);

  X509 *x509 = X509_new();
  REQUIRE(x509 != nullptr);
  ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
  X509_gmtime_adj(X509_getm_notBefore(x509), 0);
  X509_gmtime_adj(X509_getm_notAfter(x509), 60L * 60L * 24L * 365L);
  REQUIRE(X509_set_pubkey(x509, pkey) == 1);

  X509_NAME *name = X509_NAME_dup(X509_get_subject_name(x509));
  REQUIRE(name != nullptr);
  X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<unsigned char const *>("ats-test"), -1, -1, 0);
  REQUIRE(X509_set_subject_name(x509, name) == 1);
  REQUIRE(X509_set_issuer_name(x509, name) == 1);
  X509_NAME_free(name);
  REQUIRE(X509_sign(x509, pkey, EVP_sha256()) > 0);

  BIO *cert_bio = BIO_new(BIO_s_mem());
  REQUIRE(cert_bio != nullptr);
  REQUIRE(PEM_write_bio_X509(cert_bio, x509) == 1);
  std::string const cert_pem{bio_to_string(cert_bio)};
  BIO_free(cert_bio);
  X509_free(x509);

  std::string const key_pem{key_to_pem(pkey, cipher, pass)};
  EVP_PKEY_free(pkey);
  return {cert_pem, key_pem};
}

class TempFile
{
public:
  explicit TempFile(std::string const &contents)
  {
    char tmpl[] = "/tmp/ats_dhparams_XXXXXX";
    int  fd     = mkstemp(tmpl);
    REQUIRE(fd != -1);
    this->path = tmpl;
    if (!contents.empty()) {
      REQUIRE(write(fd, contents.data(), contents.size()) == static_cast<ssize_t>(contents.size()));
    }
    close(fd);
  }
  TempFile(TempFile const &)            = delete;
  TempFile(TempFile &&)                 = delete;
  TempFile &operator=(TempFile const &) = delete;
  TempFile &operator=(TempFile &&)      = delete;
  ~TempFile() { unlink(this->path.c_str()); }

  char const *
  get_path() const
  {
    return this->path.c_str();
  }

private:
  std::string path;
};

// Drives ssl_context_enable_dhe via init_server_ssl_ctx, holding every
// non-DHE input fixed and varying only dhparamsFile. An empty CertLoadData
// selects the "default generated ctx" branch which still traverses
// ssl_context_enable_dhe but skips cert/key loading entirely, so a non-empty
// returned vector with a non-null SSL_CTX is observable iff DHE configuration
// succeeded.
bool
init_with_dhparams(char const *dhparams_file)
{
  SSLConfigParams params;
  params.dhparamsFile = dhparams_file ? ats_strdup(dhparams_file) : nullptr;

  SSLMultiCertConfigLoader               loader{&params};
  SSLMultiCertConfigLoader::CertLoadData data;
  auto                                   contexts = loader.init_server_ssl_ctx(data, nullptr);

  bool ok = !contexts.empty() && contexts.front().ctx != nullptr;
  for (auto const &lc : contexts) {
    SSL_CTX_free(lc.ctx);
  }
  return ok;
}

// A fixed-passphrase callback, matching how SSLPrivateKeyHandler consults the
// SSL_CTX default password callback to decrypt an encrypted private key.
char test_passphrase[]{"ats-secret-pass"};

int
fixed_passphrase_cb(char *buf, int size, int /* rwflag */, void * /* u */)
{
  int len{static_cast<int>(std::strlen(test_passphrase))};
  if (len > size) {
    len = size;
  }
  std::memcpy(buf, test_passphrase, len);
  return len;
}

// Drives SSLPrivateKeyHandler via the public static load_certs boundary,
// holding the certificate fixed and valid so the only variable under test is
// the private key material. The certificate and key are read from real files,
// exactly as a production ssl_multicert entry would be, so that the file-load
// path (load_rsa_pkey_from_file) is genuinely exercised.
//
// An empty key_path selects the "key bundled in the certificate file" branch,
// where the file load is skipped and the key is read from the certificate
// secret. A non-null passwd_cb is installed as the SSL_CTX default password
// callback, exactly as init_server_ssl_ctx's dialog setup would do for an
// encrypted key.
bool
load_key_via_load_certs(char const *cert_path, char const *key_path, pem_password_cb *passwd_cb = nullptr)
{
  SSLConfigParams          params;
  SSLMultiCertConfigParams settings;
  settings.cert = ats_strdup(cert_path);

  SSLMultiCertConfigLoader::CertLoadData data;
  data.cert_names_list.emplace_back(cert_path);
  data.key_list.emplace_back(key_path);

  SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
  REQUIRE(ctx != nullptr);
  if (passwd_cb != nullptr) {
    SSL_CTX_set_default_passwd_cb(ctx, passwd_cb);
  }

  bool ok = SSLMultiCertConfigLoader::load_certs(ctx, data.cert_names_list, data.key_list, data, &params, &settings);

  SSL_CTX_free(ctx);
  return ok;
}

} // namespace

TEST_CASE("ssl_context_enable_dhe: nullptr dhparams file falls back to built-in DH parameters")
{
  CHECK(init_with_dhparams(nullptr));
}

TEST_CASE("ssl_context_enable_dhe: valid dh_2048_256 DH PEM file is accepted")
{
  TempFile dh{make_valid_dh_pem()};
  CHECK(init_with_dhparams(dh.get_path()));
}

TEST_CASE("ssl_context_enable_dhe: nonexistent dhparams path is rejected")
{
  CHECK_FALSE(init_with_dhparams("/tmp/ats_dhparams_does_not_exist_zzz_xyz"));
}

TEST_CASE("ssl_context_enable_dhe: empty dhparams file is rejected")
{
  TempFile empty{""};
  CHECK_FALSE(init_with_dhparams(empty.get_path()));
}

TEST_CASE("ssl_context_enable_dhe: non-PEM garbage in dhparams file is rejected")
{
  TempFile garbage{"this is definitely not a PEM-encoded DH parameter block\n"};
  CHECK_FALSE(init_with_dhparams(garbage.get_path()));
}

TEST_CASE("ssl_context_enable_dhe: PEM of wrong key type (RSA) is rejected by DH-only decoder")
{
  TempFile rsa{make_rsa_pem()};
  CHECK_FALSE(init_with_dhparams(rsa.get_path()));
}

TEST_CASE("ssl_context_enable_dhe: truncated DH PEM (missing END marker) is rejected")
{
  std::string pem = make_valid_dh_pem();
  auto        end = pem.find("-----END");
  REQUIRE(end != std::string::npos);
  TempFile truncated{pem.substr(0, end)};
  CHECK_FALSE(init_with_dhparams(truncated.get_path()));
}

TEST_CASE("SSLPrivateKeyHandler: a key file matching the certificate is loaded")
{
  CertAndKey ck = make_cert_and_key();
  TempFile   cert{ck.cert_pem};
  TempFile   key{ck.key_pem};
  CHECK(load_key_via_load_certs(cert.get_path(), key.get_path()));
}

TEST_CASE("SSLPrivateKeyHandler: an empty key path loads the key bundled in the certificate file")
{
  CertAndKey ck = make_cert_and_key();
  TempFile   cert{ck.cert_pem + ck.key_pem};
  CHECK(load_key_via_load_certs(cert.get_path(), ""));
}

TEST_CASE("SSLPrivateKeyHandler: a valid key file not matching the certificate is rejected")
{
  TempFile cert{make_cert_and_key().cert_pem};
  TempFile key{make_cert_and_key().key_pem};
  CHECK_FALSE(load_key_via_load_certs(cert.get_path(), key.get_path()));
}

TEST_CASE("SSLPrivateKeyHandler: an unparseable key file is rejected")
{
  TempFile cert{make_cert_and_key().cert_pem};
  TempFile key{"-----BEGIN PRIVATE KEY-----\nnot base64\n-----END PRIVATE KEY-----\n"};
  CHECK_FALSE(load_key_via_load_certs(cert.get_path(), key.get_path()));
}

TEST_CASE("SSLPrivateKeyHandler: an encrypted key file is decrypted via the SSL_CTX password callback")
{
  CertAndKey ck = make_cert_and_key(EVP_aes_256_cbc(), test_passphrase);
  TempFile   cert{ck.cert_pem};
  TempFile   key{ck.key_pem};
  CHECK(load_key_via_load_certs(cert.get_path(), key.get_path(), fixed_passphrase_cb));
}

TEST_CASE("SSLPrivateKeyHandler: an encrypted key file with the wrong passphrase is rejected")
{
  char       wrong_pass[]{"the-wrong-passphrase"};
  CertAndKey ck = make_cert_and_key(EVP_aes_256_cbc(), wrong_pass);
  TempFile   cert{ck.cert_pem};
  TempFile   key{ck.key_pem};
  CHECK_FALSE(load_key_via_load_certs(cert.get_path(), key.get_path(), fixed_passphrase_cb));
}
