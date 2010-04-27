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

/****************************************************************************
 *
 *  MgmtServerRPC.cc 
 * 
 *  Deals with Traffic Manager communicating with Traffic Server's
 *  RAF port
 *
 *  
 *
 ****************************************************************************/

#include "inktomi++.h"
#include "ink_platform.h"
#include "ink_unused.h"        /* MAGIC_EDITING_TAG */
#include "ink_sock.h"

#include "MgmtServerRPC.h"
#include "LocalManager.h"
#include "CoreAPIShared.h"

// --------------------------------------------------------------------------
// defines
// --------------------------------------------------------------------------
#define SIZE_RESPONSE      1024


// NOTE:  
// Usually identifiers in messages are used to identify RAF requests; 
// however, there is only one type of request being sent and per connection, 
// so to keep it simple for now use the same id for all requests

// --------------------------------------------------------------------------
// raf_writen
// --------------------------------------------------------------------------
//   Simple, inefficient, write line function. Takes a fd to write to, an
// unsigned char * containing the data, and the number of bytes to write. 
// Returns:    num bytes not written
//              -1  error
ssize_t
raf_writen(int fd, const char *ptr, size_t n)
{
  size_t nleft;
  ssize_t nwritten;

  nleft = n;
  while (nleft > 0) {
    if ((nwritten = ink_write_socket(fd, ptr, nleft)) <= 0) {
      if (errno == EINTR)
        nwritten = 0;           /* and call write() again */
      else
        return (-1);            /* error */
    }

    nleft -= nwritten;
    ptr += nwritten;
  }
  return (n);
}


// --------------------------------------------------------------------------
// raf_readn
// --------------------------------------------------------------------------
//   Simple, inefficient, read line function. Takes a fd to read from,
// an unsigned char * to write into, and a max len to read.  Reads until
// the max length is encountered or a newline character is found
// Returns:  num bytes read
//          -1  error
ssize_t
raf_readn(int fd, char *ptr, size_t n)
{
  size_t nleft;
  ssize_t nread;

  nleft = n;
  while (nleft > 0) {
    if ((nread = ink_read_socket(fd, ptr, nleft)) < 0) {
      if (errno == EINTR)
        nread = 0;              /* and call read() again */
      else
        return (-1);
    } else if (nread == 0)
      break;                    /* EOF */

    nleft -= nread;
    ptr += nread;

    if (*(ptr - 1) == '\n')
      break;                    /* check if newline character */
  }
  return (n - nleft);           /* return >= 0 */
}

// --------------------------------------------------------------------------
// send_exit_request
// --------------------------------------------------------------------------
// Input: the file descriptor to send the "0 exit" request to; 
//        will close the fd !!!!
// Output: returns 0 if exit successful 
//         returns -1 if error code is non-zero 
// 
int
send_exit_request(int fd)
{
  int r, len, argNum;
  char *curPtr;
  char response[SIZE_RESPONSE];
  const char exit_msg[] = "0 exit\n";

  raf_writen(fd, exit_msg, strlen(exit_msg));

  memset(response, 0, SIZE_RESPONSE);
  while ((r = raf_readn(fd, response, SIZE_RESPONSE)) >= 0) {
    if (r < SIZE_RESPONSE) {
      break;
    }
  }
  close(fd);

  // remove the newline character at end
  len = strlen(response);
  if (response[len - 1] == '\n')
    response[len - 1] = '\0';

  // check for valid error code
  curPtr = response;
  argNum = 0;
  while ((curPtr = strchr(curPtr, ' '))) {
    curPtr++;                   // skip whitespace
    if (argNum == 0)            // check error code
      if (*curPtr != '0')
        goto Lerror;
    if (argNum == 1)
      break;
    argNum++;
  }

  return 0;

Lerror:
  return -1;
}

// --------------------------------------------------------------------------
// send_cli_congest_request
// --------------------------------------------------------------------------
// Sends the arguments in a RAF request to the Traffic Server RAF port. 
// Input: arguments - follows the "id congest ...." of RAF request
// Output: returns the socket file descriptor to read the RAF response from
// 
int
send_cli_congest_request(const char *arguments)
{
  int s, connect_result;
  bool found;
  struct sockaddr_in servaddr;
  char request[257];

  // get traffic server's raf port
  RecInt port;
  int rec_err = RecGetRecordInt("proxy.config.raf.port", &port);
  found = (rec_err == REC_ERR_OKAY);
  if (!found || (int) port <= 0) {
    Debug("raf", "[send_cli_congest_request] raf port unspecified");
    return -1;
  }
  // connects to traffic server on the TS RAF port
  s = socket(AF_INET, SOCK_STREAM, 0);
  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
  servaddr.sin_port = htons((int) port);

  // connect will also fail if RAF is not enabled for TS
  connect_result = connect(s, (struct sockaddr *) &servaddr, sizeof(servaddr));
  if (connect_result != 0) {
    Debug("raf", "[send_cli_congest_request] connect failed (%d)", connect_result);
    return -1;
  }
  // send RAF query request 
  memset(request, 0, 257);
  ink_snprintf(request, 256, "0 congest %s\n", arguments);
  raf_writen(s, request, strlen(request));

  return s;
}
