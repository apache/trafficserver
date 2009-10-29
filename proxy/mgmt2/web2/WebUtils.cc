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

#include "ink_unused.h"  /* MAGIC_EDITING_TAG */

#include "ink_assert.h"
#include "ink_sock.h"
#include "WebUtils.h"
#include "WebGlobals.h"
#include "MgmtUtils.h"

/****************************************************************************
 *
 *  WebUtils.cc - Misc Utility Functions for the web server internface
 *
 *  
 * 
 ****************************************************************************/

#ifdef HAVE_LIBSSL

#include "openssl/ssl.h"

#ifndef _WIN32

/* Ugly hack - define HEAP_H and STACK_H to prevent stuff
 *   from the template library from being included which
 *   SUNPRO CC does not not like.  
 */
#define HEAP_H
#define STACK_H

#endif // !_WIN32

#endif // HAVE_LIBSSL

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


#define DECODE(x) printableToSixBit[(unsigned int)x]
#define MAX_PRINT_VAL 63

int
UU_decode(const char *inBuffer, int outBufSize, unsigned char *outBuffer)
{

  int inBytes = 0;
  int decodedBytes = 0;
  unsigned char *outStart = outBuffer;
  int inputBytesDecoded = 0;

  // Figure out much encoded string is really there
  while (printableToSixBit[inBuffer[inBytes]] <= MAX_PRINT_VAL) {
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

ssize_t
socket_write(SocketInfo socketD, const char *buf, size_t nbyte)
{
  if (socketD.SSLcon != NULL) {
#ifdef HAVE_LIBSSL
    return SSL_write((SSL *) socketD.SSLcon, (char *) buf, nbyte);
#else
    mgmt_fatal(stderr, "[socket_write] Attempt to use disabled SSL\n");
#endif

  } else {
    return ink_write_socket(socketD.fd, buf, nbyte);
  }
  return -1;
}

ssize_t
socket_read(SocketInfo socketD, char *buf, size_t nbyte)
{
  if (socketD.SSLcon != NULL) {
#ifdef HAVE_LIBSSL
    return SSL_read((SSL *) socketD.SSLcon, (char *) buf, nbyte);
#else
    mgmt_fatal(stderr, "[socket_read] Attempt to use disabled SSL\n");
#endif
  } else {
    return ink_read_socket(socketD.fd, buf, nbyte);
  }
  return -1;
}


// int sigfdreadln(int fd, char *s, int len)
//
//  An inefficient way to read a line from a socket
//     within the constrants of the Web Administration
//     interface
//  reads from the passed in file descriptor to a
//    new line or until all space in the buffer is exhausted
//  returns the number of characters read
//
//  Intentially, stops if the read is interrupted by a signal
//    The reaper will interupt us with a signal if we
//     are stuck
//
//  Returns -1 if the read fails
//
int
sigfdrdln(SocketInfo socketD, char *s, int len)
{
  char c;
  int result;
  char *bufStart = s;

  do {

#ifndef _WIN32
    do {
      result = socket_read(socketD, &c, 1);
    } while (result < 0 && errno == EAGAIN);
#else
    do {
      result = socket_read(socketD, &c, 1);
    } while (result == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK);
#endif

    // If we are out of bytes or there is an
    //   error, we are done
    if (result < 0 || result == 0) {
      c = '\n';
    }

    *s++ = c;
    len--;
  } while (c != '\n' && len > 1);

  if (c == '\n') {
    s--;
    *s = '\0';
  } else {
    *s = '\0';
  }

  if (result < 0) {
    return -1;
  } else {
    return (s - bufStart);
  }
}
