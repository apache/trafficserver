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

/*   CacheAPITester.h
 *
 */

#ifndef CACHE_API_TESTER_H
#define CACHE_API_TESTER_H

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include "ts.h"

#define DEBUG_TAG "CacheAPITester-dbg"

#define PLUGIN_NAME "CacheAPITester"
#define VALID_POINTER(X) ((X != NULL) && (X != TS_ERROR_PTR))
#define LOG_SET_FUNCTION_NAME(NAME) const char * FUNCTION_NAME = NAME

#define LOG_ERROR(API_NAME) { TSDebug(PLUGIN_NAME, "%s: %s %s %s File %s, line number %d",\
        PLUGIN_NAME, API_NAME, "APIFAIL", FUNCTION_NAME, __FILE__, __LINE__); }

#define LOG_ERROR_AND_RETURN(API_NAME) { LOG_ERROR(API_NAME); return -1; }
#define LOG_ERROR_AND_CLEANUP(API_NAME) { LOG_ERROR(API_NAME); goto Lcleanup; }

/* for the foll. macro make sure a var of txnp is defined and in scope */
#define LOG_ERROR_AND_REENABLE(API_NAME) { \
  LOG_ERROR(API_NAME); \
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE); \
}

#define LOG_ERROR_NEG(API_NAME) { TSDebug(PLUGIN_NAME, "%s: %s %s %s File %s, line number %d",\
        PLUGIN_NAME, API_NAME, "NEGAPIFAIL", FUNCTION_NAME, __FILE__, __LINE__); }

#define MAX_URL_LEN 255


#define MAGIC_ALIVE (void *) 0xfeedbabe
#define MAGIC_DEAD  (void *) 0xdeadbeef

typedef struct
{
  void *magic;
  char *url;
  int url_len;
  int pin_time;
  TSCacheKey key;
  TSIOBuffer bufp;
  TSIOBufferReader bufp_reader;
  TSHttpTxn txnp;
  int write_again_after_remove;
} CACHE_URL_DATA;

int get_client_req(TSHttpTxn, char **, char **, int *, int *);
int insert_in_response(TSHttpTxn, char *);
void cache_exercise(TSHttpTxn, char *, int, int, TSCont);
static int handle_cache_events(TSCont, TSEvent, void *);

#endif
