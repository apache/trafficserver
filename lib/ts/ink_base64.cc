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
#include "inktomi++.h"

#include "ink_assert.h"
#include "ink_bool.h"
#include "ink_resource.h"
#include "ink_unused.h"

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

#ifdef decode
# undef decode
#endif
#define decode(A) ((unsigned int)codes[(unsigned char)input[A]])

// NOTE: ink_base64_decode returns xmalloc'd memory

char *
ink_base64_decode(const char *input, int input_len, int *output_len)
{
  char *output;
  char *obuf;
  static bool initialized = FALSE;
  static char codes[256];
  int cc = 0;
  int len;
  int i;

  if (!initialized) {
    /* Build translation table */
    for (i = 0; i < 256; i++)
      codes[i] = 0;
    for (i = 'A'; i <= 'Z'; i++)
      codes[i] = cc++;
    for (i = 'a'; i <= 'z'; i++)
      codes[i] = cc++;
    for (i = '0'; i <= '9'; i++)
      codes[i] = cc++;
    codes[0 + '+'] = cc++;
    codes[0 + '/'] = cc++;
    initialized = TRUE;
  }
  // compute ciphertext length
  for (len = 0; len < input_len && input[len] != '='; len++);

  output = obuf = (char *) xmalloc((len * 6) / 8 + 4);
  ink_assert(output != NULL);

  while (len > 0) {
    *output++ = decode(0) << 2 | decode(1) >> 4;
    *output++ = decode(1) << 4 | decode(2) >> 2;
    *output++ = decode(2) << 6 | decode(3);
    len -= 4;
    input += 4;
  }

  /*
   * We don't need to worry about leftover bits because
   * we've allocated a few extra characters and if there
   * are leftover bits they will be zeros because the extra
   * inputs will be '='s and '=' decodes to 0.
   */

  *output = '\0';
  *output_len = (int) (output - obuf);
  return obuf;
}

#undef decode

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

// NOTE: ink_base64_encode returns xmalloc'd memory

char *
ink_base64_encode(const char *input, int input_len, int *output_len)
{
  return ink_base64_encode_unsigned((const unsigned char *) input, input_len, output_len);
}

char *
ink_base64_encode_unsigned(const unsigned char *input, int input_len, int *output_len)
{
  char *output;
  char *obuf;
  static char codes[66] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  char in_tail[4];
  int len;

  len = input_len;

  output = obuf = (char *) xmalloc((len * 8) / 6 + 4);
  ink_assert(output != NULL);

  while (len > 2) {
    *output++ = codes[(input[0] >> 2) & 077];

    *output++ = codes[((input[0] & 03) << 4)
                      | ((input[1] >> 4) & 017)];

    *output++ = codes[((input[1] & 017) << 2)
                      | ((input[2] >> 6) & 017)];

    *output++ = codes[input[2] & 077];

    len -= 3;
    input += 3;
  }

  /*
   * We've done all the input groups of three chars.  We're left
   * with 0, 1, or 2 input chars.  We have to add zero-bits to the
   * right if we don't have enough input chars.
   * If 0 chars left, we're done.
   * If 1 char left, form 2 output chars, and add 2 pad chars to output.
   * If 2 chars left, form 3 output chars, add 1 pad char to output.
   */

  if (len == 0) {
    *output_len = (int) (output - obuf);
    *output = '\0';
    return obuf;
  } else {
    memset(in_tail, 0, sizeof(in_tail));
    memcpy(in_tail, input, len);

    *(output) = codes[(in_tail[0] >> 2) & 077];

    *(output + 1) = codes[((in_tail[0] & 03) << 4)
                          | ((in_tail[1] >> 4) & 017)];

    *(output + 2) = codes[((in_tail[1] & 017) << 2)
                          | ((in_tail[2] >> 6) & 017)];

    *(output + 3) = codes[in_tail[2] & 077];

    if (len == 1)
      *(output + 2) = '=';

    *(output + 3) = '=';

    *(output + 4) = '\0';

    *output_len = (int) ((output + 4) - obuf);
    return obuf;
  }
}


/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

// int ink_base64_decode()
//
// The above functions require malloc'ing buffer which isn't good for
//   Traffic Server performance and the the decode routine is not
//   reentrant.  The following function fixes both those problems

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


#ifdef DECODE
#undef DECODE
#endif

#define DECODE(x) printableToSixBit[(unsigned char)x]
#define MAX_PRINT_VAL 63

int
ink_base64_decode(const char *inBuffer, int outBufSize, unsigned char *outBuffer)
{

  int inBytes = 0;
  int decodedBytes = 0;
  unsigned char *outStart = outBuffer;
  int inputBytesDecoded = 0;

  // Figure out much encoded string is really there
  while (printableToSixBit[(uint8)inBuffer[inBytes]] <= MAX_PRINT_VAL) {
    inBytes++;
  }

  // Make sure there is sufficient space in the output buffer
  //   if not shorten the number of bytes in
  if ((((inBytes + 3) / 4) * 3) > outBufSize - 1) {
    inBytes = ((outBufSize - 1) * 4) / 3;
  }

  for (int i = 0; i < inBytes; i += 4) {

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
  if ((inBytes - inputBytesDecoded) & 0x3) {
    if (DECODE(inBuffer[-2]) > MAX_PRINT_VAL) {
      decodedBytes -= 2;
    } else {
      decodedBytes -= 1;
    }
  }

  outStart[decodedBytes] = '\0';

  return decodedBytes;
}


char six2pr[64] = {
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
  'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
  'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
  'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
  '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'
};

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
ink_base64_uuencode(const char *bufin, int nbytes, unsigned char *outBuffer)
{

  int i;

  for (i = 0; i < nbytes; i += 3) {
    *(outBuffer++) = six2pr[*bufin >> 2];       /* c1 */
    *(outBuffer++) = six2pr[((*bufin << 4) & 060) | ((bufin[1] >> 4) & 017)];   /*c2 */
    *(outBuffer++) = six2pr[((bufin[1] << 2) & 074) | ((bufin[2] >> 6) & 03)];  /*c3 */
    *(outBuffer++) = six2pr[bufin[2] & 077];    /* c4 */

    bufin += 3;
  }

  /* If nbytes was not a multiple of 3, then we have encoded too
   * many characters.  Adjust appropriately.
   */
  if (i == nbytes + 1) {
    /* There were only 2 bytes in that last group */
    outBuffer[-1] = '=';
  } else if (i == nbytes + 2) {
    /* There was only 1 byte in that last group */
    outBuffer[-1] = '=';
    outBuffer[-2] = '=';
  }

  *outBuffer = '\0';

  return TRUE;
}
