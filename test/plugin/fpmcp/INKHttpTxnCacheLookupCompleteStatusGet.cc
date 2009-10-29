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


/* Use type-o-serve to avoid requesting a document from a site
 * that may include in the response an:
 * 	Expires Header
 * 
 * 
 * Plugin passes if there are no interface errors
 * The test generator (human or machine) determines 
 * test pass/fail. 
 *
*/

#include "InkAPI.h"
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>


/* Values for test generator interface definitions */

/* Number of MIME entries in this test inclusive, tell us when to stop 
 * looking 
*/
const char *API_NUM_ENTRIES = "x-api_num_entries";
const char *API_INTERFACE_NAME = "x-api_interface_name";        /* name of interface */

const char *EXPECTED_CALL_RESULT = "x-expected_call_result";
const char *ACTUAL_CALL_RESULT = "x-actual_call_result";

const char *EXPECTED_TEST_RESULT = "x-expected_test_result";
const char *ACTUAL_TEST_RESULT = "x-actual_test_result";

const char *EXPECTED_LOOKUP_COUNT = "x-expected_lookup_count";
const char *ACTUAL_LOOKUP_COUNT = "x-actual_lookup_count";

const char *EXPECTED_CACHE_LOOKUP_STATUS = "x-expected_cache_lookup_status";
const char *ACTUAL_CACHE_LOOKUP_STATUS = "x-actual_cache_lookup_status";

const char *SDK_INTERFACE = "INKHttpTxnCacheLookupStatusGet";


int API_NUM_ENTRIES_LEN;
int API_INTERFACE_NAME_LEN;

int EXPECTED_CALL_RESULT_LEN;
int ACTUAL_CALL_RESULT_LEN;

int EXPECTED_TEST_RESULT_LEN;
int ACTUAL_TEST_RESULT_LEN;

int EXPECTED_LOOKUP_COUNT_LEN;
int ACTUAL_LOOKUP_COUNT_LEN;

int EXPECTED_CACHE_LOOKUP_STATUS_LEN;
int ACTUAL_CACHE_LOOKUP_STATUS_LEN;

/* Set len values at INKPluginInit time */
void
constDataInit()
{

  API_NUM_ENTRIES_LEN = strlen(API_NUM_ENTRIES);
  API_INTERFACE_NAME_LEN = strlen(API_INTERFACE_NAME);

  EXPECTED_CALL_RESULT_LEN = strlen(EXPECTED_CALL_RESULT);
  ACTUAL_CALL_RESULT_LEN = strlen(ACTUAL_CALL_RESULT);

  EXPECTED_TEST_RESULT_LEN = strlen(EXPECTED_TEST_RESULT);
  ACTUAL_TEST_RESULT_LEN = strlen(ACTUAL_TEST_RESULT);

  EXPECTED_LOOKUP_COUNT_LEN = strlen(EXPECTED_LOOKUP_COUNT);
  ACTUAL_LOOKUP_COUNT_LEN = strlen(ACTUAL_LOOKUP_COUNT);

  EXPECTED_CACHE_LOOKUP_STATUS_LEN = strlen(EXPECTED_CACHE_LOOKUP_STATUS);
  ACTUAL_CACHE_LOOKUP_STATUS_LEN = strlen(ACTUAL_CACHE_LOOKUP_STATUS);
}

const char *TEST_PASS = "pass";
const char *TEST_FAIL = "fail";
const char *VALUE_NOT_FOUND = "<extension header values not found>";

/* mirrors values from InkAPI.h */
char *cacheLookupResult[] = {
  "INK_CACHE_LOOKUP_MISS",
  "INK_CACHE_LOOKUP_HIT_STALE",
  "INK_CACHE_LOOKUP_HIT_FRESH"
};

/* Not a string:  x-specific-di-test: someValId=Value, ... */
#define SEPARATOR_TOKEN			'='



static void
dump_field_mloc(INKMBuffer bufp, INKMLoc offset)
{
  const char *str;
  int i, len, value_count, fieldCnt;
  INKMLoc field_offset, next_field_offset;

  if (!offset) {
    fprintf(stderr, "FIELD <NULL>\n");
    return;
  }

  fieldCnt = 0;
  field_offset = INKMimeHdrFieldGet(bufp, offset, fieldCnt);
  while (field_offset) {
    /* str = INKMimeFieldNameGet(bufp, field_loc, &len); */
    str = INKMimeHdrFieldNameGet(bufp, offset, field_offset, &len);
    fprintf(stderr, "FIELD 0x%08X: [name='%s', ", field_offset, str);
    /* INKHandleStringRelease(bufp, offset, str); */
    INKHandleStringRelease(bufp, field_offset, str);

    /* value_count = INKMimeFieldValuesCount(bufp, field_loc); */
    value_count = INKMimeHdrFieldValuesCount(bufp, offset, field_offset);
    fprintf(stderr, "#vals=%d, ", value_count);

    /* str = INKMimeFieldValueGet(bufp, field_loc, -1, &len); */
    str = INKMimeHdrFieldValueGet(bufp, offset, field_offset, -1, &len);
    fprintf(stderr, "values='%s', ", str);
    /* INKHandleStringRelease(bufp, offset, str); */
    INKHandleStringRelease(bufp, field_offset, str);

    /* len = INKMimeFieldLengthGet(bufp, field_loc); */
    len = INKMimeHdrFieldLengthGet(bufp, offset, field_offset);
    fprintf(stderr, "total_length=%d]\n", len);

    fprintf(stderr, "                  [ ");
    for (i = 0; i < value_count; i++) {
      /* str = INKMimeFieldValueGet(bufp, offset, i, &len); */
      str = INKMimeHdrFieldValueGet(bufp, offset, field_offset, i, &len);
      fprintf(stderr, "sz=%d <%s> ", len, str);
      /* INKHandleStringRelease(bufp, offset, str); */
      INKHandleStringRelease(bufp, field_offset, str);
    }
    fprintf(stderr, "]\n");

/*
field_offset = INKMimeHdrFieldGet(bufp, offset, ++fieldCnt);
*/
    next_field_offset = INKMimeHdrFieldNext(bufp, offset, field_offset);
    INKHandleMLocRelease(bufp, offset, field_offset);
    field_offset = next_field_offset;
  }
}

char *
mimeValueGet(const char *pval, int separator_token)
{
  char *ptr = NULL;
  if (!pval)
    return NULL;
  ptr = strchr(pval, separator_token);
  if (!ptr) {
    INKDebug("INKHttpTxnCacheLookupStatusGet",
             "\n mimeValueGet: in [%s], separator [%c] !found \n", pval, separator_token);
    return NULL;
  }
  ptr++;
  if (!ptr) {
    INKDebug("INKHttpTxnCacheLookupStatusGet", "\n mimeValueGet: in [%s], value after separator !found \n", pval);
    return NULL;
  }
  return ptr;
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
    INKDebug("INKHttpTxnCacheLookupStatusGet", "\n getTestParam: INKMimeHdrFieldFind did not find %s\n", mimeHdr);
    return 0;
  }
  INKDebug("INFO-INKHttpTxnCacheLookupStatusGet", "\n getTestParam: INKMimeHdrFieldFind found %s\n", mimeHdr);

  ptr = INKMimeHdrFieldValueGet(buff, loc, field_loc, -1, &len);
  if (!ptr || (len == 0)) {
    INKDebug("INKHttpTxnCacheLookupStatusGet", "\n getTestParam: INKMimeHdrFieldValueGet did not find %s \n", mimeHdr);
    return 0;
  }
  INKDebug("INFO-INKHttpTxnCacheLookupStatusGet", "\n getTestParam: INKMimeHdrFieldFind found MIME values %s \n", ptr);

  *mimeValue = (void *) INKstrndup(ptr, strlen(ptr));
  INKDebug("INKHttpTxnCacheLookupStatusGet",
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
      INKDebug("INKHttpTxnCacheLookupStatusGet", "\n setTestResult: INKMimeHdrFieldCreate failed ");
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
  INKDebug("INKHttpTxnCacheLookupStatusGet", "\n setTestResult: %s  [%s: %s] \n", action, mimeHdr, mimeValues);
  return 1;
}

static int
Request2Response(INKHttpTxn txn)
{
  INKMBuffer reqBuff, respBuff;
  INKMLoc reqLoc, respLoc;
  void *pval = NULL;
  int err = 0;

  if (!INKHttpTxnClientReqGet(txn, &reqBuff, &reqLoc)) {
    INKDebug("INKHttpTxnCacheLookupStatusGet", "\n Request2Response: INKHttpTxnClientReqGet failed\n ");
    return 0;
  }
  if (!INKHttpTxnClientRespGet(txn, &respBuff, &respLoc)) {
    INKDebug("INKHttpTxnCacheLookupStatusGet", "\n Request2Response: INKHttpTxnClientRespGet failed\n ");
    return 0;
  }

  getTestParam(reqBuff, reqLoc, ACTUAL_CACHE_LOOKUP_STATUS, ACTUAL_CACHE_LOOKUP_STATUS_LEN, &pval);
  if (!pval) {
    pval = (void *) VALUE_NOT_FOUND;
    err = 0;
  }
  setTestResult(respBuff, respLoc, ACTUAL_CACHE_LOOKUP_STATUS, ACTUAL_CACHE_LOOKUP_STATUS_LEN, (char *) pval);
  if (!(pval == VALUE_NOT_FOUND))
    INKfree(pval);
  pval = NULL;


  getTestParam(reqBuff, reqLoc, ACTUAL_CALL_RESULT, ACTUAL_CALL_RESULT_LEN, &pval);
  if (!pval) {
    pval = (void *) VALUE_NOT_FOUND;
    err = 0;
  }
  setTestResult(respBuff, respLoc, ACTUAL_CALL_RESULT, ACTUAL_CALL_RESULT_LEN, (char *) pval);
  if (!(pval == VALUE_NOT_FOUND))
    INKfree(pval);
  pval = NULL;

  getTestParam(reqBuff, reqLoc, ACTUAL_LOOKUP_COUNT, ACTUAL_LOOKUP_COUNT_LEN, &pval);
  if (!pval) {
    pval = (void *) VALUE_NOT_FOUND;
    err = 0;
  }
  setTestResult(respBuff, respLoc, ACTUAL_LOOKUP_COUNT, ACTUAL_LOOKUP_COUNT_LEN, (char *) pval);
  if (!(pval == VALUE_NOT_FOUND))
    INKfree(pval);
  pval = NULL;


  getTestParam(reqBuff, reqLoc, ACTUAL_TEST_RESULT, ACTUAL_TEST_RESULT_LEN, &pval);
  if (!pval) {
    pval = (void *) VALUE_NOT_FOUND;
    err = 0;
  }
  setTestResult(respBuff, respLoc, ACTUAL_TEST_RESULT, ACTUAL_TEST_RESULT_LEN, (char *) pval);
  if (!(pval == VALUE_NOT_FOUND))
    INKfree(pval);
  pval = NULL;

  setTestResult(respBuff, respLoc, API_INTERFACE_NAME, API_INTERFACE_NAME_LEN, (char *) SDK_INTERFACE);
  /* final step */
  INKHandleMLocRelease(reqBuff, reqLoc, INK_NULL_MLOC);
  INKHandleMLocRelease(respBuff, respLoc, INK_NULL_MLOC);

  return err;
}


/* Returns 1 on success, 0 or value of "re" on failure */
static int
CacheLookupStatusGet(INKHttpTxn txn)
{
  int re = 0, expectedRe = 0, err = 1;
  int lookupStatus = 0;
  static int expectedLookupCount = 0, lookupCntCounter = 0;
  int count = 0;
  static int priorCount = 0;

  char *finalTestResult = (char *) TEST_PASS;

  char bufHdr[BUFSIZ];          /* generic */
  char bufValues[BUFSIZ];       /* generic */

  void *pval = NULL, *newpVal = NULL;   /* generic pointer */

  INKMBuffer reqBuff;
  INKMLoc reqLoc;

  if (!INKHttpTxnClientReqGet(txn, &reqBuff, &reqLoc)) {
    INKDebug("INKHttpTxnCacheLookupStatusGet", "\n CacheLookupStatusGet: INKHttpTxnClientReqGet failed ");
    return 0;
  }

  if (!expectedLookupCount) {
    getTestParam(reqBuff, reqLoc, EXPECTED_LOOKUP_COUNT, EXPECTED_LOOKUP_COUNT_LEN, &pval);
    if (!pval) {
      finalTestResult = (char *) TEST_FAIL;
      err = 0;
      goto skipTestCmp_LOOKUP_COUNT;
    }
    newpVal = mimeValueGet((const char *) pval, SEPARATOR_TOKEN);
    if (!newpVal) {
      finalTestResult = (char *) TEST_FAIL;
      err = 0;
      goto skipTestCmp_LOOKUP_COUNT;
    }
    expectedLookupCount = atoi((char *) newpVal);
    INKfree(pval);
    pval = NULL;
    lookupCntCounter = expectedLookupCount - 1;
  }

  if (lookupCntCounter) {
    INKMBuffer urlBuff;
    INKMLoc urlLoc;
    const char *qStr = NULL, *hostName = NULL;
    int qLen = 0, hostLen = 0, re;

    urlBuff = INKMBufferCreate();
    urlLoc = INKUrlCreate(urlBuff);

    re = INKHttpTxnCacheLookupUrlGet(txn, urlBuff, urlLoc);
    if (!re)
      INKDebug("INKHttpTxnCacheLookupStatusGet", "\n CacheLookupStatusGet: INKHttpTxnCacheLookupUrlGet failed ");

    hostName = INKUrlHostGet(urlBuff, urlLoc, &hostLen);
    if (hostLen) {
      INKDebug("INKHttpTxnCacheLookupStatusGet",
               "\n CacheLookupStatusGet: %d look up of [%s]\n", lookupCntCounter, hostName);
      INKHandleStringRelease(urlBuff, urlLoc, hostName);
    }
    /* from DI plug-in */
    qStr = INKUrlHttpQueryGet(urlBuff, urlLoc, &qLen);
    if (qLen > 0) {
      INKUrlHttpQuerySet(urlBuff, urlLoc, NULL, 0);
      INKHandleStringRelease(urlBuff, urlLoc, qStr);
    }

    INKHttpTxnNewCacheLookupDo(txn, urlBuff, urlLoc);
    lookupCntCounter--;
    INKUrlDestroy(urlBuff, urlLoc);
    INKMBufferDestroy(urlBuff);
    INKHandleMLocRelease(reqBuff, INK_NULL_MLOC, reqLoc);
    return 0;
  }


  /* Tests 2,3, 5 and 6
   * check for cache status as the last test to walk through
   */
  getTestParam(reqBuff, reqLoc, EXPECTED_CACHE_LOOKUP_STATUS, EXPECTED_CACHE_LOOKUP_STATUS_LEN, &pval);
  if (!pval) {
    finalTestResult = (char *) TEST_FAIL;
    err = 0;
    goto testDone;
  }
  newpVal = mimeValueGet((const char *) pval, SEPARATOR_TOKEN);

  re = INKHttpTxnCacheLookupStatusGet(txn, &lookupStatus, &count);

  if (!strcmp((char *) newpVal, "INK_CACHE_LOOKUP_MISS")) {
    if (lookupStatus != INK_CACHE_LOOKUP_MISS) {
      INKDebug("INKHttpTxnCacheLookupStatusGet",
               "\n INKHttpTxnCacheLookupStatusGet failed expected %s != actual %s\n",
               (char *) newpVal, cacheLookupResult[lookupStatus]);
      finalTestResult = (char *) TEST_FAIL;
      err = 0;
    }
    /* report  
       api-interface: name=INKHttpTxnCacheLookupStatusGet, result=pass
       sprintf(bufValues, " name=%s", cacheLookupResult[lookupStatus]);
     */
  } else if (!strcmp((char *) newpVal, "INK_CACHE_LOOKUP_HIT_STALE")) {
    if (lookupStatus != INK_CACHE_LOOKUP_HIT_STALE) {
      INKDebug("INKHttpTxnCacheLookupStatusGet",
               "\n INKHttpTxnCacheLookupStatusGet failed expected %s != actual %s\n",
               (char *) newpVal, cacheLookupResult[lookupStatus]);
      finalTestResult = (char *) TEST_FAIL;
      err = 0;
    }

  } else if (!strcmp((char *) newpVal, "INK_CACHE_LOOKUP_HIT_FRESH")) {
    if (lookupStatus != INK_CACHE_LOOKUP_HIT_FRESH) {
      INKDebug("INKHttpTxnCacheLookupStatusGet",
               "\n INKHttpTxnCacheLookupStatusGet failed expected %s != actual %s\n",
               (char *) newpVal, cacheLookupResult[lookupStatus]);
      finalTestResult = (char *) TEST_FAIL;
      err = 0;
    }
  } else {                      /* some type of failure */
    INKDebug("INKHttpTxnCacheLookupStatusGet",
             "\n INKHttpTxnCacheLookupStatusGet failed expected %s != actual %s\n",
             (char *) newpVal, cacheLookupResult[lookupStatus]);
    finalTestResult = (char *) TEST_FAIL;
    err = 0;
  }
  INKfree(pval);
  pval = NULL;
  newpVal = NULL;
  sprintf(bufValues, " status=%s", cacheLookupResult[lookupStatus]);
  setTestResult(reqBuff, reqLoc, ACTUAL_CACHE_LOOKUP_STATUS, ACTUAL_CACHE_LOOKUP_STATUS_LEN, bufValues);



  /* Test 7: test of lookup count */
  if ((count != expectedLookupCount) /***** ||  ((count-1) != priorCount) ******/ ) {
    INKDebug("INKHttpTxnCacheLookupStatusGet", "\n FAILED: INKHttpTxnCacheLookupStatusGet count %d", count);
    finalTestResult = (char *) TEST_FAIL;
    err = 0;
  }
skipTestCmp_LOOKUP_COUNT:
  priorCount = count;
  sprintf(bufValues, "got=%d", count);  /* test generator asses pass/fail */
  setTestResult(reqBuff, reqLoc, ACTUAL_LOOKUP_COUNT, ACTUAL_LOOKUP_COUNT_LEN, bufValues);
  expectedLookupCount = 0;


  getTestParam(reqBuff, reqLoc, EXPECTED_CALL_RESULT, EXPECTED_CALL_RESULT_LEN, &pval);
  if (!pval) {
    finalTestResult = (char *) TEST_FAIL;
    err = 0;
    goto skipTestCmp_CALL_RESULT;
  }
  newpVal = mimeValueGet((const char *) pval, SEPARATOR_TOKEN);
  if (!newpVal) {
    finalTestResult = (char *) TEST_FAIL;
    err = 0;
    goto skipTestCmp_CALL_RESULT;
  }
  expectedRe = atoi((char *) newpVal);
  INKfree(pval);
  pval = NULL;
  if (re != expectedRe || (re == 0)) {
    INKDebug("INKHttpTxnCacheLookupStatusGet",
             "\n INKHttpTxnCacheLookupStatusGet: expected re %d, got %d ", expectedRe, re);
    finalTestResult = (char *) TEST_FAIL;
    err = 0;
  }
skipTestCmp_CALL_RESULT:
  sprintf(bufValues, "got=%d", re);     /* test generator asses pass/fail */
  setTestResult(reqBuff, reqLoc, ACTUAL_CALL_RESULT, ACTUAL_CALL_RESULT_LEN, bufValues);



testDone:
  /* process test result */
  getTestParam(reqBuff, reqLoc, EXPECTED_TEST_RESULT, EXPECTED_TEST_RESULT_LEN, &pval);
  if (!pval) {
    finalTestResult = (char *) TEST_FAIL;
    err = 0;
    sprintf(bufValues, " result=%s", finalTestResult);
  } else {
    newpVal = mimeValueGet((const char *) pval, SEPARATOR_TOKEN);
    if (!newpVal) {
      finalTestResult = (char *) TEST_FAIL;
      err = 0;
      sprintf(bufValues, " result=%s", finalTestResult);
    } else {                    /* expected    actual */
      if (!strcmp((char *) newpVal, finalTestResult))
        sprintf(bufValues, " result=%s,  <exp:%s>=<actual:%s>", TEST_PASS, (char *) newpVal, finalTestResult);
      else
        sprintf(bufValues, " result=%s,  <exp:%s>=<actual:%s>", TEST_FAIL, (char *) newpVal, finalTestResult);
    }
  }
  setTestResult(reqBuff, reqLoc, ACTUAL_TEST_RESULT, ACTUAL_TEST_RESULT_LEN, bufValues);

  INKHandleMLocRelease(reqBuff, INK_NULL_MLOC, reqLoc);
  return err;
}


static int
handle_event_TxnCacheLookupStatsGet(INKCont contp, INKEvent event, void *edata)
{
  int re = 0;
  INKHttpTxn txn = (INKHttpTxn) edata;
  INKDebug("INKHttpTxnCacheLookupStatusGet", "handle_event(txn=0x%08x, event=%d)", txn, event);
  switch (event) {

  case INK_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
    /* client response not available */
    re = CacheLookupStatusGet(txn);
    INKHttpTxnReenable(txn, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_SEND_RESPONSE_HDR:
    /* client response available */
    /* re = CacheLookupStatusGet(txn,edata); */
    re = Request2Response(txn);
    INKHttpTxnReenable(txn, INK_EVENT_HTTP_CONTINUE);
    break;

  default:
    INKDebug("INKHttpTxnCacheLookupStatusGet", "undefined event %d", event);
    INKHttpTxnReenable(txn, INK_EVENT_HTTP_CONTINUE);
    break;
  }
  return re;
}

void
INKPluginInit(int argc, const char *argv[])
{
  INKCont contp;

  constDataInit();

  contp = INKContCreate(handle_event_TxnCacheLookupStatsGet, INKMutexCreate());

  INKHttpHookAdd(INK_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, contp);

  /* Get client response */
  INKHttpHookAdd(INK_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
}
