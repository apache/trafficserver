/** @file

  This file contains functions that are shared by local and remote
  API; in particular it has helper functions used by TSMgmtAPI.cc

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
#include "tscore/ink_platform.h"
#include "tscore/ink_sock.h"
#include "tscore/ink_string.h"
#include "tscore/ink_memory.h"

#include "CoreAPIShared.h"
#include "MgmtSocket.h"

// Forward declarations, used to be in the CoreAPIShared.h include file but
// that doesn't make any sense since these are both statically declared. /leif
static int poll_write(int fd, int timeout);
static int poll_read(int fd, int timeout);

/* parseHTTPResponse
 * - parse the response buffer into header and body and calculate
 *   the correct size of the header and body.
 * INPUT:  buffer   -- response buffer to be parsed
 *         header   -- pointer to the head of the header
 *         hdr_size -- size of the header
 *         body     -- pointer to the head of the body
 *         bdy_size -- size of the body
 * OUTPUT: TSMgmtError -- error status
 */
TSMgmtError
parseHTTPResponse(char *buffer, char **header, int *hdr_size, char **body, int *bdy_size)
{
  TSMgmtError err = TS_ERR_OKAY;
  char *buf;

  // locate HTTP divider
  if (!(buf = strstr(buffer, HTTP_DIVIDER))) {
    err = TS_ERR_FAIL;
    goto END;
  }
  // calculate header info
  if (header) {
    *header = buffer;
  }
  if (hdr_size) {
    *hdr_size = buf - buffer;
  }

  // calculate body info
  buf += strlen(HTTP_DIVIDER);
  if (body) {
    *body = buf;
  }
  if (bdy_size) {
    *bdy_size = strlen(buf);
  }

END:
  return err;
}

/* readHTTPResponse
 * - read from an opened socket to memory-allocated buffer and close the
 *   socket regardless success or failure.
 * INPUT:  sock -- the socket to read the response from
 *         buffer -- the buffer to be filled with the HTTP response
 *         bufsize -- the size allocated for the buffer
 * OUTPUT: bool -- true if everything went well. false otherwise
 */
TSMgmtError
readHTTPResponse(int sock, char *buffer, int bufsize, uint64_t timeout)
{
  int64_t err, idx;

  idx = 0;
  for (;;) {
    //      printf("%d\n", idx);
    if (idx >= bufsize) {
      //      printf("(test) response is too large [%d] %d\n", idx, bufsize);
      goto error;
    }
    //      printf("before poll_read\n");
    err = poll_read(sock, timeout);
    if (err < 0) {
      //      printf("(test) poll read failed [%d '%s']\n", errno, strerror (errno));
      goto error;
    } else if (err == 0) {
      //      printf("(test) read timeout\n");
      goto error;
    }
    //      printf("before do\n");
    do {
      //      printf("in do\n");
      err = read(sock, &buffer[idx], bufsize - idx);
    } while ((err < 0) && ((errno == EINTR) || (errno == EAGAIN)));
    //       printf("content: %s\n", buffer);

    if (err < 0) {
      //      printf("(test) read failed [%d '%s']\n", errno, strerror (errno));
      goto error;
    } else if (err == 0) {
      buffer[idx] = '\0';
      close(sock);
      return TS_ERR_OKAY;
    } else {
      idx += err;
    }
  }

error: /* "Houston, we have a problem!" (Apollo 13) */
  if (sock >= 0) {
    close_socket(sock);
  }
  return TS_ERR_NET_READ;
}

/* sendHTTPRequest
 * - Compose a HTTP GET request and sent it via an opened socket.
 * INPUT:  sock -- the socket to send the message to
 *         req  -- the request to send
 * OUTPUT: bool -- true if everything went well. false otherwise (and sock is
 *                 closed)
 */
TSMgmtError
sendHTTPRequest(int sock, char *req, uint64_t timeout)
{
  char request[BUFSIZ];
  size_t length = 0;

  memset(request, 0, BUFSIZ);
  snprintf(request, BUFSIZ, "GET %s HTTP/1.0\r\n\r\n", req);
  length = strlen(request);

  int err = poll_write(sock, timeout);
  if (err < 0) {
    //      printf("(test) poll write failed [%d '%s']\n", errno, strerror (errno));
    goto error;
  } else if (err == 0) {
    //      printf("(test) write timeout\n");
    goto error;
  }
  // Write the request to the server.
  while (length > 0) {
    do {
      err = write(sock, request, length);
    } while ((err < 0) && ((errno == EINTR) || (errno == EAGAIN)));

    if (err < 0) {
      //      printf("(test) write failed [%d '%s']\n", errno, strerror (errno));
      goto error;
    }
    length -= err;
  }

  /* everything went well */
  return TS_ERR_OKAY;

error: /* "Houston, we have a problem!" (Apollo 13) */
  if (sock >= 0) {
    close_socket(sock);
  }
  return TS_ERR_NET_WRITE;
}

int
connectDirect(const char *host, int port, uint64_t /* timeout ATS_UNUSED */)
{
  int sock;

  // Create a socket
  do {
    sock = socket(AF_INET, SOCK_STREAM, 0);
  } while ((sock < 0) && ((errno == EINTR) || (errno == EAGAIN)));

  if (sock < 0) {
    //        printf("(test) unable to create socket [%d '%s']\n", errno, strerror(errno));
    goto error;
  }

  struct sockaddr_in name;
  memset((void *)&name, 0, sizeof(sockaddr_in));

  int err;

  // Put the socket in non-blocking mode...just to be extra careful
  // that we never block.
  do {
    err = fcntl(sock, F_SETFL, O_NONBLOCK);
  } while ((err < 0) && ((errno == EINTR) || (errno == EAGAIN)));

  if (err < 0) {
    //        printf("(test) unable to put socket in non-blocking mode [%d '%s']\n", errno, strerror (errno));
    goto error;
  }
  // Connect to the specified port on the machine we're running on.
  name.sin_family = AF_INET;
  name.sin_port   = htons(port);

  struct hostent *pHostent;
  pHostent = gethostbyname(host);
  if (!pHostent) {
    goto error;
  }
  memcpy(reinterpret_cast<caddr_t>(&(name.sin_addr)), pHostent->h_addr, pHostent->h_length);

  do {
    err = connect(sock, reinterpret_cast<struct sockaddr *>(&name), sizeof(name));
  } while ((err < 0) && ((errno == EINTR) || (errno == EAGAIN)));

  if ((err < 0) && (errno != EINPROGRESS)) {
    //        printf("(test) unable to connect to server [%d '%s'] at port %d\n", errno, strerror (errno), port);
    goto error;
  }
  return sock;

error:
  if (sock >= 0) {
    close_socket(sock);
  }
  return -1;
} /* connectDirect */

static int
poll_read(int fd, int timeout)
{
  struct pollfd info;
  int err;

  info.fd      = fd;
  info.events  = POLLIN;
  info.revents = 0;

  do {
    err = poll(&info, 1, timeout);
  } while ((err < 0) && ((errno == EINTR) || (errno == EAGAIN)));

  if ((err > 0) && (info.revents & POLLIN)) {
    return 1;
  }

  return err;
}

static int
poll_write(int fd, int timeout)
{
  struct pollfd info;
  int err;

  info.fd      = fd;
  info.events  = POLLOUT;
  info.revents = 0;

  do {
    err = poll(&info, 1, timeout);
  } while ((err < 0) && ((errno == EINTR) || (errno == EAGAIN)));

  if ((err > 0) && (info.revents & POLLOUT)) {
    return 1;
  }

  return err;
}

/**********************************************************************
 * Events
 **********************************************************************/
/**********************************************************************
 * get_event_id
 *
 * Purpose: Given the event_name, returns the event's corresponding
 *          event id
 * Note: this conversion is based on list defined in Alarms.h and
 *       the identical list defined in CoreAPIShared.cc
 *********************************************************************/
int
get_event_id(const char *event_name)
{
  if (strcmp("MGMT_ALARM_PROXY_PROCESS_DIED", event_name) == 0) {
    return MGMT_ALARM_PROXY_PROCESS_DIED;
  } else if (strcmp("MGMT_ALARM_PROXY_PROCESS_BORN", event_name) == 0) {
    return MGMT_ALARM_PROXY_PROCESS_BORN;
  } else if (strcmp("MGMT_ALARM_PROXY_CONFIG_ERROR", event_name) == 0) {
    return MGMT_ALARM_PROXY_CONFIG_ERROR;
  } else if (strcmp("MGMT_ALARM_PROXY_SYSTEM_ERROR", event_name) == 0) {
    return MGMT_ALARM_PROXY_SYSTEM_ERROR;
  } else if (strcmp("MGMT_ALARM_PROXY_CACHE_ERROR", event_name) == 0) {
    return MGMT_ALARM_PROXY_CACHE_ERROR;
  } else if (strcmp("MGMT_ALARM_PROXY_CACHE_WARNING", event_name) == 0) {
    return MGMT_ALARM_PROXY_CACHE_WARNING;
  } else if (strcmp("MGMT_ALARM_PROXY_LOGGING_ERROR", event_name) == 0) {
    return MGMT_ALARM_PROXY_LOGGING_ERROR;
  } else if (strcmp("MGMT_ALARM_PROXY_LOGGING_WARNING", event_name) == 0) {
    return MGMT_ALARM_PROXY_LOGGING_WARNING;
  } else if (strcmp("MGMT_ALARM_CONFIG_UPDATE_FAILED", event_name) == 0) {
    return MGMT_ALARM_CONFIG_UPDATE_FAILED;
  }

  return -1;
}

/**********************************************************************
 * get_event_id
 *
 * Purpose: based on alarm_id, determine the corresponding alarm name
 * Note:    allocates memory for the name returned
 *********************************************************************/
char *
get_event_name(int id)
{
  char name[MAX_EVENT_NAME_SIZE];

  memset(name, 0, MAX_EVENT_NAME_SIZE);
  switch (id) {
  case MGMT_ALARM_PROXY_PROCESS_DIED:
    ink_strlcpy(name, "MGMT_ALARM_PROXY_PROCESS_DIED", sizeof(name));
    break;
  case MGMT_ALARM_PROXY_PROCESS_BORN:
    ink_strlcpy(name, "MGMT_ALARM_PROXY_PROCESS_BORN", sizeof(name));
    break;
  case MGMT_ALARM_PROXY_CONFIG_ERROR:
    ink_strlcpy(name, "MGMT_ALARM_PROXY_CONFIG_ERROR", sizeof(name));
    break;
  case MGMT_ALARM_PROXY_SYSTEM_ERROR:
    ink_strlcpy(name, "MGMT_ALARM_PROXY_SYSTEM_ERROR", sizeof(name));
    break;
  case MGMT_ALARM_PROXY_CACHE_ERROR:
    ink_strlcpy(name, "MGMT_ALARM_PROXY_CACHE_ERROR", sizeof(name));
    break;
  case MGMT_ALARM_PROXY_CACHE_WARNING:
    ink_strlcpy(name, "MGMT_ALARM_PROXY_CACHE_WARNING", sizeof(name));
    break;
  case MGMT_ALARM_PROXY_LOGGING_ERROR:
    ink_strlcpy(name, "MGMT_ALARM_PROXY_LOGGING_ERROR", sizeof(name));
    break;
  case MGMT_ALARM_PROXY_LOGGING_WARNING:
    ink_strlcpy(name, "MGMT_ALARM_PROXY_LOGGING_WARNING", sizeof(name));
    break;
  case MGMT_ALARM_CONFIG_UPDATE_FAILED:
    ink_strlcpy(name, "MGMT_ALARM_CONFIG_UPDATE_FAILED", sizeof(name));
    break;
  default:
    return nullptr;
  }

  return ats_strdup(name);
}
