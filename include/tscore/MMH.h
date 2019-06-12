/** @file

  A brief file description

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

#pragma once

#include "tscore/ink_code.h"
#include "tscore/ink_defs.h"
#include "tscore/CryptoHash.h"

struct MMH_CTX {
  uint64_t state[4];
  unsigned char buffer[32];
  int buffer_size;
  int blocks;
};

// signed-unsigned-const gratuitous differences brought
// to you by history and the ANSI committee

int inkcoreapi ink_code_incr_MMH_init(MMH_CTX *context);
int inkcoreapi ink_code_incr_MMH_update(MMH_CTX *context, const char *input, int input_length);
int inkcoreapi ink_code_incr_MMH_final(uint8_t *sixteen_byte_hash_pointer, MMH_CTX *context);
int inkcoreapi ink_code_MMH(unsigned char *input, int len, unsigned char *sixteen_byte_hash);

/**
  MMH will return different values on big-endian and little-endian
  machines. It can be adapted to return the same values at some additional
  cost.

*/
class MMHContext : public ats::CryptoContextBase
{
protected:
  MMH_CTX _ctx;

public:
  MMHContext();
  /// Update the hash with @a data of @a length bytes.
  bool update(void const *data, int length) override;
  /// Finalize and extract the @a hash.
  bool finalize(CryptoHash &hash) override;
#if 0
  MMH & loadFromBuffer(char *MMH_buf)
  {
    int i;
    char *s, *d;

    for (i = 0, s = MMH_buf, d = (char *) (&(b[0])); i < 8; i++, *d++ = *s++);
    for (i = 0, d = (char *) (&(b[1])); i < 8; i++, *d++ = *s++);
    return *this;
  }
  MMH & storeToBuffer(char *MMH_buf) {
    int i;
    char *s, *d;

    for (i = 0, d = MMH_buf, s = (char *) (&(b[0])); i < 8; i++, *d++ = *s++);
    for (i = 0, s = (char *) (&(b[1])); i < 8; i++, *d++ = *s++);
    return *this;
  }
  MMH & operator =(char *MMH) {
    return loadFromBuffer(MMH);
  }
  MMH & operator =(unsigned char *MMH) {
    return loadFromBuffer(reinterpret_cast<char *>(MMH));
  }

  char *toStr(char *MMH_str) const
  {
    int i;
    char *s, *d;

    for (i = 0, d = MMH_str, s = (char *) (&(b[0])); i < 8; i++, *d++ = *s++);
    for (i = 0, s = (char *) (&(b[1])); i < 8; i++, *d++ = *s++);

    return MMH_str;
  }
  void encodeBuffer(unsigned char *buffer, int len)
  {
    unsigned char MMH[16];
    ink_code_MMH(buffer, len, MMH);
    *this = MMH;
  }
  void encodeBuffer(char *buffer, int len)
  {
    encodeBuffer((unsigned char *) buffer, len);
  }
  char *str()
  {
    return reinterpret_cast<char *>(b);
  }
  char *toHexStr(char hex_MMH[33])
  {
    return ink_code_md5_stringify_fast(hex_MMH, str());
  }
#endif
};
