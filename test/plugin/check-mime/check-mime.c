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

#include "InkAPI.h"

#define STRING_SIZE 100


static void
printMimeFields(INKMBuffer hdrBuf, INKMLoc httpHdrLoc, char *comment)
{
  INKMLoc fieldLoc, currentFieldLoc;

  int iFieldIndex, iFieldNameLength, iFieldValueLength, iFieldCount, iHdrLength;
  char *outputString;
  const char *sFieldValue = '\0';
  const char *sFieldName = '\0';

  iFieldIndex = 0;

  outputString = (char *) INKmalloc(STRING_SIZE);


  printf("**********************************************************\n");
  /* Get the total MIME field count */
  iFieldCount = INKMimeHdrFieldsCount(hdrBuf, httpHdrLoc);
  printf("(%s): Total # of Mime fields = %d\n", comment, iFieldCount);

  /* Get the MIME header length */
  iHdrLength = INKMimeHdrLengthGet(hdrBuf, httpHdrLoc);
  printf("(%s) MIME Header length: %d\n", comment, iHdrLength);

  fieldLoc = INKMimeHdrFieldGet(hdrBuf, httpHdrLoc, 0);



  /* Print all the Mime Field in the MIME header */
  while (fieldLoc) {
    /* Remember current field location */
    currentFieldLoc = fieldLoc;

    printf("--------------------------\n");

    sFieldName = INKMimeHdrFieldNameGet(hdrBuf, httpHdrLoc, fieldLoc, &iFieldNameLength);
    if (iFieldNameLength) {
      strncpy(outputString, sFieldName, iFieldNameLength);
      outputString[iFieldNameLength] = '\0';
      printf("(%s) Field Name [%d]: %s\n", comment, iFieldNameLength, outputString);
    }

    do {
      sFieldValue = INKMimeHdrFieldValueGet(hdrBuf, httpHdrLoc, fieldLoc, iFieldIndex, &iFieldValueLength);
      if (iFieldValueLength) {
        strncpy(outputString, sFieldValue, iFieldValueLength);
        outputString[iFieldValueLength] = '\0';
        printf("(%s) Field Value [%d]: %s\n", comment, iFieldValueLength, outputString);
      }
    } while ((fieldLoc = INKMimeHdrFieldNextDup(hdrBuf, httpHdrLoc, fieldLoc)) != (INKMLoc) NULL);

    fieldLoc = INKMimeHdrFieldNext(hdrBuf, httpHdrLoc, currentFieldLoc);
  }
  printf("**********************************************************\n");

  /* Release */
}



static void
addDupFields(INKMBuffer hdrBuf, INKMLoc httpHdrLoc)
{
  INKMBuffer tmpBuf;
  INKMLoc tmpHttpHdrLoc;
  INKMLoc tmpFieldLoc, newFieldLoc;


  printf(">>>>>> checkDupField <<<<<<\n");
  tmpBuf = INKMBufferCreate();

  tmpHttpHdrLoc = INKHttpHdrCreate(tmpBuf);

  /* Copy the resp Mime Header to the tmp Mime Header */
  INKHttpHdrCopy(tmpBuf, tmpHttpHdrLoc, hdrBuf, httpHdrLoc);

  tmpFieldLoc = INKMimeHdrFieldGet(tmpBuf, tmpHttpHdrLoc, 0);


  /* Create a MIME field */
  newFieldLoc = INKMimeHdrFieldCreate(tmpBuf, tmpHttpHdrLoc);
  INKMimeHdrFieldNameSet(tmpBuf, tmpHttpHdrLoc, newFieldLoc, "Dummy-Field-1", strlen("Dummy-Field-1"));
  INKMimeHdrFieldValueInsert(tmpBuf, tmpHttpHdrLoc, newFieldLoc, "dummy-value-1", strlen("dummy-value-1"), -1);
  INKMimeHdrFieldInsert(tmpBuf, tmpHttpHdrLoc, newFieldLoc, -1);

  newFieldLoc = INKMimeHdrFieldCreate(tmpBuf, tmpHttpHdrLoc);
  INKMimeHdrFieldInsert(tmpBuf, tmpHttpHdrLoc, newFieldLoc, -1);
  INKMimeHdrFieldNameSet(tmpBuf, tmpHttpHdrLoc, newFieldLoc, "Dummy-Field-2", strlen("Dummy-Field-2"));
  INKMimeHdrFieldValueInsert(tmpBuf, tmpHttpHdrLoc, newFieldLoc, "dummy-value-2", strlen("dummy-value-2"), -1);

  /* Insert a some duplicate fields (duplicate name) */
  newFieldLoc = INKMimeHdrFieldCreate(tmpBuf, tmpHttpHdrLoc);
  INKMimeHdrFieldInsert(tmpBuf, tmpHttpHdrLoc, newFieldLoc, 1);
  INKMimeHdrFieldNameSet(tmpBuf, tmpHttpHdrLoc, newFieldLoc, "Dummy-Field-2", strlen("Dummy-Field-2"));
  INKMimeHdrFieldValueInsert(tmpBuf, tmpHttpHdrLoc, newFieldLoc, "dup_dummy-value-1", strlen("dup_dummy-value-1"), -1);

  newFieldLoc = INKMimeHdrFieldCreate(tmpBuf, tmpHttpHdrLoc);
  INKMimeHdrFieldInsert(tmpBuf, tmpHttpHdrLoc, newFieldLoc, 1);
  INKMimeHdrFieldNameSet(tmpBuf, tmpHttpHdrLoc, newFieldLoc, "Dummy-Field-2", strlen("Dummy-Field-2"));
  INKMimeHdrFieldValueInsert(tmpBuf, tmpHttpHdrLoc, newFieldLoc, "dup_dummy-value-2", strlen("dup_dummy-value-2"), -1);

  printMimeFields(tmpBuf, tmpHttpHdrLoc, "addDupFields:");

  /* Release */
}



static void
sectionMimeHdr(INKMBuffer hdrBuf, INKMLoc httpHdrLoc)
{
  INKMBuffer tmpBuf;

  INKMLoc tmpHttpHdrLoc;
  INKMLoc fieldLoc, tmpFieldLoc, newFieldLoc;

  INKHttpType httpType;

  int iFieldCount;
  time_t respDate;


  /* Get the field location */
  fieldLoc = INKMimeHdrFieldGet(hdrBuf, httpHdrLoc, 0);

  httpType = INKHttpHdrTypeGet(hdrBuf, httpHdrLoc);

  printf("\n>>> sectionMimeHdr <<<<\n");


        /************* INK_HTTP_TYPE_REQUEST ******************/
  if (httpType == INK_HTTP_TYPE_REQUEST) {
    printf("\n>>> REQUEST <<<<\n");

    printMimeFields(hdrBuf, httpHdrLoc, "INK_HTTP_TYPE_REQUEST");

  }
  /* httpType == INK_HTTP_TYPE_REQUEST */
  printf("------- 1\n");

        /************ INK_HTTP_TYPE_RESPONSE ******************/
  if (httpType == INK_HTTP_TYPE_RESPONSE) {
    printf("\n>>> RESPONSE <<<<\n");

                /**** 1: Simply print the response header ****/
    printMimeFields(hdrBuf, httpHdrLoc, "RESP: 1");

                /**** Insert some duplicate fields ****/
    addDupFields(hdrBuf, httpHdrLoc);

                /**** 2: delete some MIME fields ****/
    /* Copy the respHdrBuf Mime Headers to a tmp buf and print the details */
    tmpBuf = INKMBufferCreate();
    tmpHttpHdrLoc = INKHttpHdrCreate(tmpBuf);
    /*INKHttpHdrCopy (tmpBuf, tmpHttpHdrLoc, hdrBuf, httpHdrLoc); */
    INKMimeHdrCopy(tmpBuf, tmpHttpHdrLoc, hdrBuf, httpHdrLoc);


    /* Remove the "Via" fielsd */
    tmpFieldLoc = INKMimeHdrFieldFind(tmpBuf, tmpHttpHdrLoc, "Via", strlen("Via"));
    INKMimeHdrFieldRemove(tmpBuf, tmpHttpHdrLoc, tmpFieldLoc);

    printMimeFields(tmpBuf, tmpHttpHdrLoc, "RESP: 2: after remove");

    /* Re-attach the "removed" field */
    INKMimeHdrFieldInsert(tmpBuf, tmpHttpHdrLoc, tmpFieldLoc, -1);
    printMimeFields(tmpBuf, tmpHttpHdrLoc, "RESP: 2: after remove/reattach");

    /* Delete the "Via" field */
    tmpFieldLoc = INKMimeHdrFieldFind(tmpBuf, tmpHttpHdrLoc, "Via", strlen("Via"));
    INKMimeHdrFieldDelete(tmpBuf, tmpHttpHdrLoc, tmpFieldLoc);

    /*INKqa08815: to be consistant, release the handle must be done for MIME hdr
       delete or destroy operations */
    INKHandleMLocRelease(tmpBuf, tmpHttpHdrLoc, tmpFieldLoc);


    /* Get the field count again */
    iFieldCount = INKMimeHdrFieldsCount(tmpBuf, tmpHttpHdrLoc);
    printf("(RESP): >>> Total # of Mime fields = %d\n", iFieldCount);

    printMimeFields(tmpBuf, tmpHttpHdrLoc, "RESP: 2: after delete");



                /**** section 3 ****/
    /* --------------------------------------------------------------------
     * Now, insert some fields into the MIME buffer
     * Note: 
     *      1. Field name can be set before/after INKMimeHdrFieldInsert
     *      2. Field value could be set *only* after INKMimeHdrFieldValueInsert
     *      
     * (Point 1. and 2. implies that its possible to insert fields with empty
     * name and values)
     * --------------------------------------------------------------------*/

    /* Create a MIME field */
    newFieldLoc = INKMimeHdrFieldCreate(tmpBuf, tmpHttpHdrLoc);
    INKMimeHdrFieldNameSet(tmpBuf, tmpHttpHdrLoc, newFieldLoc, "Dummy-Field-1", strlen("Dummy-Field-1"));
    INKMimeHdrFieldValueInsert(tmpBuf, tmpHttpHdrLoc, newFieldLoc, "dummy-value-1", strlen("dummy-value-1"), -1);

    /* Now, do the insert : prepend it to the list of fields */
    INKMimeHdrFieldInsert(tmpBuf, tmpHttpHdrLoc, newFieldLoc, 0);

    printMimeFields(tmpBuf, tmpHttpHdrLoc, "RESP: 3");


                /**** 4: append some field value ****/
    /* Now again change the new added field value */
    newFieldLoc = INKMimeHdrFieldFind(tmpBuf, tmpHttpHdrLoc, "Dummy-Field-1", strlen("Dummy-Field-1"));
    INKMimeHdrFieldValueSet(tmpBuf, tmpHttpHdrLoc, newFieldLoc, -1, "dummy-value-3", strlen("dummy-value-3"));

    /* Now, append a string to the newly set field value */
    INKMimeHdrFieldValueAppend(tmpBuf, tmpHttpHdrLoc, newFieldLoc, 0, "<appended-text>", strlen("<appended-text>"));

    printMimeFields(tmpBuf, tmpHttpHdrLoc, "RESP: 4");


                /***** 5: clear values for few fields ******/
    /* fieldLoc = INKMimeHdrFieldFind (tmpBuf, tmpHttpHdrLoc, "Date", strlen("Date")); */
    fieldLoc = INKMimeHdrFieldRetrieve(tmpBuf, tmpHttpHdrLoc, INK_MIME_FIELD_DATE);
    INKMimeHdrFieldValuesClear(tmpBuf, tmpHttpHdrLoc, fieldLoc);

    /*fieldLoc = INKMimeHdrFieldFind (tmpBuf, tmpHttpHdrLoc, "Age", strlen("Age")); */
    fieldLoc = INKMimeHdrFieldRetrieve(tmpBuf, tmpHttpHdrLoc, INK_MIME_FIELD_AGE);
    INKMimeHdrFieldValuesClear(tmpBuf, tmpHttpHdrLoc, fieldLoc);

    /*fieldLoc = INKMimeHdrFieldFind (tmpBuf, tmpHttpHdrLoc, "Content-Type", strlen("Content-Type")); */
    fieldLoc = INKMimeHdrFieldRetrieve(tmpBuf, tmpHttpHdrLoc, INK_MIME_FIELD_CONTENT_TYPE);
    INKMimeHdrFieldValuesClear(tmpBuf, tmpHttpHdrLoc, fieldLoc);

    printMimeFields(tmpBuf, tmpHttpHdrLoc, "RESP: 5");

                /***** 6: clear all the MIME fields *****/
    INKMimeHdrFieldsClear(tmpBuf, tmpHttpHdrLoc);
    printMimeFields(tmpBuf, tmpHttpHdrLoc, "RESP: 6");

    /* Release */

  }
  /* httpType == INK_HTTP_TYPE_RESPONSE */
}


/****************************************************************************
 * handleReadRequest:
 *
 * Description: handler for INK_HTTP_READ_REQUEST_HDR_HOOK
 ****************************************************************************/

static void
handleReadRequest(INKCont pCont, INKHttpTxn pTxn)
{
  INKMBuffer reqHdrBuf;
  INKMLoc reqHttpHdrLoc;

  printf("\n>>>>>> handleReadRequest <<<<<<<\n");

  /* Get Request Marshall Buffer */
  if (!INKHttpTxnClientReqGet(pTxn, &reqHdrBuf, &reqHttpHdrLoc)) {
    INKError("couldn't retrieve client request header\n");
    /* Release parent handle to the marshall buffer */
    INKHandleMLocRelease(reqHdrBuf, INK_NULL_MLOC, reqHttpHdrLoc);
    goto done;
  }

  /* Do the Mime stuff now */
  sectionMimeHdr(reqHdrBuf, reqHttpHdrLoc);

  /* Release */
  INKHandleMLocRelease(reqHdrBuf, INK_NULL_MLOC, reqHttpHdrLoc);


done:
  INKHttpTxnReenable(pTxn, INK_EVENT_HTTP_CONTINUE);

}


/************************************************************************
 * handleSendResponse:
 *
 * Description:	handler for INK_HTTP_SEND_RESPONSE_HOOK
 ************************************************************************/

static void
handleSendResponse(INKCont pCont, INKHttpTxn pTxn)
{
  INKMBuffer respHdrBuf;
  INKMLoc respHttpHdrLoc;

  printf("\n>>> handleSendResponse <<<<\n");

  /* Get Response Marshall Buffer */
  if (!INKHttpTxnClientRespGet(pTxn, &respHdrBuf, &respHttpHdrLoc)) {
    INKError("couldn't retrieve server response header\n");
    INKHandleMLocRelease(respHdrBuf, INK_NULL_MLOC, respHttpHdrLoc);
    goto done;
  }

  /* Do the Mime stuff now */
  sectionMimeHdr(respHdrBuf, respHttpHdrLoc);

  /* Release */
  INKHandleMLocRelease(respHdrBuf, INK_NULL_MLOC, respHttpHdrLoc);


done:
  INKHttpTxnReenable(pTxn, INK_EVENT_HTTP_CONTINUE);
}




static void
handleTxnStart(INKCont pCont, INKHttpTxn pTxn)
{
  printf("This is a transaction start hook --- 1\n");

  /* add READ_REQUEST_HDR_HOOK */
  INKHttpTxnHookAdd(pTxn, INK_HTTP_READ_REQUEST_HDR_HOOK, pCont);
  INKHttpTxnHookAdd(pTxn, INK_HTTP_SEND_RESPONSE_HDR_HOOK, pCont);

done:
  INKHttpTxnReenable(pTxn, INK_EVENT_HTTP_CONTINUE);
}


static int
samplePlugin(INKCont pCont, INKEvent event, void *edata)
{
  INKHttpTxn pTxn = (INKHttpTxn) edata;

  switch (event) {
  case INK_EVENT_HTTP_TXN_START:
    handleTxnStart(pCont, pTxn);
    return 0;
  case INK_EVENT_HTTP_SEND_RESPONSE_HDR:
    handleSendResponse(pCont, pTxn);
    return 0;
  case INK_EVENT_HTTP_READ_REQUEST_HDR:
    handleReadRequest(pCont, pTxn);
    return 0;
  default:
    break;
  }

  return 0;
}


void
INKPluginInit(int argc, const char *argv[])
{
  INKCont pCont;

  const char *ts_install_dir = INKInstallDirGet();
  const char *plugin_dir = INKPluginDirGet();

  /* Print the Traffic Server install and the plugin directory */
  printf("TS install dir: %s\n", ts_install_dir);
  printf("Plugin dir: %s\n", plugin_dir);

  pCont = INKContCreate(samplePlugin, NULL);

  INKHttpHookAdd(INK_HTTP_TXN_START_HOOK, pCont);
}
