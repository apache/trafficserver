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

#ifndef _INK_MD5_h_
#define	_INK_MD5_h_

#include "ink_code.h"
#include "ink_defs.h"

struct INK_MD5
{

  // This union is needed to fix strict-aliasing warnings.
  // It is anonymous so that the 'b' member can remain in
  // the top level scope of the struct. This is because
  // other code touches that directly. Once that code is
  // also fixed we can make this an Alias64 type from
  // ink_align.h
  union
  {
    uint64_t b[2];   // Legacy placeholder
    uint64_t u64[2];
    uint32_t u32[4];
    uint8_t  u8[16];
  };

  const INK_MD5 & operator =(const INK_MD5 & md5)
  {
    u64[0] = md5.u64[0];
    u64[1] = md5.u64[1];
    return md5;
  }
  uint32_t word(int i)
  {
    return u32[i];
  }
  unsigned char byte(int i)
  {
    return u8[i];
  }
  INK_MD5 & loadFromBuffer(char *md5_buf) {
    memcpy((void *) u8, (void *) md5_buf, 16);
    return (*this);
  }
  INK_MD5 & storeToBuffer(char *md5_buf) {
    memcpy((void *) md5_buf, (void *) u8, 16);
    return (*this);
  }
  INK_MD5 & operator =(char *md5) {
    return (loadFromBuffer(md5));
  }
  INK_MD5 & operator =(unsigned char *md5) {
    return (loadFromBuffer((char *) md5));
  }

  char *toStr(char *md5_str)
  {
    return (char *) memcpy((void *) md5_str, (void *) u8, 16);
  }
  void encodeBuffer(unsigned char *buffer, int len)
  {
    ink_code_md5(buffer, len, u8);
  }
  void encodeBuffer(const char *buffer, int len)
  {
    encodeBuffer((unsigned char *) buffer, len);
  }
  char *str()
  {
    return ((char *) u8);
  }
  char *toHexStr(char hex_md5[33])
  {
    return (ink_code_md5_stringify_fast(hex_md5, str()));
  }
  void set(INK_MD5 & md5)
  {
    loadFromBuffer((char *) md5.u8);
  }
  void set(INK_MD5 * md5)
  {
    loadFromBuffer((char *) md5->u8);
  }
  void set(char *p)
  {
    loadFromBuffer(p);
  }
  void set(uint64_t a1, uint64_t a2)
  {
    u64[0] = a1;
    u64[1] = a2;
  }
  char *string(char buf[33])
  {
    char hex_digits[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
    int i, j;

    for (i = 0, j = 0; i < 16; i += 1, j += 2) {
      buf[j + 0] = hex_digits[u8[i] >> 4];
      buf[j + 1] = hex_digits[u8[i] & 0xF];
    }
    buf[32] = '\0';
    return buf;
  }

  uint64_t fold() const
  {
    return (u64[0] ^ u64[1]);
  }

  uint64_t operator[] (int i) const
  {
    return u64[i];
  }
  bool operator==(INK_MD5 const& md5)
  {
    return u64[0] == md5.u64[0] && u64[1] == md5.u64[1];
  }
  INK_MD5() {
    u64[0] = 0;
    u64[1] = 0;
  }
  INK_MD5(uint64_t a1, uint64_t a2) {
    u64[0] = a1;
    u64[1] = a2;
  }
};

#endif
