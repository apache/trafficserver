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

#include <stdio.h>

//
// fast memchr
//
void *
ink_memchr(const void *as, int ac, size_t an)
{
  unsigned char c = (unsigned char)ac;
  unsigned char *s = (unsigned char *)as;

  // initial segment

  int i_len = (int)(((uintptr_t)8 - (uintptr_t)as) & 7);

  // too short to concern us

  if ((int)an < i_len) {
    for (int i = 0; i < (int)an; i++)
      if (s[i] == c)
        return &s[i];
    return 0;
  }
  // bytes 0-3

  switch (i_len & 3) {
  case 3:
    if (*s++ == c)
      return s - 1;
  case 2:
    if (*s++ == c)
      return s - 1;
  case 1:
    if (*s++ == c)
      return s - 1;
  case 0:
    break;
  }

  // bytes 4-8

  unsigned int ib = c;
  ib |= (ib << 8);
  ib |= (ib << 16);
  unsigned int im = 0x7efefeff;
  if (i_len & 4) {
    unsigned int ibp = *(unsigned int *)s;
    unsigned int ibb = ibp ^ ib;
    ibb = ((ibb + im) ^ ~ibb) & ~im;
    if (ibb) {
      if (s[0] == c)
        return &s[0];
      if (s[1] == c)
        return &s[1];
      if (s[2] == c)
        return &s[2];
      if (s[3] == c)
        return &s[3];
    }
    s += 4;
  }
  // next 8x bytes
  uint64_t m = 0x7efefefefefefeffLL;
  uint64_t b = ((uint64_t)ib);
  b |= (b << 32);
  uint64_t *p = (uint64_t *)s;
  unsigned int n = (((unsigned int)an) - (s - (unsigned char *)as)) >> 3;
  uint64_t *end = p + n;
  while (p < end) {
    uint64_t bp = *p;
    uint64_t bb = bp ^ b;
    bb = ((bb + m) ^ ~bb) & ~m;
    if (bb) {
      s = (unsigned char *)p;
      if (s[0] == c)
        return &s[0];
      if (s[1] == c)
        return &s[1];
      if (s[2] == c)
        return &s[2];
      if (s[3] == c)
        return &s[3];
      if (s[4] == c)
        return &s[4];
      if (s[5] == c)
        return &s[5];
      if (s[6] == c)
        return &s[6];
      if (s[7] == c)
        return &s[7];
    }
    p++;
  }

  // terminal segement

  i_len = an - (((unsigned char *)p) - ((unsigned char *)as));
  s = (unsigned char *)p;

  // n-(4..8)..n bytes

  if (i_len & 4) {
    unsigned int ibp = *(unsigned int *)s;
    unsigned int ibb = ibp ^ ib;
    ibb = ((ibb + im) ^ ~ibb) & ~im;
    if (ibb) {
      if (s[0] == c)
        return &s[0];
      if (s[1] == c)
        return &s[1];
      if (s[2] == c)
        return &s[2];
      if (s[3] == c)
        return &s[3];
    }
    s += 4;
  }
  // n-(0..3)..n bytes

  switch (i_len & 3) {
  case 3:
    if (*s++ == c)
      return s - 1;
  case 2:
    if (*s++ == c)
      return s - 1;
  case 1:
    if (*s++ == c)
      return s - 1;
  case 0:
    break;
  }
  return 0;
}


#define MEMCHR(_s, _c) ink_memchr(_s, _c, strlen(_s)) ?: ""
main()
{
  int i = 0;
  printf("%d %s\n", i++, MEMCHR("a;ldkfjoiwenalkdufla asdfj3i", ' '));
  printf("%d %s\n", i++, MEMCHR("a;ldkfjoiwenalkdufla asdfj3i", '3'));
  printf("%d %s\n", i++, MEMCHR("a;ldkfjoiwenalkdufla asdfj3i", '\n'));
  printf("%d %s\n", i++, MEMCHR("a;ldkfjoiwenalk$uflaE$$dfj3i", '$'));
  printf("%d %s\n", i++, MEMCHR("a;ldkfjoiwenalkd#####asdfj3i", '#'));
  printf("%d %s\n", i++, MEMCHR("a;ldkfjoiwenalkdufla a^^sdfj3i", '^'));
  printf("%d %s\n", i++, MEMCHR("a;ldkfjoiwenalkdufla asd*************fj3i", '*'));
}
