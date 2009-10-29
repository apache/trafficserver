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


#include "InkAPI.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>


const char *TEST_FAIL = "fail";
const char *TEST_PASS = "pass";

const char *API_INTERFACE_NAME = "x-api_interface_name";
static int API_INTERFACE_NAME_LEN;

const char *INTERFACE = "INKHttpTxnCacheLookupStatusSet";

const char *EXPECTED_CALL_RESULT = "x-expected_call_result";
static int EXPECTED_CALL_RESULT_LEN;

const char *ACTUAL_CALL_RESULT = "x-actual_call_result";
static int ACTUAL_CALL_RESULT_LEN;

const char *EXPECTED_GCS = "x-expected_get_cache_status";
static int EXPECTED_GCS_LEN;

const char *EXPECTED_SCS = "x-expected_set_cache_status";
static int EXPECTED_SCS_LEN;

const char *EXPECTED_TEST_RESULT = "x-expected_test_result";
static int EXPECTED_TEST_RESULT_LEN;

const char *ACTUAL_GCS = "x-actual_get_cache_status";
static int ACTUAL_GCS_LEN;

const char *ACTUAL_TEST_RESULT = "x-actual_test_result";
static int ACTUAL_TEST_RESULT_LEN;

static int SEPARATOR_TOKEN = '=';

static char *VALUE_NOT_FOUND = "<extension header not found>";


static void
constDataInit()
{
  API_INTERFACE_NAME_LEN = strlen(API_INTERFACE_NAME);
  EXPECTED_CALL_RESULT_LEN = strlen(EXPECTED_CALL_RESULT);
  ACTUAL_CALL_RESULT_LEN = strlen(ACTUAL_CALL_RESULT);

  EXPECTED_GCS_LEN = strlen(EXPECTED_GCS);
  EXPECTED_SCS_LEN = strlen(EXPECTED_SCS);

  EXPECTED_TEST_RESULT_LEN = strlen(EXPECTED_TEST_RESULT);

  EXPECTED_CALL_RESULT_LEN = strlen(EXPECTED_CALL_RESULT);

  ACTUAL_GCS_LEN = strlen(ACTUAL_GCS);

  ACTUAL_TEST_RESULT_LEN = strlen(ACTUAL_TEST_RESULT);
}



char *
mimeValueGet(const char *pval, int separator_token)
{
  char *ptr = NULL;
  if (!pval)
    return NULL;
  ptr = strchr(pval, separator_token);
  if (!ptr) {
    INKDebug("INKHttpTxnCacheLookupStatusSet",
             "\n mimeValueGet: in [%s], separator [%c] !found \n", pval, separator_token);
    return NULL;
  }
  ptr++;
  if (!ptr) {
    INKDebug("INKHttpTxnCacheLookupStatusSet", "\n mimeValueGet: in [%s], value after separator !found \n", pval);
    return NULL;
  }
  return INKstrndup(ptr, strlen(ptr));
}

/* 
 *	incoming: mimeHdr  (must be null terminated)
 * 	incoming: lenHdr
 *	outgoing: mimeValue 
 * 
 * If mimeValue is not found, mimeValue pointer is not set.
*/
static int
getTestParam(INKMBuffer buff, INKMLoc loc, const char *mimeHdr, int lenHdr, void **mimeValue)
{
  INKMLoc field_loc;
  const char *ptr, *nptr;
  int len;

#ifdef DEBUG_DUMP
  dump_field_mloc(buff, loc);
#endif

  field_loc = INKMimeHdrFieldFind(buff, loc, mimeHdr, lenHdr);
  if (!field_loc) {
    INKDebug("INKHttpTxnCacheLookupStatusSet", "\n getTestParam: INKMimeHdrFieldFind did not find %s\n", mimeHdr);
    return 0;
  }

  ptr = INKMimeHdrFieldValueGet(buff, loc, field_loc, -1, &len);
  if (!ptr || (len == 0)) {
    INKDebug("INKHttpTxnCacheLookupStatusSet", "\n getTestParam: INKMimeHdrFieldValueGet did not find %s \n", mimeHdr);
    return 0;
  }

  *mimeValue = (void *) INKstrndup(ptr, strlen(ptr));
  INKDebug("INKHttpTxnCacheLookupStatusSet",
           "\n getTestParam: hdr = [%s], val = [%s]\n", mimeHdr, *(char **) mimeValue);

  INKHandleStringRelease(buff, field_loc, ptr);
  INKHandleMLocRelease(buff, loc, field_loc);

  return 1;
}


/*	
 *		incoming: mimeEntry
 * 		outgoing: none
 *      	txn outgoing: values from mimeValue
*/
static int
setTestResult(INKMBuffer buff, INKMLoc loc, const char *mimeHdr, int mimeHdrLen, char *mimeValues)
{
  INKMLoc field_offset, dupLoc;
  char *action = NULL;

  /* Walk the list of MIME entries and append values to first duplicate
   *       found in list. Other duplicates will not be affected.
   */
  dupLoc = INKMimeHdrFieldFind(buff, loc, mimeHdr, mimeHdrLen);
  if (dupLoc) {
    action = " appended ";
    /* insert append *//* len here is not fixed */
    INKMimeHdrFieldValueInsert(buff, loc, dupLoc, mimeValues, strlen(mimeValues), -1);
  } else {
    action = " added ";

    /* Insert mimeHdr */
    field_offset = INKMimeHdrFieldCreate(buff, loc);
    if (!field_offset) {
      INKDebug("INKHttpTxnCacheLookupStatusSet", "\n setTestResult: INKMimeHdrFieldCreate failed ");
      return 0;
    }

    /* insert append, position is not significant */
    INKMimeHdrFieldInsert(buff, loc, field_offset, -1);
    INKMimeHdrFieldNameSet(buff, loc, field_offset, mimeHdr, strlen(mimeHdr));

    /* insert append */
    INKMimeHdrFieldValueInsert(buff, loc, field_offset, mimeValues, strlen(mimeValues), -1);

    INKHandleMLocRelease(buff, loc, field_offset);
    INKHandleMLocRelease(buff, INK_NULL_MLOC, loc);
  }
  INKDebug("INKHttpTxnCacheLookupStatusSet", "\n setTestResult: %s  [%s: %s] \n", action, mimeHdr, mimeValues);
  return 1;
}


#if 0
typedef enum
{
  INK_CACHE_LOOKUP_MISS,
  INK_CACHE_LOOKUP_HIT_STALE,
  INK_CACHE_LOOKUP_HIT_FRESH
} INKCacheLookupResult;
#endif



const char *statusTable[] = {
  "INK_CACHE_LOOKUP_MISS",
  "INK_CACHE_LOOKUP_HIT_STALE",
  "INK_CACHE_LOOKUP_HIT_FRESH"
};


static int
stringStatus2intStatus(const char *status)
{
  if (!strcmp(status, statusTable[0]))
    return 0;
  else if (!strcmp(status, statusTable[1]))
    return 1;
  else if (!strcmp(status, statusTable[2]))
    return 2;
}

static int
CacheLookupStatusSet(INKHttpTxn txn)
{
  int err = 1, reSet = 0, reGet = 0;
  char *finalResult = (char *) TEST_PASS;
  INKMBuffer reqBuf;
  INKMLoc reqLoc;
  void *pField = NULL, *pVal = NULL;
  char bufVal[BUFSIZ];

  char *expectedGCS = NULL;
  char *expectedSCS = NULL;
  char *expectedTestResult = NULL;
  int expectedCallResult = NULL;
  int cacheLookup, cacheLookupCount;

  if (!INKHttpTxnClientReqGet(txn, &reqBuf, &reqLoc)) {
    INKDebug("INKHttpTxnCacheLookupStatusSet", "CacheLookupStatusSet: INKHttpTxnClientReqGet failed ");

    finalResult = (char *) TEST_PASS;
    err = 0;
    goto finalTestDone;
  }

  getTestParam(reqBuf, reqLoc, EXPECTED_GCS, EXPECTED_GCS_LEN, &pField);
  pVal = mimeValueGet((const char *) pField, SEPARATOR_TOKEN);
  INKfree(pField);
  pField = NULL;
  expectedGCS = (char *) pVal;

  getTestParam(reqBuf, reqLoc, EXPECTED_SCS, EXPECTED_SCS_LEN, &pField);
  pVal = mimeValueGet((const char *) pField, SEPARATOR_TOKEN);
  INKfree(pField);
  pField = NULL;
  expectedSCS = (char *) pVal;

  getTestParam(reqBuf, reqLoc, EXPECTED_TEST_RESULT, EXPECTED_TEST_RESULT_LEN, &pField);
  pVal = mimeValueGet((const char *) pField, SEPARATOR_TOKEN);
  INKfree(pField);
  pField = NULL;
  expectedTestResult = (char *) pVal;

  getTestParam(reqBuf, reqLoc, EXPECTED_CALL_RESULT, EXPECTED_CALL_RESULT_LEN, &pField);
  pVal = mimeValueGet((const char *) pField, SEPARATOR_TOKEN);
  INKfree(pField);
  pField = NULL;
  expectedCallResult = atoi((char *) pVal);
  INKfree(pVal);

  reGet = INKHttpTxnCacheLookupStatusGet(txn, &cacheLookup, &cacheLookupCount);
  if (cacheLookup != stringStatus2intStatus(expectedGCS)) {
    finalResult = (char *) TEST_FAIL;
    err = 0;
    INKDebug("INKHttpTxnCacheLookupStatusSet",
             "\n CacheLookupStatusSet: test not correctly set-up actual=[%s = %d] != expected=[%s = %d] \n",
             statusTable[cacheLookup], cacheLookup, expectedGCS, stringStatus2intStatus(expectedGCS));

    goto finalTestDone;
  }

  INKDebug("INKHttpTxnCacheLookupStatusSet",
           "\n CacheLookupStatusSet: from [%s = %d] to [%s = %d] \n",
           expectedGCS, stringStatus2intStatus(expectedSCS), expectedSCS, stringStatus2intStatus(expectedSCS));


  reSet = INKHttpTxnCacheLookupStatusSet(txn, stringStatus2intStatus(expectedSCS));

  sprintf(bufVal, " result=%d", reSet);
  setTestResult(reqBuf, reqLoc, ACTUAL_CALL_RESULT, ACTUAL_CALL_RESULT_LEN, bufVal);

  reGet = INKHttpTxnCacheLookupStatusGet(txn, &cacheLookup, &cacheLookupCount);

  sprintf(bufVal, " status=%s", statusTable[cacheLookup]);
  setTestResult(reqBuf, reqLoc, ACTUAL_GCS, ACTUAL_GCS_LEN, bufVal);

  if (reSet != expectedCallResult) {
    finalResult = (char *) TEST_FAIL;
    err = 0;
    goto finalTestDone;
  }
  if (cacheLookup != stringStatus2intStatus(expectedSCS)) {
    finalResult = (char *) TEST_FAIL;
    err = 0;
    goto finalTestDone;
  }

finalTestDone:

  sprintf(bufVal, " result=%s", finalResult);
  setTestResult(reqBuf, reqLoc, ACTUAL_TEST_RESULT, ACTUAL_TEST_RESULT_LEN, bufVal);

  INKHandleMLocRelease(reqBuf, reqLoc, INK_NULL_MLOC);
  return err;
}

static int
Request2Response(INKHttpTxn txn)
{
  int err = 1, re;
  INKMBuffer reqBuf;
  INKMLoc reqLoc;
  INKMBuffer respBuf;
  INKMLoc respLoc;
  void *pField = NULL;

  if (!(re = INKHttpTxnClientReqGet(txn, &reqBuf, &reqLoc))) {
    INKDebug("INKHttpTxnCacheLookupStatusSet", "Request2Response: INKHttpTxnClientReqGet failed ");
    return re;
  }
  if (!(re = INKHttpTxnClientRespGet(txn, &respBuf, &respLoc))) {
    INKDebug("INKHttpTxnCacheLookupStatusSet", "CacheLookupStatusSet: INKHttpTxnCacheLookupStatusSet failed ");
    return re;
  }

  getTestParam(reqBuf, reqLoc, ACTUAL_CALL_RESULT, ACTUAL_CALL_RESULT_LEN, &pField);
  if (!pField)
    pField = VALUE_NOT_FOUND;

  setTestResult(respBuf, respLoc, ACTUAL_CALL_RESULT, ACTUAL_CALL_RESULT_LEN, (char *) pField);
  if (pField != VALUE_NOT_FOUND)
    INKfree(pField);
  pField = NULL;


  getTestParam(reqBuf, reqLoc, ACTUAL_GCS, ACTUAL_GCS_LEN, &pField);
  if (!pField)
    pField = VALUE_NOT_FOUND;

  setTestResult(respBuf, respLoc, ACTUAL_GCS, ACTUAL_GCS_LEN, (char *) pField);
  if (pField != VALUE_NOT_FOUND)
    INKfree(pField);
  pField = NULL;


  getTestParam(reqBuf, reqLoc, ACTUAL_TEST_RESULT, ACTUAL_TEST_RESULT_LEN, &pField);
  if (!pField)
    pField = VALUE_NOT_FOUND;

  setTestResult(respBuf, respLoc, ACTUAL_TEST_RESULT, ACTUAL_TEST_RESULT_LEN, (char *) pField);
  if (pField != VALUE_NOT_FOUND)
    INKfree(pField);
  pField = NULL;

  setTestResult(respBuf, respLoc, API_INTERFACE_NAME, API_INTERFACE_NAME_LEN, (char *) INTERFACE);

  INKHandleMLocRelease(reqBuf, reqLoc, INK_NULL_MLOC);
  INKHandleMLocRelease(respBuf, respLoc, INK_NULL_MLOC);
  return err;
}

static int
eventHandler(INKCont contp, INKEvent event, void *eData)
{
  INKHttpTxn txnp = (INKHttpTxn) eData;
  int re = 1;

  switch (event) {

  case INK_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
    re = CacheLookupStatusSet(txnp);
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_SEND_RESPONSE_HDR:
    re = Request2Response(txnp);
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  }
  return re;
}


void
INKPluginInit(int argc, const char *argv[])
{
  constDataInit();

  INKCont cont = INKContCreate(eventHandler, INKMutexCreate());

  INKHttpHookAdd(INK_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, cont);
  INKHttpHookAdd(INK_HTTP_SEND_RESPONSE_HDR_HOOK, cont);
}
