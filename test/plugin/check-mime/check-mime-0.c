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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if !defined (_WIN32)
#	include <unistd.h>
#else
#	include <windows.h>
#endif

#include "ts.h"
#include "macro.h"

#define STRING_SIZE 100

/* debug tags */
#define GENERAL     "general"
#define REQ         "request"
#define RESP        "response"
#define DEBUG_TAG   "API_ERROR"
#define AUTO_TAG    "AUTO_ERROR"
#define PLUGIN_NAME "check-mime-0"

/* added by ymou */
#define MY_TEST_HDR_1 "MY_TEST_HDR_1"
#define MY_TEST_HDR_2 "MY_TEST_HDR_2"
#define MY_TEST_HDR_3 "MY_TEST_HDR_3"


static void
printMimeFields(TSMBuffer hdrBuf, TSMLoc mimeHdrLoc, char *debugTag, float section)
{
  LOG_SET_FUNCTION_NAME("printMimeFields");

  TSMLoc fieldLoc = NULL, nextFieldLoc = NULL, nextDupFieldLoc = NULL;

  int iFieldIndex, iFieldNameLength, iFieldValueLength, iFieldCount, iHdrLength;
  char *outputString = NULL;
  const char *sFieldValue = NULL, *sFieldName = NULL;

  iFieldIndex = -1;

  TSDebug(debugTag, "***********************( %g )***********************", section);

  /* Get the total MIME field count */
  if ((iFieldCount = TSMimeHdrFieldsCount(hdrBuf, mimeHdrLoc)) == TS_ERROR) {
    LOG_API_ERROR("TSMimeHdrFieldsCount");
  } else {
    TSDebug(debugTag, "(%g) Total # of MIME fields = %d", section, iFieldCount);
  }

  /* Get the MIME header length */
  if ((iHdrLength = TSMimeHdrLengthGet(hdrBuf, mimeHdrLoc)) == TS_ERROR) {
    LOG_API_ERROR("TSMimeHdrLengthGet");
  } else {
    TSDebug(debugTag, "(%g) MIME header length = %d", section, iHdrLength);
  }

  if ((fieldLoc = TSMimeHdrFieldGet(hdrBuf, mimeHdrLoc, 0)) == TS_ERROR_PTR) {
    LOG_API_ERROR("TSMimeHdrFieldGet");
  }

  /* negative test */
#ifdef DEBUG
  if (TSMimeHdrFieldsCount(NULL, mimeHdrLoc) != TS_ERROR) {
    LOG_ERROR_NEG("TSMimeHdrFieldsCount");
  }
  if (TSMimeHdrFieldsCount(hdrBuf, NULL) != TS_ERROR) {
    LOG_ERROR_NEG("TSMimeHdrFieldsCount");
  }

  if (TSMimeHdrLengthGet(NULL, mimeHdrLoc) != TS_ERROR) {
    LOG_ERROR_NEG("TSMimeHdrLengthGet");
  }
  if (TSMimeHdrLengthGet(hdrBuf, NULL) != TS_ERROR) {
    LOG_ERROR_NEG("TSMimeHdrLengthGet");
  }

  if (TSMimeHdrFieldNext(NULL, mimeHdrLoc, fieldLoc) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSMimeHdrFieldNext");
  }
  if (TSMimeHdrFieldNext(hdrBuf, NULL, fieldLoc) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSMimeHdrFieldNext");
  }
  if (TSMimeHdrFieldNext(hdrBuf, mimeHdrLoc, NULL) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSMimeHdrFieldNext");
  }
#endif

  /* Print all the Mime Field in the MIME header */
  while (fieldLoc) {
    /* store the next field loc here */
    if ((nextFieldLoc = TSMimeHdrFieldNext(hdrBuf, mimeHdrLoc, fieldLoc)) == TS_ERROR_PTR) {
      LOG_API_ERROR("TSMimeHdrFieldNext");
    }

    TSDebug(debugTag, "-----------------------");
    if ((sFieldName = TSMimeHdrFieldNameGet(hdrBuf, mimeHdrLoc, fieldLoc, &iFieldNameLength))
        == TS_ERROR_PTR) {
      LOG_API_ERROR("TSMimeHdrFieldNameGet");
    } else {
      outputString = TSstrndup(sFieldName, iFieldNameLength);
      TSDebug(debugTag, "(%g) Field Name[%d] = %s", section, iFieldNameLength, outputString);
      FREE(outputString);
    }

    do {
      if (TSMimeHdrFieldValueStringGet(hdrBuf, mimeHdrLoc, fieldLoc, iFieldIndex, &sFieldValue,
                                        &iFieldValueLength) == TS_ERROR) {
        LOG_API_ERROR("TSMimeHdrFieldValueStringGet");
      } else {
        outputString = TSstrndup(sFieldValue, iFieldValueLength);
        TSDebug(debugTag, "(%g) Field Value[%d] = %s", section, iFieldValueLength, outputString);
        FREE(outputString);
      }

      if ((nextDupFieldLoc = TSMimeHdrFieldNextDup(hdrBuf, mimeHdrLoc, fieldLoc)) == TS_ERROR_PTR) {
        LOG_API_ERROR("TSMimeHdrFieldNextDup");
      }

      HANDLE_RELEASE(hdrBuf, mimeHdrLoc, fieldLoc);
      fieldLoc = nextDupFieldLoc;

    } while (nextDupFieldLoc && nextDupFieldLoc != TS_ERROR_PTR);

    fieldLoc = nextFieldLoc;
  }
}                               /* printMimeFields */


/* added by ymou */
static void
printField(TSMBuffer bufp, TSMLoc hdr, char *name, char *debugTag, float section)
{
  LOG_SET_FUNCTION_NAME("printField");

  int i;
  unsigned int j;
  int length, count;
  TSMLoc field = NULL;

  if ((field = TSMimeHdrFieldFind(bufp, hdr, name, strlen(name))) == TS_ERROR_PTR) {
    LOG_API_ERROR("TSMimeHdrFieldFind");
  }
  /* FIXME: Error code? */
  length = TSMimeHdrFieldLengthGet(bufp, hdr, field);

  if ((count = TSMimeHdrFieldValuesCount(bufp, hdr, field)) == TS_ERROR) {
    LOG_API_ERROR("TSMimeHdrFieldValuesCount");
  }
  TSMimeHdrFieldValueIntGet(bufp, hdr, field, 0, &i);
  TSMimeHdrFieldValueUintGet(bufp, hdr, field, 1, &j);

  TSDebug(debugTag, "***********************( %g )***********************", section);
  TSDebug(debugTag, "(%g) The length of the field %s = %d", section, name, length);
  TSDebug(debugTag, "(%g) The count of the field values = %d", section, count);
  TSDebug(debugTag, "(%g) The values of the field %s are %d and %u", section, name, i, j);

  /* negative test for TSMimeHdrFieldLengthGet */
#ifdef DEBUG
  if (TSMimeHdrFieldLengthGet(NULL, hdr, field) != TS_ERROR) {
    LOG_ERROR_NEG("TSMimeHdrFieldLengthGet");
  }
  if (TSMimeHdrFieldLengthGet(bufp, NULL, field) != TS_ERROR) {
    LOG_ERROR_NEG("TSMimeHdrFieldLengthGet");
  }
  if (TSMimeHdrFieldLengthGet(bufp, hdr, NULL) != TS_ERROR) {
    LOG_ERROR_NEG("TSMimeHdrFieldLengthGet");
  }
#endif

  /* release the string handle */
  HANDLE_RELEASE(bufp, hdr, field);
}


/* added by ymou */
static void
printDateDifference(TSMBuffer bufp, TSMLoc hdr, char *name, time_t currentTime, char *debugTag, float section)
{
  LOG_SET_FUNCTION_NAME("printDateDifference");

  time_t fieldTime;
  int length, count;
  TSMLoc field = NULL;

  if ((field = TSMimeHdrFieldFind(bufp, hdr, name, strlen(name))) == TS_ERROR_PTR) {
    LOG_API_ERROR("TSMimeHdrFieldFind");
  }
  /* FIXME: Error code? */
  length = TSMimeHdrFieldLengthGet(bufp, hdr, field);

  if ((count = TSMimeHdrFieldValuesCount(bufp, hdr, field)) == TS_ERROR) {
    LOG_API_ERROR("TSMimeHdrFieldValuesCount");
  }
  TSMimeHdrFieldValueDateGet(bufp, hdr, field, 0, &fieldTime);

  TSDebug(debugTag, "***********************( %g )***********************", section);
  TSDebug(debugTag, "(%g) The length of the field %s = %d", section, name, length);
  TSDebug(debugTag, "(%g) The count of the field values = %d", section, count);

  if (fieldTime != currentTime) {
    LOG_AUTO_ERROR("TSMimeHdrFieldValueStringInsert",
                   "The retrieved Date field value is different from the Date field value set");
  } else {
    TSDebug(debugTag, "(%g) The retrieved Date field value is the same as the Date field value set", section);
  }
  HANDLE_RELEASE(bufp, hdr, field);
}


/* added by ymou */
static void
printHeader(TSMBuffer bufp, TSMLoc hdr_loc)
{
  TSIOBuffer output_buffer;
  TSIOBufferReader reader;
  int total_avail;

  TSIOBufferBlock block;
  const char *block_start;
  int block_avail;

  char *output_string = NULL;
  int output_len;

  LOG_SET_FUNCTION_NAME("printHeader");

  output_buffer = TSIOBufferCreate();

  if (!output_buffer) {
    TSError("couldn't allocate IOBuffer\n");
  }

  reader = TSIOBufferReaderAlloc(output_buffer);

  /* This will print  just MIMEFields and not
     the http request line */
  TSMimeHdrPrint(bufp, hdr_loc, output_buffer);

  /* negative test for TSMimeHdrPrint */
#ifdef DEBUG
  if (TSMimeHdrPrint(NULL, hdr_loc, output_buffer) != TS_ERROR) {
    LOG_ERROR_NEG("TSMimeHdrPrint");
  }
  if (TSMimeHdrPrint(bufp, NULL, output_buffer) != TS_ERROR) {
    LOG_ERROR_NEG("TSMimeHdrPrint");
  }
  if (TSMimeHdrPrint(bufp, hdr_loc, NULL) != TS_ERROR) {
    LOG_ERROR_NEG("TSMimeHdrPrint");
  }
#endif

  /* Find out how the big the complete header is by
     seeing the total bytes in the buffer.  We need to
     look at the buffer rather than the first block to
     see the size of the entire header */
  total_avail = TSIOBufferReaderAvail(reader);

  /* Allocate the string with an extra byte for the string
     terminator */
  output_string = (char *) TSmalloc(total_avail + 1);
  output_len = 0;

  /* We need to loop over all the buffer blocks to make
     sure we get the complete header since the header can
     be in multiple blocks */
  block = TSIOBufferReaderStart(reader);
  while (block) {

    block_start = TSIOBufferBlockReadStart(block, reader, &block_avail);

    /* We'll get a block pointer back even if there is no data
       left to read so check for this condition and break out of
       the loop. A block with no data to read means we've exhausted
       buffer of data since if there was more data on a later
       block in the chain, this block would have been skipped over */
    if (block_avail == 0) {
      break;
    }

    memcpy(output_string + output_len, block_start, block_avail);
    output_len += block_avail;

    /* Consume the data so that we get to the next block */
    TSIOBufferReaderConsume(reader, block_avail);

    /* Get the next block now that we've consumed the
       data off the last block */
    block = TSIOBufferReaderStart(reader);
  }

  /* Terminate the string */
  output_string[output_len] = '\0';
  output_len++;

  /* Free up the TSIOBuffer that we used to print out the header */
  TSIOBufferReaderFree(reader);
  TSIOBufferDestroy(output_buffer);

  /* Although I'd never do this a production plugin, printf
     the header so that we can see it's all there */
  TSDebug(RESP, "**************** output header ****************");
  TSDebug(RESP, "%s", output_string);

  FREE(output_string);
}


static void
addDupFields(TSMBuffer hdrBuf, TSMLoc httpHdrLoc, char *debugTag, int section)
{
  LOG_SET_FUNCTION_NAME("addDupFields");

  TSMBuffer tmpBuf = NULL;
  TSMLoc tmpMimeHdrLoc = NULL;
  TSMLoc tmpFieldLoc = NULL, newFieldLoc = NULL, tmpNextDupFieldLoc = NULL;

  int iFieldNameLength;
  const char *tmpFieldValue = NULL;


  TSDebug(GENERAL, ">>>>>> addDupField <<<<<<");

  if ((tmpBuf = TSMBufferCreate()) == TS_ERROR_PTR) {
    LOG_API_ERROR("TSMBufferCreate");
  } else if ((tmpMimeHdrLoc = TSMimeHdrCreate(tmpBuf)) == TS_ERROR_PTR) {
    LOG_API_ERROR("TSMimeHdrCreate");
  }

  /* Copy the resp MIME Header to the tmp MIME Header */
  if (TSMimeHdrCopy(tmpBuf, tmpMimeHdrLoc, hdrBuf, httpHdrLoc) == TS_ERROR) {
    LOG_API_ERROR("TSMimeHdrCopy");
  }

  /* negative test for TSMimeHdrCopy */
#ifdef DEBUG
  if (TSMimeHdrCopy(NULL, tmpMimeHdrLoc, hdrBuf, httpHdrLoc) != TS_ERROR) {
    LOG_ERROR_NEG("TSMimeHdrCopy");
  }
  if (TSMimeHdrCopy(tmpBuf, NULL, hdrBuf, httpHdrLoc) != TS_ERROR) {
    LOG_ERROR_NEG("TSMimeHdrCopy");
  }
  if (TSMimeHdrCopy(tmpBuf, tmpMimeHdrLoc, NULL, httpHdrLoc) != TS_ERROR) {
    LOG_ERROR_NEG("TSMimeHdrCopy");
  }
  if (TSMimeHdrCopy(tmpBuf, tmpMimeHdrLoc, hdrBuf, NULL) != TS_ERROR) {
    LOG_ERROR_NEG("TSMimeHdrCopy");
  }
#endif

  /* Create a MIME field */
  if ((newFieldLoc = TSMimeHdrFieldCreate(tmpBuf, tmpMimeHdrLoc)) == TS_ERROR_PTR) {
    LOG_API_ERROR("TSMimeHdrFieldCreate");
  }

  /* negative test for TSMimeHdrFieldCreate */
#ifdef DEBUG
  if (TSMimeHdrFieldCreate(NULL, tmpMimeHdrLoc) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSMimeHdrFieldCreate");
  }
  if (TSMimeHdrFieldCreate(tmpBuf, NULL) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSMimeHdrFieldCreate");
  }
#endif

  if (TSMimeHdrFieldNameSet(tmpBuf, tmpMimeHdrLoc, newFieldLoc, "Field-1", strlen("Field-1"))
      == TS_ERROR) {
    LOG_API_ERROR("TSMimeHdrFieldNameSet");
  } else if (TSMimeHdrFieldValueStringInsert(tmpBuf, tmpMimeHdrLoc, newFieldLoc, "field-1-value-1",
                                        strlen("field-1-value-1"), -1) == TS_ERROR) {
    LOG_API_ERROR("TSMimeHdrFieldValueStringInsert");
  } else if (TSMimeHdrFieldAppend(tmpBuf, tmpMimeHdrLoc, newFieldLoc) == TS_ERROR) {
    LOG_API_ERROR("TSMimeHdrFieldAppend");
  }
  HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, newFieldLoc);

  /* auto: Now retrieve the field value back */
  if ((tmpFieldLoc = TSMimeHdrFieldFind(tmpBuf, tmpMimeHdrLoc, "Field-1", strlen("Field-1")))
      == TS_ERROR_PTR) {
    LOG_API_ERROR("TSMimeHdrFieldFind");
  } else if (tmpFieldLoc == NULL) {
    LOG_AUTO_ERROR("TSMimeHdrFieldFind", "Cannot find the newly created field");
  } else {
    /* CAUTION: idx == -1 is UNDOCUMENTED but valid argument */
    if (TSMimeHdrFieldValueStringGet(tmpBuf, tmpMimeHdrLoc, tmpFieldLoc, 0, &tmpFieldValue,
                                      &iFieldNameLength) == TS_ERROR) {
      LOG_API_ERROR("TSMimeHdrFieldValueStringGet");
    } else if (!tmpFieldValue) {
      LOG_AUTO_ERROR("TSMimeHdrFieldValueStringGet", "can't retrieve the field value");
    } else {
      if (strncmp(tmpFieldValue, "field-1-value-1", iFieldNameLength)) {
        LOG_AUTO_ERROR("TSMimeHdrFieldValueStringInsert", "Field value different from the inserted one");
      }
    }
    HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, tmpFieldLoc);
  }

  /* Insert another field */
  if ((newFieldLoc = TSMimeHdrFieldCreate(tmpBuf, tmpMimeHdrLoc)) == TS_ERROR_PTR) {
    LOG_API_ERROR("TSMimeHdrFieldCreate");
  } else if (TSMimeHdrFieldAppend(tmpBuf, tmpMimeHdrLoc, newFieldLoc) == TS_ERROR) {
    LOG_API_ERROR("TSMimeHdrFieldAppend");
  } else if (TSMimeHdrFieldNameSet(tmpBuf, tmpMimeHdrLoc, newFieldLoc, "Dup-Field-1", strlen("Dup-Field-1"))
             == TS_ERROR) {
    LOG_API_ERROR("TSMimeHdrFieldNameSet");
  } else if (TSMimeHdrFieldValueStringInsert(tmpBuf, tmpMimeHdrLoc, newFieldLoc, "dup-field-1-value-1",
                                        strlen("dup-field-1-value-1"), -1) == TS_ERROR) {
    LOG_API_ERROR("TSMimeHdrFieldValueStringInsert");
  }
  HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, newFieldLoc);

  /* auto: Now retrieve it back to check if its been insered */
  if ((tmpFieldLoc = TSMimeHdrFieldFind(tmpBuf, tmpMimeHdrLoc, "Dup-Field-1", strlen("Dup-Field-1")))
      == TS_ERROR_PTR) {
    LOG_API_ERROR("TSMimeHdrFieldFind");
  } else if (!tmpFieldLoc) {
    LOG_AUTO_ERROR("TSMimeHdrFieldFind", "Cannot find the newly inserted field");
  } else {
    if (TSMimeHdrFieldValueStringGet(tmpBuf, tmpMimeHdrLoc, tmpFieldLoc, 0, &tmpFieldValue, &iFieldNameLength)
        == TS_ERROR) {
      LOG_API_ERROR("TSMimeHdrFieldValueStringGet");
    } else if (!tmpFieldValue) {
      LOG_AUTO_ERROR("TSMimeHdrFieldValueStringGet", "can't retrieve the field value");
    } else {
      if (strncmp(tmpFieldValue, "dup-field-1-value-1", iFieldNameLength)) {
        LOG_AUTO_ERROR("TSMimeHdrFieldValueStringInsert", "Field value different from the inserted one");
      }
    }
  }
  HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, tmpFieldLoc);

  if ((newFieldLoc = TSMimeHdrFieldCreate(tmpBuf, tmpMimeHdrLoc)) == TS_ERROR_PTR) {
    LOG_API_ERROR("TSMimeHdrFieldCreate");
  } else if (TSMimeHdrFieldAppend(tmpBuf, tmpMimeHdrLoc, newFieldLoc) == TS_ERROR) {
    LOG_API_ERROR("TSMimeHdrFieldAppend");
  } else if (TSMimeHdrFieldNameSet(tmpBuf, tmpMimeHdrLoc, newFieldLoc, "Dup-Field-1", strlen("Dup-Field-1"))
             == TS_ERROR) {
    LOG_API_ERROR("TSMimeHdrFieldNameSet");
  } else if (TSMimeHdrFieldValueStringInsert(tmpBuf, tmpMimeHdrLoc, newFieldLoc, "dup-field-1-value-2",
                                        strlen("dup-field-1-value-2"), -1) == TS_ERROR) {
    LOG_API_ERROR("TSMimeHdrFieldValueStringInsert");
  }
  HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, newFieldLoc);


  /* auto: Now retrieve the 1st duplicate field and check its value for correctness */
  if ((tmpFieldLoc = TSMimeHdrFieldFind(tmpBuf, tmpMimeHdrLoc, "Dup-Field-1", strlen("Dup-Field-1")))
      == TS_ERROR_PTR) {
    LOG_API_ERROR("TSMimeHdrFieldFind");
  } else if (tmpFieldLoc == NULL) {     /* check for NULL -- if the field is not found */
    LOG_AUTO_ERROR("TSMimeHdrFieldFind", "cannot find the newly inserted field");
  } else {
    /* negative test for TSMimeHdrFieldNextDup */
#ifdef DEBUG
    if (TSMimeHdrFieldNextDup(NULL, tmpMimeHdrLoc, tmpFieldLoc) != TS_ERROR_PTR) {
      LOG_ERROR_NEG("TSMimeHdrFieldNextDup");
    }
    if (TSMimeHdrFieldNextDup(tmpBuf, NULL, tmpFieldLoc) != TS_ERROR_PTR) {
      LOG_ERROR_NEG("TSMimeHdrFieldNextDup");
    }
    if (TSMimeHdrFieldNextDup(tmpBuf, tmpMimeHdrLoc, NULL) != TS_ERROR_PTR) {
      LOG_ERROR_NEG("TSMimeHdrFieldNextDup");
    }
#endif
    if ((tmpNextDupFieldLoc = TSMimeHdrFieldNextDup(tmpBuf, tmpMimeHdrLoc, tmpFieldLoc))
        == TS_ERROR_PTR) {
      LOG_API_ERROR("TSMimeHdrFieldNextDup");
    } else if (tmpNextDupFieldLoc == NULL) {    /* check for NULL -- if NOT found */
      LOG_AUTO_ERROR("TSMimeHdrFieldNextDup", "cannot retrieve the 1st dup field loc");
    } else {
      if (TSMimeHdrFieldValueStringGet(tmpBuf, tmpMimeHdrLoc, tmpNextDupFieldLoc, 0, &tmpFieldValue,
                                        &iFieldNameLength) == TS_ERROR) {
        LOG_API_ERROR("TSMimeHdrFieldValueStringGet");
      } else if (!tmpFieldValue) {
        LOG_AUTO_ERROR("TSMimeHdrFieldValueStringGet", "can't retrieve the 1st dup field value");
      } else {
        if (strncmp(tmpFieldValue, "dup-field-1-value-2", iFieldNameLength)) {
          LOG_AUTO_ERROR("TSMimeHdrFieldValueStringInsert", "Field value different from the inserted one");
        }
      }
      HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, tmpFieldLoc);
      tmpFieldLoc = tmpNextDupFieldLoc; /* preserve the fieldLoc here */
      /*HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, tmpNextDupFieldLoc); */
    }
  }


  if ((newFieldLoc = TSMimeHdrFieldCreate(tmpBuf, tmpMimeHdrLoc)) == TS_ERROR_PTR) {
    LOG_API_ERROR("TSMimeHdrFieldCreate");
  } else if (TSMimeHdrFieldAppend(tmpBuf, tmpMimeHdrLoc, newFieldLoc) == TS_ERROR) {
    LOG_API_ERROR("TSMimeHdrFieldAppend");
  } else if (TSMimeHdrFieldNameSet(tmpBuf, tmpMimeHdrLoc, newFieldLoc, "Dup-Field-1", strlen("Dup-Field-1"))
             == TS_ERROR) {
    LOG_API_ERROR("TSMimeHdrFieldNameSet");
  } else if (TSMimeHdrFieldValueStringInsert(tmpBuf, tmpMimeHdrLoc, newFieldLoc, "dup-field-1-value-3",
                                        strlen("dup-field-1-value-3"), -1) == TS_ERROR) {
    LOG_API_ERROR("TSMimeHdrFieldValueStringInsert");
  }
  HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, newFieldLoc);


  /* auto: Now retrieve the 2nd duplicate (using the "preserved" field loc from above) field
   * value back and check for correctness */

  if ((tmpNextDupFieldLoc = TSMimeHdrFieldNextDup(tmpBuf, tmpMimeHdrLoc, tmpFieldLoc))
      == TS_ERROR_PTR) {
    LOG_API_ERROR("TSMimeHdrFieldNextDup");
  } else if (!tmpNextDupFieldLoc) {
    LOG_AUTO_ERROR("TSMimeHdrFieldNextDup", "cannot retrieve the 2nd dup field loc");
  } else {
    if (TSMimeHdrFieldValueStringGet(tmpBuf, tmpMimeHdrLoc, tmpNextDupFieldLoc, 0, &tmpFieldValue,
                                      &iFieldNameLength) == TS_ERROR) {
      LOG_API_ERROR("TSMimeHdrFieldValueStringGet");
    } else if (!tmpFieldValue) {
      LOG_AUTO_ERROR("TSMimeHdrFieldValueStringGet", "can't retrieve the 2nd dup field value");
    } else {
      if (strncmp(tmpFieldValue, "dup-field-1-value-3", iFieldNameLength)) {
        LOG_AUTO_ERROR("TSMimeHdrFieldValueStringInsert", "Field value different from the inserted one");
      }
    }
    HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, tmpNextDupFieldLoc);
  }

  printMimeFields(tmpBuf, tmpMimeHdrLoc, debugTag, section);

  /* clean-up */
  HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, tmpFieldLoc);
  HANDLE_RELEASE(tmpBuf, TS_NULL_MLOC, tmpMimeHdrLoc);

  BUFFER_DESTROY(tmpBuf);
}                               /* addDupFields */



/****************************************************************************
 * sectionMimeHdr:
 ****************************************************************************/
static void
sectionMimeHdr(TSMBuffer hdrBuf, TSMLoc httpHdrLoc)
{
  LOG_SET_FUNCTION_NAME("sectionMimeHdr");

  TSMBuffer tmpBuf = NULL;
  TSMLoc fieldLoc = NULL, tmpFieldLoc = NULL, newFieldLoc = NULL, tmpMimeHdrLoc = NULL;
  TSHttpType httpType;

  const char *tmpFieldValueString;
  const char *tmpFieldName = NULL, *tmpFieldValue1 = NULL, *tmpFieldValue2 = NULL;
  int tmpFieldNameLength, tmpFieldValueLength, count;
  time_t tmpDate1, tmpDate2;

  /* added by ymou */
  TSMLoc fieldLoc1 = NULL, fieldLoc2 = NULL, fieldLoc3 = NULL;
  time_t currentTime, fieldTime, retrievedDate;
  int valueInt, idx, retrievedInt;
  unsigned int valueUint, retrievedUint;

  /* Get the field location */
  fieldLoc = TSMimeHdrFieldGet(hdrBuf, httpHdrLoc, 0);

  /* negative test for TSMimeHdrFieldGet */
#ifdef DEBUG
  if (TSMimeHdrFieldGet(NULL, httpHdrLoc, 0) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSMimeHdrFieldGet");
  }
  if (TSMimeHdrFieldGet(hdrBuf, NULL, 0) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSMimeHdrFieldGet");
  }
#endif

  httpType = TSHttpHdrTypeGet(hdrBuf, httpHdrLoc);

  TSDebug(GENERAL, "\n>>> sectionMimeHdr <<<<");


        /************* TS_HTTP_TYPE_REQUEST ******************/
  if (httpType == TS_HTTP_TYPE_REQUEST) {
    TSDebug(REQ, "\n>>> REQUEST <<<<");

                /***** (1): simply print the request header *****/
    printMimeFields(hdrBuf, httpHdrLoc, REQ, 1);

  }


  /* httpType == TS_HTTP_TYPE_REQUEST */
 /************ TS_HTTP_TYPE_RESPONSE ******************/
  if (httpType == TS_HTTP_TYPE_RESPONSE) {
    TSDebug(RESP, "\n>>> RESPONSE <<<<");

                /****** (1): Simply print the response header ****/
    printMimeFields(hdrBuf, httpHdrLoc, RESP, 1);


                /****** (2): Insert some duplicate fields ****/
    addDupFields(hdrBuf, httpHdrLoc, RESP, 2);


                /****** (3): Do MIME hdr copy and print *****/
    /* Copy the respHdrBuf MIME Headers to a tmp buf and print the details */

    /* CAUTION: (reference - TSqa8336)
     * Here we are doing a TSMimeHdrCopy without creating a HTTP header first.
     * So the dest MIME header (tmpMimeHdrLoc) is not associated with any HTTP header.
     * This is hardly ever the case, and should NOT be practised in general.  I am
     * doing so merely to test the API's functional correctness and NOT to suggest
     * a possible usage of the API */

    if ((tmpBuf = TSMBufferCreate()) == TS_ERROR_PTR) {
      LOG_API_ERROR_COMMENT("TSMBufferCreate", "abnormal exit to 'done'");
      goto done;
    }

    if ((tmpMimeHdrLoc = TSMimeHdrCreate(tmpBuf)) == TS_ERROR_PTR) {
      LOG_API_ERROR("TSMimeHdrCreate");
    } else if (TSMimeHdrCopy(tmpBuf, tmpMimeHdrLoc, hdrBuf, httpHdrLoc) == TS_ERROR) {
      LOG_API_ERROR("TSMimeHdrCopy");
    } else {
      printMimeFields(tmpBuf, tmpMimeHdrLoc, RESP, 3);
    }


                /****** (4): Remove some MIME field *****/
    /* Remove the "Via" field */
    if ((tmpFieldLoc = TSMimeHdrFieldFind(tmpBuf, tmpMimeHdrLoc, "Via", strlen("Via")))
        == TS_ERROR_PTR) {
      LOG_API_ERROR("TSMimeHdrFieldFind");
    } else if (TSMimeHdrFieldRemove(tmpBuf, tmpMimeHdrLoc, tmpFieldLoc) == TS_ERROR) {
      LOG_API_ERROR("TSMimeHdrFieldRemove");
    } else if (TSMimeHdrFieldValueStringGet(tmpBuf, tmpMimeHdrLoc, tmpFieldLoc, -1, &tmpFieldValue1,
                                             &tmpFieldValueLength) == TS_ERROR) {
      LOG_API_ERROR("TSMimeHdrFieldValueStringGet");
    }

    /* negative test */
#ifdef DEBUG
    if (TSMimeHdrFieldFind(NULL, tmpMimeHdrLoc, "Via", strlen("Via")) != TS_ERROR_PTR) {
      LOG_ERROR_NEG("TSMimeHdrFieldFind");
    }
    if (TSMimeHdrFieldFind(tmpBuf, NULL, "Via", strlen("Via")) != TS_ERROR_PTR) {
      LOG_ERROR_NEG("TSMimeHdrFieldFind");
    }
    if (TSMimeHdrFieldFind(tmpBuf, tmpMimeHdrLoc, NULL, 0) != TS_ERROR_PTR) {
      LOG_ERROR_NEG("TSMimeHdrFieldFind");
    }

    if (TSMimeHdrFieldRemove(NULL, tmpMimeHdrLoc, tmpFieldLoc) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldRemove");
    }
    if (TSMimeHdrFieldRemove(tmpBuf, NULL, tmpFieldLoc) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldRemove");
    }
    if (TSMimeHdrFieldRemove(tmpBuf, tmpMimeHdrLoc, NULL) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldRemove");
    }
#endif

    /* auto: now FINDing the field should STILL pass */
    if (TSMimeHdrFieldFind(tmpBuf, tmpMimeHdrLoc, "Via", strlen("Via")) == TS_ERROR_PTR) {
      LOG_API_ERROR("TSMimeHdrFieldFind");
      LOG_AUTO_ERROR("TSMimeHdrFieldRemove", "TS_Find failing afterTS_Remove");
    }

    printMimeFields(tmpBuf, tmpMimeHdrLoc, RESP, 4.1);

    /* Re-attach the "removed" field */
    if (TSMimeHdrFieldAppend(tmpBuf, tmpMimeHdrLoc, tmpFieldLoc) == TS_ERROR) {
      LOG_API_ERROR("TSMimeHdrFieldInset");
    }
    if ((tmpFieldLoc = TSMimeHdrFieldFind(tmpBuf, tmpMimeHdrLoc, "Via", strlen("Via")))
        == TS_ERROR_PTR) {
      LOG_API_ERROR("TSMimeHdrFieldFind");
    } else if (TSMimeHdrFieldValueStringGet(tmpBuf, tmpMimeHdrLoc, tmpFieldLoc, -1, &tmpFieldValue2,
                                             &tmpFieldValueLength) == TS_ERROR) {
      LOG_API_ERROR("TSMimeHdrFieldValueStringGet");
    }
    if (strncmp(tmpFieldValue1, tmpFieldValue2, tmpFieldValueLength)) {
      LOG_AUTO_ERROR("TSMimeHdrFieldAppend", "Field value different w/ the re-attach after TS_Remove");
    }

    printMimeFields(tmpBuf, tmpMimeHdrLoc, RESP, 4.2);

    /* cleanup */
    HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, tmpFieldLoc);


                /****** (5): delete some MIME fields ****/
    /* Delete the "Via" field */
    if ((tmpFieldLoc = TSMimeHdrFieldFind(tmpBuf, tmpMimeHdrLoc, "Via", strlen("Via")))
        == TS_ERROR_PTR) {
      LOG_API_ERROR("TSMimeHdrFieldFind");
    } else if (TSMimeHdrFieldDestroy(tmpBuf, tmpMimeHdrLoc, tmpFieldLoc) == TS_ERROR) {
      LOG_API_ERROR("TSMimeHdrFieldDestroy");
    }

    /* auto: now FINDing the field should fail */
    if ((tmpFieldLoc = TSMimeHdrFieldFind(tmpBuf, tmpMimeHdrLoc, "Via", strlen("Via")))
        == TS_ERROR_PTR) {
      LOG_API_ERROR("TSMimeHdrFieldFind");
    } else if (tmpFieldLoc) {
      LOG_AUTO_ERROR("TSMimeHdrFieldRemove", "Can STILL TS_Find after TS_Delete");
    }

    printMimeFields(tmpBuf, tmpMimeHdrLoc, RESP, 5);

    HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, tmpFieldLoc);

                /****** section (6) ******/
    /* -----------------------------------------------------------------------
     * Now, insert some fields into the MIME buffer
     * Note:
     *      1. Field name can be set before and/or after TSMimeHdrFieldAppend
     *      2. Field value could be set *only* after TSMimeHdrFieldValueStringInsert
     *
     * (point 1. and 2. implies that its possible to insert fields with empty
     * name and values)
     * -----------------------------------------------------------------------*/

    TSDebug(RESP, "***********************( 6.2 )***********************");

        /*** (6.2): append some *field value* ******/
    if ((newFieldLoc = TSMimeHdrFieldCreate(tmpBuf, tmpMimeHdrLoc)) == TS_ERROR_PTR) {
      LOG_API_ERROR_COMMENT("TSMimeHdrFieldCreate", "Skip to section 6.3");
      goto section_63;
    } else if (TSMimeHdrFieldNameSet(tmpBuf, tmpMimeHdrLoc, newFieldLoc, "Append-Field",
                                      strlen("Append-Field")) == TS_ERROR) {
      LOG_API_ERROR("TSMimeHdrFieldNameSet");
    } else if (TSMimeHdrFieldValueStringInsert(tmpBuf, tmpMimeHdrLoc, newFieldLoc, "append-field-value",
                                          strlen("append-field-value"), -1) == TS_ERROR) {
      LOG_API_ERROR("TSMimeHdrFieldValueStringInsert");
    }

    /* negative test for TSMimeHdrFieldNameSet */
#ifdef DEBUG
    if (TSMimeHdrFieldNameSet(NULL, tmpMimeHdrLoc, newFieldLoc, "Append-Field", strlen("Append-Field")) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldNameSet");
    }
    if (TSMimeHdrFieldNameSet(tmpBuf, NULL, newFieldLoc, "Append-Field", strlen("Append-Field")) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldNameSet");
    }
    if (TSMimeHdrFieldNameSet(tmpBuf, tmpMimeHdrLoc, NULL, "Append-Field", strlen("Append-Field")) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldNameSet");
    }
    if (TSMimeHdrFieldNameSet(tmpBuf, tmpMimeHdrLoc, newFieldLoc, NULL, 0) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldNameSet");
    }
    if (TSMimeHdrFieldNameSet(tmpBuf, tmpMimeHdrLoc, newFieldLoc, NULL, -1) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldNameSet");
    }
#endif

    /* Now, do the insert : append to the list of fields */
    if (TSMimeHdrFieldAppend(tmpBuf, tmpMimeHdrLoc, newFieldLoc) == TS_ERROR) {
      LOG_API_ERROR("TSMimeHdrFieldAppend");
    }

    /* auto: check the appended field using the last idx value */
    if ((idx = TSMimeHdrFieldsCount(tmpBuf, tmpMimeHdrLoc)) == TS_ERROR) {
      LOG_API_ERROR("TSMimeHdrFieldsCount");
    } else if ((tmpFieldLoc = TSMimeHdrFieldGet(tmpBuf, tmpMimeHdrLoc, --idx)) == TS_ERROR_PTR) {
      LOG_API_ERROR("TSMimeHdrFieldGet");
    } else {
      if ((tmpFieldName = TSMimeHdrFieldNameGet(tmpBuf, tmpMimeHdrLoc, tmpFieldLoc,
                                                 &tmpFieldNameLength)) == TS_ERROR_PTR) {
        LOG_API_ERROR("TSMimeHdrFieldNameGet");
      } else {
        if (strncmp(tmpFieldName, "Append-Field", strlen("Append-Field"))) {
          LOG_AUTO_ERROR("TSMimeHdrFieldAppend", "New field not appended!");
        }
      }
      /* negative test for TSMimeHdrFieldNameGet */
#ifdef DEBUG
      if (TSMimeHdrFieldNameGet(NULL, tmpMimeHdrLoc, tmpFieldLoc, &tmpFieldNameLength) != TS_ERROR_PTR) {
        LOG_ERROR_NEG("TSMimeHdrFieldNameGet");
      }
      if (TSMimeHdrFieldNameGet(tmpBuf, NULL, tmpFieldLoc, &tmpFieldNameLength) != TS_ERROR_PTR) {
        LOG_ERROR_NEG("TSMimeHdrFieldNameGet");
      }
      if (TSMimeHdrFieldNameGet(tmpBuf, tmpMimeHdrLoc, NULL, &tmpFieldNameLength) != TS_ERROR_PTR) {
        LOG_ERROR_NEG("TSMimeHdrFieldNameGet");
      }
#endif

      HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, tmpFieldLoc);
    }
    printMimeFields(tmpBuf, tmpMimeHdrLoc, RESP, 6.2);
    HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, newFieldLoc);

  section_63:
    TSDebug(RESP, "***********************( 6.3 )***********************");

                /**** (6.3): append field-values (comma seperated) to "Append-Field" ***/

    if ((newFieldLoc = TSMimeHdrFieldFind(tmpBuf, tmpMimeHdrLoc, "Append-Field",
                                           strlen("Append-Field"))) == TS_ERROR_PTR) {
      LOG_API_ERROR_COMMENT("TSMimeHdrFieldFind", "Skip to section 7");
      goto section_7;
    } else if (TSMimeHdrFieldValueStringInsert(tmpBuf, tmpMimeHdrLoc, newFieldLoc, "append-field-value-2",
                                          strlen("append-field-value-2"), -1) == TS_ERROR) {
      LOG_API_ERROR("TSMimeHdrFieldValueStringInsert");
    }

    /* auto: check the newly appended field value w/ idx == 1 */
    if (TSMimeHdrFieldValueStringGet(tmpBuf, tmpMimeHdrLoc, newFieldLoc, 1, &tmpFieldValueString,
                                      &tmpFieldNameLength) == TS_ERROR) {
      LOG_API_ERROR("TSMimeHdrFieldValueStringGet");
      TSDebug(RESP, "string = %s", tmpFieldValueString);
    } else {
      if (strncmp(tmpFieldValueString, "append-field-value-2", strlen("append-field-value-2"))) {
        LOG_AUTO_ERROR("TSMimeHdrFieldValueStringInsert", "New field value not appended!");
      }
    }

    printMimeFields(tmpBuf, tmpMimeHdrLoc, RESP, 6.3);
    HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, newFieldLoc);

  section_7:
    TSDebug(RESP, "***********************( 7 )***********************");

                /****** (7): Now modify the field values ******/

                /*** (7.1): Totally change the field value *****/
    if ((newFieldLoc = TSMimeHdrFieldFind(tmpBuf, tmpMimeHdrLoc, "Append-Field",
                                           strlen("Append-Field"))) == TS_ERROR_PTR) {
      LOG_API_ERROR_COMMENT("TSMimeHdrFieldFind", "Skip to section 7.2");
      goto section_8;
    }

    /* FIXME: TSqa8060: check if idx == -1 is an unpublised behaviour still */
    if (TSMimeHdrFieldValueStringSet(tmpBuf, tmpMimeHdrLoc, newFieldLoc, -1, "new-append-field-value",
                                      strlen("new-append-field-value")) == TS_ERROR) {
      LOG_API_ERROR("TSMimeHdrFieldValueStringSet");
    }

    /* auto: check the newly changed field value */
    if (TSMimeHdrFieldValueStringGet(tmpBuf, tmpMimeHdrLoc, newFieldLoc, -1, &tmpFieldValueString,
                                      &tmpFieldNameLength) == TS_ERROR) {
      LOG_API_ERROR("TSMimeHdrFieldValueStringGet");
    } else {
      if (strncmp(tmpFieldValueString, "new-append-field-value", strlen("new-append-field-value"))) {
        LOG_AUTO_ERROR("TSMimeHdrFieldValueStringInsert", "New field value not replaced properly !");
      }
    }

    printMimeFields(tmpBuf, tmpMimeHdrLoc, RESP, 7.1);

    /* negative test */
#ifdef DEBUG
    if (TSMimeHdrFieldValueStringSet(NULL, tmpMimeHdrLoc, newFieldLoc, 0, "neg-test-field-value",
                                      strlen("neg-test-field-value")) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueStringSet");
    }
    if (TSMimeHdrFieldValueStringSet(tmpBuf, NULL, newFieldLoc, 0, "neg-test-field-value",
                                      strlen("neg-test-field-value")) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueStringSet");
    }
    if (TSMimeHdrFieldValueStringSet(tmpBuf, tmpMimeHdrLoc, NULL, 0, "neg-test-field-value",
                                      strlen("neg-test-field-value")) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueStringSet");
    }
    if (TSMimeHdrFieldValueStringSet(tmpBuf, tmpMimeHdrLoc, newFieldLoc, 0, NULL, -1) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueStringSet");
    }

    if (TSMimeHdrFieldValueStringInsert(NULL, tmpMimeHdrLoc, newFieldLoc, 0, "neg-test-field-value",
                                         strlen("neg-test-field-value")) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueStringInsert");
    }
    if (TSMimeHdrFieldValueStringInsert(tmpBuf, NULL, newFieldLoc, 0, "neg-test-field-value",
                                         strlen("neg-test-field-value")) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueStringInsert");
    }
    if (TSMimeHdrFieldValueStringInsert(tmpBuf, tmpMimeHdrLoc, NULL, 0, "neg-test-field-value",
                                         strlen("neg-test-field-value")) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueStringInsert");
    }
    if (TSMimeHdrFieldValueStringInsert(tmpBuf, tmpMimeHdrLoc, newFieldLoc, 0, NULL, -1) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueStringInsert");
    }

    if (TSMimeHdrFieldValueStringGet(NULL, tmpMimeHdrLoc, newFieldLoc, -1, &tmpFieldValueString,
                                      &tmpFieldNameLength) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueStringGet");
    }
    if (TSMimeHdrFieldValueStringGet(tmpBuf, NULL, newFieldLoc, -1, &tmpFieldValueString,
                                      &tmpFieldNameLength) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueStringGet");
    }
    if (TSMimeHdrFieldValueStringGet(tmpBuf, tmpMimeHdrLoc, NULL, -1, &tmpFieldValueString,
                                      &tmpFieldNameLength) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueStringGet");
    }
    if (TSMimeHdrFieldValueStringGet(tmpBuf, tmpMimeHdrLoc, newFieldLoc, -1, NULL, &tmpFieldNameLength) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueStringGet");
    }
    if (TSMimeHdrFieldValueStringGet(tmpBuf, tmpMimeHdrLoc, newFieldLoc, -1, &tmpFieldValueString, NULL) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueStringGet");
    }
#endif

                /*** (7.2): Now append a string to a field value ***/

    if (TSMimeHdrFieldValueAppend(tmpBuf, tmpMimeHdrLoc, newFieldLoc, 0, "<appended-text>",
                                   strlen("<appended-text>")) == TS_ERROR) {
      LOG_API_ERROR("TSMimeHdrFieldValueAppend");
    }

    /* negative test for TSMimeHdrFieldValueAppend */
#ifdef DEBUG
    if (TSMimeHdrFieldValueAppend(NULL, tmpMimeHdrLoc, newFieldLoc, 0, "<appended-text>", strlen("<appended-text>")) !=
        TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueAppend");
    }
    if (TSMimeHdrFieldValueAppend(tmpBuf, NULL, newFieldLoc, 0, "<appended-text>", strlen("<appended-text>")) !=
        TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueAppend");
    }
    if (TSMimeHdrFieldValueAppend(tmpBuf, tmpMimeHdrLoc, NULL, 0, "<appended-text>", strlen("<appended-text>")) !=
        TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueAppend");
    }
#endif

    /* auto: check the newly changed field value */
    /* FIXME: check if idx == -1 is still undocumented ?? */
    if (TSMimeHdrFieldValueStringGet(tmpBuf, tmpMimeHdrLoc, newFieldLoc, -1, &tmpFieldValueString,
                                      &tmpFieldNameLength) == TS_ERROR) {
      LOG_API_ERROR("TSMimeHdrFieldValueStringGet");
    } else {
      if (!strstr(tmpFieldValueString, "<appended-text>")) {
        LOG_AUTO_ERROR("TSMimeHdrFieldValueStringInsert", "Cannot located the appended text to field value!");
      }
    }

    printMimeFields(tmpBuf, tmpMimeHdrLoc, RESP, 7.2);

    /* clean-up */
    HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, newFieldLoc);

  section_8:
    TSDebug(RESP, "***********************( 8 )***********************");

                /****** (8): clear values for few fields ******/
    /* fieldLoc = TSMimeHdrFieldFind (tmpBuf, tmpMimeHdrLoc, "Date", strlen("Date")); */

    if ((fieldLoc = TSMimeHdrFieldFind(tmpBuf, tmpMimeHdrLoc, TS_MIME_FIELD_DATE, TS_MIME_LEN_DATE))
        == TS_ERROR_PTR) {
      LOG_API_ERROR("TSMimeHdrFieldFind");
    } else {
      if (TSMimeHdrFieldValueDateGet(tmpBuf, tmpMimeHdrLoc, fieldLoc, &tmpDate1) == TS_ERROR) {
        LOG_API_ERROR("TSMimeHdrFieldValueDateGet");
      } else if (TSMimeHdrFieldValuesClear(tmpBuf, tmpMimeHdrLoc, fieldLoc) == TS_ERROR) {
        LOG_API_ERROR("TSMimeHdrFieldValuesClear");
      }
    }

    /* negative test for TSMimeHdrFieldValuesClear */
#ifdef DEBUG
    if (TSMimeHdrFieldValuesClear(NULL, tmpMimeHdrLoc, fieldLoc) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValuesClear");
    }
    if (TSMimeHdrFieldValuesClear(tmpBuf, NULL, fieldLoc) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValuesClear");
    }
    if (TSMimeHdrFieldValuesClear(tmpBuf, tmpMimeHdrLoc, NULL) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValuesClear");
    }
#endif

    /* auto: RETRIEVing the DATE field back after CLEAR should fail */
    if ((fieldLoc = TSMimeHdrFieldFind(tmpBuf, tmpMimeHdrLoc, TS_MIME_FIELD_DATE, TS_MIME_LEN_DATE))
        == TS_ERROR_PTR) {
      LOG_API_ERROR("TSMimeHdrFieldFind");
    } else {
      if (TSMimeHdrFieldValueDateGet(tmpBuf, tmpMimeHdrLoc, fieldLoc, &tmpDate2) != TS_ERROR) {
        /*LOG_AUTO_ERROR("TSMimeHdrFieldValuesClear", "Can STILL retrieve DATE value after TS_CLEAR"); */
        if (tmpDate1 == tmpDate2) {
          LOG_AUTO_ERROR("TSMimeHdrFieldValuesClear", "DATE value STILL the same after TS_CLEAR");
        }
      }
    }

    if ((fieldLoc = TSMimeHdrFieldFind(tmpBuf, tmpMimeHdrLoc, TS_MIME_FIELD_CONTENT_TYPE, TS_MIME_LEN_CONTENT_TYPE))
        == TS_ERROR_PTR) {
      LOG_API_ERROR("TSMimeHdrFieldFind");
    } else if (fieldLoc) {
      if (TSMimeHdrFieldValueStringGet(tmpBuf, tmpMimeHdrLoc, fieldLoc, -1,
                                        &tmpFieldValue1, &tmpFieldValueLength) == TS_ERROR) {
        LOG_API_ERROR("TSMimeHdrFieldValueStringGet");
      }
      if (TSMimeHdrFieldValuesClear(tmpBuf, tmpMimeHdrLoc, fieldLoc) == TS_ERROR) {
        LOG_API_ERROR("TSMimeHdrFieldValuesClear");
      }
    }
    HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, fieldLoc);

    /* auto: */
    if ((fieldLoc = TSMimeHdrFieldFind(tmpBuf, tmpMimeHdrLoc, TS_MIME_FIELD_CONTENT_TYPE, TS_MIME_LEN_CONTENT_TYPE))
        == TS_ERROR_PTR) {
      LOG_API_ERROR("TSMimeHdrFieldFind");
    } else if (fieldLoc) {
      if (TSMimeHdrFieldValueStringGet(tmpBuf, tmpMimeHdrLoc, fieldLoc, -1,
                                        &tmpFieldValue2, &tmpFieldValueLength) != TS_ERROR) {
        /*LOG_AUTO_ERROR("TSMimeHdrFieldValuesClear", "Can STILL retrieve CONTENT_TYPE value after TS_CLEAR"); */
        if (strcmp(tmpFieldValue2, "\0") && !strncmp(tmpFieldValue1, tmpFieldValue2, tmpFieldValueLength)) {
          LOG_AUTO_ERROR("TSMimeHdrFieldValuesClear", "CONTENT_TYPE value STILL same after TS_CLEAR");
        }
      }
    }
    printMimeFields(tmpBuf, tmpMimeHdrLoc, RESP, 8);

    /* clean-up */
    HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, fieldLoc);

                /******* (9): Desotroy ALL the MIME fields using TS_Clear *******/
    if (TSMimeHdrFieldsClear(tmpBuf, tmpMimeHdrLoc) == TS_ERROR) {
      LOG_API_ERROR("TSMimeHdrFieldsClear");
    }
    HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, fieldLoc);

    /* negative test for TSMimeHdrFieldsClear */
#ifdef DEBUG
    if (TSMimeHdrFieldsClear(NULL, tmpMimeHdrLoc) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldsClear");
    }
    if (TSMimeHdrFieldsClear(tmpBuf, NULL) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldsClear");
    }
#endif

    /* auto: TS_Retrieve'ing field after TS_Clear should fail */
    if ((fieldLoc = TSMimeHdrFieldFind(tmpBuf, tmpMimeHdrLoc, TS_MIME_FIELD_AGE, TS_MIME_LEN_AGE)) != TS_ERROR_PTR && fieldLoc) {
      LOG_AUTO_ERROR("TSMimeHdrFieldsClear", "Can STILL retrieve AGE fieldLoc afer TS_FieldsClear");
      if (TSMimeHdrFieldValueStringGet(tmpBuf, tmpMimeHdrLoc, fieldLoc, -1, &tmpFieldValueString,
                                        &tmpFieldValueLength) != TS_ERROR) {
        LOG_AUTO_ERROR("TSMimeHdrFieldsClear", "Can STILL retrieve AGE fieldValue afer TS_FieldsClear");
      }
    }

    printMimeFields(tmpBuf, tmpMimeHdrLoc, RESP, 9);

    /* clean-up */
    HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, fieldLoc);

    /* Final clean-up */
    HANDLE_RELEASE(tmpBuf, TS_NULL_MLOC, tmpMimeHdrLoc);
    BUFFER_DESTROY(tmpBuf);

    /* added by ymou */
                /***** (10): create a new mime header and play with TSMimeHdrFieldValue[Insert|Get]Date *****/
    /* create a new mime header */
    tmpBuf = TSMBufferCreate();
    tmpMimeHdrLoc = TSMimeHdrCreate(tmpBuf);

    /* create a new field */
    fieldLoc1 = TSMimeHdrFieldCreate(tmpBuf, tmpMimeHdrLoc);
    TSMimeHdrFieldAppend(tmpBuf, tmpMimeHdrLoc, fieldLoc1);
    TSMimeHdrFieldNameSet(tmpBuf, tmpMimeHdrLoc, fieldLoc1, MY_TEST_HDR_1, strlen(MY_TEST_HDR_1));

    /* insert (append) a Date value into the new field */
    currentTime = time(&currentTime);
    if (TSMimeHdrFieldValueDateInsert(tmpBuf, tmpMimeHdrLoc, fieldLoc1, currentTime) != TS_SUCCESS) {
      LOG_API_ERROR("TSMimeHdrFieldValueDateInsert");
    }

    /* negative test for TSMimeHdrFieldValueDateInsert */
#ifdef DEBUG
    if (TSMimeHdrFieldValueDateInsert(NULL, tmpMimeHdrLoc, fieldLoc1, currentTime) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueDateInsert");
    }
    if (TSMimeHdrFieldValueDateInsert(tmpBuf, NULL, fieldLoc1, currentTime) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueDateInsert");
    }
    if (TSMimeHdrFieldValueDateInsert(tmpBuf, tmpMimeHdrLoc, NULL, currentTime) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueDateInsert");
    }
#endif

    /* get the field value and print it out */
    printHeader(tmpBuf, tmpMimeHdrLoc);
    /* auto */
    printDateDifference(tmpBuf, tmpMimeHdrLoc, MY_TEST_HDR_1, currentTime, RESP, 10);

                /***** (11): create a new mime field and play with TSMimeHdrFieldValue[Insert|Set|Get]* *****/
    /* create the second new field */
    if ((fieldLoc2 = TSMimeHdrFieldCreate(tmpBuf, tmpMimeHdrLoc)) == TS_ERROR_PTR) {
      LOG_API_ERROR("TSMimeHdrFieldCreate");
    }
    if (TSMimeHdrFieldAppend(tmpBuf, tmpMimeHdrLoc, fieldLoc2) != TS_SUCCESS) {
      LOG_API_ERROR("TSMimeHdrFieldAppend");
    }
    if (TSMimeHdrFieldNameSet(tmpBuf, tmpMimeHdrLoc, fieldLoc2, MY_TEST_HDR_2, strlen(MY_TEST_HDR_2)) != TS_SUCCESS) {
      LOG_API_ERROR("TSMimeHdrFieldNameSet");
    }

    /* insert values into the new field */
    valueInt = -1;
    valueUint = 2;
    if (TSMimeHdrFieldValueIntInsert(tmpBuf, tmpMimeHdrLoc, fieldLoc2, -1, valueInt) != TS_SUCCESS) {
      LOG_API_ERROR("TSMimeHdrFieldValueIntInsert");
    }
    /* auto: retrive the newly inserted (last) Int value and check */
    idx = TSMimeHdrFieldValuesCount(tmpBuf, tmpMimeHdrLoc, fieldLoc2) - 1;
    int tmp_int;

    TSMimeHdrFieldValueIntGet(tmpBuf, tmpMimeHdrLoc, fieldLoc2, idx, &tmp_int)
    if (tmp_int != valueInt) {
      LOG_AUTO_ERROR("TSMimeHdrFieldValueIntInsert",
                     "TSMimeHdrFieldValueIntGet different from TSMimeHdrFieldValueIntInsert");
    }

    if (TSMimeHdrFieldValueIntSet(tmpBuf, tmpMimeHdrLoc, fieldLoc2, 0, 10) != TS_SUCCESS) {
      LOG_API_ERROR("TSMimeHdrFieldValueIntSet");
    }
    idx = TSMimeHdrFieldValuesCount(tmpBuf, tmpMimeHdrLoc, fieldLoc2) - 1;
    TSMimeHdrFieldValueIntGet(tmpBuf, tmpMimeHdrLoc, fieldLoc2, idx, &tmp_int);
    if (tmp_int != 10) {
      LOG_AUTO_ERROR("TSMimeHdrFieldValueIntSet",
                     "TSMimeHdrFieldValueIntGet different from TSMimeHdrFieldValueIntInsert");
    }

    if (TSMimeHdrFieldValueUintInsert(tmpBuf, tmpMimeHdrLoc, fieldLoc2, -1, valueUint) != TS_SUCCESS) {
      LOG_API_ERROR("TSMimeHdrFieldValueUintInsert");
    }

    /* auto: retrive the newly inserted (last) Uint value and check */
    idx = TSMimeHdrFieldValuesCount(tmpBuf, tmpMimeHdrLoc, fieldLoc2) - 1;

    unsigned int tmp_uint;

    TSMimeHdrFieldValueUintGet(tmpBuf, tmpMimeHdrLoc, fieldLoc2, idx, &tmp_uint)
    if (tmp_uint != valueUint) {
      LOG_AUTO_ERROR("TSMimeHdrFieldValueUintInsert",
                     "TSMimeHdrFieldValueUintGet different from TSMimeHdrFieldValueUintInsert");
    }
    printField(tmpBuf, tmpMimeHdrLoc, MY_TEST_HDR_2, RESP, 11);

    /* negative test for TSMimeHdrFieldValue[Int|Uint]Insert */
#ifdef DEBUG
    if (TSMimeHdrFieldValueIntInsert(NULL, tmpMimeHdrLoc, fieldLoc2, valueInt, -1) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueIntInsert");
    }
    if (TSMimeHdrFieldValueIntInsert(tmpBuf, NULL, fieldLoc2, valueInt, -1) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueIntInsert");
    }
    if (TSMimeHdrFieldValueIntInsert(tmpBuf, tmpMimeHdrLoc, NULL, valueInt, -1) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueIntInsert");
    }

    if (TSMimeHdrFieldValueUintInsert(NULL, tmpMimeHdrLoc, fieldLoc2, valueUint, -1) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueUintInsert");
    }
    if (TSMimeHdrFieldValueUintInsert(tmpBuf, NULL, fieldLoc2, valueUint, -1) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueUintInsert");
    }
    if (TSMimeHdrFieldValueUintInsert(tmpBuf, tmpMimeHdrLoc, NULL, valueUint, -1) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueUintInsert");
    }
#endif

                /***** (12): play with TSMimeHdrFieldCopyValues *****/
    /* create the third new field */
    if ((fieldLoc3 = TSMimeHdrFieldCreate(tmpBuf, tmpMimeHdrLoc)) == TS_ERROR_PTR) {
      LOG_API_ERROR("TSMimeHdrFieldCreate");
    }
    if (TSMimeHdrFieldAppend(tmpBuf, tmpMimeHdrLoc, fieldLoc3) != TS_SUCCESS) {
      LOG_API_ERROR("TSMimeHdrFieldAppend");
    }
    if (TSMimeHdrFieldNameSet(tmpBuf, tmpMimeHdrLoc, fieldLoc3, MY_TEST_HDR_3, strlen(MY_TEST_HDR_3)) != TS_SUCCESS) {
      LOG_API_ERROR("TSMimeHdrFieldNameSet");
    }

    /* copy the values from the second header field to the third one */
    if (TSMimeHdrFieldCopyValues(tmpBuf, tmpMimeHdrLoc, fieldLoc3, tmpBuf, tmpMimeHdrLoc, fieldLoc2) != TS_SUCCESS) {
      LOG_API_ERROR("TSMimeHdrFieldCopyValues");
    }
    printField(tmpBuf, tmpMimeHdrLoc, MY_TEST_HDR_3, RESP, 12);

    /* negative test for TSMimeHdrFieldCopyValues */
#ifdef DEBUG
    if (TSMimeHdrFieldCopyValues(NULL, tmpMimeHdrLoc, fieldLoc3, tmpBuf, tmpMimeHdrLoc, fieldLoc2) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldCopyValues");
    }
    if (TSMimeHdrFieldCopyValues(tmpBuf, NULL, fieldLoc3, tmpBuf, tmpMimeHdrLoc, fieldLoc2) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldCopyValues");
    }
    if (TSMimeHdrFieldCopyValues(tmpBuf, tmpMimeHdrLoc, NULL, tmpBuf, tmpMimeHdrLoc, fieldLoc2) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldCopyValues");
    }
    if (TSMimeHdrFieldCopyValues(tmpBuf, tmpMimeHdrLoc, fieldLoc3, NULL, tmpMimeHdrLoc, fieldLoc2) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldCopyValues");
    }
    if (TSMimeHdrFieldCopyValues(tmpBuf, tmpMimeHdrLoc, fieldLoc3, tmpBuf, NULL, fieldLoc2) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldCopyValues");
    }
    if (TSMimeHdrFieldCopyValues(tmpBuf, tmpMimeHdrLoc, fieldLoc3, tmpBuf, tmpMimeHdrLoc, NULL) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldCopyValues");
    }
#endif

    /* auto: Get the field value of fieldLoc3 and compare w/ fieldLoc2 */
    /* CAUTION: using idx = -1 is an undocumented internal usage */
    TSMimeHdrFieldValueStringGet(tmpBuf, tmpMimeHdrLoc, fieldLoc2, -1, &tmpFieldValue1, &tmpFieldValueLength);
    TSMimeHdrFieldValueStringGet(tmpBuf, tmpMimeHdrLoc, fieldLoc3, -1, &tmpFieldValue2, &tmpFieldValueLength);

    if (strncmp(tmpFieldValue1, tmpFieldValue2, tmpFieldValueLength)) {
      LOG_AUTO_ERROR("TSMimeHdrFieldCopy", "New copy of field values different from original");
    }


        /***** (13): play with TSMimeHdrFieldValueSet* *****/
    currentTime = time(&currentTime);

    /* set other values to the field */
    valueInt = -2;
    valueUint = 1;
    if (TSMimeHdrFieldValueIntSet(tmpBuf, tmpMimeHdrLoc, fieldLoc3, 0, valueInt) != TS_SUCCESS) {
      LOG_API_ERROR("TSMimeHdrFieldValueIntSet");
    }
    if (TSMimeHdrFieldValueUintSet(tmpBuf, tmpMimeHdrLoc, fieldLoc3, 1, valueUint) != TS_SUCCESS) {
      LOG_API_ERROR("TSMimeHdrFieldValueUintSet");
    }
    if (TSMimeHdrFieldValueDateSet(tmpBuf, tmpMimeHdrLoc, fieldLoc1, currentTime) != TS_SUCCESS) {
      LOG_API_ERROR("TSMimeHdrFieldValueDateSet");
    }
    printDateDifference(tmpBuf, tmpMimeHdrLoc, MY_TEST_HDR_1, currentTime, RESP, 13);
    printField(tmpBuf, tmpMimeHdrLoc, MY_TEST_HDR_3, RESP, 13);

    /* auto: Get the field values again and check */
    if (TSMimeHdrFieldValueIntGet(tmpBuf, tmpMimeHdrLoc, fieldLoc3, 0, &retrievedInt) != TS_SUCCESS) {
      LOG_API_ERROR("TSMimeHdrFieldValueIntGet");
    }
    if (retrievedInt != valueInt) {
      LOG_AUTO_ERROR("TSMimeHdrFieldValueIntSet",
                     "TSMimeHdrFieldValueIntGet different from TSMimeHdrFieldValueIntSet");
    }
    if (TSMimeHdrFieldValueUintGet(tmpBuf, tmpMimeHdrLoc, fieldLoc3, 1, &retrievedUint) != TS_SUCCESS) {
      LOG_API_ERROR("TSMimeHdrFieldValueUintGet");
    }
    if (retrievedUint != valueUint) {
      LOG_AUTO_ERROR("TSMimeHdrFieldValueUintSet",
                     "TSMimeHdrFieldValueUintGet different from TSMimeHdrFieldValueUintSet");
    }
    if (TSMimeHdrFieldValueDateGet(tmpBuf, tmpMimeHdrLoc, fieldLoc1, &retrievedDate) != TS_SUCCESS) {
      LOG_API_ERROR("TSMimeHdrFieldValueDateGet");
    }
    if (retrievedDate != currentTime) {
      LOG_AUTO_ERROR("TSMimeHdrFieldValueDateSet",
                     "TSMimeHdrFieldValueDateGet different from TSMimeHdrFieldValueDateSet");
    }

    /* negative test */
#ifdef DEBUG
    if (TSMimeHdrFieldValueIntSet(NULL, tmpMimeHdrLoc, fieldLoc3, 0, valueInt) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueIntSet");
    }
    if (TSMimeHdrFieldValueIntSet(tmpBuf, NULL, fieldLoc3, 0, valueInt) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueIntSet");
    }
    if (TSMimeHdrFieldValueIntSet(tmpBuf, tmpMimeHdrLoc, NULL, 0, valueInt) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueIntSet");
    }

    if (TSMimeHdrFieldValueUintSet(NULL, tmpMimeHdrLoc, fieldLoc3, 1, valueUint) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueUintSet");
    }
    if (TSMimeHdrFieldValueUintSet(tmpBuf, NULL, fieldLoc3, 1, valueUint) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueUintSet");
    }
    if (TSMimeHdrFieldValueUintSet(tmpBuf, tmpMimeHdrLoc, NULL, 1, valueUint) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueUintSet");
    }

    if (TSMimeHdrFieldValueDateSet(NULL, tmpMimeHdrLoc, fieldLoc1, currentTime) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueDateSet");
    }
    if (TSMimeHdrFieldValueDateSet(tmpBuf, NULL, fieldLoc1, currentTime) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueDateSet");
    }
    if (TSMimeHdrFieldValueDateSet(tmpBuf, tmpMimeHdrLoc, NULL, currentTime) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueDateSet");
    }

    if (TSMimeHdrFieldValueIntGet(NULL, tmpMimeHdrLoc, fieldLoc3, 0, &retrievedInt) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueIntGet");
    }
    if (TSMimeHdrFieldValueIntGet(tmpBuf, NULL, fieldLoc3, 0, &retrievedInt) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueIntGet");
    }
    if (TSMimeHdrFieldValueIntGet(tmpBuf, tmpMimeHdrLoc, NULL, 0, &retrievedInt) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueIntGet");
    }

    if (TSMimeHdrFieldValueUintGet(NULL, tmpMimeHdrLoc, fieldLoc3, 1, &retrievedUint) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueUintGet");
    }
    if (TSMimeHdrFieldValueUintGet(tmpBuf, NULL, fieldLoc3, 1, &retrievedUint) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueUintGet");
    }
    if (TSMimeHdrFieldValueUintGet(tmpBuf, tmpMimeHdrLoc, NULL, 1, &retrievedUint) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueUintGet");
    }

    if (TSMimeHdrFieldValueDateGet(NULL, tmpMimeHdrLoc, fieldLoc1, &retrievedDate) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueDateGet");
    }
    if (TSMimeHdrFieldValueDateGet(tmpBuf, NULL, fieldLoc1, &retrievedDate) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueDateGet");
    }
    if (TSMimeHdrFieldValueDateGet(tmpBuf, tmpMimeHdrLoc, NULL, &retrievedDate) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueDateGet");
    }
#endif

                /***** (14): play with TSMimeHdrFieldValueDelete *****/
    /* delete a field value */
    count = TSMimeHdrFieldValuesCount(tmpBuf, tmpMimeHdrLoc, fieldLoc3);

    TSMimeHdrFieldValueDelete(tmpBuf, tmpMimeHdrLoc, fieldLoc3, 1);
    printField(tmpBuf, tmpMimeHdrLoc, MY_TEST_HDR_3, RESP, 14);

    /* auto: try retrieving the deleted value now */
    if (TSMimeHdrFieldValuesCount(tmpBuf, tmpMimeHdrLoc, fieldLoc3) == count) {
      LOG_AUTO_ERROR("TSMimeHdrFieldValueDelete", "Field value count still the same after delete");
    }

    /* negative test for TSMimeHdrFieldValuesCount and TSMimeHdrFieldValueDelete */
#ifdef DEBUG
    if (TSMimeHdrFieldValuesCount(NULL, tmpMimeHdrLoc, fieldLoc3) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValuesCount");
    }
    if (TSMimeHdrFieldValuesCount(tmpBuf, NULL, fieldLoc3) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValuesCount");
    }
    if (TSMimeHdrFieldValuesCount(tmpBuf, tmpMimeHdrLoc, NULL) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValuesCount");
    }

    if (TSMimeHdrFieldValueDelete(NULL, tmpMimeHdrLoc, fieldLoc3, 1) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueDelete");
    }
    if (TSMimeHdrFieldValueDelete(tmpBuf, NULL, fieldLoc3, 1) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueDelete");
    }
    if (TSMimeHdrFieldValueDelete(tmpBuf, tmpMimeHdrLoc, NULL, 1) != TS_ERROR) {
      LOG_ERROR_NEG("TSMimeHdrFieldValueDelete");
    }
#endif

  done:
    /* Final cleanup */
    HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, fieldLoc1);
    HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, fieldLoc2);
    HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, fieldLoc3);
    HANDLE_RELEASE(tmpBuf, TS_NULL_MLOC, tmpMimeHdrLoc);
    BUFFER_DESTROY(tmpBuf);
  }
  /* httpType == TS_HTTP_TYPE_RESPONSE */
}                               /* sectionMimeHdr */


/****************************************************************************
 * handleReadRequest:
 *
 * Description: handler for TS_HTTP_READ_REQUEST_HDR_HOOK
 ****************************************************************************/

static void
handleReadRequest(TSCont pCont, TSHttpTxn pTxn)
{
  LOG_SET_FUNCTION_NAME("handleReadRequest");

  TSMBuffer reqHdrBuf = NULL;
  TSMLoc reqHttpHdrLoc = NULL;

  TSDebug(REQ, ">>>>>> handleReadRequest <<<<<<<");

  /* Get Request Marshall Buffer */
  if (!TSHttpTxnClientReqGet(pTxn, &reqHdrBuf, &reqHttpHdrLoc)) {
    LOG_AUTO_ERROR("TSHttpTxnClientReqGet", "ERROR: Can't retrieve client req hdr")
      goto done;
  }

  /* Do the Mime stuff now */
  sectionMimeHdr(reqHdrBuf, reqHttpHdrLoc);

done:
  HANDLE_RELEASE(reqHdrBuf, TS_NULL_MLOC, reqHttpHdrLoc);

  if (TSHttpTxnReenable(pTxn, TS_EVENT_HTTP_CONTINUE) == TS_ERROR) {
    LOG_API_ERROR("TSHttpTxnReenable");
  }

}


/************************************************************************
 * handleSendResponse:
 *
 * Description:	handler for TS_HTTP_SEND_RESPONSE_HOOK
 ************************************************************************/

static void
handleSendResponse(TSCont pCont, TSHttpTxn pTxn)
{
  LOG_SET_FUNCTION_NAME("handleSendResponse");

  TSMBuffer respHdrBuf = NULL;
  TSMLoc respHttpHdrLoc = NULL;

  TSDebug(RESP, "\n>>> handleSendResponse <<<<");

  /* Get Response Marshall Buffer */
  if (!TSHttpTxnClientRespGet(pTxn, &respHdrBuf, &respHttpHdrLoc)) {
    LOG_AUTO_ERROR("TSHttpTxnClientRespGet", "ERROR: Can't retrieve client resp hdr")
      goto done;
  }

  /* Do the Mime stuff now */
  sectionMimeHdr(respHdrBuf, respHttpHdrLoc);

done:
  HANDLE_RELEASE(respHdrBuf, TS_NULL_MLOC, respHttpHdrLoc);

  if (TSHttpTxnReenable(pTxn, TS_EVENT_HTTP_CONTINUE) == TS_ERROR) {
    LOG_API_ERROR("TSHttpTxnReenable");
  }
}




static void
handleTxnStart(TSCont pCont, TSHttpTxn pTxn)
{
  LOG_SET_FUNCTION_NAME("handleTxnStart");

  if (TSHttpTxnHookAdd(pTxn, TS_HTTP_READ_REQUEST_HDR_HOOK, pCont) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHookAdd");
  }
  if (TSHttpTxnHookAdd(pTxn, TS_HTTP_SEND_RESPONSE_HDR_HOOK, pCont) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHookAdd");
  }

  if (TSHttpTxnReenable(pTxn, TS_EVENT_HTTP_CONTINUE) == TS_ERROR) {
    LOG_API_ERROR("TSHttpTxnReenable");
  }
}


static int
contHandler(TSCont pCont, TSEvent event, void *edata)
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
  LOG_SET_FUNCTION_NAME("TSPluginInit");

  TSCont pCont;

  if ((pCont = TSContCreate(contHandler, NULL)) == TS_ERROR_PTR) {
    LOG_API_ERROR("TSContCreate");
  } else if (TSHttpHookAdd(TS_HTTP_TXN_START_HOOK, pCont) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHookAdd");
  }
}
