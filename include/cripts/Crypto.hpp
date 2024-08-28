/*
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
#pragma once

#include <openssl/aes.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/crypto.h>
#include <openssl/md5.h>
#include <openssl/hmac.h>

#include "tsutil/StringConvert.h"
#include "ts/ts.h"
#include "ts/remap.h"

#include "cripts/Lulu.hpp"

namespace cripts::Crypto
{
class Base64
{
  using self_type = Base64;

public:
  Base64()                          = delete;
  Base64(const self_type &)         = delete;
  void operator=(const self_type &) = delete;

  static cripts::string Encode(cripts::string_view str);
  static cripts::string Decode(cripts::string_view str);

}; // End class Crypto::Base64

class Escape
{
  using self_type = Escape;

public:
  Escape()                          = delete;
  Escape(const self_type &)         = delete;
  void operator=(const self_type &) = delete;

  static cripts::string Encode(cripts::string_view str);
  static cripts::string Decode(cripts::string_view str);

}; // End class Crypto::Escape

// These are various (real) crypto functions, all using the Crypto::Base class
namespace detail
{
  class Digest
  {
    using self_type = Digest;

  public:
    Digest()                          = delete;
    void operator=(const self_type &) = delete;
    Digest(self_type &&that)          = default; // For later use

    Digest(size_t len) : _length(len) { TSAssert(len <= EVP_MAX_MD_SIZE); }

    [[nodiscard]] cripts::string
    Hex() const
    {
      return ts::hex({reinterpret_cast<const char *>(_hash), _length});
    }

    operator cripts::string() const { return Hex(); }

    [[nodiscard]] cripts::string
    String() const
    {
      return {reinterpret_cast<const char *>(_hash), _length};
    }

    [[nodiscard]] cripts::string
    Base64() const
    {
      return Crypto::Base64::Encode(cripts::string_view(reinterpret_cast<const char *>(_hash), _length));
    }

    [[nodiscard]] const unsigned char *
    Hash() const
    {
      return _hash;
    }

  protected:
    // Size for the bigest digest
    unsigned char _hash[EVP_MAX_MD_SIZE] = {};
    size_t        _length                = EVP_MAX_MD_SIZE;

  }; // End class Crypto::Digest;

  class Cipher
  {
    using self_type = Cipher;

  public:
    Cipher()                          = delete;
    void operator=(const self_type &) = delete;

    ~Cipher() { EVP_CIPHER_CTX_free(_ctx); }

    virtual void                Encrypt(cripts::string_view str);
    virtual cripts::string_view Finalize();

    operator cripts::string_view() const { return {_message}; }

    [[nodiscard]] cripts::string
    Message() const
    {
      return _message;
    }

    [[nodiscard]] cripts::string
    Base64() const
    {
      return Crypto::Base64::Encode(_message);
    }

    [[nodiscard]] cripts::string
    Hex() const
    {
      return ts::hex(_message);
    }

    operator cripts::string() const { return Hex(); }

  protected:
    Cipher(const unsigned char *key, int len) : _key_len(len) { memcpy(_key, key, len); }
    virtual void _initialize();

    cripts::string    _message;
    unsigned char     _key[EVP_MAX_KEY_LENGTH];
    int               _key_len = 0;
    int               _length  = 0;
    EVP_CIPHER_CTX   *_ctx     = nullptr;
    const EVP_CIPHER *_cipher  = nullptr;

  }; // End class Cipher

} // namespace detail

class SHA256 : public detail::Digest
{
  using super_type = detail::Digest;
  using self_type  = SHA256;

  SHA256(SHA256 &&that) = default;

public:
  SHA256() : detail::Digest(SHA256_DIGEST_LENGTH){};

  SHA256(const self_type &)         = delete;
  void operator=(const self_type &) = delete;

  static self_type Encode(cripts::string_view str);
}; // End class SHA256

class SHA512 : public detail::Digest
{
  using super_type = detail::Digest;
  using self_type  = SHA512;

  SHA512(SHA512 &&that) = default;

public:
  SHA512() : detail::Digest(SHA512_DIGEST_LENGTH){};

  SHA512(const self_type &)         = delete;
  void operator=(const self_type &) = delete;

  static self_type Encode(cripts::string_view str);
}; // End class SHA512

class MD5 : public detail::Digest
{
  using self_type  = MD5;
  using super_type = detail::Digest;

  MD5(MD5 &&that) = default;

public:
  MD5() : detail::Digest(MD5_DIGEST_LENGTH){};

  MD5(const self_type &)            = delete;
  void operator=(const self_type &) = delete;

  static self_type Encode(cripts::string_view str);
}; // End class MD5

class AES256 : public detail::Cipher
{
  using super_type = detail::Cipher;
  using self_type  = AES256;

  AES256(AES256 &&that) noexcept = default;

public:
  using super_type::Cipher;
  using super_type::Encrypt;

  AES256(const SHA256 &key) : super_type(key.Hash(), SHA256_DIGEST_LENGTH) {}
  AES256(const unsigned char *key) : super_type(key, SHA256_DIGEST_LENGTH) {}

  AES256(const self_type &)         = delete;
  void operator=(const self_type &) = delete;

  // The key has to be 256-bit afaik
  static self_type Encrypt(cripts::string_view str, const unsigned char *key);

  static self_type
  Encrypt(cripts::string_view str, SHA256 &key)
  {
    return Encrypt(str, key.Hash());
  }

private:
  // The initialization has to be deferred, since it's virtual, after the ctor.
  void
  _initialize() override
  {
    _cipher = EVP_aes_256_cbc();
    super_type::_initialize();
  }
}; // End class AES256

namespace HMAC
{
  class SHA256 : public detail::Digest
  {
    using super_type = detail::Digest;
    using self_type  = SHA256;

    SHA256(SHA256 &&that) = default;

  public:
    SHA256() : detail::Digest(SHA256_DIGEST_LENGTH){};

    SHA256(const self_type &)         = delete;
    void operator=(const self_type &) = delete;

    static self_type Encrypt(cripts::string_view str, const cripts::string &key);
  }; // End class SHA256
} // namespace HMAC

} // namespace cripts::Crypto

// Formatters for {fmt}
namespace fmt
{
template <> struct formatter<cripts::Crypto::SHA256> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(cripts::Crypto::SHA256 &sha, FormatContext &ctx) -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", sha.Hex());
  }
};

template <> struct formatter<cripts::Crypto::SHA512> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(cripts::Crypto::SHA512 &sha, FormatContext &ctx) -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", sha.Hex());
  }
};

template <> struct formatter<cripts::Crypto::AES256> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(cripts::Crypto::AES256 &sha, FormatContext &ctx) -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", cripts::Crypto::Base64::Encode(sha.Message()));
  }
};

} // namespace fmt
