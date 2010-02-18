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

#ifndef TEST_PROTOCOL_H
#define TEST_PROTOCOL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "ts.h"

#define WATER_MARK 5
#define MAX_REQUEST_LENGTH 2048
#define MAX_PATTERN_LENGTH 1024

#define DEBUG_TAG "test-protocol-dbg"

#define PLUGIN_NAME "test-protocol"
#define VALID_POINTER(X) ((X != NULL) && (X != INK_ERROR_PTR))
#define LOG_SET_FUNCTION_NAME(NAME) const char * FUNCTION_NAME = NAME
#define LOG_ERROR(API_NAME) { \
    INKDebug(PLUGIN_NAME, "%s: %s %s %s File %s, line number %d", PLUGIN_NAME, API_NAME, "APIFAIL", \
	     FUNCTION_NAME, __FILE__, __LINE__); \
}
#define LOG_ERROR_AND_RETURN(API_NAME) { \
    LOG_ERROR(API_NAME); \
    return -1; \
}
#define LOG_ERROR_AND_CLEANUP(API_NAME) { \
  LOG_ERROR(API_NAME); \
  goto Lclean; \
}
#define LOG_ERROR_AND_REENABLE(API_NAME) { \
  LOG_ERROR(API_NAME); \
  INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE); \
}
#define LOG_ERROR_NEG(API_NAME) { \
    INKDebug(PLUGIN_NAME, "%s: %s %s %s File %s, line number %d",PLUGIN_NAME, API_NAME, "NEGAPIFAIL", \
             FUNCTION_NAME, __FILE__, __LINE__); \
}

typedef int (*ConnHandler) (INKCont contp, INKEvent event, void *data);

typedef struct _ConnData
{
  INKMutex mutex;
  INKAction pending_action;
  ConnHandler current_handler;

  INKVConn client_vconn;

  char *client_request;
  char *client_response;

  char *pattern;
  int number;

  INKVIO client_read_vio;
  INKVIO client_write_vio;
  INKIOBuffer client_request_buffer;
  INKIOBuffer client_response_buffer;
  INKIOBufferReader client_request_buffer_reader;
  INKIOBufferReader client_response_buffer_reader;

} ConnData;

/* global variable */
INKTextLogObject plugin_log = NULL;

#endif /* TEST_PROTOCOL_H */
