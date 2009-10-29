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

#ifndef _ink_base64_h_
#define _ink_base64_h_
#include "ink_apidefs.h"
/*
 * Base64 encoding and decoding as according to RFC1521.  Similar to uudecode.
 * See RFC1521 for specificiation.
 *
 * RFC 1521 requires inserting line breaks for long lines.  The basic web
 * authentication scheme does not require them.  This implementation is
 * intended for web-related use, and line breaks are not implemented.
 *
 * These routines return char*'s to malloc-ed strings.  The caller is
 * responsible for freeing the strings.
 *
 */

// These functions return xmalloc'd memory which caller needs to xfree
inkcoreapi char *ink_base64_decode(const char *input, int input_len, int *output_len);
char *ink_base64_encode(const char *input, int input_len, int *output_len);
char *ink_base64_encode_unsigned(const unsigned char *input, int input_len, int *output_len);

// Decodes into user supplied buffer.  Returns number of bytes decoded
inkcoreapi int ink_base64_decode(const char *inBuffer, int outBufSize, unsigned char *outBuffer);
int ink_base64_uuencode(const char *bufin, int nbytes, unsigned char *outBuffer);

#endif
