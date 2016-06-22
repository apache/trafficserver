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
 *  WebIntrMain.cc - main loop for the Web Interface
 *
 ****************************************************************************/

#include "ts/ink_platform.h"
#include "ts/ink_sock.h"
#include "ts/I_Layout.h"
#include "LocalManager.h"
#include "Alarms.h"
#include "MgmtUtils.h"
#include "MgmtSocket.h"
#include "NetworkUtilsRemote.h"
#include "MIME.h"
#include "Cop.h"

// INKqa09866
#include "TSControlMain.h"
#include "EventControlMain.h"

int aconf_port_arg = -1;

//  fd newTcpSocket(int port)
//
//  returns a file descriptor associated with a new socket
//    on the specified port
//
//  If the socket could not be created, returns -1
//
//  Thread Safe: NO!  Call only from main Web interface thread
//
static int
newTcpSocket(int port)
{
  struct sockaddr_in socketInfo;
  int socketFD;
  int one = 1;

  memset(&socketInfo, 0, sizeof(sockaddr_in));

  // Create the new TCP Socket
  if ((socketFD = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    mgmt_fatal(stderr, errno, "[newTcpSocket]: %s", "Unable to Create Socket\n");
    return -1;
  }
  // Specify our port number is network order
  memset(&socketInfo, 0, sizeof(socketInfo));
  socketInfo.sin_family      = AF_INET;
  socketInfo.sin_port        = htons(port);
  socketInfo.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  // Allow for immediate re-binding to port
  if (setsockopt(socketFD, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(int)) < 0) {
    mgmt_fatal(stderr, errno, "[newTcpSocket] Unable to set socket options.\n");
  }
  // Bind the port to the socket
  if (bind(socketFD, (sockaddr *)&socketInfo, sizeof(socketInfo)) < 0) {
    mgmt_elog(stderr, 0, "[newTcpSocket] Unable to bind port %d to socket: %s\n", port, strerror(errno));
    close_socket(socketFD);
    return -1;
  }
  // Listen on the new socket
  if (listen(socketFD, 5) < 0) {
    mgmt_elog(stderr, errno, "[newTcpSocket] %s\n", "Unable to listen on the socket");
    close_socket(socketFD);
    return -1;
  }
  // Set the close on exec flag so our children do not
  //  have this socket open
  if (fcntl(socketFD, F_SETFD, 1) < 0) {
    mgmt_elog(stderr, errno, "[newTcpSocket] Unable to set close on exec flag\n");
  }

  return socketFD;
}

bool
api_socket_is_restricted()
{
  RecInt intval;

  // If the socket is not administratively restricted, check whether we have platform
  // support. Otherwise, default to making it restricted.
  if (RecGetRecordInt("proxy.config.admin.api.restricted", &intval) == REC_ERR_OKAY) {
    if (intval == 0) {
      return !mgmt_has_peereid();
    }
  }

  return true;
}

static const char SyntheticResponse[] = "HTTP/1.0 200 OK\r\n"
                                        "Server: Traffic Manager\r\n"
                                        "Date: %s\r\n"
                                        "Cache-Control: no-store\r\n"
                                        "Pragma: no-cache\r\n"
                                        "Content-type: text/plain\r\n"
                                        "Content-Length: %d\r\n\r\n%s%s%s";

static const char SyntheticData[] = "abcdefghijklmnopqrstuvwxyz\r\n"
                                    "abcdefghijklmnopqrstuvwxyz\r\n"
                                    "abcdefghijklmnopqrstuvwxyz\r\n"
                                    "abcdefghijklmnopqrstuvwxyz\r\n"
                                    "abcdefghijklmnopqrstuvwxyz\r\n"
                                    "abcdefghijklmnopqrstuvwxyz\r\n"
                                    "abcdefghijklmnopqrstuvwxyz\r\n"
                                    "abcdefghijklmnopqrstuvwxyz\r\n"
                                    "abcdefghijklmnopqrstuvwxyz\r\n"
                                    "abcdefghijklmnopqrstuvwxyz\r\n"
                                    "abcdefghijklmnopqrstuvwxyz\r\n"
                                    "abcdefghijklmnopqrstuvwxyz\r\n"
                                    "abcdefghijklmnopqrstuvwxyz\r\n"
                                    "abcdefghijklmnopqrstuvwxyz\r\n"
                                    "abcdefghijklmnopqrstuvwxyz\r\n"
                                    "abcdefghijklmnopqrstuvwxyz\r\n"
                                    "abcdefghijklmnopqrstuvwxyz\r\n"
                                    "abcdefghijklmnopqrstuvwxyz\r\n"
                                    "abcdefghijklmnopqrstuvwxyz\r\n"
                                    "abcdefghijklmnopqrstuvwxyz\r\n";

static const char RequestStr[] = "GET /synthetic.txt HTTP/1"; // Minimum viable request that we support

void *
synthetic_thread(void *info)
{
  char buffer[4096];
  char dateBuf[128];
  char *bufp;
  int clientFD = *(int *)info;
  ssize_t bytes;
  size_t len = 0;

  // Read the request
  bufp = buffer;
  while (len < strlen(RequestStr)) {
    if (read_ready(clientFD, cop_server_timeout * 1000) <= 0) {
      mgmt_log(stderr, "[SyntheticHealthServer] poll() failed, no request to read()");
      goto error;
    }
    bytes = read(clientFD, buffer, sizeof(buffer));
    if (0 == bytes) {
      mgmt_log(stderr, "[SyntheticHealthServer] EOF on the socket, likely prematurely closed");
      goto error;
    } else if (bytes < 0) {
      if (errno == EINTR || errno == EAGAIN) {
        continue;
      } else {
        mgmt_log(stderr, "[SyntheticHealthServer] Failed to read the request");
        goto error;
      }
    } else {
      len += bytes;
      bufp += bytes;
    }
  }

  // Bare minimum check if the request looks reasonable (i.e. from traffic_cop)
  if (len < strlen(RequestStr) || 0 != strncasecmp(buffer, RequestStr, strlen(RequestStr))) {
    mgmt_log(stderr, "[SyntheticHealthServer] Unsupported request provided");
    goto error;
  }

  // Format the response
  mime_format_date(dateBuf, time(NULL));
  len = snprintf(buffer, sizeof(buffer), SyntheticResponse, dateBuf, (int)strlen(SyntheticData) * 3, SyntheticData, SyntheticData,
                 SyntheticData);

  // Write it
  bufp = buffer;
  while (len) {
    if (write_ready(clientFD, cop_server_timeout * 1000) <= 0) {
      mgmt_log(stderr, "[SyntheticHealthServer] poll() failed, no response to write()");
      goto error;
    }
    bytes = write(clientFD, buffer, len);
    if (bytes < 0) {
      if (errno == EINTR || errno == EAGAIN) {
        continue;
      } else {
        mgmt_log(stderr, "[SyntheticHealthServer] Failed to write the response");
        goto error;
      }
    } else {
      len -= bytes;
      bufp += bytes;
    }
  }

error:
  close_socket(clientFD);
  ink_thread_exit(NULL);

  return NULL;
}

void *
mgmt_synthetic_main(void *)
{
  int autoconfFD = -1; // FD for incoming autoconf connections
  int clientFD   = -1; // FD for accepted connections
  int publicPort = -1; // Port for incoming autoconf connections

#if !defined(linux)
  sigset_t allSigs; // Set of all signals
#endif

  RecInt tempInt;
  bool found;

#if !defined(linux)
  // Start by blocking all signals
  sigfillset(&allSigs);
  ink_thread_sigsetmask(SIG_SETMASK, &allSigs, NULL);
#endif

  if (aconf_port_arg > 0) {
    publicPort = aconf_port_arg;
  } else {
    found = (RecGetRecordInt("proxy.config.admin.synthetic_port", &tempInt) == REC_ERR_OKAY);
    ink_release_assert(found);
    publicPort = (int)tempInt;
  }
  Debug("ui", "[WebIntrMain] Starting Client AutoConfig Server on Port %d", publicPort);

  if ((autoconfFD = newTcpSocket(publicPort)) < 0) {
    mgmt_elog(stderr, errno, "[WebIntrMain] Unable to start client autoconf server\n");
    lmgmt->alarm_keeper->signalAlarm(MGMT_ALARM_WEB_ERROR, "Healthcheck service failed to initialize");
  }

  while (1) {
    struct sockaddr_in clientInfo; // Info about client connection
    socklen_t addrLen = sizeof(clientInfo);

    ink_zero(clientInfo);
    if ((clientFD = mgmt_accept(autoconfFD, (sockaddr *)&clientInfo, &addrLen)) < 0) {
      mgmt_log(stderr, "[SyntheticHealthServer] accept() on incoming port failed: %s\n", strerror(errno));
    } else if (safe_setsockopt(clientFD, IPPROTO_TCP, TCP_NODELAY, SOCKOPT_ON, sizeof(int)) < 0) {
      mgmt_log(stderr, "[SyntheticHealthServer] Failed to set sock options: %s\n", strerror(errno));
      close_socket(clientFD);
    } else if (clientInfo.sin_addr.s_addr != htonl(INADDR_LOOPBACK)) {
      mgmt_log(stderr, "[SyntheticHealthServer] Connect by disallowed client %s, closing\n", inet_ntoa(clientInfo.sin_addr));
      close_socket(clientFD);
    } else {
      ink_thread thrId = ink_thread_create(synthetic_thread, (void *)&clientFD, 1);

      if (thrId <= 0) {
        mgmt_log(stderr, "[SyntheticHealthServer] Failed to create worker thread");
      }
    }
  }

  ink_release_assert(!"impossible"); // should never get here
  return NULL;
}

/*
  HTTP/1.0 200 OK
  Server: Traffic Manager
  Date: Wed, 10 Jun 2015 04:28:07 GMT
  Cache-Control: no-store
  Pragma: no-cache
  Content-type: text/plain
  Content-length: 1620
*/
