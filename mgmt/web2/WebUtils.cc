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

#include "ink_config.h"

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

#include "openssl/ssl.h"

ssize_t
socket_write(SocketInfo socketD, const char *buf, size_t nbyte)
{
  if (socketD.SSLcon != NULL) {
    return SSL_write((SSL *) socketD.SSLcon, (char *) buf, nbyte);
  } else {
    return write_socket(socketD.fd, buf, nbyte);
  }
  return -1;
}

ssize_t
socket_read(SocketInfo socketD, char *buf, size_t nbyte)
{
  if (socketD.SSLcon != NULL) {
    return SSL_read((SSL *) socketD.SSLcon, (char *) buf, nbyte);
  } else {
    return read_socket(socketD.fd, buf, nbyte);
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

    do {
      result = socket_read(socketD, &c, 1);
    } while (result < 0 && errno == EAGAIN);

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
