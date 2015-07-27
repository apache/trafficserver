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

#ifndef _ink_code_h_
#define _ink_code_h_

#include "ts/ink_apidefs.h"
#include "ts/ink_defs.h"
#include <openssl/md5.h>

/* INK_MD5 context. */
typedef MD5_CTX INK_DIGEST_CTX;

/*
  Wrappers around the MD5 functions, all of this should be depericated and just use the functions directly
*/

inkcoreapi int ink_code_md5(unsigned char const *input, int input_length, unsigned char *sixteen_byte_hash_pointer);
inkcoreapi char *ink_code_md5_stringify(char *dest33, const size_t destSize, const char *md5);
inkcoreapi char *ink_code_to_hex_str(char *dest33, uint8_t const *md5);

inkcoreapi int ink_code_incr_md5_init(INK_DIGEST_CTX *context);
inkcoreapi int ink_code_incr_md5_update(INK_DIGEST_CTX *context, const char *input, int input_length);
inkcoreapi int ink_code_incr_md5_final(char *sixteen_byte_hash_pointer, INK_DIGEST_CTX *context);
#endif
