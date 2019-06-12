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
 * @file utils.h
 * @brief Various utility functions (Headers).
 * @see utils.cc
 */

#pragma once

#include <string_view>   /* std:string_view */
#include <openssl/evp.h> /* EVP_* constants, structures and functions. */
#include <cstring>       /* strlen, strncmp, strncpy, memset, size_t */

#define MAX_MSGDIGEST_BUFFER_SIZE EVP_MAX_MD_SIZE

bool parseStrLong(const char *s, size_t len, long &val);

/* ******* Encoding/Decoding functions ******* */

size_t hexEncode(const char *in, size_t inLen, char *out, size_t outLen);
size_t hexDecode(const char *in, size_t inLen, char *out, size_t outLen);

size_t urlEncode(const char *in, size_t inLen, char *out, size_t outLen);
size_t urlDecode(const char *in, size_t inLen, char *out, size_t outLen);

/* ******* Functions using OpenSSL library ******* */

void cryptoMagicInit();
void cryptoMagicCleanup();

size_t cryptoMessageDigestGet(const char *digestType, const char *data, size_t dataLen, const char *key, size_t keyLen, char *out,
                              size_t outLen);
bool cryptoMessageDigestEqual(const char *md1, size_t md1Len, const char *md2, size_t md2Len);

size_t cryptoBase64EncodedSize(size_t decodedSize);
size_t cryptoBase64DecodeSize(const char *encoded, size_t encodedLen);
size_t cryptoBase64Encode(const char *in, size_t inLen, char *out, size_t outLen);
size_t cryptoBase64Decode(const char *in, size_t inLen, char *out, size_t outLen);
size_t cryptoModifiedBase64Encode(const char *in, size_t inLen, char *out, size_t outLen);
size_t cryptoModifiedBase64Decode(const char *in, size_t inLen, char *out, size_t outLen);
