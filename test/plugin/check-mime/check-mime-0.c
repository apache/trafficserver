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
printMimeFields(INKMBuffer hdrBuf, INKMLoc mimeHdrLoc, char *debugTag, float section)
{
  LOG_SET_FUNCTION_NAME("printMimeFields");

  INKMLoc fieldLoc = NULL, nextFieldLoc = NULL, nextDupFieldLoc = NULL;

  int iFieldIndex, iFieldNameLength, iFieldValueLength, iFieldCount, iHdrLength;
  char *outputString = NULL;
  const char *sFieldValue = NULL, *sFieldName = NULL;

  iFieldIndex = -1;

  INKDebug(debugTag, "***********************( %g )***********************", section);

  /* Get the total MIME field count */
  if ((iFieldCount = INKMimeHdrFieldsCount(hdrBuf, mimeHdrLoc)) == INK_ERROR) {
    LOG_API_ERROR("INKMimeHdrFieldsCount");
  } else {
    INKDebug(debugTag, "(%g) Total # of MIME fields = %d", section, iFieldCount);
  }

  /* Get the MIME header length */
  if ((iHdrLength = INKMimeHdrLengthGet(hdrBuf, mimeHdrLoc)) == INK_ERROR) {
    LOG_API_ERROR("INKMimeHdrLengthGet");
  } else {
    INKDebug(debugTag, "(%g) MIME header length = %d", section, iHdrLength);
  }

  if ((fieldLoc = INKMimeHdrFieldGet(hdrBuf, mimeHdrLoc, 0)) == INK_ERROR_PTR) {
    LOG_API_ERROR("INKMimeHdrFieldGet");
  }

  /* negative test */
#ifdef DEBUG
  if (INKMimeHdrFieldsCount(NULL, mimeHdrLoc) != INK_ERROR) {
    LOG_ERROR_NEG("INKMimeHdrFieldsCount");
  }
  if (INKMimeHdrFieldsCount(hdrBuf, NULL) != INK_ERROR) {
    LOG_ERROR_NEG("INKMimeHdrFieldsCount");
  }

  if (INKMimeHdrLengthGet(NULL, mimeHdrLoc) != INK_ERROR) {
    LOG_ERROR_NEG("INKMimeHdrLengthGet");
  }
  if (INKMimeHdrLengthGet(hdrBuf, NULL) != INK_ERROR) {
    LOG_ERROR_NEG("INKMimeHdrLengthGet");
  }

  if (INKMimeHdrFieldNext(NULL, mimeHdrLoc, fieldLoc) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKMimeHdrFieldNext");
  }
  if (INKMimeHdrFieldNext(hdrBuf, NULL, fieldLoc) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKMimeHdrFieldNext");
  }
  if (INKMimeHdrFieldNext(hdrBuf, mimeHdrLoc, NULL) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKMimeHdrFieldNext");
  }
#endif

  /* Print all the Mime Field in the MIME header */
  while (fieldLoc) {
    /* store the next field loc here */
    if ((nextFieldLoc = INKMimeHdrFieldNext(hdrBuf, mimeHdrLoc, fieldLoc)) == INK_ERROR_PTR) {
      LOG_API_ERROR("INKMimeHdrFieldNext");
    }

    INKDebug(debugTag, "-----------------------");
    if ((sFieldName = INKMimeHdrFieldNameGet(hdrBuf, mimeHdrLoc, fieldLoc, &iFieldNameLength))
        == INK_ERROR_PTR) {
      LOG_API_ERROR("INKMimeHdrFieldNameGet");
    } else {
      outputString = INKstrndup(sFieldName, iFieldNameLength);
      INKDebug(debugTag, "(%g) Field Name[%d] = %s", section, iFieldNameLength, outputString);
      FREE(outputString);
      STR_RELEASE(hdrBuf, mimeHdrLoc, sFieldName);
    }

    do {
      if (INKMimeHdrFieldValueStringGet(hdrBuf, mimeHdrLoc, fieldLoc, iFieldIndex, &sFieldValue,
                                        &iFieldValueLength) == INK_ERROR) {
        LOG_API_ERROR("INKMimeHdrFieldValueStringGet");
      } else {
        outputString = INKstrndup(sFieldValue, iFieldValueLength);
        INKDebug(debugTag, "(%g) Field Value[%d] = %s", section, iFieldValueLength, outputString);
        FREE(outputString);
        STR_RELEASE(hdrBuf, mimeHdrLoc, sFieldValue);
      }

      if ((nextDupFieldLoc = INKMimeHdrFieldNextDup(hdrBuf, mimeHdrLoc, fieldLoc)) == INK_ERROR_PTR) {
        LOG_API_ERROR("INKMimeHdrFieldNextDup");
      }

      HANDLE_RELEASE(hdrBuf, mimeHdrLoc, fieldLoc);
      fieldLoc = nextDupFieldLoc;

    } while (nextDupFieldLoc && nextDupFieldLoc != INK_ERROR_PTR);

    fieldLoc = nextFieldLoc;
  }
}                               /* printMimeFields */


/* added by ymou */
static void
printField(INKMBuffer bufp, INKMLoc hdr, char *name, char *debugTag, float section)
{
  LOG_SET_FUNCTION_NAME("printField");

  int i;
  unsigned int j;
  int length, count;
  INKMLoc field = NULL;

  if ((field = INKMimeHdrFieldFind(bufp, hdr, name, strlen(name))) == INK_ERROR_PTR) {
    LOG_API_ERROR("INKMimeHdrFieldFind");
  }
  /* FIXME: Error code? */
  length = INKMimeHdrFieldLengthGet(bufp, hdr, field);

  if ((count = INKMimeHdrFieldValuesCount(bufp, hdr, field)) == INK_ERROR) {
    LOG_API_ERROR("INKMimeHdrFieldValuesCount");
  }
  INKMimeHdrFieldValueIntGet(bufp, hdr, field, 0, &i);
  INKMimeHdrFieldValueUintGet(bufp, hdr, field, 1, &j);

  INKDebug(debugTag, "***********************( %g )***********************", section);
  INKDebug(debugTag, "(%g) The length of the field %s = %d", section, name, length);
  INKDebug(debugTag, "(%g) The count of the field values = %d", section, count);
  INKDebug(debugTag, "(%g) The values of the field %s are %d and %u", section, name, i, j);

  /* negative test for INKMimeHdrFieldLengthGet */
#ifdef DEBUG
  if (INKMimeHdrFieldLengthGet(NULL, hdr, field) != INK_ERROR) {
    LOG_ERROR_NEG("INKMimeHdrFieldLengthGet");
  }
  if (INKMimeHdrFieldLengthGet(bufp, NULL, field) != INK_ERROR) {
    LOG_ERROR_NEG("INKMimeHdrFieldLengthGet");
  }
  if (INKMimeHdrFieldLengthGet(bufp, hdr, NULL) != INK_ERROR) {
    LOG_ERROR_NEG("INKMimeHdrFieldLengthGet");
  }
#endif

  /* release the string handle */
  HANDLE_RELEASE(bufp, hdr, field);
}


/* added by ymou */
static void
printDateDifference(INKMBuffer bufp, INKMLoc hdr, char *name, time_t currentTime, char *debugTag, float section)
{
  LOG_SET_FUNCTION_NAME("printDateDifference");

  time_t fieldTime;
  int length, count;
  INKMLoc field = NULL;

  if ((field = INKMimeHdrFieldFind(bufp, hdr, name, strlen(name))) == INK_ERROR_PTR) {
    LOG_API_ERROR("INKMimeHdrFieldFind");
  }
  /* FIXME: Error code? */
  length = INKMimeHdrFieldLengthGet(bufp, hdr, field);

  if ((count = INKMimeHdrFieldValuesCount(bufp, hdr, field)) == INK_ERROR) {
    LOG_API_ERROR("INKMimeHdrFieldValuesCount");
  }
  INKMimeHdrFieldValueDateGet(bufp, hdr, field, 0, &fieldTime);

  INKDebug(debugTag, "***********************( %g )***********************", section);
  INKDebug(debugTag, "(%g) The length of the field %s = %d", section, name, length);
  INKDebug(debugTag, "(%g) The count of the field values = %d", section, count);

  if (fieldTime != currentTime) {
    LOG_AUTO_ERROR("INKMimeHdrFieldValueStringInsert",
                   "The retrieved Date field value is different from the Date field value set");
  } else {
    INKDebug(debugTag, "(%g) The retrieved Date field value is the same as the Date field value set", section);
  }
  HANDLE_RELEASE(bufp, hdr, field);
}


/* added by ymou */
static void
printHeader(INKMBuffer bufp, INKMLoc hdr_loc)
{
  INKIOBuffer output_buffer;
  INKIOBufferReader reader;
  int total_avail;

  INKIOBufferBlock block;
  const char *block_start;
  int block_avail;

  char *output_string = NULL;
  int output_len;

  LOG_SET_FUNCTION_NAME("printHeader");

  output_buffer = INKIOBufferCreate();

  if (!output_buffer) {
    INKError("couldn't allocate IOBuffer\n");
  }

  reader = INKIOBufferReaderAlloc(output_buffer);

  /* This will print  just MIMEFields and not
     the http request line */
  INKMimeHdrPrint(bufp, hdr_loc, output_buffer);

  /* negative test for INKMimeHdrPrint */
#ifdef DEBUG
  if (INKMimeHdrPrint(NULL, hdr_loc, output_buffer) != INK_ERROR) {
    LOG_ERROR_NEG("INKMimeHdrPrint");
  }
  if (INKMimeHdrPrint(bufp, NULL, output_buffer) != INK_ERROR) {
    LOG_ERROR_NEG("INKMimeHdrPrint");
  }
  if (INKMimeHdrPrint(bufp, hdr_loc, NULL) != INK_ERROR) {
    LOG_ERROR_NEG("INKMimeHdrPrint");
  }
#endif

  /* Find out how the big the complete header is by
     seeing the total bytes in the buffer.  We need to
     look at the buffer rather than the first block to
     see the size of the entire header */
  total_avail = INKIOBufferReaderAvail(reader);

  /* Allocate the string with an extra byte for the string
     terminator */
  output_string = (char *) INKmalloc(total_avail + 1);
  output_len = 0;

  /* We need to loop over all the buffer blocks to make
     sure we get the complete header since the header can
     be in multiple blocks */
  block = INKIOBufferReaderStart(reader);
  while (block) {

    block_start = INKIOBufferBlockReadStart(block, reader, &block_avail);

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
    INKIOBufferReaderConsume(reader, block_avail);

    /* Get the next block now that we've consumed the
       data off the last block */
    block = INKIOBufferReaderStart(reader);
  }

  /* Terminate the string */
  output_string[output_len] = '\0';
  output_len++;

  /* Free up the INKIOBuffer that we used to print out the header */
  INKIOBufferReaderFree(reader);
  INKIOBufferDestroy(output_buffer);

  /* Although I'd never do this a production plugin, printf
     the header so that we can see it's all there */
  INKDebug(RESP, "**************** output header ****************");
  INKDebug(RESP, "%s", output_string);

  FREE(output_string);
}


static void
addDupFields(INKMBuffer hdrBuf, INKMLoc httpHdrLoc, char *debugTag, int section)
{
  LOG_SET_FUNCTION_NAME("addDupFields");

  INKMBuffer tmpBuf = NULL;
  INKMLoc tmpMimeHdrLoc = NULL;
  INKMLoc tmpFieldLoc = NULL, newFieldLoc = NULL, tmpNextDupFieldLoc = NULL;

  int iFieldNameLength;
  const char *tmpFieldValue = NULL;


  INKDebug(GENERAL, ">>>>>> addDupField <<<<<<");

  if ((tmpBuf = INKMBufferCreate()) == INK_ERROR_PTR) {
    LOG_API_ERROR("INKMBufferCreate");
  } else if ((tmpMimeHdrLoc = INKMimeHdrCreate(tmpBuf)) == INK_ERROR_PTR) {
    LOG_API_ERROR("INKMimeHdrCreate");
  }

  /* Copy the resp MIME Header to the tmp MIME Header */
  if (INKMimeHdrCopy(tmpBuf, tmpMimeHdrLoc, hdrBuf, httpHdrLoc) == INK_ERROR) {
    LOG_API_ERROR("INKMimeHdrCopy");
  }

  /* negative test for INKMimeHdrCopy */
#ifdef DEBUG
  if (INKMimeHdrCopy(NULL, tmpMimeHdrLoc, hdrBuf, httpHdrLoc) != INK_ERROR) {
    LOG_ERROR_NEG("INKMimeHdrCopy");
  }
  if (INKMimeHdrCopy(tmpBuf, NULL, hdrBuf, httpHdrLoc) != INK_ERROR) {
    LOG_ERROR_NEG("INKMimeHdrCopy");
  }
  if (INKMimeHdrCopy(tmpBuf, tmpMimeHdrLoc, NULL, httpHdrLoc) != INK_ERROR) {
    LOG_ERROR_NEG("INKMimeHdrCopy");
  }
  if (INKMimeHdrCopy(tmpBuf, tmpMimeHdrLoc, hdrBuf, NULL) != INK_ERROR) {
    LOG_ERROR_NEG("INKMimeHdrCopy");
  }
#endif

  /* Create a MIME field */
  if ((newFieldLoc = INKMimeHdrFieldCreate(tmpBuf, tmpMimeHdrLoc)) == INK_ERROR_PTR) {
    LOG_API_ERROR("INKMimeHdrFieldCreate");
  }

  /* negative test for INKMimeHdrFieldCreate */
#ifdef DEBUG
  if (INKMimeHdrFieldCreate(NULL, tmpMimeHdrLoc) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKMimeHdrFieldCreate");
  }
  if (INKMimeHdrFieldCreate(tmpBuf, NULL) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKMimeHdrFieldCreate");
  }
#endif

  if (INKMimeHdrFieldNameSet(tmpBuf, tmpMimeHdrLoc, newFieldLoc, "Field-1", strlen("Field-1"))
      == INK_ERROR) {
    LOG_API_ERROR("INKMimeHdrFieldNameSet");
  } else if (INKMimeHdrFieldValueStringInsert(tmpBuf, tmpMimeHdrLoc, newFieldLoc, "field-1-value-1",
                                        strlen("field-1-value-1"), -1) == INK_ERROR) {
    LOG_API_ERROR("INKMimeHdrFieldValueStringInsert");
  } else if (INKMimeHdrFieldAppend(tmpBuf, tmpMimeHdrLoc, newFieldLoc) == INK_ERROR) {
    LOG_API_ERROR("INKMimeHdrFieldAppend");
  }
  HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, newFieldLoc);

  /* auto: Now retrieve the field value back */
  if ((tmpFieldLoc = INKMimeHdrFieldFind(tmpBuf, tmpMimeHdrLoc, "Field-1", strlen("Field-1")))
      == INK_ERROR_PTR) {
    LOG_API_ERROR("INKMimeHdrFieldFind");
  } else if (tmpFieldLoc == NULL) {
    LOG_AUTO_ERROR("INKMimeHdrFieldFind", "Cannot find the newly created field");
  } else {
    /* CAUTION: idx == -1 is UNDOCUMENTED but valid argument */
    if (INKMimeHdrFieldValueStringGet(tmpBuf, tmpMimeHdrLoc, tmpFieldLoc, 0, &tmpFieldValue,
                                      &iFieldNameLength) == INK_ERROR) {
      LOG_API_ERROR("INKMimeHdrFieldValueStringGet");
    } else if (!tmpFieldValue) {
      LOG_AUTO_ERROR("INKMimeHdrFieldValueStringGet", "can't retrieve the field value");
    } else {
      if (strncmp(tmpFieldValue, "field-1-value-1", iFieldNameLength)) {
        LOG_AUTO_ERROR("INKMimeHdrFieldValueStringInsert", "Field value different from the inserted one");
      }
      STR_RELEASE(tmpBuf, newFieldLoc, tmpFieldValue);
    }
    HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, tmpFieldLoc);
  }

  /* Insert another field */
  if ((newFieldLoc = INKMimeHdrFieldCreate(tmpBuf, tmpMimeHdrLoc)) == INK_ERROR_PTR) {
    LOG_API_ERROR("INKMimeHdrFieldCreate");
  } else if (INKMimeHdrFieldAppend(tmpBuf, tmpMimeHdrLoc, newFieldLoc) == INK_ERROR) {
    LOG_API_ERROR("INKMimeHdrFieldAppend");
  } else if (INKMimeHdrFieldNameSet(tmpBuf, tmpMimeHdrLoc, newFieldLoc, "Dup-Field-1", strlen("Dup-Field-1"))
             == INK_ERROR) {
    LOG_API_ERROR("INKMimeHdrFieldNameSet");
  } else if (INKMimeHdrFieldValueStringInsert(tmpBuf, tmpMimeHdrLoc, newFieldLoc, "dup-field-1-value-1",
                                        strlen("dup-field-1-value-1"), -1) == INK_ERROR) {
    LOG_API_ERROR("INKMimeHdrFieldValueStringInsert");
  }
  HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, newFieldLoc);

  /* auto: Now retrieve it back to check if its been insered */
  if ((tmpFieldLoc = INKMimeHdrFieldFind(tmpBuf, tmpMimeHdrLoc, "Dup-Field-1", strlen("Dup-Field-1")))
      == INK_ERROR_PTR) {
    LOG_API_ERROR("INKMimeHdrFieldFind");
  } else if (!tmpFieldLoc) {
    LOG_AUTO_ERROR("INKMimeHdrFieldFind", "Cannot find the newly inserted field");
  } else {
    if (INKMimeHdrFieldValueStringGet(tmpBuf, tmpMimeHdrLoc, tmpFieldLoc, 0, &tmpFieldValue, &iFieldNameLength)
        == INK_ERROR) {
      LOG_API_ERROR("INKMimeHdrFieldValueStringGet");
    } else if (!tmpFieldValue) {
      LOG_AUTO_ERROR("INKMimeHdrFieldValueStringGet", "can't retrieve the field value");
    } else {
      if (strncmp(tmpFieldValue, "dup-field-1-value-1", iFieldNameLength)) {
        LOG_AUTO_ERROR("INKMimeHdrFieldValueStringInsert", "Field value different from the inserted one");
      }
      STR_RELEASE(tmpBuf, tmpFieldLoc, tmpFieldValue);
    }
  }
  HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, tmpFieldLoc);

  if ((newFieldLoc = INKMimeHdrFieldCreate(tmpBuf, tmpMimeHdrLoc)) == INK_ERROR_PTR) {
    LOG_API_ERROR("INKMimeHdrFieldCreate");
  } else if (INKMimeHdrFieldAppend(tmpBuf, tmpMimeHdrLoc, newFieldLoc) == INK_ERROR) {
    LOG_API_ERROR("INKMimeHdrFieldAppend");
  } else if (INKMimeHdrFieldNameSet(tmpBuf, tmpMimeHdrLoc, newFieldLoc, "Dup-Field-1", strlen("Dup-Field-1"))
             == INK_ERROR) {
    LOG_API_ERROR("INKMimeHdrFieldNameSet");
  } else if (INKMimeHdrFieldValueStringInsert(tmpBuf, tmpMimeHdrLoc, newFieldLoc, "dup-field-1-value-2",
                                        strlen("dup-field-1-value-2"), -1) == INK_ERROR) {
    LOG_API_ERROR("INKMimeHdrFieldValueStringInsert");
  }
  HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, newFieldLoc);


  /* auto: Now retrieve the 1st duplicate field and check its value for correctness */
  if ((tmpFieldLoc = INKMimeHdrFieldFind(tmpBuf, tmpMimeHdrLoc, "Dup-Field-1", strlen("Dup-Field-1")))
      == INK_ERROR_PTR) {
    LOG_API_ERROR("INKMimeHdrFieldFind");
  } else if (tmpFieldLoc == NULL) {     /* check for NULL -- if the field is not found */
    LOG_AUTO_ERROR("INKMimeHdrFieldFind", "cannot find the newly inserted field");
  } else {
    /* negative test for INKMimeHdrFieldNextDup */
#ifdef DEBUG
    if (INKMimeHdrFieldNextDup(NULL, tmpMimeHdrLoc, tmpFieldLoc) != INK_ERROR_PTR) {
      LOG_ERROR_NEG("INKMimeHdrFieldNextDup");
    }
    if (INKMimeHdrFieldNextDup(tmpBuf, NULL, tmpFieldLoc) != INK_ERROR_PTR) {
      LOG_ERROR_NEG("INKMimeHdrFieldNextDup");
    }
    if (INKMimeHdrFieldNextDup(tmpBuf, tmpMimeHdrLoc, NULL) != INK_ERROR_PTR) {
      LOG_ERROR_NEG("INKMimeHdrFieldNextDup");
    }
#endif
    if ((tmpNextDupFieldLoc = INKMimeHdrFieldNextDup(tmpBuf, tmpMimeHdrLoc, tmpFieldLoc))
        == INK_ERROR_PTR) {
      LOG_API_ERROR("INKMimeHdrFieldNextDup");
    } else if (tmpNextDupFieldLoc == NULL) {    /* check for NULL -- if NOT found */
      LOG_AUTO_ERROR("INKMimeHdrFieldNextDup", "cannot retrieve the 1st dup field loc");
    } else {
      if (INKMimeHdrFieldValueStringGet(tmpBuf, tmpMimeHdrLoc, tmpNextDupFieldLoc, 0, &tmpFieldValue,
                                        &iFieldNameLength) == INK_ERROR) {
        LOG_API_ERROR("INKMimeHdrFieldValueStringGet");
      } else if (!tmpFieldValue) {
        LOG_AUTO_ERROR("INKMimeHdrFieldValueStringGet", "can't retrieve the 1st dup field value");
      } else {
        if (strncmp(tmpFieldValue, "dup-field-1-value-2", iFieldNameLength)) {
          LOG_AUTO_ERROR("INKMimeHdrFieldValueStringInsert", "Field value different from the inserted one");
        }
        STR_RELEASE(tmpBuf, newFieldLoc, tmpFieldValue);
      }
      HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, tmpFieldLoc);
      tmpFieldLoc = tmpNextDupFieldLoc; /* preserve the fieldLoc here */
      /*HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, tmpNextDupFieldLoc); */
    }
  }


  if ((newFieldLoc = INKMimeHdrFieldCreate(tmpBuf, tmpMimeHdrLoc)) == INK_ERROR_PTR) {
    LOG_API_ERROR("INKMimeHdrFieldCreate");
  } else if (INKMimeHdrFieldAppend(tmpBuf, tmpMimeHdrLoc, newFieldLoc) == INK_ERROR) {
    LOG_API_ERROR("INKMimeHdrFieldAppend");
  } else if (INKMimeHdrFieldNameSet(tmpBuf, tmpMimeHdrLoc, newFieldLoc, "Dup-Field-1", strlen("Dup-Field-1"))
             == INK_ERROR) {
    LOG_API_ERROR("INKMimeHdrFieldNameSet");
  } else if (INKMimeHdrFieldValueStringInsert(tmpBuf, tmpMimeHdrLoc, newFieldLoc, "dup-field-1-value-3",
                                        strlen("dup-field-1-value-3"), -1) == INK_ERROR) {
    LOG_API_ERROR("INKMimeHdrFieldValueStringInsert");
  }
  HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, newFieldLoc);


  /* auto: Now retrieve the 2nd duplicate (using the "preserved" field loc from above) field
   * value back and check for correctness */

  if ((tmpNextDupFieldLoc = INKMimeHdrFieldNextDup(tmpBuf, tmpMimeHdrLoc, tmpFieldLoc))
      == INK_ERROR_PTR) {
    LOG_API_ERROR("INKMimeHdrFieldNextDup");
  } else if (!tmpNextDupFieldLoc) {
    LOG_AUTO_ERROR("INKMimeHdrFieldNextDup", "cannot retrieve the 2nd dup field loc");
  } else {
    if (INKMimeHdrFieldValueStringGet(tmpBuf, tmpMimeHdrLoc, tmpNextDupFieldLoc, 0, &tmpFieldValue,
                                      &iFieldNameLength) == INK_ERROR) {
      LOG_API_ERROR("INKMimeHdrFieldValueStringGet");
    } else if (!tmpFieldValue) {
      LOG_AUTO_ERROR("INKMimeHdrFieldValueStringGet", "can't retrieve the 2nd dup field value");
    } else {
      if (strncmp(tmpFieldValue, "dup-field-1-value-3", iFieldNameLength)) {
        LOG_AUTO_ERROR("INKMimeHdrFieldValueStringInsert", "Field value different from the inserted one");
      }
      STR_RELEASE(tmpBuf, tmpNextDupFieldLoc, tmpFieldValue);
    }
    HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, tmpNextDupFieldLoc);
  }

  printMimeFields(tmpBuf, tmpMimeHdrLoc, debugTag, section);

  /* clean-up */
  HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, tmpFieldLoc);
  HANDLE_RELEASE(tmpBuf, INK_NULL_MLOC, tmpMimeHdrLoc);

  BUFFER_DESTROY(tmpBuf);
}                               /* addDupFields */



/****************************************************************************
 * sectionMimeHdr:
 ****************************************************************************/
static void
sectionMimeHdr(INKMBuffer hdrBuf, INKMLoc httpHdrLoc)
{
  LOG_SET_FUNCTION_NAME("sectionMimeHdr");

  INKMBuffer tmpBuf = NULL;
  INKMLoc fieldLoc = NULL, tmpFieldLoc = NULL, newFieldLoc = NULL, tmpMimeHdrLoc = NULL;
  INKHttpType httpType;

  const char *tmpFieldValueString;
  const char *tmpFieldName = NULL, *tmpFieldValue1 = NULL, *tmpFieldValue2 = NULL;
  int tmpFieldNameLength, tmpFieldValueLength, count;
  time_t tmpDate1, tmpDate2;

  /* added by ymou */
  INKMLoc fieldLoc1 = NULL, fieldLoc2 = NULL, fieldLoc3 = NULL;
  time_t currentTime, fieldTime, retrievedDate;
  int valueInt, idx, retrievedInt;
  unsigned int valueUint, retrievedUint;

  /* Get the field location */
  fieldLoc = INKMimeHdrFieldGet(hdrBuf, httpHdrLoc, 0);

  /* negative test for INKMimeHdrFieldGet */
#ifdef DEBUG
  if (INKMimeHdrFieldGet(NULL, httpHdrLoc, 0) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKMimeHdrFieldGet");
  }
  if (INKMimeHdrFieldGet(hdrBuf, NULL, 0) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKMimeHdrFieldGet");
  }
#endif

  httpType = INKHttpHdrTypeGet(hdrBuf, httpHdrLoc);

  INKDebug(GENERAL, "\n>>> sectionMimeHdr <<<<");


        /************* INK_HTTP_TYPE_REQUEST ******************/
  if (httpType == INK_HTTP_TYPE_REQUEST) {
    INKDebug(REQ, "\n>>> REQUEST <<<<");

                /***** (1): simply print the request header *****/
    printMimeFields(hdrBuf, httpHdrLoc, REQ, 1);

  }


  /* httpType == INK_HTTP_TYPE_REQUEST */
 /************ INK_HTTP_TYPE_RESPONSE ******************/
  if (httpType == INK_HTTP_TYPE_RESPONSE) {
    INKDebug(RESP, "\n>>> RESPONSE <<<<");

                /****** (1): Simply print the response header ****/
    printMimeFields(hdrBuf, httpHdrLoc, RESP, 1);


                /****** (2): Insert some duplicate fields ****/
    addDupFields(hdrBuf, httpHdrLoc, RESP, 2);


                /****** (3): Do MIME hdr copy and print *****/
    /* Copy the respHdrBuf MIME Headers to a tmp buf and print the details */

    /* CAUTION: (reference - INKqa8336)
     * Here we are doing a INKMimeHdrCopy without creating a HTTP header first.
     * So the dest MIME header (tmpMimeHdrLoc) is not associated with any HTTP header.
     * This is hardly ever the case, and should NOT be practised in general.  I am
     * doing so merely to test the API's functional correctness and NOT to suggest
     * a possible usage of the API */

    if ((tmpBuf = INKMBufferCreate()) == INK_ERROR_PTR) {
      LOG_API_ERROR_COMMENT("INKMBufferCreate", "abnormal exit to 'done'");
      goto done;
    }

    if ((tmpMimeHdrLoc = INKMimeHdrCreate(tmpBuf)) == INK_ERROR_PTR) {
      LOG_API_ERROR("INKMimeHdrCreate");
    } else if (INKMimeHdrCopy(tmpBuf, tmpMimeHdrLoc, hdrBuf, httpHdrLoc) == INK_ERROR) {
      LOG_API_ERROR("INKMimeHdrCopy");
    } else {
      printMimeFields(tmpBuf, tmpMimeHdrLoc, RESP, 3);
    }


                /****** (4): Remove some MIME field *****/
    /* Remove the "Via" field */
    if ((tmpFieldLoc = INKMimeHdrFieldFind(tmpBuf, tmpMimeHdrLoc, "Via", strlen("Via")))
        == INK_ERROR_PTR) {
      LOG_API_ERROR("INKMimeHdrFieldFind");
    } else if (INKMimeHdrFieldRemove(tmpBuf, tmpMimeHdrLoc, tmpFieldLoc) == INK_ERROR) {
      LOG_API_ERROR("INKMimeHdrFieldRemove");
    } else if (INKMimeHdrFieldValueStringGet(tmpBuf, tmpMimeHdrLoc, tmpFieldLoc, -1, &tmpFieldValue1,
                                             &tmpFieldValueLength) == INK_ERROR) {
      LOG_API_ERROR("INKMimeHdrFieldValueStringGet");
    }

    /* negative test */
#ifdef DEBUG
    if (INKMimeHdrFieldFind(NULL, tmpMimeHdrLoc, "Via", strlen("Via")) != INK_ERROR_PTR) {
      LOG_ERROR_NEG("INKMimeHdrFieldFind");
    }
    if (INKMimeHdrFieldFind(tmpBuf, NULL, "Via", strlen("Via")) != INK_ERROR_PTR) {
      LOG_ERROR_NEG("INKMimeHdrFieldFind");
    }
    if (INKMimeHdrFieldFind(tmpBuf, tmpMimeHdrLoc, NULL, 0) != INK_ERROR_PTR) {
      LOG_ERROR_NEG("INKMimeHdrFieldFind");
    }

    if (INKMimeHdrFieldRemove(NULL, tmpMimeHdrLoc, tmpFieldLoc) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldRemove");
    }
    if (INKMimeHdrFieldRemove(tmpBuf, NULL, tmpFieldLoc) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldRemove");
    }
    if (INKMimeHdrFieldRemove(tmpBuf, tmpMimeHdrLoc, NULL) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldRemove");
    }
#endif

    /* auto: now FINDing the field should STILL pass */
    if (INKMimeHdrFieldFind(tmpBuf, tmpMimeHdrLoc, "Via", strlen("Via")) == INK_ERROR_PTR) {
      LOG_API_ERROR("INKMimeHdrFieldFind");
      LOG_AUTO_ERROR("INKMimeHdrFieldRemove", "INK_Find failing afterINK_Remove");
    }

    printMimeFields(tmpBuf, tmpMimeHdrLoc, RESP, 4.1);

    /* Re-attach the "removed" field */
    if (INKMimeHdrFieldAppend(tmpBuf, tmpMimeHdrLoc, tmpFieldLoc) == INK_ERROR) {
      LOG_API_ERROR("INKMimeHdrFieldInset");
    }
    if ((tmpFieldLoc = INKMimeHdrFieldFind(tmpBuf, tmpMimeHdrLoc, "Via", strlen("Via")))
        == INK_ERROR_PTR) {
      LOG_API_ERROR("INKMimeHdrFieldFind");
    } else if (INKMimeHdrFieldValueStringGet(tmpBuf, tmpMimeHdrLoc, tmpFieldLoc, -1, &tmpFieldValue2,
                                             &tmpFieldValueLength) == INK_ERROR) {
      LOG_API_ERROR("INKMimeHdrFieldValueStringGet");
    }
    if (strncmp(tmpFieldValue1, tmpFieldValue2, tmpFieldValueLength)) {
      LOG_AUTO_ERROR("INKMimeHdrFieldAppend", "Field value different w/ the re-attach after INK_Remove");
    }

    printMimeFields(tmpBuf, tmpMimeHdrLoc, RESP, 4.2);

    /* cleanup */
    STR_RELEASE(tmpBuf, tmpFieldLoc, tmpFieldValue1);
    STR_RELEASE(tmpBuf, tmpFieldLoc, tmpFieldValue2);
    HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, tmpFieldLoc);


                /****** (5): delete some MIME fields ****/
    /* Delete the "Via" field */
    if ((tmpFieldLoc = INKMimeHdrFieldFind(tmpBuf, tmpMimeHdrLoc, "Via", strlen("Via")))
        == INK_ERROR_PTR) {
      LOG_API_ERROR("INKMimeHdrFieldFind");
    } else if (INKMimeHdrFieldDestroy(tmpBuf, tmpMimeHdrLoc, tmpFieldLoc) == INK_ERROR) {
      LOG_API_ERROR("INKMimeHdrFieldDestroy");
    }

    /* auto: now FINDing the field should fail */
    if ((tmpFieldLoc = INKMimeHdrFieldFind(tmpBuf, tmpMimeHdrLoc, "Via", strlen("Via")))
        == INK_ERROR_PTR) {
      LOG_API_ERROR("INKMimeHdrFieldFind");
    } else if (tmpFieldLoc) {
      LOG_AUTO_ERROR("INKMimeHdrFieldRemove", "Can STILL INK_Find after INK_Delete");
    }

    printMimeFields(tmpBuf, tmpMimeHdrLoc, RESP, 5);

    HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, tmpFieldLoc);

                /****** section (6) ******/
    /* -----------------------------------------------------------------------
     * Now, insert some fields into the MIME buffer
     * Note:
     *      1. Field name can be set before and/or after INKMimeHdrFieldAppend
     *      2. Field value could be set *only* after INKMimeHdrFieldValueStringInsert
     *
     * (point 1. and 2. implies that its possible to insert fields with empty
     * name and values)
     * -----------------------------------------------------------------------*/

    INKDebug(RESP, "***********************( 6.2 )***********************");

        /*** (6.2): append some *field value* ******/
    if ((newFieldLoc = INKMimeHdrFieldCreate(tmpBuf, tmpMimeHdrLoc)) == INK_ERROR_PTR) {
      LOG_API_ERROR_COMMENT("INKMimeHdrFieldCreate", "Skip to section 6.3");
      goto section_63;
    } else if (INKMimeHdrFieldNameSet(tmpBuf, tmpMimeHdrLoc, newFieldLoc, "Append-Field",
                                      strlen("Append-Field")) == INK_ERROR) {
      LOG_API_ERROR("INKMimeHdrFieldNameSet");
    } else if (INKMimeHdrFieldValueStringInsert(tmpBuf, tmpMimeHdrLoc, newFieldLoc, "append-field-value",
                                          strlen("append-field-value"), -1) == INK_ERROR) {
      LOG_API_ERROR("INKMimeHdrFieldValueStringInsert");
    }

    /* negative test for INKMimeHdrFieldNameSet */
#ifdef DEBUG
    if (INKMimeHdrFieldNameSet(NULL, tmpMimeHdrLoc, newFieldLoc, "Append-Field", strlen("Append-Field")) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldNameSet");
    }
    if (INKMimeHdrFieldNameSet(tmpBuf, NULL, newFieldLoc, "Append-Field", strlen("Append-Field")) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldNameSet");
    }
    if (INKMimeHdrFieldNameSet(tmpBuf, tmpMimeHdrLoc, NULL, "Append-Field", strlen("Append-Field")) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldNameSet");
    }
    if (INKMimeHdrFieldNameSet(tmpBuf, tmpMimeHdrLoc, newFieldLoc, NULL, 0) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldNameSet");
    }
    if (INKMimeHdrFieldNameSet(tmpBuf, tmpMimeHdrLoc, newFieldLoc, NULL, -1) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldNameSet");
    }
#endif

    /* Now, do the insert : append to the list of fields */
    if (INKMimeHdrFieldAppend(tmpBuf, tmpMimeHdrLoc, newFieldLoc) == INK_ERROR) {
      LOG_API_ERROR("INKMimeHdrFieldAppend");
    }

    /* auto: check the appended field using the last idx value */
    if ((idx = INKMimeHdrFieldsCount(tmpBuf, tmpMimeHdrLoc)) == INK_ERROR) {
      LOG_API_ERROR("INKMimeHdrFieldsCount");
    } else if ((tmpFieldLoc = INKMimeHdrFieldGet(tmpBuf, tmpMimeHdrLoc, --idx)) == INK_ERROR_PTR) {
      LOG_API_ERROR("INKMimeHdrFieldGet");
    } else {
      if ((tmpFieldName = INKMimeHdrFieldNameGet(tmpBuf, tmpMimeHdrLoc, tmpFieldLoc,
                                                 &tmpFieldNameLength)) == INK_ERROR_PTR) {
        LOG_API_ERROR("INKMimeHdrFieldNameGet");
      } else {
        if (strncmp(tmpFieldName, "Append-Field", strlen("Append-Field"))) {
          LOG_AUTO_ERROR("INKMimeHdrFieldAppend", "New field not appended!");
        }
        STR_RELEASE(tmpBuf, tmpMimeHdrLoc, tmpFieldName);
      }
      /* negative test for INKMimeHdrFieldNameGet */
#ifdef DEBUG
      if (INKMimeHdrFieldNameGet(NULL, tmpMimeHdrLoc, tmpFieldLoc, &tmpFieldNameLength) != INK_ERROR_PTR) {
        LOG_ERROR_NEG("INKMimeHdrFieldNameGet");
      }
      if (INKMimeHdrFieldNameGet(tmpBuf, NULL, tmpFieldLoc, &tmpFieldNameLength) != INK_ERROR_PTR) {
        LOG_ERROR_NEG("INKMimeHdrFieldNameGet");
      }
      if (INKMimeHdrFieldNameGet(tmpBuf, tmpMimeHdrLoc, NULL, &tmpFieldNameLength) != INK_ERROR_PTR) {
        LOG_ERROR_NEG("INKMimeHdrFieldNameGet");
      }
#endif

      HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, tmpFieldLoc);
    }
    printMimeFields(tmpBuf, tmpMimeHdrLoc, RESP, 6.2);
    HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, newFieldLoc);

  section_63:
    INKDebug(RESP, "***********************( 6.3 )***********************");

                /**** (6.3): append field-values (comma seperated) to "Append-Field" ***/

    if ((newFieldLoc = INKMimeHdrFieldFind(tmpBuf, tmpMimeHdrLoc, "Append-Field",
                                           strlen("Append-Field"))) == INK_ERROR_PTR) {
      LOG_API_ERROR_COMMENT("INKMimeHdrFieldFind", "Skip to section 7");
      goto section_7;
    } else if (INKMimeHdrFieldValueStringInsert(tmpBuf, tmpMimeHdrLoc, newFieldLoc, "append-field-value-2",
                                          strlen("append-field-value-2"), -1) == INK_ERROR) {
      LOG_API_ERROR("INKMimeHdrFieldValueStringInsert");
    }

    /* auto: check the newly appended field value w/ idx == 1 */
    if (INKMimeHdrFieldValueStringGet(tmpBuf, tmpMimeHdrLoc, newFieldLoc, 1, &tmpFieldValueString,
                                      &tmpFieldNameLength) == INK_ERROR) {
      LOG_API_ERROR("INKMimeHdrFieldValueStringGet");
      INKDebug(RESP, "string = %s", tmpFieldValueString);
    } else {
      if (strncmp(tmpFieldValueString, "append-field-value-2", strlen("append-field-value-2"))) {
        LOG_AUTO_ERROR("INKMimeHdrFieldValueStringInsert", "New field value not appended!");
      }
      STR_RELEASE(tmpBuf, tmpMimeHdrLoc, tmpFieldValueString);
    }

    printMimeFields(tmpBuf, tmpMimeHdrLoc, RESP, 6.3);
    HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, newFieldLoc);

  section_7:
    INKDebug(RESP, "***********************( 7 )***********************");

                /****** (7): Now modify the field values ******/

                /*** (7.1): Totally change the field value *****/
    if ((newFieldLoc = INKMimeHdrFieldFind(tmpBuf, tmpMimeHdrLoc, "Append-Field",
                                           strlen("Append-Field"))) == INK_ERROR_PTR) {
      LOG_API_ERROR_COMMENT("INKMimeHdrFieldFind", "Skip to section 7.2");
      goto section_8;
    }

    /* FIXME: INKqa8060: check if idx == -1 is an unpublised behaviour still */
    if (INKMimeHdrFieldValueStringSet(tmpBuf, tmpMimeHdrLoc, newFieldLoc, -1, "new-append-field-value",
                                      strlen("new-append-field-value")) == INK_ERROR) {
      LOG_API_ERROR("INKMimeHdrFieldValueStringSet");
    }

    /* auto: check the newly changed field value */
    if (INKMimeHdrFieldValueStringGet(tmpBuf, tmpMimeHdrLoc, newFieldLoc, -1, &tmpFieldValueString,
                                      &tmpFieldNameLength) == INK_ERROR) {
      LOG_API_ERROR("INKMimeHdrFieldValueStringGet");
    } else {
      if (strncmp(tmpFieldValueString, "new-append-field-value", strlen("new-append-field-value"))) {
        LOG_AUTO_ERROR("INKMimeHdrFieldValueStringInsert", "New field value not replaced properly !");
      }
      STR_RELEASE(tmpBuf, tmpMimeHdrLoc, tmpFieldValueString);
    }

    printMimeFields(tmpBuf, tmpMimeHdrLoc, RESP, 7.1);

    /* negative test */
#ifdef DEBUG
    if (INKMimeHdrFieldValueStringSet(NULL, tmpMimeHdrLoc, newFieldLoc, 0, "neg-test-field-value",
                                      strlen("neg-test-field-value")) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueStringSet");
    }
    if (INKMimeHdrFieldValueStringSet(tmpBuf, NULL, newFieldLoc, 0, "neg-test-field-value",
                                      strlen("neg-test-field-value")) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueStringSet");
    }
    if (INKMimeHdrFieldValueStringSet(tmpBuf, tmpMimeHdrLoc, NULL, 0, "neg-test-field-value",
                                      strlen("neg-test-field-value")) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueStringSet");
    }
    if (INKMimeHdrFieldValueStringSet(tmpBuf, tmpMimeHdrLoc, newFieldLoc, 0, NULL, -1) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueStringSet");
    }

    if (INKMimeHdrFieldValueStringInsert(NULL, tmpMimeHdrLoc, newFieldLoc, 0, "neg-test-field-value",
                                         strlen("neg-test-field-value")) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueStringInsert");
    }
    if (INKMimeHdrFieldValueStringInsert(tmpBuf, NULL, newFieldLoc, 0, "neg-test-field-value",
                                         strlen("neg-test-field-value")) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueStringInsert");
    }
    if (INKMimeHdrFieldValueStringInsert(tmpBuf, tmpMimeHdrLoc, NULL, 0, "neg-test-field-value",
                                         strlen("neg-test-field-value")) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueStringInsert");
    }
    if (INKMimeHdrFieldValueStringInsert(tmpBuf, tmpMimeHdrLoc, newFieldLoc, 0, NULL, -1) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueStringInsert");
    }

    if (INKMimeHdrFieldValueStringGet(NULL, tmpMimeHdrLoc, newFieldLoc, -1, &tmpFieldValueString,
                                      &tmpFieldNameLength) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueStringGet");
    }
    if (INKMimeHdrFieldValueStringGet(tmpBuf, NULL, newFieldLoc, -1, &tmpFieldValueString,
                                      &tmpFieldNameLength) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueStringGet");
    }
    if (INKMimeHdrFieldValueStringGet(tmpBuf, tmpMimeHdrLoc, NULL, -1, &tmpFieldValueString,
                                      &tmpFieldNameLength) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueStringGet");
    }
    if (INKMimeHdrFieldValueStringGet(tmpBuf, tmpMimeHdrLoc, newFieldLoc, -1, NULL, &tmpFieldNameLength) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueStringGet");
    }
    if (INKMimeHdrFieldValueStringGet(tmpBuf, tmpMimeHdrLoc, newFieldLoc, -1, &tmpFieldValueString, NULL) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueStringGet");
    }
#endif

                /*** (7.2): Now append a string to a field value ***/

    if (INKMimeHdrFieldValueAppend(tmpBuf, tmpMimeHdrLoc, newFieldLoc, 0, "<appended-text>",
                                   strlen("<appended-text>")) == INK_ERROR) {
      LOG_API_ERROR("INKMimeHdrFieldValueAppend");
    }

    /* negative test for INKMimeHdrFieldValueAppend */
#ifdef DEBUG
    if (INKMimeHdrFieldValueAppend(NULL, tmpMimeHdrLoc, newFieldLoc, 0, "<appended-text>", strlen("<appended-text>")) !=
        INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueAppend");
    }
    if (INKMimeHdrFieldValueAppend(tmpBuf, NULL, newFieldLoc, 0, "<appended-text>", strlen("<appended-text>")) !=
        INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueAppend");
    }
    if (INKMimeHdrFieldValueAppend(tmpBuf, tmpMimeHdrLoc, NULL, 0, "<appended-text>", strlen("<appended-text>")) !=
        INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueAppend");
    }
#endif

    /* auto: check the newly changed field value */
    /* FIXME: check if idx == -1 is still undocumented ?? */
    if (INKMimeHdrFieldValueStringGet(tmpBuf, tmpMimeHdrLoc, newFieldLoc, -1, &tmpFieldValueString,
                                      &tmpFieldNameLength) == INK_ERROR) {
      LOG_API_ERROR("INKMimeHdrFieldValueStringGet");
    } else {
      if (!strstr(tmpFieldValueString, "<appended-text>")) {
        LOG_AUTO_ERROR("INKMimeHdrFieldValueStringInsert", "Cannot located the appended text to field value!");
      }
      STR_RELEASE(tmpBuf, tmpMimeHdrLoc, tmpFieldValueString);
    }

    printMimeFields(tmpBuf, tmpMimeHdrLoc, RESP, 7.2);

    /* clean-up */
    HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, newFieldLoc);

  section_8:
    INKDebug(RESP, "***********************( 8 )***********************");

                /****** (8): clear values for few fields ******/
    /* fieldLoc = INKMimeHdrFieldFind (tmpBuf, tmpMimeHdrLoc, "Date", strlen("Date")); */

    if ((fieldLoc = INKMimeHdrFieldFind(tmpBuf, tmpMimeHdrLoc, INK_MIME_FIELD_DATE, INK_MIME_LEN_DATE))
        == INK_ERROR_PTR) {
      LOG_API_ERROR("INKMimeHdrFieldFind");
    } else {
      if (INKMimeHdrFieldValueDateGet(tmpBuf, tmpMimeHdrLoc, fieldLoc, &tmpDate1) == INK_ERROR) {
        LOG_API_ERROR("INKMimeHdrFieldValueDateGet");
      } else if (INKMimeHdrFieldValuesClear(tmpBuf, tmpMimeHdrLoc, fieldLoc) == INK_ERROR) {
        LOG_API_ERROR("INKMimeHdrFieldValuesClear");
      }
    }

    /* negative test for INKMimeHdrFieldValuesClear */
#ifdef DEBUG
    if (INKMimeHdrFieldValuesClear(NULL, tmpMimeHdrLoc, fieldLoc) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValuesClear");
    }
    if (INKMimeHdrFieldValuesClear(tmpBuf, NULL, fieldLoc) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValuesClear");
    }
    if (INKMimeHdrFieldValuesClear(tmpBuf, tmpMimeHdrLoc, NULL) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValuesClear");
    }
#endif

    /* auto: RETRIEVing the DATE field back after CLEAR should fail */
    if ((fieldLoc = INKMimeHdrFieldFind(tmpBuf, tmpMimeHdrLoc, INK_MIME_FIELD_DATE, INK_MIME_LEN_DATE))
        == INK_ERROR_PTR) {
      LOG_API_ERROR("INKMimeHdrFieldFind");
    } else {
      if (INKMimeHdrFieldValueDateGet(tmpBuf, tmpMimeHdrLoc, fieldLoc, &tmpDate2) != INK_ERROR) {
        /*LOG_AUTO_ERROR("INKMimeHdrFieldValuesClear", "Can STILL retrieve DATE value after INK_CLEAR"); */
        if (tmpDate1 == tmpDate2) {
          LOG_AUTO_ERROR("INKMimeHdrFieldValuesClear", "DATE value STILL the same after INK_CLEAR");
        }
      }
    }

    if ((fieldLoc = INKMimeHdrFieldFind(tmpBuf, tmpMimeHdrLoc, INK_MIME_FIELD_CONTENT_TYPE, INK_MIME_LEN_CONTENT_TYPE))
        == INK_ERROR_PTR) {
      LOG_API_ERROR("INKMimeHdrFieldFind");
    } else if (fieldLoc) {
      if (INKMimeHdrFieldValueStringGet(tmpBuf, tmpMimeHdrLoc, fieldLoc, -1,
                                        &tmpFieldValue1, &tmpFieldValueLength) == INK_ERROR) {
        LOG_API_ERROR("INKMimeHdrFieldValueStringGet");
      }
      if (INKMimeHdrFieldValuesClear(tmpBuf, tmpMimeHdrLoc, fieldLoc) == INK_ERROR) {
        LOG_API_ERROR("INKMimeHdrFieldValuesClear");
      }
    }
    HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, fieldLoc);

    /* auto: */
    if ((fieldLoc = INKMimeHdrFieldFind(tmpBuf, tmpMimeHdrLoc, INK_MIME_FIELD_CONTENT_TYPE, INK_MIME_LEN_CONTENT_TYPE))
        == INK_ERROR_PTR) {
      LOG_API_ERROR("INKMimeHdrFieldFind");
    } else if (fieldLoc) {
      if (INKMimeHdrFieldValueStringGet(tmpBuf, tmpMimeHdrLoc, fieldLoc, -1,
                                        &tmpFieldValue2, &tmpFieldValueLength) != INK_ERROR) {
        /*LOG_AUTO_ERROR("INKMimeHdrFieldValuesClear", "Can STILL retrieve CONTENT_TYPE value after INK_CLEAR"); */
        if (strcmp(tmpFieldValue2, "\0") && !strncmp(tmpFieldValue1, tmpFieldValue2, tmpFieldValueLength)) {
          LOG_AUTO_ERROR("INKMimeHdrFieldValuesClear", "CONTENT_TYPE value STILL same after INK_CLEAR");
        }
      }
    }
    printMimeFields(tmpBuf, tmpMimeHdrLoc, RESP, 8);

    /* clean-up */
    STR_RELEASE(tmpBuf, tmpMimeHdrLoc, tmpFieldValue1);
    STR_RELEASE(tmpBuf, tmpMimeHdrLoc, tmpFieldValue2);
    HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, fieldLoc);

                /******* (9): Desotroy ALL the MIME fields using INK_Clear *******/
    if (INKMimeHdrFieldsClear(tmpBuf, tmpMimeHdrLoc) == INK_ERROR) {
      LOG_API_ERROR("INKMimeHdrFieldsClear");
    }
    HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, fieldLoc);

    /* negative test for INKMimeHdrFieldsClear */
#ifdef DEBUG
    if (INKMimeHdrFieldsClear(NULL, tmpMimeHdrLoc) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldsClear");
    }
    if (INKMimeHdrFieldsClear(tmpBuf, NULL) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldsClear");
    }
#endif

    /* auto: INK_Retrieve'ing field after INK_Clear should fail */
    if ((fieldLoc = INKMimeHdrFieldFind(tmpBuf, tmpMimeHdrLoc, INK_MIME_FIELD_AGE, INK_MIME_LEN_AGE)) != INK_ERROR_PTR && fieldLoc) {
      LOG_AUTO_ERROR("INKMimeHdrFieldsClear", "Can STILL retrieve AGE fieldLoc afer INK_FieldsClear");
      if (INKMimeHdrFieldValueStringGet(tmpBuf, tmpMimeHdrLoc, fieldLoc, -1, &tmpFieldValueString,
                                        &tmpFieldValueLength) != INK_ERROR) {
        LOG_AUTO_ERROR("INKMimeHdrFieldsClear", "Can STILL retrieve AGE fieldValue afer INK_FieldsClear");
        STR_RELEASE(tmpBuf, tmpMimeHdrLoc, tmpFieldValueString);
      }
    }

    printMimeFields(tmpBuf, tmpMimeHdrLoc, RESP, 9);

    /* clean-up */
    HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, fieldLoc);

    /* Final clean-up */
    HANDLE_RELEASE(tmpBuf, INK_NULL_MLOC, tmpMimeHdrLoc);
    BUFFER_DESTROY(tmpBuf);

    /* added by ymou */
                /***** (10): create a new mime header and play with INKMimeHdrFieldValue[Insert|Get]Date *****/
    /* create a new mime header */
    tmpBuf = INKMBufferCreate();
    tmpMimeHdrLoc = INKMimeHdrCreate(tmpBuf);

    /* create a new field */
    fieldLoc1 = INKMimeHdrFieldCreate(tmpBuf, tmpMimeHdrLoc);
    INKMimeHdrFieldAppend(tmpBuf, tmpMimeHdrLoc, fieldLoc1);
    INKMimeHdrFieldNameSet(tmpBuf, tmpMimeHdrLoc, fieldLoc1, MY_TEST_HDR_1, strlen(MY_TEST_HDR_1));

    /* insert (append) a Date value into the new field */
    currentTime = time(&currentTime);
    if (INKMimeHdrFieldValueDateInsert(tmpBuf, tmpMimeHdrLoc, fieldLoc1, currentTime) != INK_SUCCESS) {
      LOG_API_ERROR("INKMimeHdrFieldValueDateInsert");
    }

    /* negative test for INKMimeHdrFieldValueDateInsert */
#ifdef DEBUG
    if (INKMimeHdrFieldValueDateInsert(NULL, tmpMimeHdrLoc, fieldLoc1, currentTime) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueDateInsert");
    }
    if (INKMimeHdrFieldValueDateInsert(tmpBuf, NULL, fieldLoc1, currentTime) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueDateInsert");
    }
    if (INKMimeHdrFieldValueDateInsert(tmpBuf, tmpMimeHdrLoc, NULL, currentTime) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueDateInsert");
    }
#endif

    /* get the field value and print it out */
    printHeader(tmpBuf, tmpMimeHdrLoc);
    /* auto */
    printDateDifference(tmpBuf, tmpMimeHdrLoc, MY_TEST_HDR_1, currentTime, RESP, 10);

                /***** (11): create a new mime field and play with INKMimeHdrFieldValue[Insert|Set|Get]* *****/
    /* create the second new field */
    if ((fieldLoc2 = INKMimeHdrFieldCreate(tmpBuf, tmpMimeHdrLoc)) == INK_ERROR_PTR) {
      LOG_API_ERROR("INKMimeHdrFieldCreate");
    }
    if (INKMimeHdrFieldAppend(tmpBuf, tmpMimeHdrLoc, fieldLoc2) != INK_SUCCESS) {
      LOG_API_ERROR("INKMimeHdrFieldAppend");
    }
    if (INKMimeHdrFieldNameSet(tmpBuf, tmpMimeHdrLoc, fieldLoc2, MY_TEST_HDR_2, strlen(MY_TEST_HDR_2)) != INK_SUCCESS) {
      LOG_API_ERROR("INKMimeHdrFieldNameSet");
    }

    /* insert values into the new field */
    valueInt = -1;
    valueUint = 2;
    if (INKMimeHdrFieldValueIntInsert(tmpBuf, tmpMimeHdrLoc, fieldLoc2, -1, valueInt) != INK_SUCCESS) {
      LOG_API_ERROR("INKMimeHdrFieldValueIntInsert");
    }
    /* auto: retrive the newly inserted (last) Int value and check */
    idx = INKMimeHdrFieldValuesCount(tmpBuf, tmpMimeHdrLoc, fieldLoc2) - 1;
    int tmp_int;

    INKMimeHdrFieldValueIntGet(tmpBuf, tmpMimeHdrLoc, fieldLoc2, idx, &tmp_int)
    if (tmp_int != valueInt) {
      LOG_AUTO_ERROR("INKMimeHdrFieldValueIntInsert",
                     "INKMimeHdrFieldValueIntGet different from INKMimeHdrFieldValueIntInsert");
    }

    if (INKMimeHdrFieldValueIntSet(tmpBuf, tmpMimeHdrLoc, fieldLoc2, 0, 10) != INK_SUCCESS) {
      LOG_API_ERROR("INKMimeHdrFieldValueIntSet");
    }
    idx = INKMimeHdrFieldValuesCount(tmpBuf, tmpMimeHdrLoc, fieldLoc2) - 1;
    INKMimeHdrFieldValueIntGet(tmpBuf, tmpMimeHdrLoc, fieldLoc2, idx, &tmp_int);
    if (tmp_int != 10) {
      LOG_AUTO_ERROR("INKMimeHdrFieldValueIntSet",
                     "INKMimeHdrFieldValueIntGet different from INKMimeHdrFieldValueIntInsert");
    }

    if (INKMimeHdrFieldValueUintInsert(tmpBuf, tmpMimeHdrLoc, fieldLoc2, -1, valueUint) != INK_SUCCESS) {
      LOG_API_ERROR("INKMimeHdrFieldValueUintInsert");
    }

    /* auto: retrive the newly inserted (last) Uint value and check */
    idx = INKMimeHdrFieldValuesCount(tmpBuf, tmpMimeHdrLoc, fieldLoc2) - 1;

    unsigned int tmp_uint;

    INKMimeHdrFieldValueUintGet(tmpBuf, tmpMimeHdrLoc, fieldLoc2, idx, &tmp_uint)
    if (tmp_uint != valueUint) {
      LOG_AUTO_ERROR("INKMimeHdrFieldValueUintInsert",
                     "INKMimeHdrFieldValueUintGet different from INKMimeHdrFieldValueUintInsert");
    }
    printField(tmpBuf, tmpMimeHdrLoc, MY_TEST_HDR_2, RESP, 11);

    /* negative test for INKMimeHdrFieldValue[Int|Uint]Insert */
#ifdef DEBUG
    if (INKMimeHdrFieldValueIntInsert(NULL, tmpMimeHdrLoc, fieldLoc2, valueInt, -1) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueIntInsert");
    }
    if (INKMimeHdrFieldValueIntInsert(tmpBuf, NULL, fieldLoc2, valueInt, -1) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueIntInsert");
    }
    if (INKMimeHdrFieldValueIntInsert(tmpBuf, tmpMimeHdrLoc, NULL, valueInt, -1) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueIntInsert");
    }

    if (INKMimeHdrFieldValueUintInsert(NULL, tmpMimeHdrLoc, fieldLoc2, valueUint, -1) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueUintInsert");
    }
    if (INKMimeHdrFieldValueUintInsert(tmpBuf, NULL, fieldLoc2, valueUint, -1) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueUintInsert");
    }
    if (INKMimeHdrFieldValueUintInsert(tmpBuf, tmpMimeHdrLoc, NULL, valueUint, -1) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueUintInsert");
    }
#endif

                /***** (12): play with INKMimeHdrFieldCopyValues *****/
    /* create the third new field */
    if ((fieldLoc3 = INKMimeHdrFieldCreate(tmpBuf, tmpMimeHdrLoc)) == INK_ERROR_PTR) {
      LOG_API_ERROR("INKMimeHdrFieldCreate");
    }
    if (INKMimeHdrFieldAppend(tmpBuf, tmpMimeHdrLoc, fieldLoc3) != INK_SUCCESS) {
      LOG_API_ERROR("INKMimeHdrFieldAppend");
    }
    if (INKMimeHdrFieldNameSet(tmpBuf, tmpMimeHdrLoc, fieldLoc3, MY_TEST_HDR_3, strlen(MY_TEST_HDR_3)) != INK_SUCCESS) {
      LOG_API_ERROR("INKMimeHdrFieldNameSet");
    }

    /* copy the values from the second header field to the third one */
    if (INKMimeHdrFieldCopyValues(tmpBuf, tmpMimeHdrLoc, fieldLoc3, tmpBuf, tmpMimeHdrLoc, fieldLoc2) != INK_SUCCESS) {
      LOG_API_ERROR("INKMimeHdrFieldCopyValues");
    }
    printField(tmpBuf, tmpMimeHdrLoc, MY_TEST_HDR_3, RESP, 12);

    /* negative test for INKMimeHdrFieldCopyValues */
#ifdef DEBUG
    if (INKMimeHdrFieldCopyValues(NULL, tmpMimeHdrLoc, fieldLoc3, tmpBuf, tmpMimeHdrLoc, fieldLoc2) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldCopyValues");
    }
    if (INKMimeHdrFieldCopyValues(tmpBuf, NULL, fieldLoc3, tmpBuf, tmpMimeHdrLoc, fieldLoc2) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldCopyValues");
    }
    if (INKMimeHdrFieldCopyValues(tmpBuf, tmpMimeHdrLoc, NULL, tmpBuf, tmpMimeHdrLoc, fieldLoc2) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldCopyValues");
    }
    if (INKMimeHdrFieldCopyValues(tmpBuf, tmpMimeHdrLoc, fieldLoc3, NULL, tmpMimeHdrLoc, fieldLoc2) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldCopyValues");
    }
    if (INKMimeHdrFieldCopyValues(tmpBuf, tmpMimeHdrLoc, fieldLoc3, tmpBuf, NULL, fieldLoc2) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldCopyValues");
    }
    if (INKMimeHdrFieldCopyValues(tmpBuf, tmpMimeHdrLoc, fieldLoc3, tmpBuf, tmpMimeHdrLoc, NULL) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldCopyValues");
    }
#endif

    /* auto: Get the field value of fieldLoc3 and compare w/ fieldLoc2 */
    /* CAUTION: using idx = -1 is an undocumented internal usage */
    INKMimeHdrFieldValueStringGet(tmpBuf, tmpMimeHdrLoc, fieldLoc2, -1, &tmpFieldValue1, &tmpFieldValueLength);
    INKMimeHdrFieldValueStringGet(tmpBuf, tmpMimeHdrLoc, fieldLoc3, -1, &tmpFieldValue2, &tmpFieldValueLength);

    if (strncmp(tmpFieldValue1, tmpFieldValue2, tmpFieldValueLength)) {
      LOG_AUTO_ERROR("INKMimeHdrFieldCopy", "New copy of field values different from original");
    }

    STR_RELEASE(tmpBuf, tmpMimeHdrLoc, tmpFieldValue1);
    STR_RELEASE(tmpBuf, tmpMimeHdrLoc, tmpFieldValue2);


        /***** (13): play with INKMimeHdrFieldValueSet* *****/
    currentTime = time(&currentTime);

    /* set other values to the field */
    valueInt = -2;
    valueUint = 1;
    if (INKMimeHdrFieldValueIntSet(tmpBuf, tmpMimeHdrLoc, fieldLoc3, 0, valueInt) != INK_SUCCESS) {
      LOG_API_ERROR("INKMimeHdrFieldValueIntSet");
    }
    if (INKMimeHdrFieldValueUintSet(tmpBuf, tmpMimeHdrLoc, fieldLoc3, 1, valueUint) != INK_SUCCESS) {
      LOG_API_ERROR("INKMimeHdrFieldValueUintSet");
    }
    if (INKMimeHdrFieldValueDateSet(tmpBuf, tmpMimeHdrLoc, fieldLoc1, currentTime) != INK_SUCCESS) {
      LOG_API_ERROR("INKMimeHdrFieldValueDateSet");
    }
    printDateDifference(tmpBuf, tmpMimeHdrLoc, MY_TEST_HDR_1, currentTime, RESP, 13);
    printField(tmpBuf, tmpMimeHdrLoc, MY_TEST_HDR_3, RESP, 13);

    /* auto: Get the field values again and check */
    if (INKMimeHdrFieldValueIntGet(tmpBuf, tmpMimeHdrLoc, fieldLoc3, 0, &retrievedInt) != INK_SUCCESS) {
      LOG_API_ERROR("INKMimeHdrFieldValueIntGet");
    }
    if (retrievedInt != valueInt) {
      LOG_AUTO_ERROR("INKMimeHdrFieldValueIntSet",
                     "INKMimeHdrFieldValueIntGet different from INKMimeHdrFieldValueIntSet");
    }
    if (INKMimeHdrFieldValueUintGet(tmpBuf, tmpMimeHdrLoc, fieldLoc3, 1, &retrievedUint) != INK_SUCCESS) {
      LOG_API_ERROR("INKMimeHdrFieldValueUintGet");
    }
    if (retrievedUint != valueUint) {
      LOG_AUTO_ERROR("INKMimeHdrFieldValueUintSet",
                     "INKMimeHdrFieldValueUintGet different from INKMimeHdrFieldValueUintSet");
    }
    if (INKMimeHdrFieldValueDateGet(tmpBuf, tmpMimeHdrLoc, fieldLoc1, &retrievedDate) != INK_SUCCESS) {
      LOG_API_ERROR("INKMimeHdrFieldValueDateGet");
    }
    if (retrievedDate != currentTime) {
      LOG_AUTO_ERROR("INKMimeHdrFieldValueDateSet",
                     "INKMimeHdrFieldValueDateGet different from INKMimeHdrFieldValueDateSet");
    }

    /* negative test */
#ifdef DEBUG
    if (INKMimeHdrFieldValueIntSet(NULL, tmpMimeHdrLoc, fieldLoc3, 0, valueInt) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueIntSet");
    }
    if (INKMimeHdrFieldValueIntSet(tmpBuf, NULL, fieldLoc3, 0, valueInt) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueIntSet");
    }
    if (INKMimeHdrFieldValueIntSet(tmpBuf, tmpMimeHdrLoc, NULL, 0, valueInt) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueIntSet");
    }

    if (INKMimeHdrFieldValueUintSet(NULL, tmpMimeHdrLoc, fieldLoc3, 1, valueUint) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueUintSet");
    }
    if (INKMimeHdrFieldValueUintSet(tmpBuf, NULL, fieldLoc3, 1, valueUint) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueUintSet");
    }
    if (INKMimeHdrFieldValueUintSet(tmpBuf, tmpMimeHdrLoc, NULL, 1, valueUint) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueUintSet");
    }

    if (INKMimeHdrFieldValueDateSet(NULL, tmpMimeHdrLoc, fieldLoc1, currentTime) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueDateSet");
    }
    if (INKMimeHdrFieldValueDateSet(tmpBuf, NULL, fieldLoc1, currentTime) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueDateSet");
    }
    if (INKMimeHdrFieldValueDateSet(tmpBuf, tmpMimeHdrLoc, NULL, currentTime) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueDateSet");
    }

    if (INKMimeHdrFieldValueIntGet(NULL, tmpMimeHdrLoc, fieldLoc3, 0, &retrievedInt) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueIntGet");
    }
    if (INKMimeHdrFieldValueIntGet(tmpBuf, NULL, fieldLoc3, 0, &retrievedInt) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueIntGet");
    }
    if (INKMimeHdrFieldValueIntGet(tmpBuf, tmpMimeHdrLoc, NULL, 0, &retrievedInt) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueIntGet");
    }

    if (INKMimeHdrFieldValueUintGet(NULL, tmpMimeHdrLoc, fieldLoc3, 1, &retrievedUint) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueUintGet");
    }
    if (INKMimeHdrFieldValueUintGet(tmpBuf, NULL, fieldLoc3, 1, &retrievedUint) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueUintGet");
    }
    if (INKMimeHdrFieldValueUintGet(tmpBuf, tmpMimeHdrLoc, NULL, 1, &retrievedUint) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueUintGet");
    }

    if (INKMimeHdrFieldValueDateGet(NULL, tmpMimeHdrLoc, fieldLoc1, &retrievedDate) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueDateGet");
    }
    if (INKMimeHdrFieldValueDateGet(tmpBuf, NULL, fieldLoc1, &retrievedDate) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueDateGet");
    }
    if (INKMimeHdrFieldValueDateGet(tmpBuf, tmpMimeHdrLoc, NULL, &retrievedDate) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueDateGet");
    }
#endif

                /***** (14): play with INKMimeHdrFieldValueDelete *****/
    /* delete a field value */
    count = INKMimeHdrFieldValuesCount(tmpBuf, tmpMimeHdrLoc, fieldLoc3);

    INKMimeHdrFieldValueDelete(tmpBuf, tmpMimeHdrLoc, fieldLoc3, 1);
    printField(tmpBuf, tmpMimeHdrLoc, MY_TEST_HDR_3, RESP, 14);

    /* auto: try retrieving the deleted value now */
    if (INKMimeHdrFieldValuesCount(tmpBuf, tmpMimeHdrLoc, fieldLoc3) == count) {
      LOG_AUTO_ERROR("INKMimeHdrFieldValueDelete", "Field value count still the same after delete");
    }

    /* negative test for INKMimeHdrFieldValuesCount and INKMimeHdrFieldValueDelete */
#ifdef DEBUG
    if (INKMimeHdrFieldValuesCount(NULL, tmpMimeHdrLoc, fieldLoc3) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValuesCount");
    }
    if (INKMimeHdrFieldValuesCount(tmpBuf, NULL, fieldLoc3) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValuesCount");
    }
    if (INKMimeHdrFieldValuesCount(tmpBuf, tmpMimeHdrLoc, NULL) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValuesCount");
    }

    if (INKMimeHdrFieldValueDelete(NULL, tmpMimeHdrLoc, fieldLoc3, 1) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueDelete");
    }
    if (INKMimeHdrFieldValueDelete(tmpBuf, NULL, fieldLoc3, 1) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueDelete");
    }
    if (INKMimeHdrFieldValueDelete(tmpBuf, tmpMimeHdrLoc, NULL, 1) != INK_ERROR) {
      LOG_ERROR_NEG("INKMimeHdrFieldValueDelete");
    }
#endif

  done:
    /* Final cleanup */
    HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, fieldLoc1);
    HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, fieldLoc2);
    HANDLE_RELEASE(tmpBuf, tmpMimeHdrLoc, fieldLoc3);
    HANDLE_RELEASE(tmpBuf, INK_NULL_MLOC, tmpMimeHdrLoc);
    BUFFER_DESTROY(tmpBuf);
  }
  /* httpType == INK_HTTP_TYPE_RESPONSE */
}                               /* sectionMimeHdr */


/****************************************************************************
 * handleReadRequest:
 *
 * Description: handler for INK_HTTP_READ_REQUEST_HDR_HOOK
 ****************************************************************************/

static void
handleReadRequest(INKCont pCont, INKHttpTxn pTxn)
{
  LOG_SET_FUNCTION_NAME("handleReadRequest");

  INKMBuffer reqHdrBuf = NULL;
  INKMLoc reqHttpHdrLoc = NULL;

  INKDebug(REQ, ">>>>>> handleReadRequest <<<<<<<");

  /* Get Request Marshall Buffer */
  if (!INKHttpTxnClientReqGet(pTxn, &reqHdrBuf, &reqHttpHdrLoc)) {
    LOG_AUTO_ERROR("INKHttpTxnClientReqGet", "ERROR: Can't retrieve client req hdr")
      goto done;
  }

  /* Do the Mime stuff now */
  sectionMimeHdr(reqHdrBuf, reqHttpHdrLoc);

done:
  HANDLE_RELEASE(reqHdrBuf, INK_NULL_MLOC, reqHttpHdrLoc);

  if (INKHttpTxnReenable(pTxn, INK_EVENT_HTTP_CONTINUE) == INK_ERROR) {
    LOG_API_ERROR("INKHttpTxnReenable");
  }

}


/************************************************************************
 * handleSendResponse:
 *
 * Description:	handler for INK_HTTP_SEND_RESPONSE_HOOK
 ************************************************************************/

static void
handleSendResponse(INKCont pCont, INKHttpTxn pTxn)
{
  LOG_SET_FUNCTION_NAME("handleSendResponse");

  INKMBuffer respHdrBuf = NULL;
  INKMLoc respHttpHdrLoc = NULL;

  INKDebug(RESP, "\n>>> handleSendResponse <<<<");

  /* Get Response Marshall Buffer */
  if (!INKHttpTxnClientRespGet(pTxn, &respHdrBuf, &respHttpHdrLoc)) {
    LOG_AUTO_ERROR("INKHttpTxnClientRespGet", "ERROR: Can't retrieve client resp hdr")
      goto done;
  }

  /* Do the Mime stuff now */
  sectionMimeHdr(respHdrBuf, respHttpHdrLoc);

done:
  HANDLE_RELEASE(respHdrBuf, INK_NULL_MLOC, respHttpHdrLoc);

  if (INKHttpTxnReenable(pTxn, INK_EVENT_HTTP_CONTINUE) == INK_ERROR) {
    LOG_API_ERROR("INKHttpTxnReenable");
  }
}




static void
handleTxnStart(INKCont pCont, INKHttpTxn pTxn)
{
  LOG_SET_FUNCTION_NAME("handleTxnStart");

  if (INKHttpTxnHookAdd(pTxn, INK_HTTP_READ_REQUEST_HDR_HOOK, pCont) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHookAdd");
  }
  if (INKHttpTxnHookAdd(pTxn, INK_HTTP_SEND_RESPONSE_HDR_HOOK, pCont) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHookAdd");
  }

  if (INKHttpTxnReenable(pTxn, INK_EVENT_HTTP_CONTINUE) == INK_ERROR) {
    LOG_API_ERROR("INKHttpTxnReenable");
  }
}


static int
contHandler(INKCont pCont, INKEvent event, void *edata)
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
  LOG_SET_FUNCTION_NAME("INKPluginInit");

  INKCont pCont;

  if ((pCont = INKContCreate(contHandler, NULL)) == INK_ERROR_PTR) {
    LOG_API_ERROR("INKContCreate");
  } else if (INKHttpHookAdd(INK_HTTP_TXN_START_HOOK, pCont) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHookAdd");
  }
}
