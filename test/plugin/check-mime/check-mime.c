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

/**************************************************************
 * Sample-1.c
 *
 * Plugin to test all the HTTP Header functions to make sure
 * SDK 2.0 plugins developed on Panda are binary compatible
 * with Tomcat (on Sol only).
 *
 **************************************************************/


#include <stdio.h>
#include <string.h>
#include <time.h>

#if !defined (_WIN32)
#	include <unistd.h>
#else
#	include <windows.h>
#endif

#include "ts.h"

#define STRING_SIZE 100


static void
printMimeFields(TSMBuffer hdrBuf, TSMLoc httpHdrLoc, char *comment)
{
  TSMLoc fieldLoc, currentFieldLoc;

  int iFieldIndex, iFieldNameLength, iFieldValueLength, iFieldCount, iHdrLength;
  char *outputString;
  const char *sFieldValue = '\0';
  const char *sFieldName = '\0';

  iFieldIndex = 0;

  outputString = (char *) TSmalloc(STRING_SIZE);


  printf("**********************************************************\n");
  /* Get the total MIME field count */
  iFieldCount = TSMimeHdrFieldsCount(hdrBuf, httpHdrLoc);
  printf("(%s): Total # of Mime fields = %d\n", comment, iFieldCount);

  /* Get the MIME header length */
  iHdrLength = TSMimeHdrLengthGet(hdrBuf, httpHdrLoc);
  printf("(%s) MIME Header length: %d\n", comment, iHdrLength);

  fieldLoc = TSMimeHdrFieldGet(hdrBuf, httpHdrLoc, 0);



  /* Print all the Mime Field in the MIME header */
  while (fieldLoc) {
    /* Remember current field location */
    currentFieldLoc = fieldLoc;

    printf("--------------------------\n");

    sFieldName = TSMimeHdrFieldNameGet(hdrBuf, httpHdrLoc, fieldLoc, &iFieldNameLength);
    if (iFieldNameLength) {
      strncpy(outputString, sFieldName, iFieldNameLength);
      outputString[iFieldNameLength] = '\0';
      printf("(%s) Field Name [%d]: %s\n", comment, iFieldNameLength, outputString);
    }

    do {
      TSMimeHdrFieldValueStringGet(hdrBuf, httpHdrLoc, fieldLoc, iFieldIndex, &sFieldValue, &iFieldValueLength);
      if (iFieldValueLength) {
        strncpy(outputString, sFieldValue, iFieldValueLength);
        outputString[iFieldValueLength] = '\0';
        printf("(%s) Field Value [%d]: %s\n", comment, iFieldValueLength, outputString);
      }
    } while ((fieldLoc = TSMimeHdrFieldNextDup(hdrBuf, httpHdrLoc, fieldLoc)) != (TSMLoc) NULL);

    fieldLoc = TSMimeHdrFieldNext(hdrBuf, httpHdrLoc, currentFieldLoc);
  }
  printf("**********************************************************\n");

  /* Release */
}



static void
addDupFields(TSMBuffer hdrBuf, TSMLoc httpHdrLoc)
{
  TSMBuffer tmpBuf;
  TSMLoc tmpHttpHdrLoc;
  TSMLoc tmpFieldLoc, newFieldLoc;


  printf(">>>>>> checkDupField <<<<<<\n");
  tmpBuf = TSMBufferCreate();

  tmpHttpHdrLoc = TSHttpHdrCreate(tmpBuf);

  /* Copy the resp Mime Header to the tmp Mime Header */
  TSHttpHdrCopy(tmpBuf, tmpHttpHdrLoc, hdrBuf, httpHdrLoc);

  tmpFieldLoc = TSMimeHdrFieldGet(tmpBuf, tmpHttpHdrLoc, 0);


  /* Create a MIME field */
  newFieldLoc = TSMimeHdrFieldCreate(tmpBuf, tmpHttpHdrLoc);
  TSMimeHdrFieldNameSet(tmpBuf, tmpHttpHdrLoc, newFieldLoc, "Dummy-Field-1", strlen("Dummy-Field-1"));
  TSMimeHdrFieldValueStringInsert(tmpBuf, tmpHttpHdrLoc, newFieldLoc, "dummy-value-1", strlen("dummy-value-1"), -1);
  TSMimeHdrFieldAppend(tmpBuf, tmpHttpHdrLoc, newFieldLoc);

  newFieldLoc = TSMimeHdrFieldCreate(tmpBuf, tmpHttpHdrLoc);
  TSMimeHdrFieldAppend(tmpBuf, tmpHttpHdrLoc, newFieldLoc);
  TSMimeHdrFieldNameSet(tmpBuf, tmpHttpHdrLoc, newFieldLoc, "Dummy-Field-2", strlen("Dummy-Field-2"));
  TSMimeHdrFieldValueStringInsert(tmpBuf, tmpHttpHdrLoc, newFieldLoc, "dummy-value-2", strlen("dummy-value-2"), -1);

  /* Insert a some duplicate fields (duplicate name) */
  newFieldLoc = TSMimeHdrFieldCreate(tmpBuf, tmpHttpHdrLoc);
  TSMimeHdrFieldAppend(tmpBuf, tmpHttpHdrLoc, newFieldLoc);
  TSMimeHdrFieldNameSet(tmpBuf, tmpHttpHdrLoc, newFieldLoc, "Dummy-Field-2", strlen("Dummy-Field-2"));
  TSMimeHdrFieldValueStringInsert(tmpBuf, tmpHttpHdrLoc, newFieldLoc, "dup_dummy-value-1", strlen("dup_dummy-value-1"), -1);

  newFieldLoc = TSMimeHdrFieldCreate(tmpBuf, tmpHttpHdrLoc);
  TSMimeHdrFieldAppend(tmpBuf, tmpHttpHdrLoc, newFieldLoc);
  TSMimeHdrFieldNameSet(tmpBuf, tmpHttpHdrLoc, newFieldLoc, "Dummy-Field-2", strlen("Dummy-Field-2"));
  TSMimeHdrFieldValueStringInsert(tmpBuf, tmpHttpHdrLoc, newFieldLoc, "dup_dummy-value-2", strlen("dup_dummy-value-2"), -1);

  printMimeFields(tmpBuf, tmpHttpHdrLoc, "addDupFields:");

  /* Release */
}



static void
sectionMimeHdr(TSMBuffer hdrBuf, TSMLoc httpHdrLoc)
{
  TSMBuffer tmpBuf;

  TSMLoc tmpHttpHdrLoc;
  TSMLoc fieldLoc, tmpFieldLoc, newFieldLoc;

  TSHttpType httpType;

  int iFieldCount;
  time_t respDate;


  /* Get the field location */
  fieldLoc = TSMimeHdrFieldGet(hdrBuf, httpHdrLoc, 0);

  httpType = TSHttpHdrTypeGet(hdrBuf, httpHdrLoc);

  printf("\n>>> sectionMimeHdr <<<<\n");


        /************* TS_HTTP_TYPE_REQUEST ******************/
  if (httpType == TS_HTTP_TYPE_REQUEST) {
    printf("\n>>> REQUEST <<<<\n");

    printMimeFields(hdrBuf, httpHdrLoc, "TS_HTTP_TYPE_REQUEST");

  }
  /* httpType == TS_HTTP_TYPE_REQUEST */
  printf("------- 1\n");

        /************ TS_HTTP_TYPE_RESPONSE ******************/
  if (httpType == TS_HTTP_TYPE_RESPONSE) {
    printf("\n>>> RESPONSE <<<<\n");

                /**** 1: Simply print the response header ****/
    printMimeFields(hdrBuf, httpHdrLoc, "RESP: 1");

                /**** Insert some duplicate fields ****/
    addDupFields(hdrBuf, httpHdrLoc);

                /**** 2: delete some MIME fields ****/
    /* Copy the respHdrBuf Mime Headers to a tmp buf and print the details */
    tmpBuf = TSMBufferCreate();
    tmpHttpHdrLoc = TSHttpHdrCreate(tmpBuf);
    /*TSHttpHdrCopy (tmpBuf, tmpHttpHdrLoc, hdrBuf, httpHdrLoc); */
    TSMimeHdrCopy(tmpBuf, tmpHttpHdrLoc, hdrBuf, httpHdrLoc);


    /* Remove the "Via" fielsd */
    tmpFieldLoc = TSMimeHdrFieldFind(tmpBuf, tmpHttpHdrLoc, "Via", strlen("Via"));
    TSMimeHdrFieldRemove(tmpBuf, tmpHttpHdrLoc, tmpFieldLoc);

    printMimeFields(tmpBuf, tmpHttpHdrLoc, "RESP: 2: after remove");

    /* Re-attach the "removed" field */
    TSMimeHdrFieldAppend(tmpBuf, tmpHttpHdrLoc, tmpFieldLoc);
    printMimeFields(tmpBuf, tmpHttpHdrLoc, "RESP: 2: after remove/reattach");

    /* Delete the "Via" field */
    tmpFieldLoc = TSMimeHdrFieldFind(tmpBuf, tmpHttpHdrLoc, "Via", strlen("Via"));
    TSMimeHdrFieldDestroy(tmpBuf, tmpHttpHdrLoc, tmpFieldLoc);

    /*TSqa08815: to be consistant, release the handle must be done for MIME hdr
       delete or destroy operations */
    TSHandleMLocRelease(tmpBuf, tmpHttpHdrLoc, tmpFieldLoc);


    /* Get the field count again */
    iFieldCount = TSMimeHdrFieldsCount(tmpBuf, tmpHttpHdrLoc);
    printf("(RESP): >>> Total # of Mime fields = %d\n", iFieldCount);

    printMimeFields(tmpBuf, tmpHttpHdrLoc, "RESP: 2: after delete");



                /**** section 3 ****/
    /* --------------------------------------------------------------------
     * Now, insert some fields into the MIME buffer
     * Note:
     *      1. Field name can be set before/after TSMimeHdrFieldAppend
     *      2. Field value could be set *only* after TSMimeHdrFieldValueStringInsert
     *
     * (Point 1. and 2. implies that its possible to insert fields with empty
     * name and values)
     * --------------------------------------------------------------------*/

    /* Create a MIME field */
    newFieldLoc = TSMimeHdrFieldCreate(tmpBuf, tmpHttpHdrLoc);
    TSMimeHdrFieldNameSet(tmpBuf, tmpHttpHdrLoc, newFieldLoc, "Dummy-Field-1", strlen("Dummy-Field-1"));
    TSMimeHdrFieldValueStringInsert(tmpBuf, tmpHttpHdrLoc, newFieldLoc, "dummy-value-1", strlen("dummy-value-1"), -1);

    /* Now, do the insert : prepend it to the list of fields. TODO: This no longer works, append only? */
    TSMimeHdrFieldAppend(tmpBuf, tmpHttpHdrLoc, newFieldLoc);

    printMimeFields(tmpBuf, tmpHttpHdrLoc, "RESP: 3");


                /**** 4: append some field value ****/
    /* Now again change the new added field value */
    newFieldLoc = TSMimeHdrFieldFind(tmpBuf, tmpHttpHdrLoc, "Dummy-Field-1", strlen("Dummy-Field-1"));
    TSMimeHdrFieldValueSet(tmpBuf, tmpHttpHdrLoc, newFieldLoc, -1, "dummy-value-3", strlen("dummy-value-3"));

    /* Now, append a string to the newly set field value */
    TSMimeHdrFieldValueAppend(tmpBuf, tmpHttpHdrLoc, newFieldLoc, 0, "<appended-text>", strlen("<appended-text>"));

    printMimeFields(tmpBuf, tmpHttpHdrLoc, "RESP: 4");


                /***** 5: clear values for few fields ******/
    /* fieldLoc = TSMimeHdrFieldFind (tmpBuf, tmpHttpHdrLoc, "Date", strlen("Date")); */
    fieldLoc = TSMimeHdrFieldFind(tmpBuf, tmpHttpHdrLoc, TS_MIME_FIELD_DATE, TS_MIME_LEN_DATE);
    TSMimeHdrFieldValuesClear(tmpBuf, tmpHttpHdrLoc, fieldLoc);

    /*fieldLoc = TSMimeHdrFieldFind (tmpBuf, tmpHttpHdrLoc, "Age", strlen("Age")); */
    fieldLoc = TSMimeHdrFieldFind(tmpBuf, tmpHttpHdrLoc, TS_MIME_FIELD_AGE, TS_MIME_LEN_AGE);
    TSMimeHdrFieldValuesClear(tmpBuf, tmpHttpHdrLoc, fieldLoc);

    /*fieldLoc = TSMimeHdrFieldFind (tmpBuf, tmpHttpHdrLoc, "Content-Type", strlen("Content-Type")); */
    fieldLoc = TSMimeHdrFieldFind(tmpBuf, tmpHttpHdrLoc, TS_MIME_FIELD_CONTENT_TYPE, TS_MIME_LEN_CONTENT_TYPE);
    TSMimeHdrFieldValuesClear(tmpBuf, tmpHttpHdrLoc, fieldLoc);

    printMimeFields(tmpBuf, tmpHttpHdrLoc, "RESP: 5");

                /***** 6: clear all the MIME fields *****/
    TSMimeHdrFieldsClear(tmpBuf, tmpHttpHdrLoc);
    printMimeFields(tmpBuf, tmpHttpHdrLoc, "RESP: 6");

    /* Release */

  }
  /* httpType == TS_HTTP_TYPE_RESPONSE */
}


/****************************************************************************
 * handleReadRequest:
 *
 * Description: handler for TS_HTTP_READ_REQUEST_HDR_HOOK
 ****************************************************************************/

static void
handleReadRequest(TSCont pCont, TSHttpTxn pTxn)
{
  TSMBuffer reqHdrBuf;
  TSMLoc reqHttpHdrLoc;

  printf("\n>>>>>> handleReadRequest <<<<<<<\n");

  /* Get Request Marshall Buffer */
  if (!TSHttpTxnClientReqGet(pTxn, &reqHdrBuf, &reqHttpHdrLoc)) {
    TSError("couldn't retrieve client request header\n");
    /* Release parent handle to the marshall buffer */
    TSHandleMLocRelease(reqHdrBuf, TS_NULL_MLOC, reqHttpHdrLoc);
    goto done;
  }

  /* Do the Mime stuff now */
  sectionMimeHdr(reqHdrBuf, reqHttpHdrLoc);

  /* Release */
  TSHandleMLocRelease(reqHdrBuf, TS_NULL_MLOC, reqHttpHdrLoc);


done:
  TSHttpTxnReenable(pTxn, TS_EVENT_HTTP_CONTINUE);

}


/************************************************************************
 * handleSendResponse:
 *
 * Description:	handler for TS_HTTP_SEND_RESPONSE_HOOK
 ************************************************************************/

static void
handleSendResponse(TSCont pCont, TSHttpTxn pTxn)
{
  TSMBuffer respHdrBuf;
  TSMLoc respHttpHdrLoc;

  printf("\n>>> handleSendResponse <<<<\n");

  /* Get Response Marshall Buffer */
  if (!TSHttpTxnClientRespGet(pTxn, &respHdrBuf, &respHttpHdrLoc)) {
    TSError("couldn't retrieve server response header\n");
    TSHandleMLocRelease(respHdrBuf, TS_NULL_MLOC, respHttpHdrLoc);
    goto done;
  }

  /* Do the Mime stuff now */
  sectionMimeHdr(respHdrBuf, respHttpHdrLoc);

  /* Release */
  TSHandleMLocRelease(respHdrBuf, TS_NULL_MLOC, respHttpHdrLoc);


done:
  TSHttpTxnReenable(pTxn, TS_EVENT_HTTP_CONTINUE);
}




static void
handleTxnStart(TSCont pCont, TSHttpTxn pTxn)
{
  printf("This is a transaction start hook --- 1\n");

  /* add READ_REQUEST_HDR_HOOK */
  TSHttpTxnHookAdd(pTxn, TS_HTTP_READ_REQUEST_HDR_HOOK, pCont);
  TSHttpTxnHookAdd(pTxn, TS_HTTP_SEND_RESPONSE_HDR_HOOK, pCont);

done:
  TSHttpTxnReenable(pTxn, TS_EVENT_HTTP_CONTINUE);
}


static int
samplePlugin(TSCont pCont, TSEvent event, void *edata)
{
  TSHttpTxn pTxn = (TSHttpTxn) edata;

  switch (event) {
  case TS_EVENT_HTTP_TXN_START:
    handleTxnStart(pCont, pTxn);
    return 0;
  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    handleSendResponse(pCont, pTxn);
    return 0;
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    handleReadRequest(pCont, pTxn);
    return 0;
  default:
    break;
  }

  return 0;
}


void
TSPluginInit(int argc, const char *argv[])
{
  TSCont pCont;

  const char *ts_install_dir = TSInstallDirGet();
  const char *plugin_dir = TSPluginDirGet();

  /* Print the Traffic Server install and the plugin directory */
  printf("TS install dir: %s\n", ts_install_dir);
  printf("Plugin dir: %s\n", plugin_dir);

  pCont = TSContCreate(samplePlugin, NULL);

  TSHttpHookAdd(TS_HTTP_TXN_START_HOOK, pCont);
}
