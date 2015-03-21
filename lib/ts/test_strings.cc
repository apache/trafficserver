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

/* This program tests the speeds of several of the string functions, both
 * from the inktomi library and from the standard library.
 * TODO : add testing for ink_strlcpy ?
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

char *small = "12345";
char *small2 = "12345";
int small_len = 5;
#define SMALL_LEN 5

char *medium = "1234512345123451234512345";
char *medium2 = "1234512345123451234512345";
int med_len = 25;
#define MED_LEN 25

char *large = "111111111122222222223333333333444444444455555555556666666666777777777788888888889999999999";
char *large2 = "111111111122222222223333333333444444444455555555556666666666777777777788888888889999999999";
int large_len = 90;
#define LARGE_LEN 90

int i, iresult, cycles;
char *sresult;
char cresult;
clock_t start, stop;

void *ink_memchr(const void *as, int ac, size_t an);

#define STRLEN_TEST(_func_, _size_)                                                                      \
  {                                                                                                      \
    start = clock();                                                                                     \
    for (i = 0; i < cycles; i++) {                                                                       \
      iresult = _func_(_size_);                                                                          \
    }                                                                                                    \
    stop = clock();                                                                                      \
    printf("%20s\t%10s\t%1.03g usec/op\n", #_func_, #_size_, ((double)stop - start) / ((double)cycles)); \
  }

#define STRCHR_TEST(_func_, _size_, _chr_)                                                                  \
  {                                                                                                         \
    start = clock();                                                                                        \
    for (i = 0; i < cycles; i++) {                                                                          \
      sresult = _func_(_size_, _chr_);                                                                      \
    }                                                                                                       \
    stop = clock();                                                                                         \
    printf("%20s\t%10s\t%1.03g usec/op\t%s\n", #_func_, #_size_, ((double)stop - start) / ((double)cycles), \
           (sresult) ? "found" : "not found");                                                              \
  }

#define JP_MEMCHR_TEST(_func_, _size_, _chr_, _len_)                                                            \
  {                                                                                                             \
    start = clock();                                                                                            \
    for (i = 0; i < cycles; i++) {                                                                              \
      sresult = (char *)ink_memchr(_size_, _chr_, _len_);                                                       \
    }                                                                                                           \
    stop = clock();                                                                                             \
    printf("%20s\t%10s\t%1.03g usec/op\t%s\n", "jp_memchr", #_size_, ((double)stop - start) / ((double)cycles), \
           (sresult) ? "found" : "not found");                                                                  \
  }

#define STRCMP_TEST(_func_, _size_, _str_)                                                                  \
  {                                                                                                         \
    start = clock();                                                                                        \
    for (i = 0; i < cycles; i++) {                                                                          \
      iresult = _func_(_size_, _str_);                                                                      \
    }                                                                                                       \
    stop = clock();                                                                                         \
    printf("%20s\t%10s\t%1.03g usec/op\t%s\n", #_func_, #_size_, ((double)stop - start) / ((double)cycles), \
           (sresult) ? "not matching" : "matching");                                                        \
  }

#define STRCPY_TEST(_func_, _size_)                                                                      \
  {                                                                                                      \
    char buf[1024];                                                                                      \
    start = clock();                                                                                     \
    for (i = 0; i < cycles; i++) {                                                                       \
      sresult = _func_(buf, _size_);                                                                     \
    }                                                                                                    \
    stop = clock();                                                                                      \
    printf("%20s\t%10s\t%1.03g usec/op\n", #_func_, #_size_, ((double)stop - start) / ((double)cycles)); \
  }

#define MEMCPY_TEST(_func_, _size_, _len_)                                                                             \
  {                                                                                                                    \
    char buf[1024];                                                                                                    \
    start = clock();                                                                                                   \
    for (i = 0; i < cycles; i++) {                                                                                     \
      sresult = (char *)_func_(buf, _size_, _len_);                                                                    \
    }                                                                                                                  \
    stop = clock();                                                                                                    \
    printf("%20s\t%10s\t%10s\t%1.03g usec/op\n", #_func_, #_size_, #_len_, ((double)stop - start) / ((double)cycles)); \
  }

/* version from ink_string.h */
inline char *
ink_strchr(char *s, char c)
{
  while (*s) {
    if (*s == c)
      return (s);
    else
      ++s;
  }
  return (0);
}

inline char *
ink_memcpy(char *d, char *s, int len)
{
  for (int i = 0; i < len; i++)
    d[i] = s[i];
  return d;
}

/* version using ink_memchr */
inline char *
jp_strchr(char *s, char c)
{
  return (char *)ink_memchr(s, c, strlen(s));
}

void
strlen_tests()
{
  printf("strlen:\n");
  STRLEN_TEST(strlen, small);
  STRLEN_TEST(strlen, medium);
  STRLEN_TEST(strlen, large);
  printf("\n");
}

void
strchr_tests()
{
  printf("strchr:\n");
  /* expect to find */
  STRCHR_TEST(strchr, small, '5');
  STRCHR_TEST(ink_strchr, small, '5');
  JP_MEMCHR_TEST(jp_memchr, small, '5', small_len);
  STRCHR_TEST(strchr, medium, '5');
  STRCHR_TEST(ink_strchr, medium, '5');
  JP_MEMCHR_TEST(jp_memchr, medium, '5', med_len);
  STRCHR_TEST(strchr, large, '5');
  STRCHR_TEST(ink_strchr, large, '5');
  JP_MEMCHR_TEST(jp_memchr, large, '5', large_len);
  /* expect NOT to find */
  STRCHR_TEST(strchr, small, 'x');
  STRCHR_TEST(ink_strchr, small, 'x');
  JP_MEMCHR_TEST(jp_memchr, small, 'x', small_len);
  STRCHR_TEST(strchr, medium, 'x');
  STRCHR_TEST(ink_strchr, medium, 'x');
  JP_MEMCHR_TEST(jp_memchr, medium, 'x', med_len);
  STRCHR_TEST(strchr, large, 'x');
  STRCHR_TEST(ink_strchr, large, 'x');
  JP_MEMCHR_TEST(jp_memchr, large, 'x', large_len);
  printf("\n");
}

void
strcmp_tests()
{
  printf("\nstrcmp:\n");
  /* expect to match */
  STRCMP_TEST(strcmp, small, small2);
  STRCMP_TEST(strcmp, medium, medium2);
  STRCMP_TEST(strcmp, large, large2);
  /* expect NOT to match */
  STRCMP_TEST(strcmp, small, "1xx");
  STRCMP_TEST(strcmp, medium, "1xx");
  STRCMP_TEST(strcmp, large, "1xx");
  printf("\n");
}

void
strcpy_tests()
{
  printf("\nstrcpy:\n");
  STRCPY_TEST(strcpy, small);
  STRCPY_TEST(strcpy, medium);
  STRCPY_TEST(strcpy, large);
  printf("\nmemcpy:\n");
  MEMCPY_TEST(memcpy, small, small_len);
  MEMCPY_TEST(memcpy, medium, med_len);
  MEMCPY_TEST(memcpy, large, large_len);
  MEMCPY_TEST(memcpy, small, SMALL_LEN);
  MEMCPY_TEST(memcpy, medium, MED_LEN);
  MEMCPY_TEST(memcpy, large, LARGE_LEN);

  MEMCPY_TEST(ink_memcpy, small, small_len);
  MEMCPY_TEST(ink_memcpy, medium, med_len);
  MEMCPY_TEST(ink_memcpy, large, large_len);
  MEMCPY_TEST(ink_memcpy, small, SMALL_LEN);
  MEMCPY_TEST(ink_memcpy, medium, MED_LEN);
  MEMCPY_TEST(ink_memcpy, large, LARGE_LEN);
  printf("\n");
}

int
main(int argc, char *argv[])
{
  if (argc < 2) {
    printf("usage: %s [cycles]\n", argv[0]);
    exit(0);
  }
  cycles = atoi(argv[1]);
  printf("%20s\t%10s\tspeed\n", "function", "str size");
  printf("--------------------\t----------\t------------------\n");

  strlen_tests();
  strchr_tests();
  strcmp_tests();
  strcpy_tests();
}

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
