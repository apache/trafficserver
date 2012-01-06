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
 * These routines return char*'s to malloc-ed strings.  The caller is
 * responsible for freeing the strings.
 */
#include "libts.h"

#include "ink_assert.h"
#include "ink_bool.h"
#include "ink_resource.h"
#include "ink_unused.h"


// TODO: This code is not paritcularly "uniform", we ought to make them
// more the same coding style (the uuencode one seems good'ish).


size_t
ats_base64_encode(const unsigned char *inBuffer, size_t inBufferSize, char *outBuffer, size_t outBufSize)
{
  static const char _codes[66] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  char *obuf = outBuffer;
  char in_tail[4];

  if (outBufSize < ATS_BASE64_ENCODE_DSTLEN(inBufferSize))
    return 0;

  while (inBufferSize > 2) {
    *obuf++ = _codes[(inBuffer[0] >> 2) & 077];
    *obuf++ = _codes[((inBuffer[0] & 03) << 4) | ((inBuffer[1] >> 4) & 017)];
    *obuf++ = _codes[((inBuffer[1] & 017) << 2) | ((inBuffer[2] >> 6) & 017)];
    *obuf++ = _codes[inBuffer[2] & 077];

    inBufferSize -= 3;
    inBuffer += 3;
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
    return (obuf - outBuffer);
  } else {
    memset(in_tail, 0, sizeof(in_tail));
    memcpy(in_tail, inBuffer, inBufferSize);

    *(obuf) = _codes[(in_tail[0] >> 2) & 077];
    *(obuf + 1) = _codes[((in_tail[0] & 03) << 4) | ((in_tail[1] >> 4) & 017)];
    *(obuf + 2) = _codes[((in_tail[1] & 017) << 2) | ((in_tail[2] >> 6) & 017)];
    *(obuf + 3) = _codes[in_tail[2] & 077];

    if (inBufferSize == 1)
      *(obuf + 2) = '=';
    *(obuf + 3) = '=';
    *(obuf + 4) = '\0';

    return ((obuf + 4) - outBuffer);
  }
}

size_t
ats_base64_encode(const char *inBuffer, size_t inBufferSize, char *outBuffer, size_t outBufSize)
{
  return ats_base64_encode((const unsigned char *)inBuffer, inBufferSize, outBuffer, outBufSize);
}


/*-------------------------------------------------------------------------
  This is a reentrant, and malloc free implemetnation of ats_base64_decode.
  -------------------------------------------------------------------------*/
#ifdef DECODE
#undef DECODE
#endif

#define DECODE(x) printableToSixBit[(unsigned char)x]
#define MAX_PRINT_VAL 63

/* Converts a printable character to it's six bit representation */
const unsigned char printableToSixBit[256] = {
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
  52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64, 64, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64, 64, 26, 27,
  28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64
};

size_t
ats_base64_decode(const char *inBuffer, size_t inBufferSize, unsigned char *outBuffer, size_t outBufSize)
{
  size_t decodedBytes = 0;
  unsigned char *outStart = outBuffer;
  int inputBytesDecoded = 0;

  // Make sure there is sufficient space in the output buffer
  if (outBufSize < ATS_BASE64_DECODE_DSTLEN(inBufferSize))
    return 0;

  for (size_t i = 0; i < inBufferSize; i += 4) {
    outBuffer[0] = (unsigned char) (DECODE(inBuffer[0]) << 2 | DECODE(inBuffer[1]) >> 4);
    outBuffer[1] = (unsigned char) (DECODE(inBuffer[1]) << 4 | DECODE(inBuffer[2]) >> 2);
    outBuffer[2] = (unsigned char) (DECODE(inBuffer[2]) << 6 | DECODE(inBuffer[3]));

    outBuffer += 3;
    inBuffer += 4;
    decodedBytes += 3;
    inputBytesDecoded += 4;
  }

  // Check to see if we decoded a multiple of 4 four
  //    bytes
  if ((inBufferSize - inputBytesDecoded) & 0x3) {
    if (DECODE(inBuffer[-2]) > MAX_PRINT_VAL) {
      decodedBytes -= 2;
    } else {
      decodedBytes -= 1;
    }
  }
  outStart[decodedBytes] = '\0';

  return decodedBytes;
}
