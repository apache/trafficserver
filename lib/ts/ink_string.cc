/** @file

  String and text processing routines for libts

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

#include "ts/ink_platform.h"
#include "ts/ink_assert.h"

#include <cassert>
#include <cstdarg>
#include <cstdlib>
#include <cstring>

#define INK_MAX_STRING_ARRAY_SIZE 128

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/
char *
ink_memcpy_until_char(char *dst, char *src, unsigned int n, unsigned char c)
{
  unsigned int i = 0;
  for (; ((i < n) && (((unsigned char)src[i]) != c)); i++) {
    dst[i] = src[i];
  }
  return &src[i];
}

/*---------------------------------------------------------------------------*

  char *ink_string_concatenate_strings(char *dest, ...)

  This routine concatenates a variable number of strings into the buffer
  <dest>, returning the pointer to <dest>.  The sequence of strings must end
  with nullptr.

 *---------------------------------------------------------------------------*/

char *
ink_string_concatenate_strings(char *dest, ...)
{
  va_list ap;
  char *s, *d;

  va_start(ap, dest);

  d = dest;

  while (true) {
    s = va_arg(ap, char *);
    if (s == nullptr) {
      break;
    }

    while (*s) {
      *d++ = *s++;
    }
  }
  *d++ = '\0';
  va_end(ap);
  return (dest);
} /* End ink_string_concatenate_strings */

/*---------------------------------------------------------------------------*

  char *ink_string_concatenate_strings_n(char *dest, int n, ...)

  This routine concatenates a variable number of strings into the buffer
  <dest>, returning the pointer to <dest>.  The sequence of strings must end
  with nullptr.  A NUL will always be placed after <dest>, and no more than
  <n> - 1 characters will ever be written to <dest>.

 *---------------------------------------------------------------------------*/

char *
ink_string_concatenate_strings_n(char *dest, int n, ...)
{
  va_list ap;
  char *s, *d;

  va_start(ap, n);

  d = dest;

  while (n > 1) {
    s = va_arg(ap, char *);
    if (s == nullptr) {
      break;
    }
    while (*s && (n > 1)) {
      *d++ = *s++;
      n--;
    }
  }
  if (n >= 1) {
    *d = '\0';
  }
  va_end(ap);
  return (dest);
} /* End ink_string_concatenate_strings_n */

/*---------------------------------------------------------------------------*

  char *ink_string_append(char *dest, char *src, int n)

  This routine appends <src> to the end of <dest>, but it insures the
  string pointed to by <dest> never grows beyond <n> characters, including
  the terminating NUL.  A NUL is always written if n > 0.

 *---------------------------------------------------------------------------*/

char *
ink_string_append(char *dest, char *src, int n)
{
  char *d, *s, *last_valid_char;

  ink_assert(src != nullptr);
  ink_assert(dest != nullptr);
  ink_assert(n >= 0);

  if (n == 0) {
    return (dest);
  }

  last_valid_char = dest + n - 1;

  /* Scan For End Of Dest */

  for (d = dest; (d <= last_valid_char) && (*d != '\0'); d++) {
    ;
  }

  /* If At End Of String, NUL Terminate & Exit */

  if (d > last_valid_char) {
    dest[n - 1] = '\0';
    return (dest);
  }

  /* Append src To String */

  s = src;
  while ((d < last_valid_char) && (*s != '\0')) {
    *d++ = *s++;
  }

  /* If At End Of String, NUL Terminate & Exit */

  if (d > last_valid_char) {
    dest[n - 1] = '\0';
  } else {
    *d = '\0';
  }
  return (dest);
} /* End ink_string_append */

#if !HAVE_STRLCPY
size_t
ink_strlcpy(char *dst, const char *src, size_t siz)
{
  char *d       = dst;
  const char *s = src;
  size_t n      = siz;

  /* Copy as many bytes as will fit */
  if (n != 0) {
    while (--n != 0) {
      if ((*d++ = *s++) == '\0') {
        break;
      }
    }
  }

  /* Not enough room in dst, add NUL and traverse rest of src */
  if (n == 0) {
    if (siz != 0) {
      *d = '\0'; /* NUL-terminate dst */
    }
    while (*s++) {
      ;
    }
  }

  return (s - src - 1); /* count does not include NUL */
}
#endif

#if !HAVE_STRLCAT
size_t
ink_strlcat(char *dst, const char *src, size_t siz)
{
  char *d       = dst;
  const char *s = src;
  size_t n      = siz;
  size_t dlen;

  /* Find the end of dst and adjust bytes left but don't go past end */
  while (n-- != 0 && *d != '\0') {
    d++;
  }
  dlen = d - dst;
  n    = siz - dlen;

  if (n == 0) {
    return (dlen + strlen(s));
  }
  while (*s != '\0') {
    if (n != 1) {
      *d++ = *s;
      n--;
    }
    s++;
  }
  *d = '\0';

  return (dlen + (s - src)); /* count does not include NUL */
}
#endif
