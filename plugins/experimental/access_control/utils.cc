/*
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

/**
 * @file utils.cc
 * @brief Various utility functions.
 * @see utils.h
 */

#include <cerrno>           /* errno */
#include <climits>          /* LONG_MIN, LONG_MAX */
#include <openssl/bio.h>    /* BIO I/O abstraction */
#include <openssl/buffer.h> /* buf_mem_st */
#include <openssl/err.h>    /* ERR_get_error() and ERR_error_string_n() */
#include <openssl/crypto.h>

#include "common.h"
#include "utils.h"

/**
 * @brief Parse a counted string containing a long integer
 *
 * @param s ptr to the counted string
 * @param lenght character count
 * @param val where the long integer to be stored
 * @return true - success, false - failed to parse
 */
bool
parseStrLong(const char *s, size_t length, long &val)
{
  // Make an extra copy since strtol expects NULL-terminated strings.
  char str[length + 1];
  strncpy(str, s, length);
  str[length] = 0;

  errno = 0;
  char *temp;
  val = strtol(str, &temp, 0);

  if (temp == str || *temp != '\0' || ((val == LONG_MIN || val == LONG_MAX) && errno == ERANGE)) {
    AccessControlError("Could not convert '%s' to a long integer and leftover string is: '%s'", str, temp);
    return false;
  }
  return true;
}

/* ******* Encoding/Decoding functions ******* */

/**
 * @brief Encode a character counted string into hexadecimal format
 *
 * @param in ptr to an input counted string
 * @param inLen input character count
 * @param out ptr to a buffer to store the hexadecimal string
 * @param outLen output character count (2 * inLen + 1)
 * @return the number of character actually added to the output buffer.
 */
size_t
hexEncode(const char *in, size_t inLen, char *out, size_t outLen)
{
  const char *src    = in;
  const char *srcEnd = in + inLen;
  char *dst          = out;
  char *dstEnd       = out + outLen;

  while (src < srcEnd && dst < dstEnd && 2 == sprintf(dst, "%02x", (unsigned char)*src)) {
    dst += 2;
    src++;
  }
  return dst - out;
}

/**
 * @brief Convert character containing [0-1], [A-F], [a-f] to unsigned char.
 *
 * @param c character to be converted.
 * @return the unsigned character if success or FF if failure.
 */
static unsigned char
hex2uchar(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return 255;
}

/**
 * @brief Decode from hexadecimal format into character counted string
 *
 * @param in ptr to an input counted string in hexadecimal format
 * @param inLen input character count
 * @param out ptr to a buffer to store the decoded counted string
 * @param outLen output character count (inLen/2)
 * @return the number of character actually added to the output buffer.
 */
size_t
hexDecode(const char *in, size_t inLen, char *out, size_t outLen)
{
  const char *src    = in;
  const char *srcEnd = in + inLen;
  char *dst          = out;
  char *dstEnd       = out + outLen;

  while (src < (srcEnd - 1) && dst < dstEnd) {
    *dst++ = hex2uchar(*src) << 4 | hex2uchar(*(src + 1));
    src += 2;
  }
  return dst - out;
}

/**
 * @brief URL(percent)-encode a counted string
 *
 * @param in ptr to an input decoded counted string
 * @param inLen input character count
 * @param out ptr to an output buffer where the encoded string will be stored.
 * @param outLen output character count (output max size, should be 3 x inLen + 1)
 * @return the number of character actually added to the output buffer.
 */
size_t
urlEncode(const char *in, size_t inLen, char *out, size_t outLen)
{
  const char *src = in;
  char *dst       = out;
  while ((size_t)(src - in) < inLen && (size_t)(dst - out) < outLen) {
    if (isalnum(*src) || *src == '-' || *src == '_' || *src == '.' || *src == '~') {
      *dst++ = *src;
    } else if (*src == ' ') {
      *dst++ = '+';
    } else {
      *dst++ = '%';
      sprintf(dst, "%02x", (unsigned char)*src);
      dst += 2;
    }
    src++;
  }
  return dst - out;
}

/**
 * @brief URL(percent)-decode a counted string
 *
 * @param in ptr to an input encoded counted string
 * @param inLen input character count
 * @param out ptr to an output buffer where the decoded string will be stored.
 * @param outLen output character count (output max size, should be inLen + 1)
 * @return the number of character actually added to the output buffer.
 */
size_t
urlDecode(const char *in, size_t inLen, char *out, size_t outLen)
{
  const char *src = in;
  char *dst       = out;
  while ((size_t)(src - in) < inLen && (size_t)(dst - out) < outLen) {
    if (*src == '%') {
      if (src[1] && src[2]) {
        int u  = hex2uchar(*(src + 1)) << 4 | hex2uchar(*(src + 2));
        *dst++ = (char)u;
        src += 2;
      }
    } else if (*src == '+') {
      *dst++ = ' ';
    } else {
      *dst++ = *src;
    }
    src++;
  }
  return dst - out;
}

/* ******* Functions using OpenSSL library ******* */

void
cryptoMagicInit()
{
#if OPENSSL_VERSION_NUMBER < 0x10100000L
  OpenSSL_add_all_digests(); /* needed for EVP_get_digestbyname() */
#endif
}

void
cryptoMagicCleanup()
{
#if OPENSSL_VERSION_NUMBER < 0x10100000L
  EVP_cleanup();
#endif
}

/**
 * @brief a helper function to get a human-readable error message in a buffer.
 *
 * @param buffer pointer to a char buffer
 * @param bufferLen - max buffer length (length should be >= 256)
 * @return buffer, filled with the error message (null-terminated)
 */
static char *
cryptoErrStr(char *buffer, size_t bufferLen)
{
  /* man ERR_error_string expects 256-byte buffer */
  if (nullptr == buffer || 256 > bufferLen) {
    return nullptr;
  }
  unsigned long err = ERR_get_error();
  if (0 == err) {
    buffer[0] = 0;
    return buffer;
  }
  ERR_error_string_n(err, buffer, bufferLen);
  return buffer;
}

/**
 * @brief Calculate message digest
 *
 * @param digestType digest name
 * @param data ptr to input message for calculating the digest
 * @param dataLen message length
 * @param key ptr to a counted string containing the key (secret)
 * @param keyLen key length
 * @param out ptr to where to store the digest
 * @param outLen length of the out buffer (must be at least MAX_MSGDIGEST_BUFFER_SIZE)
 * @return the number of character actually written to the buffer.
 */
size_t
cryptoMessageDigestGet(const char *digestType, const char *data, size_t dataLen, const char *key, size_t keyLen, char *out,
                       size_t outLen)
{
  EVP_MD_CTX *ctx  = nullptr;
  const EVP_MD *md = nullptr;
  EVP_PKEY *pkey   = nullptr;
  size_t len       = outLen;
  char buffer[256];

  size_t result = 0;
  if (!(ctx = EVP_MD_CTX_create())) {
    AccessControlError("failed to create EVP message digest context: %s", cryptoErrStr(buffer, sizeof(buffer)));
  } else {
#ifndef OPENSSL_IS_BORINGSSL
    if (!(pkey = EVP_PKEY_new_mac_key(EVP_PKEY_HMAC, nullptr, (const unsigned char *)key, keyLen))) {
#else
    if (false) {
#endif
      AccessControlError("failed to create EVP private key. %s", cryptoErrStr(buffer, sizeof(buffer)));
      EVP_MD_CTX_destroy(ctx);
    } else {
      do {
        if (!(md = EVP_get_digestbyname(digestType))) {
          AccessControlError("failed to get digest by name %s. %s", digestType, cryptoErrStr(buffer, sizeof(buffer)));
          break;
        }

        if (1 != EVP_DigestSignInit(ctx, nullptr, md, nullptr, pkey)) {
          AccessControlError("failed to set up signing context. %s", cryptoErrStr(buffer, sizeof(buffer)));
          break;
        }

        if (1 != EVP_DigestSignUpdate(ctx, data, dataLen)) {
          AccessControlError("failed to update the signing hash. %s", cryptoErrStr(buffer, sizeof(buffer)));
          break;
        }

        if (1 != EVP_DigestSignFinal(ctx, (unsigned char *)out, &len)) {
          AccessControlError("failed to finalize the signing hash. %s", cryptoErrStr(buffer, sizeof(buffer)));
        }

        /* success */
        result = len;
      } while (false);

      EVP_PKEY_free(pkey);
      EVP_MD_CTX_destroy(ctx);
    }
  }

  return result;
}

/**
 * @brief Check if 2 message digests are the same using a constant-time openssl function to avoid timing attacks.
 *
 * @param md1 first message digest to compare
 * @param md1Len md1 length
 * @param md2 first message digest to compare
 * @param md2Len md2 length
 * @return true - the same, false - different
 */
bool
cryptoMessageDigestEqual(const char *md1, size_t md1Len, const char *md2, size_t md2Len)
{
  if (md1Len != md2Len) {
    return false;
  }
  if (0 == CRYPTO_memcmp((const void *)md1, (const void *)md2, md1Len)) {
    /* Verification success */
    return true;
  }
  /* Verify failures */
  return false;
}

/**
 * @brief Calculates the size of the output buffer needed to base64 encode a message
 *
 * @param decodedSize the size of the message to encode.
 * @return output buffer size
 */
size_t
cryptoBase64EncodedSize(size_t decodedSize)
{
  return (((4 * decodedSize) / 3) + 3) & ~3;
}

/**
 * @brief Calculates the size of the output buffer needed to base64 decode a message
 *
 * @param messageSize the size of the message to decode.
 * @return output buffer size
 */
size_t
cryptoBase64DecodeSize(const char *encoded, size_t encodedLen)
{
  if (nullptr == encoded || 0 == encodedLen) {
    return 0;
  }

  size_t padding  = 0;
  const char *end = encoded + encodedLen;

  if ('=' == *(--end)) {
    padding++;
  }

  if ('=' == *(--end)) {
    padding++;
  }

  return (3 * encodedLen) / 4 - padding;
}

/**
 * @brief Base64 encode
 *
 * @param in input buffer to encode
 * @param inLen input buffer length
 * @param output buffer
 * @param output buffer length
 * @return number of character actually written to the output buffer.
 */
size_t
cryptoBase64Encode(const char *in, size_t inLen, char *out, size_t outLen)
{
  if ((nullptr == in) || (0 == inLen) || (nullptr == out) || 0 == outLen) {
    return 0;
  }

  BIO *head, *b64, *bmem;
  BUF_MEM *bptr;
  size_t len = 0;

  head = b64 = BIO_new(BIO_f_base64());
  if (nullptr != b64) {
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bmem = BIO_new(BIO_s_mem());
    if (nullptr != bmem) {
      head = BIO_push(b64, bmem);

      BIO_write(head, in, inLen);
      (void)BIO_flush(head);

      BIO_get_mem_ptr(head, &bptr);
      len = bptr->length < outLen ? bptr->length : outLen;
      strncpy(out, bptr->data, len);
    }
    BIO_free_all(head);
  }
  return len;
}

/**
 * @brief Base64 decode
 *
 * @param in input buffer to encode
 * @param inLen input buffer length
 * @param output buffer
 * @param output buffer length
 * @return number of character actually written to the output buffer.
 */
size_t
cryptoBase64Decode(const char *in, size_t inLen, char *out, size_t outLen)
{
  if ((nullptr == in) || (0 == inLen) || (nullptr == out) || 0 == outLen) {
    return 0;
  }

  BIO *head, *bmem, *b64;
  size_t len = 0;

  head = b64 = BIO_new(BIO_f_base64());
  if (nullptr != b64) {
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bmem = BIO_new_mem_buf((void *)in, inLen);
    if (nullptr != bmem) {
      head = BIO_push(b64, bmem);
      len  = BIO_read(head, out, outLen);
    }
    BIO_free_all(head);
  }

  return len;
}

/**
 * For more information see wikipedia|http://en.wikipedia.org/wiki/Base64 and
 * RFC 7515 |https://tools.ietf.org/html/rfc7515#appendix-C
 */
size_t
cryptoModifiedBase64Encode(const char *in, size_t inLen, char *out, size_t outLen)
{
  size_t len = cryptoBase64Encode(in, inLen, out, outLen);

  char *cur            = out;
  const char *end      = out + len;
  const char *padStart = out + len;
  bool foundPadStart   = false;
  while (cur < end) {
    if ('+' == *cur) {
      *cur = '-';
    } else if ('/' == *cur) {
      *cur = '_';
    } else if (*cur == '=' && !foundPadStart) {
      padStart      = cur;
      foundPadStart = true;
    }
    cur++;
  }
  return padStart - out;
}

/**
 * For more information see wikipedia: http://en.wikipedia.org/wiki/Base64 and
 * RFC 7515 |https://tools.ietf.org/html/rfc7515#appendix-C
 */
size_t
cryptoModifiedBase64Decode(const char *in, size_t inLen, char *out, size_t outLen)
{
  size_t bufferLen = inLen;
  switch (inLen % 4) {
  case 0: /* no padding */
    break;
  case 2: /* need space for '==' */
    bufferLen += 2;
    break;
  case 3: /* need space for '=' */
    bufferLen += 1;
    break;
  case 4:     /* malformed base64 */
    return 0; /* nothing will be written to the output buffer */
    break;
  }

  /* Since 'in' would like to be unmodifiable to add the padding will need a copy */
  const char *cur = in;
  const char *end = in + inLen;
  char buffer[bufferLen];
  char *dst = buffer;
  while (cur < end) {
    if ('-' == *cur) {
      *dst++ = '+';
    } else if ('_' == *cur) {
      *dst++ = '/';
    } else {
      *dst++ = *cur;
    }
    cur++;
  }

  /* Add the padding '=' to the end of the buffer */
  while (dst < buffer + bufferLen) {
    *dst++ = '=';
  }

  return cryptoBase64Decode(buffer, bufferLen, out, outLen);
}
