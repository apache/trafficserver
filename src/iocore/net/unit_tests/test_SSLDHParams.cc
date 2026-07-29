/** @file

  Catch based unit tests for the DH-parameter handling behavior of
  SSLMultiCertConfigLoader::init_server_ssl_ctx, which is the inknet
  public boundary that transitively invokes ssl_context_enable_dhe
  and (when a file is configured) load_dhparams_file.

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
#include <openssl/ssl.h>

#include <cstdio>
#include <string>

namespace
{

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
  REQUIRE(PEM_write_bio_Parameters(bio, pkey) == 1);
  BUF_MEM *bm = nullptr;
  BIO_get_mem_ptr(bio, &bm);
  std::string out{bm->data, bm->length};
  BIO_free(bio);
  EVP_PKEY_free(pkey);
  EVP_PKEY_CTX_free(pctx);
  return out;
}

std::string
make_rsa_pem()
{
  EVP_PKEY *pkey = EVP_RSA_gen(2048);
  REQUIRE(pkey != nullptr);
  BIO *bio = BIO_new(BIO_s_mem());
  REQUIRE(PEM_write_bio_PrivateKey(bio, pkey, nullptr, nullptr, 0, nullptr, nullptr) == 1);
  BUF_MEM *bm = nullptr;
  BIO_get_mem_ptr(bio, &bm);
  std::string out{bm->data, bm->length};
  BIO_free(bio);
  EVP_PKEY_free(pkey);
  return out;
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
