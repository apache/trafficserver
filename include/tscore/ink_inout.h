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

/**************************************************************************
  I/O Marshalling

**************************************************************************/

#pragma once

// source of good macros..
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
#include <arpa/nameser.h>
#ifdef __cplusplus
}
#endif /* __cplusplus */

#define GETCHAR(s, cp) \
  {                    \
    (s) = *(cp)++;     \
  }

#define PUTCHAR(s, cp) \
  {                    \
    *(cp)++ = (s);     \
  }

#define GETLONGLONG(l, cp) \
  {                        \
    (l) = *(cp)++ << 8;    \
    (l) |= *(cp)++;        \
    (l) <<= 8;             \
    (l) |= *(cp)++;        \
    (l) <<= 8;             \
    (l) |= *(cp)++;        \
    (l) <<= 8;             \
    (l) |= *(cp)++;        \
    (l) <<= 8;             \
    (l) |= *(cp)++;        \
    (l) <<= 8;             \
    (l) |= *(cp)++;        \
    (l) <<= 8;             \
    (l) |= *(cp)++;        \
  }

/*
 * Warning: PUTLONGLONG destroys its first argument.
 */
#define PUTLONGLONG(l, cp) \
  {                        \
    (cp)[7] = l;           \
    (cp)[6] = (l >>= 8);   \
    (cp)[5] = (l >>= 8);   \
    (cp)[4] = (l >>= 8);   \
    (cp)[3] = (l >>= 8);   \
    (cp)[2] = (l >>= 8);   \
    (cp)[1] = (l >>= 8);   \
    (cp)[0] = l >> 8;      \
    (cp) += 8;             \
  }
