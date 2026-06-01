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

/*
 * Base64 encoding and decoding as according to RFC1521.  Similar to uudecode.
 *
 * RFC 1521 requires inserting line breaks for long lines.  The basic web
 * authentication scheme does not require them.  This implementation is
 * intended for web-related use, and line breaks are not implemented.
 *
 */
#include "tscore/ink_platform.h"
#include "tscore/ink_base64.h"
#include "tscore/ink_assert.h"

bool
ats_base64_encode(const unsigned char *inBuffer, size_t inBufferSize, char *outBuffer, size_t outBufSize, size_t *length)
{
  static const char _codes[66] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  char             *obuf       = outBuffer;
  char              in_tail[4];

  if (outBufSize < ats_base64_encode_dstlen(inBufferSize)) {
    return false;
  }

  while (inBufferSize > 2) {
    *obuf++ = _codes[(inBuffer[0] >> 2) & 077];
    *obuf++ = _codes[((inBuffer[0] & 03) << 4) | ((inBuffer[1] >> 4) & 017)];
    *obuf++ = _codes[((inBuffer[1] & 017) << 2) | ((inBuffer[2] >> 6) & 017)];
    *obuf++ = _codes[inBuffer[2] & 077];

    inBufferSize -= 3;
    inBuffer     += 3;
  }

  /*
   * We've done all the input groups of three chars.  We're left
   * with 0, 1, or 2 input chars.  We have to add zero-bits to the
   * right if we don't have enough input chars.
   * If 0 chars left, we're done.
   * If 1 char left, form 2 output chars, and add 2 pad chars to output.
   * If 2 chars left, form 3 output chars, add 1 pad char to output.
   */
  if (inBufferSize == 0) {
    *obuf = '\0';
    if (length) {
      *length = (obuf - outBuffer);
    }
  } else {
    memset(in_tail, 0, sizeof(in_tail));
    memcpy(in_tail, inBuffer, inBufferSize);

    *(obuf)     = _codes[(in_tail[0] >> 2) & 077];
    *(obuf + 1) = _codes[((in_tail[0] & 03) << 4) | ((in_tail[1] >> 4) & 017)];
    *(obuf + 2) = _codes[((in_tail[1] & 017) << 2) | ((in_tail[2] >> 6) & 017)];
    *(obuf + 3) = _codes[in_tail[2] & 077];

    if (inBufferSize == 1) {
      *(obuf + 2) = '=';
    }
    *(obuf + 3) = '=';
    *(obuf + 4) = '\0';

    if (length) {
      *length = (obuf + 4) - outBuffer;
    }
  }

  return true;
}

bool
ats_base64_encode(const char *inBuffer, size_t inBufferSize, char *outBuffer, size_t outBufSize, size_t *length)
{
  return ats_base64_encode(reinterpret_cast<const unsigned char *>(inBuffer), inBufferSize, outBuffer, outBufSize, length);
}

/*-------------------------------------------------------------------------
  This is a reentrant, and malloc free implementation of ats_base64_decode.
  -------------------------------------------------------------------------*/
#ifdef DECODE
#undef DECODE
#endif

#define DECODE(x)     printableToSixBit[(unsigned char)x]
#define MAX_PRINT_VAL 63

/* Converts a printable character to it's six bit representation */
const unsigned char printableToSixBit[256] = {
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 62, 64, 63, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
  64, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 63,
  64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64};

bool
ats_base64_decode(const char *inBuffer, size_t inBufferSize, unsigned char *outBuffer, size_t outBufSize, size_t *length)
{
  size_t         decodedBytes = 0;
  unsigned char *buf          = outBuffer;

  // Make sure there is sufficient space in the output buffer
  if (outBufSize < ats_base64_decode_dstlen(inBufferSize)) {
    return false;
  }

  // Ignore any trailing ='s or other undecodable characters: consume only the
  // leading run of base64-alphabet bytes.
  // TODO: Perhaps that ought to be an error instead?
  size_t inBytes = 0;
  while (inBytes < inBufferSize && DECODE(inBuffer[inBytes]) <= MAX_PRINT_VAL) {
    ++inBytes;
  }

  // Decode complete 4-character groups into 3 bytes each. Process only whole
  // groups here so the loop never reads past the alphabet prefix; the previous
  // code ran one extra iteration when inBytes was not a multiple of four (a
  // read out of bounds of the input) and then read inBuffer[-2].
  while (inBytes >= 4) {
    buf[0]        = static_cast<unsigned char>(DECODE(inBuffer[0]) << 2 | DECODE(inBuffer[1]) >> 4);
    buf[1]        = static_cast<unsigned char>(DECODE(inBuffer[1]) << 4 | DECODE(inBuffer[2]) >> 2);
    buf[2]        = static_cast<unsigned char>(DECODE(inBuffer[2]) << 6 | DECODE(inBuffer[3]));
    buf          += 3;
    inBuffer     += 4;
    decodedBytes += 3;
    inBytes      -= 4;
  }

  // Decode a trailing 2- or 3-character group; a lone trailing character does
  // not encode a full byte and is dropped (as an RFC 4648 decoder requires).
  if (inBytes >= 2) {
    buf[0]        = static_cast<unsigned char>(DECODE(inBuffer[0]) << 2 | DECODE(inBuffer[1]) >> 4);
    decodedBytes += 1;
    if (inBytes >= 3) {
      buf[1]        = static_cast<unsigned char>(DECODE(inBuffer[1]) << 4 | DECODE(inBuffer[2]) >> 2);
      decodedBytes += 1;
    }
  }

  outBuffer[decodedBytes] = '\0';

  if (length) {
    *length = decodedBytes;
  }

  return true;
}
