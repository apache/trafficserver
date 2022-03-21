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

#pragma once

/*
 * Base64 encoding and decoding as according to RFC1521.  Similar to uudecode.
 * See RFC1521 for specification.
 *
 * RFC 1521 requires inserting line breaks for long lines.  The basic web
 * authentication scheme does not require them.  This implementation is
 * intended for web-related use, and line breaks are not implemented.
 *
 * These routines return char*'s to malloc-ed strings.  The caller is
 * responsible for freeing the strings.
 *
 */
// Encodes / Decodes into user supplied buffer.  Returns number of bytes decoded
bool ats_base64_encode(const char *inBuffer, size_t inBufferSize, char *outBuffer, size_t outBufSize, size_t *length);
bool ats_base64_encode(const unsigned char *inBuffer, size_t inBufferSize, char *outBuffer, size_t outBufSize, size_t *length);

bool ats_base64_decode(const char *inBuffer, size_t inBufferSize, unsigned char *outBuffer, size_t outBufSize, size_t *length);

// Little helper functions to calculate minimum required output buffer for encoding/decoding.
// These sizes include one byte for null termination, because ats_base64_encode and ats_base64_decode will always write a null
// terminator.
inline constexpr size_t
ats_base64_encode_dstlen(size_t length)
{
  return ((length + 2) / 3) * 4 + 1;
}

inline constexpr size_t
ats_base64_decode_dstlen(size_t length)
{
  return ((length + 3) / 4) * 3 + 1;
}
