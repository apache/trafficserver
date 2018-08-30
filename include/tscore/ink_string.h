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

/****************************************************************************

  ink_string.h

  String and text processing routines for libts

 ****************************************************************************/

#pragma once

#include <cstdio>
#include <memory.h>
#include <strings.h>
#include <string_view>

#include "tscore/ink_assert.h"
#include "tscore/ink_error.h"
#include "tscore/ParseRules.h"
#include "tscore/ink_apidefs.h"

/*===========================================================================*

                            Function Prototypes

 *===========================================================================*/
/* these are supposed to be fast */

inkcoreapi char *ink_memcpy_until_char(char *dst, char *src, unsigned int n, unsigned char c);
inkcoreapi char *ink_string_concatenate_strings(char *dest, ...);
inkcoreapi char *ink_string_concatenate_strings_n(char *dest, int n, ...);
inkcoreapi char *ink_string_append(char *dest, char *src, int n);

/*
 * Copy src to string dst of size siz.  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz == 0).
 * Returns strlen(src); if retval >= siz, truncation occurred.
 */
#if HAVE_STRLCPY
#define ink_strlcpy strlcpy
#else
size_t ink_strlcpy(char *dst, const char *str, size_t siz);
#endif
/*
 * Appends src to string dst of size siz (unlike strncat, siz is the
 * full size of dst, not space left).  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz <= strlen(dst)).
 * Returns strlen(src) + MIN(siz, strlen(initial dst)).
 * If retval >= siz, truncation occurred.
 */
#if HAVE_STRLCAT
#define ink_strlcat strlcat
#else
size_t ink_strlcat(char *dst, const char *str, size_t siz);
#endif

/* Convert from UTF-8 to latin-1/iso-8859-1.  This can be lossy. */
void ink_utf8_to_latin1(const char *in, int inlen, char *out, int *outlen);

/*===========================================================================*

                             Inline Functions

 *===========================================================================*/

// inline int ptr_len_casecmp(const char* p1, int l1, const char* p2, int l2)
//
//     strcasecmp() functionality for two ptr length pairs
//
inline int
ptr_len_casecmp(const char *p1, int l1, const char *p2, int l2)
{
  if (l1 < l2) {
    return -1;
  } else if (l1 > l2) {
    return 1;
  }

  while (l1) {
    char p1c = ParseRules::ink_tolower(*p1);
    char p2c = ParseRules::ink_tolower(*p2);

    if (p1c != p2c) {
      if (p1c > p2c) {
        return 1;
      } else {
        return -1;
      }
    }

    p1++;
    p2++;
    l1--;
  }

  return 0;
}

// inline const char* ptr_len_str(const char* p1, int l1, const char* str)
//
//   strstr() like functionality for the ptr, len pairs
//
inline const char *
ptr_len_str(const char *p1, int l1, const char *str)
{
  if (str && str[0]) {
    int str_index           = 0;
    const char *match_start = nullptr;

    while (l1 > 0) {
      if (*p1 == str[str_index]) {
        // If this is the start of a match,
        //    record it;
        if (str_index == 0) {
          match_start = p1;
        }
        // Check to see if we are finished;
        str_index++;
        if (str[str_index] == '\0') {
          return match_start;
        }
      } else if (str_index > 0) {
        l1 += (p1 - match_start);
        p1        = match_start;
        str_index = 0;
      }

      p1++;
      l1--;
    }
  }
  return nullptr;
}

// int ptr_len_ncmp(const char* p1, int l1, const char* str, int n) {
//
//    strncmp like functionality for comparing a ptr,len pair with
//       a null terminated string for n chars
//
inline int
ptr_len_ncmp(const char *p1, int l1, const char *str, int n)
{
  while (l1 > 0 && n > 0) {
    if (*str == '\0') {
      return 1;
    }

    char p1c  = *p1;
    char strc = *str;

    if (p1c != strc) {
      if (p1c > strc) {
        return 1;
      } else if (p1c < strc) {
        return -1;
      }
    }

    p1++;
    l1--;
    n--;
    str++;
  }

  // If we've scanned our nchars, the match
  //   otherwise we're here because str is longer
  //   than p1

  if (n == 0) {
    return 0;
  } else {
    return -1;
  }
}

// int ptr_len_ncasecmp(const char* p1, int l1, const char* str, int n) {
//
//    strncasecmp like functionality for comparing a ptr,len pair with
//       a null terminated string for n chars
//
inline int
ptr_len_ncasecmp(const char *p1, int l1, const char *str, int n)
{
  while (l1 > 0 && n > 0) {
    if (*str == '\0') {
      return 1;
    }

    char p1c  = ParseRules::ink_tolower(*p1);
    char strc = ParseRules::ink_tolower(*str);

    if (p1c != strc) {
      if (p1c > strc) {
        return 1;
      } else if (p1c < strc) {
        return -1;
      }
    }

    p1++;
    l1--;
    n--;
    str++;
  }

  // If we've scanned our nchars, the match
  //   otherwise we're here because str is longer
  //   than p1

  if (n == 0) {
    return 0;
  } else {
    return -1;
  }
}

// int ptr_len_casecmp(const char* p1, int l1, const char* str) {
//
//    strcasecmp like functionality for comparing a ptr,len pair with
//       a null terminated string
//
inline int
ptr_len_casecmp(const char *p1, int l1, const char *str)
{
  while (l1 > 0) {
    if (*str == '\0') {
      return 1;
    }

    char p1c  = ParseRules::ink_tolower(*p1);
    char strc = ParseRules::ink_tolower(*str);

    if (p1c != strc) {
      if (p1c > strc) {
        return 1;
      } else if (p1c < strc) {
        return -1;
      }
    }

    p1++;
    l1--;
    str++;
  }

  // Since we're out of characters in p1
  //   str needs to be finished for the strings
  //   to get equal
  if (*str == '\0') {
    return 0;
  } else {
    return -1;
  }
}

// char* ptr_len_pbrk(const char* p1, int l1, const char* str)
//
//   strpbrk() like functionality for ptr & len pair strings
//
inline char *
ptr_len_pbrk(const char *p1, int l1, const char *str)
{
  while (l1 > 0) {
    const char *str_cur = str;

    while (*str_cur != '\0') {
      if (*p1 == *str_cur) {
        return (char *)p1;
      }
      str_cur++;
    }

    p1++;
    l1--;
  }

  return nullptr;
}

// Specialized "itoa", that is optimized for small integers, and use snprintf() otherwise.
// On error, we'll return 0, and nothing is written to the buffer.
// TODO: Do these really need to be inline?
inline int
ink_small_itoa(int val, char *buf, int buf_len)
{
  ink_assert(buf_len > 5);
  ink_assert((val >= 0) && (val < 100000));

  if (val < 10) { // 0 - 9
    buf[0] = '0' + val;
    return 1;
  } else if (val < 100) { // 10 - 99
    buf[1] = '0' + (val % 10);
    val /= 10;
    buf[0] = '0' + (val % 10);
    return 2;
  } else if (val < 1000) { // 100 - 999
    buf[2] = '0' + (val % 10);
    val /= 10;
    buf[1] = '0' + (val % 10);
    val /= 10;
    buf[0] = '0' + (val % 10);
    return 3;
  } else if (val < 10000) { // 1000 - 9999
    buf[3] = '0' + (val % 10);
    val /= 10;
    buf[2] = '0' + (val % 10);
    val /= 10;
    buf[1] = '0' + (val % 10);
    val /= 10;
    buf[0] = '0' + (val % 10);
    return 4;
  } else { // 10000 - 99999
    buf[4] = '0' + (val % 10);
    val /= 10;
    buf[3] = '0' + (val % 10);
    val /= 10;
    buf[2] = '0' + (val % 10);
    val /= 10;
    buf[1] = '0' + (val % 10);
    val /= 10;
    buf[0] = '0' + (val % 10);
    return 5;
  }
}

inline int
ink_fast_itoa(int32_t val, char *buf, int buf_len)
{
  if ((val < 0) || (val > 99999)) {
    int ret = snprintf(buf, buf_len, "%d", val);

    return (ret >= 0 ? ret : 0);
  }

  return ink_small_itoa((int)val, buf, buf_len);
}

inline int
ink_fast_uitoa(uint32_t val, char *buf, int buf_len)
{
  if (val > 99999) {
    int ret = snprintf(buf, buf_len, "%u", val);

    return (ret >= 0 ? ret : 0);
  }

  return ink_small_itoa((int)val, buf, buf_len);
}

inline int
ink_fast_ltoa(int64_t val, char *buf, int buf_len)
{
  if ((val < 0) || (val > 99999)) {
    int ret = snprintf(buf, buf_len, "%" PRId64 "", val);

    return (ret >= 0 ? ret : 0);
  }

  return ink_small_itoa((int)val, buf, buf_len);
}

/// Check for prefix.
/// @return @c true if @a lhs is a prefix (ignoring case) of @a rhs.
inline bool
IsNoCasePrefixOf(std::string_view const &lhs, std::string_view const &rhs)
{
  return lhs.size() <= rhs.size() && 0 == strncasecmp(lhs.data(), rhs.data(), lhs.size());
}

/// Check for prefix.
/// @return @c true if @a lhs is a prefix of @a rhs.
inline bool
IsPrefixOf(std::string_view const &lhs, std::string_view const &rhs)
{
  return lhs.size() <= rhs.size() && 0 == memcmp(lhs.data(), rhs.data(), lhs.size());
}
