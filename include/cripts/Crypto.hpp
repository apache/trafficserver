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

namespace Crypto
{
class Base64
{
  using self_type = Base64;

public:
  Base64()                       = delete;
  Base64(const Base64 &)         = delete;
  void operator=(const Base64 &) = delete;

  static Cript::string encode(Cript::string_view str);
  static Cript::string decode(Cript::string_view str);

}; // End class Crypto::Base64

class Escape
{
  using self_type = Escape;

public:
  Escape()                       = delete;
  Escape(const Escape &)         = delete;
  void operator=(const Escape &) = delete;

  static Cript::string encode(Cript::string_view str);
  static Cript::string decode(Cript::string_view str);

}; // End class Crypto::Escape

// These are various (real) crypto functions, all using the Crypto::Base class
namespace detail
{
  class Digest
  {
    using self_type = Digest;

  public:
    Digest() = delete;
    Digest(size_t len) : _length(len) { TSAssert(len <= EVP_MAX_MD_SIZE); }
    // ToDo: Not sure why, but some compilers says this is deprecated
    // void operator=(const Digest &) = delete;

    [[nodiscard]] Cript::string
    hex() const
    {
      return ts::hex({reinterpret_cast<const char *>(_hash), _length});
    }

    operator Cript::string() const { return hex(); }

    [[nodiscard]] Cript::string
    string() const
    {
      return {reinterpret_cast<const char *>(_hash), _length};
    }

    [[nodiscard]] Cript::string
    base64() const
    {
      return Crypto::Base64::encode(Cript::string_view(reinterpret_cast<const char *>(_hash), _length));
    }

    [[nodiscard]] const unsigned char *
    hash() const
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
    void operator=(const Cipher &) = delete;
    Cipher()                       = delete;

    ~Cipher() { EVP_CIPHER_CTX_free(_ctx); }

    virtual void               encrypt(Cript::string_view str);
    virtual Cript::string_view finalize();

    operator Cript::string_view() const { return {_message}; }

    [[nodiscard]] Cript::string
    message() const
    {
      return _message;
    }

    [[nodiscard]] Cript::string
    base64() const
    {
      return Crypto::Base64::encode(_message);
    }

    [[nodiscard]] Cript::string
    hex() const
    {
      return ts::hex(_message);
    }

    operator Cript::string() const { return hex(); }

  protected:
    Cipher(const unsigned char *key, int len) : _key_len(len) { memcpy(_key, key, len); }
    virtual void _initialize();

    Cript::string     _message;
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

  SHA256(const SHA256 &)         = delete;
  void operator=(const SHA256 &) = delete;

  static SHA256 encode(Cript::string_view str);
}; // End class SHA256

class SHA512 : public detail::Digest
{
  using super_type = detail::Digest;
  using self_type  = SHA512;

  SHA512(SHA512 &&that) = default;

public:
  SHA512() : detail::Digest(SHA512_DIGEST_LENGTH){};

  SHA512(const SHA512 &)         = delete;
  void operator=(const SHA512 &) = delete;

  static SHA512 encode(Cript::string_view str);
}; // End class SHA512

class MD5 : public detail::Digest
{
  using self_type  = MD5;
  using super_type = detail::Digest;

  MD5(MD5 &&that) = default;

public:
  MD5() : detail::Digest(MD5_DIGEST_LENGTH){};

  MD5(const MD5 &)            = delete;
  void operator=(const MD5 &) = delete;

  static MD5 encode(Cript::string_view str);
}; // End class MD5

class AES256 : public detail::Cipher
{
  using super_type = detail::Cipher;
  using self_type  = AES256;

  AES256(AES256 &&that) = default;

public:
  using super_type::Cipher;
  using super_type::encrypt;

  AES256(const SHA256 &key) : super_type(key.hash(), SHA256_DIGEST_LENGTH) {}
  AES256(const unsigned char *key) : super_type(key, SHA256_DIGEST_LENGTH) {}

  AES256(const AES256 &)         = delete;
  void operator=(const AES256 &) = delete;

  // The key has to be 256-bit afaik
  static AES256 encrypt(Cript::string_view str, const unsigned char *key);

  static AES256
  encrypt(Cript::string_view str, SHA256 &key)
  {
    return encrypt(str, key.hash());
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

    SHA256(const SHA256 &)         = delete;
    void operator=(const SHA256 &) = delete;

    static SHA256 encrypt(Cript::string_view str, const Cript::string &key);
  }; // End class SHA256
} // namespace HMAC

} // namespace Crypto

// Formatters for {fmt}
namespace fmt
{
template <> struct formatter<Crypto::SHA256> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(Crypto::SHA256 &sha, FormatContext &ctx) -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", sha.hex());
  }
};

template <> struct formatter<Crypto::SHA512> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(Crypto::SHA512 &sha, FormatContext &ctx) -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", sha.hex());
  }
};

template <> struct formatter<Crypto::AES256> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(Crypto::AES256 &sha, FormatContext &ctx) -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", Crypto::Base64::encode(sha.message()));
  }
};

} // namespace fmt
