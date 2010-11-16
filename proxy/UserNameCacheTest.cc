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

#include "api/include/ts.h"
#include "UserNameCacheTest.h"
#include <stdio.h>

TSMutex testMutexp;

char USER_NAME[] = "Lou Sheward.";
char userName[TS_MAX_USER_NAME_LEN];

FILE *fp;

void
userNameCacheTestInit()
{
  TSCont cont;


  testMutexp = TSMutexCreate();
  cont = TSContCreate(&UserNameHandleCallbacks, testMutexp);

  for (int i = 1; i < 1000; i++) {
    if (TSUserNameCacheInsert(cont, i, USER_NAME) == TS_EVENT_IMMEDIATE) {
      fp = fopen("cacheTest.txt", "a+");
      fprintf(fp, "Insertion immedaite");
      fprintf(fp, " %d,%s\n", i, USER_NAME);
      fclose(fp);
    }
    if (TSUserNameCacheLookup(cont, i, userName) == TS_EVENT_IMMEDIATE) {
      fp = fopen("cacheTest.txt", "a+");
      fprintf(fp, "lookup immedaite");
      fprintf(fp, " %d,%s\n", i, userName);
      fclose(fp);
    }
    if (TSUserNameCacheDelete(cont, i) == TS_EVENT_IMMEDIATE) {
      fp = fopen("cacheTest.txt", "a+");
      fprintf(fp, "delete immediate\n");
      fclose(fp);
    }
  }
}

int
UserNameHandleCallbacks(TSCont cont, TSEvent event, void *e)
{
  fp = fopen("cacheTest.txt", "a+");
  switch (event) {

  case TS_CACHE_COULD_NOT_FIND:
    fprintf(fp, "Lookup callback, could not find\n");
    break;

  case TS_CACHE_LOOKUP_COMPLETE:
    fprintf(fp, "Lookup callback, success\n");
    break;

  }
  fclose(fp);
  return TS_EVENT_NONE;
}
