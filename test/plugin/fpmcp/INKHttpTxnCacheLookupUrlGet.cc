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


/* Plugin passes if there are no interface errors
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

const char *EXPECTED_HTTP_DOC = "x-expected_http_doc";
static int EXPECTED_HTTP_DOC_LEN;

const char *ACTUAL_HTTP_DOC = "x-actual_http_doc";
static int ACTUAL_HTTP_DOC_LEN;


const char *TRUE = "true";
const char *INTERFACE = "INKHttpTxnCacheLookupUrlGet";

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

  EXPECTED_HTTP_DOC_LEN = strlen(EXPECTED_HTTP_DOC);

  ACTUAL_HTTP_DOC_LEN = strlen(ACTUAL_HTTP_DOC);
}


const char *TEST_PASS = "pass";
const char *TEST_FAIL = "fail";
const char *VALUE_NOT_FOUND = "<MIME values not found>";

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
    INKDebug("INKHttpTxnCacheLookupUrlGet",
             "\n mimeValueGet: in [%s], separator [%c] !found \n", pval, separator_token);
    return NULL;
  }
  ptr++;
  if (!ptr) {
    INKDebug("INKHttpTxnCacheLookupUrlGet", "\n mimeValueGet: in [%s], value after separator !found \n", pval);
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
    INKDebug("INKHttpTxnCacheLookupUrlGet", "\n getTestParam: INKMimeHdrFieldFind did not find %s\n", mimeHdr);
    return 0;
  }

  ptr = INKMimeHdrFieldValueGet(buff, loc, field_loc, -1, &len);
  if (!ptr || (len == 0)) {
    INKDebug("INKHttpTxnCacheLookupUrlGet", "\n getTestParam: INKMimeHdrFieldValueGet did not find %s \n", mimeHdr);
    return 0;
  }

  *mimeValue = (void *) INKstrndup(ptr, strlen(ptr));
  INKDebug("INKHttpTxnCacheLookupUrlGet", "\n getTestParam: hdr = [%s], val = [%s]\n", mimeHdr, *(char **) mimeValue);

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
      INKDebug("INKHttpTxnCacheLookupUrlGet", "\n setTestResult: INKMimeHdrFieldCreate failed ");
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
  INKDebug("INKHttpTxnCacheLookupUrlGet", "\n setTestResult: %s  [%s: %s] \n", action, mimeHdr, mimeValues);
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
    INKDebug("INKHttpTxnCacheLookupUrlGet", "\n Request2Response: INKHttpTxnClientReqGet failed\n ");
    return 0;
  }
  if (!INKHttpTxnClientRespGet(txn, &respBuff, &respLoc)) {
    INKDebug("INKHttpTxnCacheLookupUrlGet", "\n Request2Response: INKHttpTxnClientRespGet failed\n ");
    return 0;
  }

  getTestParam(reqBuff, reqLoc, ACTUAL_CALL_RESULT, ACTUAL_CALL_RESULT_LEN, &pval);
  if (!pval) {
    pval = (void *) VALUE_NOT_FOUND;
    err = 0;
  }
  setTestResult(respBuff, respLoc, ACTUAL_CALL_RESULT, ACTUAL_CALL_RESULT_LEN, (char *) pval);
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


  getTestParam(reqBuff, reqLoc, ACTUAL_HTTP_DOC, ACTUAL_HTTP_DOC_LEN, &pval);
  if (!pval) {
    pval = (void *) VALUE_NOT_FOUND;
    err = 0;
  }
  setTestResult(respBuff, respLoc, ACTUAL_HTTP_DOC, ACTUAL_HTTP_DOC_LEN, (char *) pval);
  if (!(pval == VALUE_NOT_FOUND))
    INKfree(pval);
  pval = NULL;

  setTestResult(respBuff, respLoc, API_INTERFACE_NAME, API_INTERFACE_NAME_LEN, (char *) INTERFACE);
  /* final step */
  INKHandleMLocRelease(reqBuff, reqLoc, INK_NULL_MLOC);
  INKHandleMLocRelease(respBuff, respLoc, INK_NULL_MLOC);

  return err;
}


/* Returns 1 on success, 0 or value of "re" on failure */
static int
CacheLookupUrlGet(INKHttpTxn txn)
{
  int re = 0, expectedRe = 0, err = 1;
  char *finalTestResult = (char *) TEST_PASS;
  const char *urlString = NULL;
  int urlStringLen = 0;
  const char *actualUrlParams = NULL;
  const char *expectedUrlParams = NULL;
  char *expectedHttpDoc = NULL;

  /* generic buffer & pointer */
  char actualHttpDocument[BUFSIZ];
  char bufValues[BUFSIZ];
  void *pField = NULL, *pVal = NULL;
  char *str = NULL;

  INKMBuffer reqBuff;
  INKMLoc reqLoc;
  INKMBuffer urlBuff;
  INKMLoc urlLoc;
  int urlLen = 0;


  if (!INKHttpTxnClientReqGet(txn, &reqBuff, &reqLoc)) {
    INKDebug("INKHttpTxnCacheLookupUrlGet", "\n CacheLookupUrlGet: INKHttpTxnClientReqGet failed ");
    return 0;
  }

  urlBuff = INKMBufferCreate();
  urlLoc = INKUrlCreate(urlBuff);

  re = INKHttpTxnCacheLookupUrlGet(txn, urlBuff, urlLoc);
  if (!re) {
    INKDebug("INKHttpTxnCacheLookupUrlGet", "\n CacheLookupUrlGet: INKHttpTxnCacheLookupUrlGet failed ");
    finalTestResult = (char *) TEST_FAIL;
    err = 0;
    goto testDone;
  }

  urlString = INKUrlStringGet(urlBuff, urlLoc, &urlStringLen);
  if (!urlStringLen) {
    finalTestResult = (char *) TEST_FAIL;
    err = 0;
    goto testDone;
  }
  str = INKstrndup(urlString, (urlStringLen + 1));
  strncpy(str, urlString, (urlStringLen + 1));
  str[urlStringLen + 1] = '\0';
  sprintf(actualHttpDocument, "%s", str);
  INKfree(str);
  INKHandleStringRelease(urlBuff, urlLoc, urlString);


  INKDebug("INKHttpTxnCacheLookupUrlGet", "\n CacheLookupUrlGet: look up of [%s]\n", actualHttpDocument);


  getTestParam(reqBuff, reqLoc, EXPECTED_HTTP_DOC, EXPECTED_HTTP_DOC_LEN, &pField);
  if (!pField) {
    finalTestResult = (char *) TEST_FAIL;
    err = 0;
    goto testDone;
  }
  pVal = mimeValueGet((const char *) pField, SEPARATOR_TOKEN);
  INKfree(pField);
  pField = NULL;
  if (!pVal) {
    finalTestResult = (char *) TEST_FAIL;
    err = 0;
    goto testDone;
  }
  expectedHttpDoc = (char *) pVal;

  INKDebug("INKHttpTxnCacheLookupUrlGet",
           "\n CacheLookupUrlGet: comparing exp=[%s] to actual=[%s] document \n", expectedHttpDoc, actualHttpDocument);

  if (strcmp(actualHttpDocument, expectedHttpDoc)) {
    finalTestResult = (char *) TEST_FAIL;
    err = 0;
  }
  setTestResult(reqBuff, reqLoc, ACTUAL_HTTP_DOC, ACTUAL_HTTP_DOC_LEN, actualHttpDocument);

  INKfree(expectedHttpDoc);
  INKUrlDestroy(urlBuff, urlLoc);
  INKMBufferDestroy(urlBuff);
  INKHandleMLocRelease(reqBuff, INK_NULL_MLOC, reqLoc);


  getTestParam(reqBuff, reqLoc, EXPECTED_CALL_RESULT, EXPECTED_CALL_RESULT_LEN, &pField);
  if (!pField) {
    finalTestResult = (char *) TEST_FAIL;
    err = 0;
    goto skipTestCmp_CALL_RESULT;
  }
  pVal = mimeValueGet((const char *) pField, SEPARATOR_TOKEN);
  if (!pVal) {
    finalTestResult = (char *) TEST_FAIL;
    err = 0;
    goto skipTestCmp_CALL_RESULT;
  }
  expectedRe = atoi((char *) pVal);
  INKfree(pField);
  pField = NULL;
  if (re != expectedRe || (re == 0)) {
    INKDebug("INKHttpTxnCacheLookupUrlGet",
             "\n INKHttpTxnCacheLookupStatusGet: expected re %d, got %d ", expectedRe, re);
    finalTestResult = (char *) TEST_FAIL;
    err = 0;
  }
  INKfree(pVal);

skipTestCmp_CALL_RESULT:
  sprintf(bufValues, "got=%d", re);     /* test generator asses pass/fail */
  setTestResult(reqBuff, reqLoc, ACTUAL_CALL_RESULT, ACTUAL_CALL_RESULT_LEN, bufValues);


testDone:
  /* process test result */
  getTestParam(reqBuff, reqLoc, EXPECTED_TEST_RESULT, EXPECTED_TEST_RESULT_LEN, &pField);
  if (!pField) {
    finalTestResult = (char *) TEST_FAIL;
    err = 0;
    sprintf(bufValues, " result=%s", finalTestResult);
  } else {
    pVal = mimeValueGet((const char *) pField, SEPARATOR_TOKEN);
    if (!pVal) {
      finalTestResult = (char *) TEST_FAIL;
      err = 0;
      sprintf(bufValues, " result=%s", finalTestResult);
    } else {                    /* expected    actual */
      if (!strcmp((char *) pVal, finalTestResult))
        sprintf(bufValues, " result=%s,  <exp:%s>=<actual:%s>", TEST_PASS, (char *) pVal, finalTestResult);
      else
        sprintf(bufValues, " result=%s,  <exp:%s>=<actual:%s>", TEST_FAIL, (char *) pVal, finalTestResult);
    }
  }
  setTestResult(reqBuff, reqLoc, ACTUAL_TEST_RESULT, ACTUAL_TEST_RESULT_LEN, bufValues);

  INKHandleMLocRelease(reqBuff, INK_NULL_MLOC, reqLoc);
  return err;
}


static int
handle_event_CacheLookupUrlGet(INKCont contp, INKEvent event, void *edata)
{
  int re = 0;
  INKHttpTxn txn = (INKHttpTxn) edata;

  switch (event) {

  case INK_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
    re = CacheLookupUrlGet(txn);
    INKHttpTxnReenable(txn, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_SEND_RESPONSE_HDR:
    re = Request2Response(txn);
    INKHttpTxnReenable(txn, INK_EVENT_HTTP_CONTINUE);
    break;

  default:
    INKDebug("INKHttpTxnCacheLookupUrlGet", "undefined event %d", event);
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

  contp = INKContCreate(handle_event_CacheLookupUrlGet, INKMutexCreate());

  INKHttpHookAdd(INK_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, contp);

  /* Get client response */
  INKHttpHookAdd(INK_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
}
