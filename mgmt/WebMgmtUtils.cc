/** @file

  Functions for interfacing to management records

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

#include "tscore/ink_string.h"
#include "tscore/Tokenizer.h"
#include "tscore/ink_file.h"
#include "MgmtUtils.h"
#include "WebMgmtUtils.h"
#include "tscore/Regex.h"

// bool varSetFloat(const char* varName, RecFloat value)
//
//  Sets the variable specified by varName to value.  varName
//   must be a RecFloat variable.  No conversion is done for
//   other types unless convert is set to true. In the case
//   of convert is true, type conversion is perform if applicable.
//   By default, convert is set to be false and can be overridden
//   when the function is called.
//
bool
varSetFloat(const char *varName, RecFloat value, bool convert)
{
  RecDataT varDataType = RECD_NULL;
  bool found           = true;
  int err              = REC_ERR_FAIL;

  err = RecGetRecordDataType(const_cast<char *>(varName), &varDataType);
  if (err != REC_ERR_OKAY) {
    return found;
  }

  switch (varDataType) {
  case RECD_FLOAT:
    RecSetRecordFloat(const_cast<char *>(varName), value, REC_SOURCE_EXPLICIT);
    break;

  case RECD_INT:
    if (convert) {
      value += 0.5; // rounding up
      RecSetRecordInt(const_cast<char *>(varName), static_cast<RecInt>(value), REC_SOURCE_EXPLICIT);
      break;
    }
    // fallthrough

  case RECD_COUNTER:
    if (convert) {
      RecSetRecordCounter(const_cast<char *>(varName), static_cast<RecCounter>(value), REC_SOURCE_EXPLICIT);
      break;
    }
    // fallthrough

  case RECD_STRING:
  case RECD_NULL:
  default:
    found = false;
    break;
  }

  return found;
}

// bool varSetCounter(const char* varName, RecCounter value)
//
//  Sets the variable specified by varName to value.  varName
//   must be an RecCounter variable.  No conversion is done for
//   other types unless convert is set to true. In the case
//   of convert is true, type conversion is perform if applicable.
//   By default, convert is set to be false and can be overridden
//   when the function is called.
//
bool
varSetCounter(const char *varName, RecCounter value, bool convert)
{
  RecDataT varDataType = RECD_NULL;
  bool found           = true;
  int err              = REC_ERR_FAIL;

  err = RecGetRecordDataType(const_cast<char *>(varName), &varDataType);
  if (err != REC_ERR_OKAY) {
    return found;
  }

  switch (varDataType) {
  case RECD_COUNTER:
    RecSetRecordCounter(const_cast<char *>(varName), value, REC_SOURCE_EXPLICIT);
    break;

  case RECD_INT:
    if (convert) {
      RecSetRecordInt(const_cast<char *>(varName), static_cast<RecInt>(value), REC_SOURCE_EXPLICIT);
      break;
    }
    // fallthrough

  case RECD_FLOAT:
    if (convert) {
      RecSetRecordFloat(const_cast<char *>(varName), static_cast<RecFloat>(value), REC_SOURCE_EXPLICIT);
      break;
    }
    // fallthrough

  case RECD_STRING:
  case RECD_NULL:
  default:
    found = false;
    break;
  }

  return found;
}

// bool varSetInt(const char* varName, RecInt value)
//
//  Sets the variable specified by varName to value.  varName
//   must be an RecInt variable.  No conversion is done for
//   other types unless convert is set to true. In the case
//   of convert is true, type conversion is perform if applicable.
//   By default, convert is set to be false and can be overridden
//   when the function is called.
//
bool
varSetInt(const char *varName, RecInt value, bool convert)
{
  RecDataT varDataType = RECD_NULL;
  bool found           = true;
  int err              = REC_ERR_FAIL;

  err = RecGetRecordDataType(const_cast<char *>(varName), &varDataType);
  if (err != REC_ERR_OKAY) {
    return found;
  }

  switch (varDataType) {
  case RECD_INT:
    RecSetRecordInt(const_cast<char *>(varName), value, REC_SOURCE_EXPLICIT);
    break;

  case RECD_COUNTER:
    if (convert) {
      RecSetRecordCounter(const_cast<char *>(varName), static_cast<RecCounter>(value), REC_SOURCE_EXPLICIT);
      break;
    }
    // fallthrough

  case RECD_FLOAT:
    if (convert) {
      RecSetRecordFloat(const_cast<char *>(varName), static_cast<RecFloat>(value), REC_SOURCE_EXPLICIT);
      break;
    }
    // fallthrough

  case RECD_STRING:
  case RECD_NULL:
  default:
    found = false;
    break;
  }

  return found;
}

// bool varSetData(RecDataT varType, const char *varName, RecData value)
//
//  Sets the variable specified by varName to value. value and varName
//   must be varType variables.
//
bool
varSetData(RecDataT varType, const char *varName, RecData value)
{
  RecErrT err = REC_ERR_FAIL;

  switch (varType) {
  case RECD_INT:
    err = RecSetRecordInt(const_cast<char *>(varName), value.rec_int, REC_SOURCE_EXPLICIT);
    break;
  case RECD_COUNTER:
    err = RecSetRecordCounter(const_cast<char *>(varName), value.rec_counter, REC_SOURCE_EXPLICIT);
    break;
  case RECD_FLOAT:
    err = RecSetRecordFloat(const_cast<char *>(varName), value.rec_float, REC_SOURCE_EXPLICIT);
    break;
  default:
    Fatal("unsupported type:%d\n", varType);
  }
  return (err == REC_ERR_OKAY);
}

// bool varDataFromName(RecDataT varType, const char *varName, RecData *value)
//
//   Sets the *value to value of the varName according varType.
//
//  return true if bufVal was successfully set
//    and false otherwise
//
bool
varDataFromName(RecDataT varType, const char *varName, RecData *value)
{
  int err;

  err = RecGetRecord_Xmalloc(varName, varType, value, true);

  return (err == REC_ERR_OKAY);
}

// void percentStrFromFloat(MgmtFloat, char* bufVal)
//
//  Converts a float to a percent string
void
percentStrFromFloat(RecFloat val, char *bufVal, int bufLen)
{
  int percent = static_cast<int>((val * 100.0) + 0.5);
  snprintf(bufVal, bufLen, "%d%%", percent);
}

// void commaStrFromInt(RecInt bytes, char* bufVal)
//   Converts an Int to string with commas in it
void
commaStrFromInt(RecInt bytes, char *bufVal, int bufLen)
{
  char *curPtr;

  int len = snprintf(bufVal, bufLen, "%" PRId64 "", bytes);

  // The string is too short to need commas
  if (len < 4) {
    return;
  }

  int numCommas = (len - 1) / 3;
  ink_release_assert(bufLen > numCommas + len);
  curPtr  = bufVal + (len + numCommas);
  *curPtr = '\0';
  curPtr--;

  for (int i = 0; i < len; i++) {
    *curPtr = bufVal[len - 1 - i];

    if ((i + 1) % 3 == 0 && curPtr != bufVal) {
      curPtr--;
      *curPtr = ',';
    }
    curPtr--;
  }

  ink_assert(curPtr + 1 == bufVal);
}

// void MbytesFromInt(RecInt bytes, char* bufVal)
//     Converts into a string in units of megabytes No unit specification is added
void
MbytesFromInt(RecInt bytes, char *bufVal, int bufLen)
{
  RecInt mBytes = bytes / 1048576;

  snprintf(bufVal, bufLen, "%" PRId64 "", mBytes);
}

// void bytesFromInt(RecInt bytes, char* bufVal)
//
//    Converts mgmt into a string with one of
//       GB, MB, KB, B units
//
//     bufVal must point to adequate space a la sprintf
void
bytesFromInt(RecInt bytes, char *bufVal, int bufLen)
{
  const int64_t gb  = 1073741824;
  const long int mb = 1048576;
  const long int kb = 1024;
  double unitBytes;

  if (bytes >= gb) {
    unitBytes = bytes / static_cast<double>(gb);
    snprintf(bufVal, bufLen, "%.1f GB", unitBytes);
  } else {
    // Reduce the precision of the bytes parameter
    //   because we know that it less than 1GB which
    //   has plenty of precision for a regular int
    //   and saves from 64 bit arithmetic which may
    //   be expensive on some processors
    int bytesP = static_cast<int>(bytes);
    if (bytesP >= mb) {
      unitBytes = bytes / static_cast<double>(mb);
      snprintf(bufVal, bufLen, "%.1f MB", unitBytes);
    } else if (bytesP >= kb) {
      unitBytes = bytes / static_cast<double>(kb);
      snprintf(bufVal, bufLen, "%.1f KB", unitBytes);
    } else {
      snprintf(bufVal, bufLen, "%d", bytesP);
    }
  }
}

//
// Removes any cr/lf line breaks from the text data
//
int
convertHtmlToUnix(char *buffer)
{
  char *read  = buffer;
  char *write = buffer;
  int numSub  = 0;

  while (*read != '\0') {
    if (*read == '\015') {
      *write = ' ';
      read++;
      write++;
      numSub++;
    } else {
      *write = *read;
      write++;
      read++;
    }
  }
  *write = '\0';
  return numSub;
}

//  Substitutes HTTP unsafe character representations
//   with their actual values.  Modifies the passed
//   in string
//
int
substituteUnsafeChars(char *buffer)
{
  char *read  = buffer;
  char *write = buffer;
  char subStr[3];
  long charVal;
  int numSub = 0;

  subStr[2] = '\0';
  while (*read != '\0') {
    if (*read == '%') {
      subStr[0] = *(++read);
      subStr[1] = *(++read);
      charVal   = strtol(subStr, (char **)nullptr, 16);
      *write    = static_cast<char>(charVal);
      read++;
      write++;
      numSub++;
    } else if (*read == '+') {
      *write = ' ';
      write++;
      read++;
    } else {
      *write = *read;
      write++;
      read++;
    }
  }
  *write = '\0';
  return numSub;
}

// Substitutes for characters that can be misconstrued
//   as part of an HTML tag
// Allocates a new string which the
//   the CALLEE MUST DELETE
//
char *
substituteForHTMLChars(const char *buffer)
{
  char *safeBuf;                          // the return "safe" character buffer
  char *safeCurrent;                      // where we are in the return buffer
  const char *inCurrent = buffer;         // where we are in the original buffer
  int inLength          = strlen(buffer); // how long the orig buffer in

  // Maximum character expansion is one to three
  unsigned int bufferToAllocate = (inLength * 5) + 1;
  safeBuf                       = new char[bufferToAllocate];
  safeCurrent                   = safeBuf;

  while (*inCurrent != '\0') {
    switch (*inCurrent) {
    case '"':
      ink_strlcpy(safeCurrent, "&quot;", bufferToAllocate);
      safeCurrent += 6;
      break;
    case '<':
      ink_strlcpy(safeCurrent, "&lt;", bufferToAllocate);
      safeCurrent += 4;
      break;
    case '>':
      ink_strlcpy(safeCurrent, "&gt;", bufferToAllocate);
      safeCurrent += 4;
      break;
    case '&':
      ink_strlcpy(safeCurrent, "&amp;", bufferToAllocate);
      safeCurrent += 5;
      break;
    default:
      *safeCurrent = *inCurrent;
      safeCurrent  += 1;
      break;
    }

    inCurrent++;
  }
  *safeCurrent = '\0';
  return safeBuf;
}
