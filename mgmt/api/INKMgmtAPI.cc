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

/*****************************************************************************
 * Filename: InkMgmtAPI.cc
 * Purpose: This file implements all traffic server management functions.
 * Created: 9/11/00
 * Created by: Lan Tran
 *
 *
 ***************************************************************************/
#include "tscore/ink_platform.h"
#include "tscore/ink_memory.h"
#include "tscore/ParseRules.h"
#include <climits>
#include "tscore/I_Layout.h"

#include "mgmtapi.h"
#include "CoreAPIShared.h"
#include "tscore/ink_llqueue.h"

#include "tscore/TextBuffer.h"

/***************************************************************************
 * API Memory Management
 ***************************************************************************/
void *
_TSmalloc(size_t size, const char * /* path ATS_UNUSED */)
{
  return ats_malloc(size);
}

void *
_TSrealloc(void *ptr, size_t size, const char * /* path ATS_UNUSED */)
{
  return ats_realloc(ptr, size);
}

char *
_TSstrdup(const char *str, int64_t length, const char * /* path ATS_UNUSED */)
{
  return ats_strndup(str, length);
}

void
_TSfree(void *ptr)
{
  ats_free(ptr);
}

/***************************************************************************
 * API Helper Functions for Data Carrier Structures
 ***************************************************************************/

/*--- TSList operations -------------------------------------------------*/
tsapi TSList
TSListCreate(void)
{
  return (void *)create_queue();
}

/* NOTE: The List must be EMPTY */
tsapi void
TSListDestroy(TSList l)
{
  if (!l) {
    return;
  }

  delete_queue(static_cast<LLQ *>(l));
  return;
}

tsapi TSMgmtError
TSListEnqueue(TSList l, void *data)
{
  int ret;

  ink_assert(l && data);
  if (!l || !data) {
    return TS_ERR_PARAMS;
  }

  ret = enqueue(static_cast<LLQ *>(l), data); /* returns TRUE=1 or FALSE=0 */
  if (ret == 0) {
    return TS_ERR_FAIL;
  } else {
    return TS_ERR_OKAY;
  }
}

tsapi void *
TSListDequeue(TSList l)
{
  ink_assert(l);
  if (!l || queue_is_empty(static_cast<LLQ *>(l))) {
    return nullptr;
  }

  return dequeue(static_cast<LLQ *>(l));
}

tsapi bool
TSListIsEmpty(TSList l)
{
  ink_assert(l);
  if (!l) {
    return true; // list doesn't exist, so it's empty
  }

  return queue_is_empty(static_cast<LLQ *>(l));
}

tsapi int
TSListLen(TSList l)
{
  ink_assert(l);
  if (!l) {
    return -1;
  }

  return queue_len(static_cast<LLQ *>(l));
}

tsapi bool
TSListIsValid(TSList l)
{
  int i, len;

  if (!l) {
    return false;
  }

  len = queue_len(static_cast<LLQ *>(l));
  for (i = 0; i < len; i++) {
    void *ele = dequeue(static_cast<LLQ *>(l));
    if (!ele) {
      return false;
    }
    enqueue(static_cast<LLQ *>(l), ele);
  }
  return true;
}

/*--- TSStringList operations --------------------------------------*/
tsapi TSStringList
TSStringListCreate()
{
  return (void *)create_queue(); /* this queue will be a list of char* */
}

/* usually, must be an empty list before destroying*/
tsapi void
TSStringListDestroy(TSStringList strl)
{
  if (!strl) {
    return;
  }

  /* dequeue each element and free it */
  while (!queue_is_empty(static_cast<LLQ *>(strl))) {
    char *str = static_cast<char *>(dequeue(static_cast<LLQ *>(strl)));
    ats_free(str);
  }

  delete_queue(static_cast<LLQ *>(strl));
}

tsapi TSMgmtError
TSStringListEnqueue(TSStringList strl, char *str)
{
  int ret;

  ink_assert(strl && str);
  if (!strl || !str) {
    return TS_ERR_PARAMS;
  }

  ret = enqueue(static_cast<LLQ *>(strl), str); /* returns TRUE=1 or FALSE=0 */
  if (ret == 0) {
    return TS_ERR_FAIL;
  } else {
    return TS_ERR_OKAY;
  }
}

tsapi char *
TSStringListDequeue(TSStringList strl)
{
  ink_assert(strl);
  if (!strl || queue_is_empty(static_cast<LLQ *>(strl))) {
    return nullptr;
  }

  return static_cast<char *>(dequeue(static_cast<LLQ *>(strl)));
}

tsapi bool
TSStringListIsEmpty(TSStringList strl)
{
  ink_assert(strl);
  if (!strl) {
    return true;
  }

  return queue_is_empty(static_cast<LLQ *>(strl));
}

tsapi int
TSStringListLen(TSStringList strl)
{
  ink_assert(strl);
  if (!strl) {
    return -1;
  }

  return queue_len(static_cast<LLQ *>(strl));
}

// returns false if any element is NULL string
tsapi bool
TSStringListIsValid(TSStringList strl)
{
  int i, len;

  if (!strl) {
    return false;
  }

  len = queue_len(static_cast<LLQ *>(strl));
  for (i = 0; i < len; i++) {
    char *str = static_cast<char *>(dequeue(static_cast<LLQ *>(strl)));
    if (!str) {
      return false;
    }
    enqueue(static_cast<LLQ *>(strl), str);
  }
  return true;
}

/*--- TSIntList operations --------------------------------------*/
tsapi TSIntList
TSIntListCreate()
{
  return (void *)create_queue(); /* this queue will be a list of int* */
}

/* usually, must be an empty list before destroying*/
tsapi void
TSIntListDestroy(TSIntList intl)
{
  if (!intl) {
    return;
  }

  /* dequeue each element and free it */
  while (!queue_is_empty(static_cast<LLQ *>(intl))) {
    int *iPtr = static_cast<int *>(dequeue(static_cast<LLQ *>(intl)));
    ats_free(iPtr);
  }

  delete_queue(static_cast<LLQ *>(intl));
  return;
}

tsapi TSMgmtError
TSIntListEnqueue(TSIntList intl, int *elem)
{
  int ret;

  ink_assert(intl && elem);
  if (!intl || !elem) {
    return TS_ERR_PARAMS;
  }

  ret = enqueue(static_cast<LLQ *>(intl), elem); /* returns TRUE=1 or FALSE=0 */
  if (ret == 0) {
    return TS_ERR_FAIL;
  } else {
    return TS_ERR_OKAY;
  }
}

tsapi int *
TSIntListDequeue(TSIntList intl)
{
  ink_assert(intl);
  if (!intl || queue_is_empty(static_cast<LLQ *>(intl))) {
    return nullptr;
  }

  return static_cast<int *>(dequeue(static_cast<LLQ *>(intl)));
}

tsapi bool
TSIntListIsEmpty(TSIntList intl)
{
  ink_assert(intl);
  if (!intl) {
    return true;
  }

  return queue_is_empty(static_cast<LLQ *>(intl));
}

tsapi int
TSIntListLen(TSIntList intl)
{
  ink_assert(intl);
  if (!intl) {
    return -1;
  }

  return queue_len(static_cast<LLQ *>(intl));
}

tsapi bool
TSIntListIsValid(TSIntList intl, int min, int max)
{
  if (!intl) {
    return false;
  }

  for (unsigned long i = 0; i < queue_len(static_cast<LLQ *>(intl)); i++) {
    int *item = static_cast<int *>(dequeue(static_cast<LLQ *>(intl)));
    if (*item < min) {
      return false;
    }
    if (*item > max) {
      return false;
    }
    enqueue(static_cast<LLQ *>(intl), item);
  }
  return true;
}

/* NOTE: user must deallocate the memory for the string returned */
char *
TSGetErrorMessage(TSMgmtError err_id)
{
  char msg[1024]; // need to define a MAX_ERR_MSG_SIZE???
  char *err_msg = nullptr;

  switch (err_id) {
  case TS_ERR_OKAY:
    snprintf(msg, sizeof(msg), "[%d] Everything's looking good.", err_id);
    break;
  case TS_ERR_READ_FILE: /* Error occur in reading file */
    snprintf(msg, sizeof(msg), "[%d] Unable to find/open file for reading.", err_id);
    break;
  case TS_ERR_WRITE_FILE: /* Error occur in writing file */
    snprintf(msg, sizeof(msg), "[%d] Unable to find/open file for writing.", err_id);
    break;
  case TS_ERR_PARSE_CONFIG_RULE: /* Error in parsing configuration file */
    snprintf(msg, sizeof(msg), "[%d] Error parsing configuration file.", err_id);
    break;
  case TS_ERR_INVALID_CONFIG_RULE: /* Invalid Configuration Rule */
    snprintf(msg, sizeof(msg), "[%d] Invalid configuration rule reached.", err_id);
    break;
  case TS_ERR_NET_ESTABLISH:
    snprintf(msg, sizeof(msg), "[%d] Error establishing socket connection.", err_id);
    break;
  case TS_ERR_NET_READ: /* Error reading from socket */
    snprintf(msg, sizeof(msg), "[%d] Error reading from socket.", err_id);
    break;
  case TS_ERR_NET_WRITE: /* Error writing to socket */
    snprintf(msg, sizeof(msg), "[%d] Error writing to socket.", err_id);
    break;
  case TS_ERR_NET_EOF: /* Hit socket EOF */
    snprintf(msg, sizeof(msg), "[%d] Reached socket EOF.", err_id);
    break;
  case TS_ERR_NET_TIMEOUT: /* Timed out waiting for socket read */
    snprintf(msg, sizeof(msg), "[%d] Timed out waiting for socket read.", err_id);
    break;
  case TS_ERR_SYS_CALL: /* Error in sys/utility call, eg.malloc */
    snprintf(msg, sizeof(msg), "[%d] Error in basic system/utility call.", err_id);
    break;
  case TS_ERR_PARAMS: /* Invalid parameters for a fn */
    snprintf(msg, sizeof(msg), "[%d] Invalid parameters passed into function call.", err_id);
    break;
  case TS_ERR_FAIL:
    snprintf(msg, sizeof(msg), "[%d] Generic Fail message (ie. CoreAPI call).", err_id);
    break;
  case TS_ERR_NOT_SUPPORTED:
    snprintf(msg, sizeof(msg), "[%d] Operation not supported on this platform.", err_id);
    break;
  case TS_ERR_PERMISSION_DENIED:
    snprintf(msg, sizeof(msg), "[%d] Operation not permitted.", err_id);
    break;

  default:
    snprintf(msg, sizeof(msg), "[%d] Invalid error type.", err_id);
    break;
  }

  err_msg = ats_strdup(msg);
  return err_msg;
}

/* ReadFromUrl: reads a remotely located config file into a buffer
 * Input:  url        - remote location of the file
 *         header     - a buffer is allocated on the header char* pointer
 *         headerSize - the size of the header buffer is returned
 *         body       - a buffer is allocated on the body char* pointer
 *         bodySize   - the size of the body buffer is returned
 * Output: TSMgmtError   - TS_ERR_OKAY if succeed, TS_ERR_FAIL otherwise
 * Obsolete:  tsapi TSMgmtError TSReadFromUrl (char *url, char **text, int *size);
 * NOTE: The URL can be expressed in the following forms:
 *       - http://www.example.com:80/products/network/index.html
 *       - http://www.example.com/products/network/index.html
 *       - http://www.example.com/products/network/
 *       - http://www.example.com/
 *       - http://www.example.com
 *       - www.example.com
 * NOTE: header and headerSize can be NULL
 */
tsapi TSMgmtError
TSReadFromUrl(char *url, char **header, int *headerSize, char **body, int *bodySize)
{
  // return ReadFromUrl(url, header, headerSize, body, bodySize);
  return TSReadFromUrlEx(url, header, headerSize, body, bodySize, URL_TIMEOUT);
}

tsapi TSMgmtError
TSReadFromUrlEx(const char *url, char **header, int *headerSize, char **body, int *bodySize, int timeout)
{
  int hFD        = -1;
  char *httpHost = nullptr;
  char *httpPath = nullptr;
  int httpPort   = HTTP_PORT;
  int bufsize    = URL_BUFSIZE;
  char buffer[URL_BUFSIZE];
  char request[BUFSIZE];
  char *hdr_temp;
  char *bdy_temp;
  TSMgmtError status = TS_ERR_OKAY;

  // Sanity check
  if (!url) {
    return TS_ERR_FAIL;
  }
  if (timeout < 0) {
    timeout = URL_TIMEOUT;
  }
  // Chop the protocol part, if it exists
  const char *doubleSlash = strstr(url, "//");
  if (doubleSlash) {
    url = doubleSlash + 2; // advance two positions to get rid of leading '//'
  }
  // the path starts after the first occurrence of '/'
  const char *tempPath = strstr(url, "/");
  char *host_and_port;
  if (tempPath) {
    host_and_port = ats_strndup(url, strlen(url) - strlen(tempPath));
    tempPath      += 1; // advance one position to get rid of leading '/'
    httpPath      = ats_strdup(tempPath);
  } else {
    host_and_port = ats_strdup(url);
    httpPath      = ats_strdup("");
  }

  // the port proceed by a ":", if it exists
  char *colon = strstr(host_and_port, ":");
  if (colon) {
    httpHost = ats_strndup(host_and_port, strlen(host_and_port) - strlen(colon));
    colon    += 1; // advance one position to get rid of leading ':'
    httpPort = ink_atoi(colon);
    if (httpPort <= 0) {
      httpPort = HTTP_PORT;
    }
  } else {
    httpHost = ats_strdup(host_and_port);
  }
  ats_free(host_and_port);

  hFD = connectDirect(httpHost, httpPort, timeout);
  if (hFD == -1) {
    status = TS_ERR_NET_ESTABLISH;
    goto END;
  }

  /* sending the HTTP request via the established socket */
  snprintf(request, BUFSIZE, "http://%s:%d/%s", httpHost, httpPort, httpPath);
  if ((status = sendHTTPRequest(hFD, request, static_cast<uint64_t>(timeout))) != TS_ERR_OKAY) {
    goto END;
  }

  memset(buffer, 0, bufsize); /* empty the buffer */
  if ((status = readHTTPResponse(hFD, buffer, bufsize, static_cast<uint64_t>(timeout))) != TS_ERR_OKAY) {
    goto END;
  }

  if ((status = parseHTTPResponse(buffer, &hdr_temp, headerSize, &bdy_temp, bodySize)) != TS_ERR_OKAY) {
    goto END;
  }

  if (header && headerSize) {
    *header = ats_strndup(hdr_temp, *headerSize);
  }
  *body = ats_strndup(bdy_temp, *bodySize);

END:
  ats_free(httpHost);
  ats_free(httpPath);

  return status;
}
