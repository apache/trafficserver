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

#include <cmath>

#include "cripts/Lulu.hpp"
#include "cripts/Preamble.hpp"

// From ATS, seems high ...
#define ENCODED_LEN(len) (((int)ceil(1.34 * (len) + 5)) + 1)
#define DECODED_LEN(len) (((int)ceil((len) / 1.33 + 5)) + 1)

namespace cripts
{

cripts::string
Crypto::Base64::Encode(cripts::string_view str)
{
  cripts::string ret;
  size_t         encoded_len = 0;

  ret.resize(ENCODED_LEN(str.size())); // Don't use reserve() here, or bad things happens.
  if (TS_SUCCESS != TSBase64Encode(str.data(), str.size(), ret.data(), ret.capacity(), &encoded_len)) {
    ret = "";
  } else {
    ret.resize(encoded_len);
  }

  return ret; // RVO
}

cripts::string
Crypto::Base64::Decode(cripts::string_view str)
{
  cripts::string ret;
  size_t         decoded_len = 0;

  ret.resize(DECODED_LEN(str.size())); // Don't use reserve() here, or bad things happens.
  if (TS_SUCCESS !=
      TSBase64Decode(str.data(), str.size(), reinterpret_cast<unsigned char *>(ret.data()), ret.capacity(), &decoded_len)) {
    ret = "";
  } else {
    ret.resize(decoded_len);
  }

  return ret; // RVO
}

cripts::string
Crypto::Escape::Encode(cripts::string_view str)
{
  static const unsigned char map[32] = {
    0xFF, 0xFF, 0xFF,
    0xFF,       // control
    0xB4,       // space " # %
    0x19,       // , + /
    0x00,       //
    0x0E,       // < > =
    0x00, 0x00, //
    0x00,       //
    0x1E, 0x80, // [ \ ] ^ `
    0x00, 0x00, //
    0x1F,       // { | } ~ DEL
    0x00, 0x00, 0x00,
    0x00, // all non-ascii characters unmodified
    0x00, 0x00, 0x00,
    0x00, //               .
    0x00, 0x00, 0x00,
    0x00, //               .
    0x00, 0x00, 0x00,
    0x00 //               .
  };

  cripts::string ret;
  size_t         encoded_len = 0;

  ret.resize(str.size() * 3 + 1); // Don't use reserve() here, or bad things happens.
  TSStringPercentEncode(str.data(), str.size(), ret.data(), ret.capacity(), &encoded_len, map);
  ret.resize(encoded_len);

  return ret; // RVO
}

cripts::string
Crypto::Escape::Decode(cripts::string_view str)
{
  cripts::string ret;
  size_t         decoded_len = 0;

  ret.resize(str.size() + 1); // Don't use reserve() here, or bad things happens.
  TSStringPercentDecode(str.data(), str.size(), ret.data(), ret.capacity(), &decoded_len);
  ret.resize(decoded_len);

  return ret; // RVO
}

Crypto::SHA256
Crypto::SHA256::Encode(cripts::string_view str)
{
  SHA256_CTX     ctx;
  Crypto::SHA256 digest;

  SHA256_Init(&ctx);
  SHA256_Update(&ctx, str.data(), str.size());
  SHA256_Final(digest._hash, &ctx);

  return digest; // RVO via the move constructor
}

Crypto::SHA512
Crypto::SHA512::Encode(cripts::string_view str)
{
  SHA512_CTX     ctx;
  Crypto::SHA512 digest;

  SHA512_Init(&ctx);
  SHA512_Update(&ctx, str.data(), str.size());
  SHA512_Final(digest._hash, &ctx);

  return digest; // RVO via the move constructor
}

Crypto::MD5
Crypto::MD5::Encode(cripts::string_view str)
{
  MD5_CTX     ctx;
  Crypto::MD5 digest;

  MD5_Init(&ctx);
  MD5_Update(&ctx, str.data(), str.size());
  MD5_Final(digest._hash, &ctx);

  return digest; // RVO via the move constructor
}

// ToDo: Deal with different IV's ?
void
Crypto::detail::Cipher::_initialize()
{
  unsigned char iv[EVP_MAX_IV_LENGTH];

  TSAssert(_ctx == nullptr);
  CAssert(_cipher != nullptr);
  CAssert(_key_len == static_cast<int>(EVP_CIPHER_key_length(_cipher))); // Make sure the crypto key was correct size

  memset(iv, 0, sizeof(iv)); // The IV is always '0x0'
  _ctx = EVP_CIPHER_CTX_new();
  EVP_EncryptInit_ex(_ctx, _cipher, nullptr, _key, iv);
}

void
Crypto::detail::Cipher::Encrypt(cripts::string_view str)
{
  int len = 0;

  if (!_ctx) {
    _initialize();
  }

  _message.resize(_length + str.size() + AES_BLOCK_SIZE + 1);
  EVP_EncryptUpdate(_ctx, reinterpret_cast<unsigned char *>(_message.data()) + _length, &len, (unsigned char *)str.data(),
                    str.size());
  _length += len;
}

cripts::string_view
Crypto::detail::Cipher::Finalize()
{
  int len = 0;

  if (!_ctx) {
    _initialize();
  }

  EVP_EncryptFinal_ex(_ctx, reinterpret_cast<unsigned char *>(_message.data()) + _length, &len);
  _length += len;
  _message.resize(_length);

  return _message;
}

Crypto::AES256
Crypto::AES256::Encrypt(cripts::string_view str, const unsigned char *key)
{
  Crypto::AES256 crypt(key);

  crypt.Encrypt(str);
  crypt.Finalize();

  return crypt;
}

Crypto::HMAC::SHA256
Crypto::HMAC::SHA256::Encrypt(cripts::string_view str, const cripts::string &key)
{
  Crypto::HMAC::SHA256 retval;

  ::HMAC(EVP_sha256(), key.data(), key.size(), reinterpret_cast<const unsigned char *>(str.data()), str.size(), retval._hash,
         nullptr);

  return retval;
}

} // namespace cripts
