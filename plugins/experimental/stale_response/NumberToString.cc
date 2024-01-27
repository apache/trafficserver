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

#include "NumberToString.h"
#include <cstring>

/*-----------------------------------------------------------------------------------------------*/
int
base16_digit(char ch)
{
  if ('0' <= ch && ch <= '9') {
    return ch - '0';
  } else if ('A' <= ch && ch <= 'F') {
    return ch - 'A' + 10;
  } else if ('a' <= ch && ch <= 'f') {
    return ch - 'a' + 10;
  } else {
    return -1;
  }
}

/*-----------------------------------------------------------------------------------------------*/
char *
base16_encode(char *dst, unsigned char const *src, size_t len)
{
  char const hex[] = "0123456789abcdef";

  size_t i;
  for (i = 0; i != len; ++i) {
    dst[i * 2 + 0] = hex[src[i] / 16];
    dst[i * 2 + 1] = hex[src[i] % 16];
  }
  dst[i * 2 + 0] = '\0';

  return dst;
}

/*-----------------------------------------------------------------------------------------------*/
unsigned char *
base16_decode(unsigned char *dst, char const *src, size_t len)
{
  unsigned char *p = dst;
  for (size_t i = 0; i + 1 < len; i += 2) {
    int msn = base16_digit(src[i + 0]);
    int lsn = base16_digit(src[i + 1]);
    if (msn < 0 || lsn < 0) {
      break;
    }
    *p++ = static_cast<unsigned char>((msn << 4) | lsn);
  }
  return dst;
}

/*-----------------------------------------------------------------------------------------------*/
