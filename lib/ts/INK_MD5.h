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
#include "MMH.h"

#include "ink_bool.h"
#include "ink_code.h"
#include "ink_port.h"

// #define USE_MMH_FOR_MD5

struct INK_MD5
{
  uint64_t b[2];
  const INK_MD5 & operator =(const INK_MD5 & md5)
  {
    b[0] = md5.b[0];
    b[1] = md5.b[1];
    return md5;
  }
  uint32_t word(int i)
  {
    uint32_t *p = (uint32_t *) & b[0];
    return p[i];
  }
  unsigned char byte(int i)
  {
    unsigned char *p = (unsigned char *) &b[0];
    return p[i];
  }
  INK_MD5 & loadFromBuffer(char *md5_buf) {
    int i;
    char *s, *d;

    for (i = 0, s = md5_buf, d = (char *) (&(b[0])); i < 8; i++, *d++ = *s++);
    for (i = 0, d = (char *) (&(b[1])); i < 8; i++, *d++ = *s++);
    return (*this);
  }
  INK_MD5 & storeToBuffer(char *md5_buf) {
    int i;
    char *s, *d;

    for (i = 0, d = md5_buf, s = (char *) (&(b[0])); i < 8; i++, *d++ = *s++);
    for (i = 0, s = (char *) (&(b[1])); i < 8; i++, *d++ = *s++);
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
    int i;
    char *s, *d;

    for (i = 0, d = md5_str, s = (char *) (&(b[0])); i < 8; i++, *d++ = *s++);
    for (i = 0, s = (char *) (&(b[1])); i < 8; i++, *d++ = *s++);
    return (md5_str);
  }
  void encodeBuffer(unsigned char *buffer, int len)
  {
    unsigned char md5[16];
    ink_code_md5(buffer, len, md5);
    *this = md5;
  }
  void encodeBuffer(const char *buffer, int len)
  {
    encodeBuffer((unsigned char *) buffer, len);
  }
  char *str()
  {
    return ((char *) b);
  }
  char *toHexStr(char hex_md5[33])
  {
    return (ink_code_md5_stringify_fast(hex_md5, str()));
  }
  void set(INK_MD5 & md5)
  {
    loadFromBuffer((char *) &md5);
  }
  void set(INK_MD5 * md5)
  {
    loadFromBuffer((char *) md5);
  }
  void set(char *p)
  {
    loadFromBuffer(p);
  }
  void set(uint64_t a1, uint64_t a2)
  {
    b[0] = a1;
    b[1] = a2;
  }
  char *string(char buf[33])
  {
    char hex_digits[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
    int i, j;
    unsigned char *u = (unsigned char *) &b[0];

    for (i = 0, j = 0; i < 16; i += 1, j += 2) {
      buf[j + 0] = hex_digits[u[i] >> 4];
      buf[j + 1] = hex_digits[u[i] & 0xF];
    }
    buf[32] = '\0';
    return buf;
  }

  uint64_t fold()
  {
    return (b[0] ^ b[1]);
  }

  uint64_t operator[] (int i)
  {
    return b[i];
  }
  bool operator==(INK_MD5 & md5)
  {
    return b[0] == md5.b[0] && b[1] == md5.b[1];
  }
  INK_MD5() {
    b[0] = 0;
    b[1] = 0;
  }
  INK_MD5(uint64_t a1, uint64_t a2) {
    b[0] = a1;
    b[1] = a2;
  }
};

#ifdef USE_MMH_FOR_MD5
#define INK_MD5 MMH
#define ink_code_incr_MD5_init ink_code_incr_MMH_init
#define ink_code_incr_MD5_update ink_code_incr_MMH_update
#define ink_code_incr_MD5_final ink_code_incr_MMH_final
#endif

#endif
