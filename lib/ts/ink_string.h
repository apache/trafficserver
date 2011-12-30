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

#ifndef _ink_string_h_
#define _ink_string_h_

#include <stdio.h>
#include <memory.h>
#include <strings.h>

#include "ink_assert.h"
#include "ink_error.h"
#include "ParseRules.h"
#include "ink_apidefs.h"


/*===========================================================================*

                            Function Prototypes

 *===========================================================================*/
/* these are supposed to be fast */

inkcoreapi char *ink_memcpy_until_char(char *dst, char *src, unsigned int n, unsigned char c);
inkcoreapi char *ink_strncpy(char *dest, const char *src, int n);
inkcoreapi char *ink_strncat(char *dest, const char *src, int n);
inkcoreapi char *ink_string_concatenate_strings(char *dest, ...);
inkcoreapi char *ink_string_concatenate_strings_n(char *dest, int n, ...);
inkcoreapi char *ink_string_append(char *dest, char *src, int n);
inkcoreapi char *ink_string_find_dotted_extension(char *str, char *ext, int max_ext_len);

/*
 * Copy src to string dst of size siz.  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz == 0).
 * Returns strlen(src); if retval >= siz, truncation occurred.
 */
#if TS_HAS_STRLCPY
#define  ink_strlcpy strlcpy
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
#if TS_HAS_STRLCAT
#define  ink_strlcat strlcat
#else
size_t ink_strlcat(char *dst, const char *str, size_t siz);
#endif

/* 9/3/98 elam: Added this because NT doesn't have strtok_r() */
char *ink_strtok_r(char *s1, const char *s2, char **lasts);

inkcoreapi int ink_strcasecmp(const char *a, const char *b);
inkcoreapi int ink_strncasecmp(const char *a, const char *b, unsigned int max);

/* Convert from UTF-8 to latin-1/iso-8859-1.  This can be lossy. */
void ink_utf8_to_latin1(const char *in, int inlen, char *out, int *outlen);

/*===========================================================================*

                             Inline Functions

 *===========================================================================*/

/*---------------------------------------------------------------------------*

  char *ink_strchr(char *s, char c)

  A faster version of strchr.

 *---------------------------------------------------------------------------*/

static inline char *
ink_strchr(char *s, char c)
{
  while (*s) {
    if (*s == c)
      return (s);
    else
      ++s;
  }
  return (NULL);
}                               /* End ink_strchr */


/*---------------------------------------------------------------------------*

  int ink_string_is_prefix(char *prefix, char *str)

  Returns 1 is <prefix> is a strict prefix of <str>, 0 otherwise.

 *---------------------------------------------------------------------------*/

static inline int
ink_string_is_prefix(char *prefix, char *str)
{
  while (*prefix && *str && *prefix == *str) {
    ++prefix;
    ++str;
  }
  if (*prefix == '\0')
    return (1);
  else
    return (0);
}                               /* End ink_string_is_prefix */


/*---------------------------------------------------------------------------*

  char *ink_string_copy(char *dest, char *src, int n)

  This routine is like ink_strncpy, but it stops writing to <dest>
  after the first NUL from <src> is written, even if <n> bytes are
  not copied.  A NUL is always written if n > 0.  Returns <dest>.

 *---------------------------------------------------------------------------*/

static inline char *
ink_string_copy(char *dest, char *src, int n)
{
  register char *s, *d;

  s = src;
  d = dest;

  while ((n > 1) && *s) {
    *d++ = *s++;
    --n;
  }

  if (n > 0)
    *d = '\0';

  return (dest);
}                               /* End ink_string_copy */


/*---------------------------------------------------------------------------*

  char *ink_string_concatenate_two_strings(char *dest, char *s1, char *s2)

  This routine concatenates the two strings <s1> and <s2> into the buffer
  <dest>, returning the pointer to <dest>.

 *---------------------------------------------------------------------------*/

static inline char *
ink_string_concatenate_two_strings(char *dest, register char *s1, register char *s2)
{
  register char *d;

  d = dest;
  while (*s1)
    *d++ = *s1++;
  while (*s2)
    *d++ = *s2++;
  *d++ = '\0';

  return (dest);
}                               /* End ink_string_concatenate_two_strings */


static inline void
ink_string_fast_strncpy(char *dest, char *src, int src_size, int nbytes)
{
  int to_copy = nbytes < src_size ? nbytes : src_size;

  ink_assert(nbytes >= 0);
  ink_assert(src_size >= 0);

  if (to_copy <= 10) {
    switch (to_copy) {
    case 1:
      dest[0] = '\0';
      break;
    case 2:
      dest[0] = src[0];
      dest[1] = '\0';
      break;
    case 3:
      dest[0] = src[0];
      dest[1] = src[1];
      dest[2] = '\0';
      break;
    case 4:
      dest[0] = src[0];
      dest[1] = src[1];
      dest[2] = src[2];
      dest[3] = '\0';
      break;
    case 5:
      dest[0] = src[0];
      dest[1] = src[1];
      dest[2] = src[2];
      dest[3] = src[3];
      dest[4] = '\0';
      break;
    case 6:
      dest[0] = src[0];
      dest[1] = src[1];
      dest[2] = src[2];
      dest[3] = src[3];
      dest[4] = src[4];
      dest[5] = '\0';
      break;
    case 7:
      dest[0] = src[0];
      dest[1] = src[1];
      dest[2] = src[2];
      dest[3] = src[3];
      dest[4] = src[4];
      dest[5] = src[5];
      dest[6] = '\0';
      break;
    case 8:
      dest[0] = src[0];
      dest[1] = src[1];
      dest[2] = src[2];
      dest[3] = src[3];
      dest[4] = src[4];
      dest[5] = src[5];
      dest[6] = src[6];
      dest[7] = '\0';
      break;
    case 9:
      dest[0] = src[0];
      dest[1] = src[1];
      dest[2] = src[2];
      dest[3] = src[3];
      dest[4] = src[4];
      dest[5] = src[5];
      dest[6] = src[6];
      dest[7] = src[7];
      dest[8] = '\0';
      break;
    case 10:
      dest[0] = src[0];
      dest[1] = src[1];
      dest[2] = src[2];
      dest[3] = src[3];
      dest[4] = src[4];
      dest[5] = src[5];
      dest[6] = src[6];
      dest[7] = src[7];
      dest[8] = src[8];
      dest[9] = '\0';
      break;
    default:
      ink_warning("Error in ink_string_fast_strncpy no copy performed d: %s s: %s n: %d\n", dest, src, nbytes);
      break;
    }
  } else if (to_copy <= 1500) {
    int i;
    for (i = 0; i < (to_copy - 1); i++) {
      dest[i] = src[i];
    }
    dest[i] = '\0';
  } else {
    memcpy(dest, src, (to_copy - 1));
    dest[to_copy] = '\0';
  }
  return;
}

static inline int
ink_string_fast_strncasecmp(const char *s0, const char *s1, int n)
{
  int i;
  for (i = 0; (i < n) && (ParseRules::ink_tolower(s0[i]) == ParseRules::ink_tolower(s1[i])); i++);
  if (i == n)
    return 0;
  else
    return 1;
}

static inline int
ink_string_fast_strcasecmp(const char *s0, const char *s1)
{
  const char *s = s0, *p = s1;
  while (*s && *p && (ParseRules::ink_tolower(*s) == ParseRules::ink_tolower(*p))) {
    s++;
    p++;
  }
  if (!(*s) && !(*p))
    return 0;
  else
    return 1;
}

static inline int
ink_string_fast_strcmp(const char *s0, const char *s1)
{
  const char *s = s0, *p = s1;

  while (*s && *p && *s == *p) {
    s++;
    p++;
  }
  if (!(*s) && !(*p))
    return 0;
  else
    return 1;
}

static inline char *
ink_string_fast_strcpy(char *dest, char *src)
{
  char *s = src, *d = dest;

  while (*s != '\0')
    *d++ = *s++;
  *d = '\0';
  return dest;
}

static inline int
ink_string_strlen(const char *str)
{
  int i;

  if (str[0] == '\0')
    return (0);
  else if (str[1] == '\0')
    return (1);
  else if (str[2] == '\0')
    return (2);
  else if (str[3] == '\0')
    return (3);
  else if (str[4] == '\0')
    return (4);
  else {
    for (i = 5; i < 16; i++)
      if (str[i] == '\0')
        return (i);
    return ((int) (16 + strlen(&(str[16]))));
  }
}

static inline int
ink_string_fast_strlen(char *src)
{
  int i;
  if (!src) {
    return -1;
  }
  for (i = 0; src[i] != '\0'; i++);
  return i;
}

static inline char *
ink_string_fast_max_strcpy(char *s0, char *s1, int max)
{
  int i, rmax = max - 1;
  char *s = s0, *p = s1;
  for (i = 0; i < rmax && *p != '\0'; i++)
    *s++ = *p++;
  *s = '\0';
  return s0;
}

// inline int ptr_len_cmp(const char* p1, int l1, const char* p2, int l2)
//
//     strcmp() functionality for two ptr length pairs
//
inline int
ptr_len_cmp(const char *p1, int l1, const char *p2, int l2)
{
  if (l1 == l2) {
    return memcmp(p1, p2, l1);
  } else if (l1 < l2) {
    return -1;
  } else {
    return 1;
  }
}

// inline int ptr_len_cmp(const char* p1, int l1, const char* p2, int l2)
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
    int str_index = 0;
    const char *match_start = NULL;

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
        p1 = match_start;
        str_index = 0;
      }

      p1++;
      l1--;
    }
  }
  return NULL;
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

    char p1c = *p1;
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

    char p1c = ParseRules::ink_tolower(*p1);
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

    char p1c = ParseRules::ink_tolower(*p1);
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


// int ptr_len_cmp(const char* p1, int l1, const char* str) {
//
//    strcmp like functionality for comparing a ptr,len pair with
//       a null terminated string
//
inline int
ptr_len_cmp(const char *p1, int l1, const char *str)
{

  while (l1 > 0) {
    if (*str == '\0') {
      return 1;
    }

    char p1c = *p1;
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
        return (char *) p1;
      }
      str_cur++;
    }

    p1++;
    l1--;
  }

  return NULL;
}

// Specialized "itoa", that is optimized for small integers, and use snprintf() otherwise.
// On error, we'll return 0, and nothing is written to the buffer.
// TODO: Do these really need to be inline?
inline int
ink_small_itoa(int val, char* buf, int buf_len)
{
  ink_assert(buf_len > 5);
  ink_assert((val >= 0) && (val < 100000));

  if (val < 10) {               // 0 - 9
    buf[0] = '0' + val;
    return 1;
  } else if (val < 100) {       // 10 - 99
    buf[1] = '0' + (val % 10);
    val /= 10;
    buf[0] = '0' + (val % 10);
    return 2;
  } else if (val < 1000) {      // 100 - 999
    buf[2] = '0' + (val % 10);
    val /= 10;
    buf[1] = '0' + (val % 10);
    val /= 10;
    buf[0] = '0' + (val % 10);
    return 3;
  } else if (val < 10000) {     // 1000 - 9999
    buf[3] = '0' + (val % 10);
    val /= 10;
    buf[2] = '0' + (val % 10);
    val /= 10;
    buf[1] = '0' + (val % 10);
    val /= 10;
    buf[0] = '0' + (val % 10);
    return 4;
  } else {                      // 10000 - 99999
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
ink_fast_itoa(int32_t val, char* buf, int buf_len)
{
  if ((val < 0) || (val > 99999)) {
    int ret = snprintf(buf, buf_len, "%d", val);

    return (ret >= 0 ? ret : 0);
  }

  return ink_small_itoa((int)val, buf, buf_len);
}

inline int
ink_fast_uitoa(uint32_t val, char* buf, int buf_len)
{
  if (val > 99999) {
    int ret = snprintf(buf, buf_len, "%u", val);

    return (ret >= 0 ? ret : 0);
  }

  return ink_small_itoa((int)val, buf, buf_len);
}

inline int
ink_fast_ltoa(int64_t val, char* buf, int buf_len)
{
  if ((val < 0) || (val > 99999)) {
    int ret = snprintf(buf, buf_len, "%" PRId64 "", val);

    return (ret >= 0 ? ret : 0);
  }

  return ink_small_itoa((int)val, buf, buf_len);
}

#endif
