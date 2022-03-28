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

/**
 * @file test_utils.cc test
 * @brief Unit tests for functions used in utils.cc
 */

#include <catch.hpp> /* catch unit-test framework */
#include "../utils.h"
#include "../common.h"

/*********************************************************************************************************************
 * Base64 related tests
 * @note the purpose of these tests is to test the usage and some corner cases.
 ********************************************************************************************************************/

TEST_CASE("Base64: estimate buffer size needed to encode a message", "[Base64][access_control][utility]")
{
  size_t encodedLen;

  /* Test with a zero decoded message length */
  encodedLen = cryptoBase64EncodedSize(0);
  CHECK(1 == encodedLen);

  /* Test with a random non-zero decoded message length */
  encodedLen = cryptoBase64EncodedSize(64);
  CHECK(89 == encodedLen);

  /* Test the space for padding. Size of encoding that would result in 2 x "=" padding */
  encodedLen = cryptoBase64EncodedSize(strlen("176a1620e31b14782ba2b66de3edc5b3cb19630475b2ce2ee292d5fd0fe41c3abc"));
  CHECK(89 == encodedLen);

  /* Test the space for padding. Size of encoding that would result in 1 x "=" padding */
  encodedLen = cryptoBase64EncodedSize(strlen("176a1620e31b14782ba2b66de3edc5b3cb19630475b2ce2ee292d5fd0fe41c3ab"));
  CHECK(89 == encodedLen);

  /* Test the space for padding. Size of encoding that would result in no padding */
  encodedLen = cryptoBase64EncodedSize(strlen("176a1620e31b14782ba2b66de3edc5b3cb19630475b2ce2ee292d5fd0fe41c3a"));
  CHECK(89 == encodedLen);
}

TEST_CASE("Base64: estimate buffer size needed to decode a message", "[Base64][access_control][utility]")
{
  size_t encodedLen;
  const char *encoded;

  /* Padding with 2 x '=' */
  encoded    = "MTc2YTE2MjBlMzFiMTQ3ODJiYTJiNjZkZTNlZGM1YjNjYjE5NjMwNDc1YjJjZTJlZTI5MmQ1ZmQwZmU0MWMzYQ==";
  encodedLen = cryptoBase64DecodeSize(encoded, strlen(encoded));
  CHECK(67 == encodedLen);

  /* Padding with 1 x '=' */
  encoded    = "MTc2YTE2MjBlMzFiMTQ3ODJiYTJiNjZkZTNlZGM1YjNjYjE5NjMwNDc1YjJjZTJlZTI5MmQ1ZmQwZmU0MWMzYWI=";
  encodedLen = cryptoBase64DecodeSize(encoded, strlen(encoded));
  CHECK(67 == encodedLen);

  /* Padding with 0 x "=" */
  encoded    = "MTc2YTE2MjBlMzFiMTQ3ODJiYTJiNjZkZTNlZGM1YjNjYjE5NjMwNDc1YjJjZTJlZTI5MmQ1ZmQwZmU0MWMzYWJj";
  encodedLen = cryptoBase64DecodeSize(encoded, strlen(encoded));
  CHECK(67 == encodedLen);

  /* Test empty encoded message calculation */
  encoded    = "";
  encodedLen = cryptoBase64DecodeSize(encoded, strlen(encoded));
  CHECK(1 == encodedLen);

  /* Test empty encoded message calculation */
  encoded    = nullptr;
  encodedLen = cryptoBase64DecodeSize(encoded, 0);
  CHECK(1 == encodedLen);
}

TEST_CASE("Base64: quick encode / decode", "[Base64][access_control][utility]")
{
  const char message[] = "176a1620e31b14782ba2b66de3edc5b3cb19630475b2ce2ee292d5fd0fe41c3a";
  size_t messageLen    = strlen(message);
  CHECK(64 == messageLen);

  size_t encodedMessageEstimatedLen = cryptoBase64EncodedSize(messageLen);
  CHECK(89 == encodedMessageEstimatedLen);
  char encodedMessage[encodedMessageEstimatedLen];

  // now encode message into encodedMessage
  size_t encodedMessageLen = cryptoBase64Encode(message, messageLen, encodedMessage, encodedMessageEstimatedLen);
  CHECK(88 == encodedMessageLen);
  CHECK(0 == strncmp(encodedMessage, "MTc2YTE2MjBlMzFiMTQ3ODJiYTJiNjZkZTNlZGM1YjNjYjE5NjMwNDc1YjJjZTJlZTI5MmQ1ZmQwZmU0MWMzYQ==",
                     encodedMessageLen));

  size_t decodedMessageEstimatedLen = cryptoBase64DecodeSize(encodedMessage, encodedMessageLen);
  CHECK(67 == decodedMessageEstimatedLen);
  char decodedMessage[encodedMessageEstimatedLen];
  size_t decodedMessageLen = cryptoBase64Decode(encodedMessage, encodedMessageLen, decodedMessage, encodedMessageLen);

  CHECK(64 == decodedMessageLen);
  CHECK(0 == strncmp(decodedMessage, message, messageLen));
}

TEST_CASE("Base64: encode empty message into empty buffer", "[Base64][access_control][utility]")
{
  /* Encode empty message */
  const char *message               = "";
  size_t messageLen                 = strlen(message);
  size_t encodedMessageEstimatedLen = 0;
  char encodedMessage[encodedMessageEstimatedLen];

  size_t encodedMessageLen = cryptoBase64Encode(message, messageLen, encodedMessage, encodedMessageEstimatedLen);

  CHECK(0 == encodedMessageLen);
  CHECK(0 == strncmp(encodedMessage, "", encodedMessageLen));
}

TEST_CASE("Base64: encode null message into null buffer", "[Base64][access_control][utility]")
{
  /* Encode using nullptr pointer and 0 sizes */
  char *message                     = nullptr;
  size_t messageLen                 = 0;
  char *encodedMessage              = nullptr;
  size_t encodedMessageEstimatedLen = 0;

  size_t encodedMessageLen = cryptoBase64Encode(message, messageLen, encodedMessage, encodedMessageEstimatedLen);

  CHECK(0 == encodedMessageLen);
  CHECK(nullptr == encodedMessage);
}

TEST_CASE("Base64: decode empty message into empty buffer", "[Base64][access_control][utility]")
{
  const char *encodedMessage        = "";
  size_t encodedMessageLen          = strlen(encodedMessage);
  size_t decodedMessageEstimatedLen = 0;
  char decodedMessage[decodedMessageEstimatedLen];

  size_t decodedMessageLen = cryptoBase64Decode(encodedMessage, encodedMessageLen, decodedMessage, encodedMessageLen);

  CHECK(0 == decodedMessageLen);
  CHECK(0 == strncmp(decodedMessage, "", decodedMessageLen));
}

TEST_CASE("Base64: decode null message into null buffer", "[Base64][access_control][utility]")
{
  const char *encodedMessage = nullptr;
  size_t encodedMessageLen   = 0;
  char *decodedMessage       = nullptr;

  size_t decodedMessageLen = cryptoBase64Decode(encodedMessage, encodedMessageLen, decodedMessage, encodedMessageLen);

  CHECK(0 == decodedMessageLen);
  CHECK(nullptr == decodedMessage);
}

TEST_CASE("Base64: quick encode / decode with '+', '/' and various paddings", "[Base64][access_control][utility]")
{
  const char *decoded[] = {"ts>ts?ts!!!!", "ts>ts?ts!!!", "ts>ts?ts!!"};
  const char *encoded[] = {"dHM+dHM/dHMhISEh", "dHM+dHM/dHMhISE=", "dHM+dHM/dHMhIQ=="};

  for (int i = 0; i < 3; i++) {
    /* Encode */
    const char *message               = decoded[i];
    size_t messageLen                 = strlen(message);
    size_t encodedMessageEstimatedLen = cryptoBase64EncodedSize(messageLen);
    char encodedMessage[encodedMessageEstimatedLen];
    size_t encodedMessageLen = cryptoBase64Encode(message, messageLen, encodedMessage, encodedMessageEstimatedLen);
    CHECK(strlen(encoded[i]) == encodedMessageLen);
    CHECK(0 == strncmp(encodedMessage, encoded[i], encodedMessageLen));

    /* Decode */
    // Keep test around in case our implementation's estimation gets better
    // size_t decodedMessageEstimatedLen = cryptoBase64DecodeSize(encodedMessage, encodedMessageLen);
    // CHECK(strlen(decoded[i]) == decodedMessageEstimatedLen);
    char decodedMessage[encodedMessageEstimatedLen];
    size_t decodedMessageLen = cryptoBase64Decode(encodedMessage, encodedMessageLen, decodedMessage, encodedMessageLen);
    CHECK(strlen(decoded[i]) == decodedMessageLen);
    CHECK(0 == strncmp(decodedMessage, message, messageLen));
  }
}

/*********************************************************************************************************************
 * Modified Base64 related test
 * (for more info see the comment in the implementation) + some corner cases.
 ********************************************************************************************************************/

TEST_CASE("Base64: modified encode / decode with '+', '/' and various paddings", "[Base64][access_control][utility]")
{
  const char *decoded[] = {"ts>ts?ts!!!!", "ts>ts?ts!!!", "ts>ts?ts!!"};
  const char *encoded[] = {"dHM-dHM_dHMhISEh", "dHM-dHM_dHMhISE", "dHM-dHM_dHMhIQ"};

  for (int i = 0; i < 3; i++) {
    /* Encode */
    const char *message               = decoded[i];
    size_t messageLen                 = strlen(message);
    size_t encodedMessageEstimatedLen = cryptoBase64EncodedSize(messageLen);
    char encodedMessage[encodedMessageEstimatedLen];
    size_t encodedMessageLen = cryptoModifiedBase64Encode(message, messageLen, encodedMessage, encodedMessageEstimatedLen);
    CHECK(strlen(encoded[i]) == encodedMessageLen);
    CHECK(0 == strncmp(encodedMessage, encoded[i], encodedMessageLen));

    /* Decode */
    size_t decodedMessageEstimatedLen = cryptoBase64DecodeSize(encodedMessage, encodedMessageLen);
    char decodedMessage[encodedMessageEstimatedLen];
    size_t decodedMessageLen =
      cryptoModifiedBase64Decode(encodedMessage, encodedMessageLen, decodedMessage, decodedMessageEstimatedLen);
    CAPTURE(i);
    CAPTURE(decoded[i]);
    CAPTURE(std::string(decodedMessage));
    CHECK(strlen(decoded[i]) == decodedMessageLen);
    CHECK(0 == strncmp(decodedMessage, message, messageLen));
  }
}

/*********************************************************************************************************************
 * Digest calculation related test
 * (for more info see the comment in the implementation) + some corner cases.
 ********************************************************************************************************************/

TEST_CASE("HMAC Digest: test various supported/unsupported types", "[MAC][access_control][utility]")
{
  cryptoMagicInit();

  const String key  = "1234567890";
  const String data = "calculate a message digest on this";

  char out[MAX_MSGDIGEST_BUFFER_SIZE];
  char hexOut[MAX_MSGDIGEST_BUFFER_SIZE];

  StringList types   = {"MD4", "MD5", "SHA1", "SHA224", "SHA256", "SHA384", "SHA512"};
  StringList digests = {"6b3057137a6e17613883ac25a628b1b3",
                        "820117c62fa161804efb3743cc838b81",
                        "0e3dfdfb04a3dfcd4d195cb1a5e4186feab2e0c1",
                        "00a6f43962e2b35cb2491f81d59ef2268309c8cde744891188c9b855",
                        "149333e1db61f9a18a91a13aca0370b89cec4c546360b85530ae2da97b7b1cb9",
                        "da500bdc5318bfce7a8a094b9da1d8ac901e145d73cc7039e41c6bff4451734269689465ca39e861b9026b481d3cc9db",
                        "e075c8b0637bc4fb82cdca66a2b72e3c1734f4f78c803e5db7ca879f85f16b2e057fa62bdd09eef5bbea562990d52a671927033056"
                        "314c19092263f753ecd019"};

#ifndef OPENSSL_IS_BORINGSSL // RIPEMD160 is not available on BoringSSL?
  types.push_back("RIPEMD160");
  digests.push_back("ccf3230972bcf229fb3b16741495c74a72bbdd14");
#endif

  StringList::iterator digestIter = digests.begin();
  for (String digestType : types) {
    size_t outLen = cryptoMessageDigestGet(digestType.c_str(), data.c_str(), data.length(), key.c_str(), key.length(), out,
                                           MAX_MSGDIGEST_BUFFER_SIZE);
    CHECK(0 < outLen);
    if (0 < outLen) {
      size_t hexOutLen = hexDecode(digestIter->c_str(), digestIter->length(), hexOut, MAX_MSGDIGEST_BUFFER_SIZE);
      CHECK(0 < hexOutLen);
      CHECK(cryptoMessageDigestEqual(hexOut, hexOutLen, out, outLen));
    }

    digestIter++;
  }

  cryptoMagicCleanup();
}
