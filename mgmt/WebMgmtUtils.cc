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

#include "ts/ink_platform.h"
#include "ts/ink_string.h"
#include "ts/Tokenizer.h"
#include "ts/ink_code.h"
#include "ts/ink_file.h"
#include "LocalManager.h"
#include "MgmtUtils.h"
#include "WebMgmtUtils.h"
#include "ts/Regex.h"

/****************************************************************************
 *
 *  WebMgmtUtils.cc - Functions for interfacing to management records
 *
 *
 *
 ****************************************************************************/

// bool varSetFromStr(const char*, const char* )
//
// Sets the named local manager variable from the value string
// passed in.  Does the appropriate type conversion on
// value string to get it to the type of the local manager
// variable
//
//  returns true if the variable was successfully set
//   and false otherwise
//
bool
varSetFromStr(const char *varName, const char *value)
{
  RecDataT varDataType = RECD_NULL;
  bool found           = true;
  int err              = REC_ERR_FAIL;
  RecData data;

  memset(&data, 0, sizeof(RecData));

  err = RecGetRecordDataType((char *)varName, &varDataType);
  if (err != REC_ERR_OKAY) {
    return found;
  }
  // Use any empty string if we get a NULL so
  //  sprintf does puke.  However, we need to
  //  switch this back to NULL for STRING types
  if (value == nullptr) {
    value = "";
  }

  switch (varDataType) {
  case RECD_INT:
    if (sscanf(value, "%" PRId64 "", &data.rec_int) == 1) {
      RecSetRecordInt((char *)varName, data.rec_int, REC_SOURCE_EXPLICIT);
    } else {
      found = false;
    }
    break;
  case RECD_COUNTER:
    if (sscanf(value, "%" PRId64 "", &data.rec_counter) == 1) {
      RecSetRecordCounter((char *)varName, data.rec_counter, REC_SOURCE_EXPLICIT);
    } else {
      found = false;
    }
    break;
  case RECD_FLOAT:
    // coverity[secure_coding]
    if (sscanf(value, "%f", &data.rec_float) == 1) {
      RecSetRecordFloat((char *)varName, data.rec_float, REC_SOURCE_EXPLICIT);
    } else {
      found = false;
    }
    break;
  case RECD_STRING:
    if (*value == '\0') {
      RecSetRecordString((char *)varName, nullptr, REC_SOURCE_EXPLICIT);
    } else {
      RecSetRecordString((char *)varName, (char *)value, REC_SOURCE_EXPLICIT);
    }
    break;
  case RECD_NULL:
  default:
    found = false;
    break;
  }

  return found;
}

// bool varSetFloat(const char* varName, RecFloat value)
//
//  Sets the variable specifed by varName to value.  varName
//   must be a RecFloat variable.  No conversion is done for
//   other types unless convert is set to ture. In the case
//   of convert is ture, type conversion is perform if applicable.
//   By default, convert is set to be false and can be overrided
//   when the function is called.
//
bool
varSetFloat(const char *varName, RecFloat value, bool convert)
{
  RecDataT varDataType = RECD_NULL;
  bool found           = true;
  int err              = REC_ERR_FAIL;

  err = RecGetRecordDataType((char *)varName, &varDataType);
  if (err != REC_ERR_OKAY) {
    return found;
  }

  switch (varDataType) {
  case RECD_FLOAT:
    RecSetRecordFloat((char *)varName, (RecFloat)value, REC_SOURCE_EXPLICIT);
    break;

  case RECD_INT:
    if (convert) {
      value += 0.5; // rounding up
      RecSetRecordInt((char *)varName, (RecInt)value, REC_SOURCE_EXPLICIT);
      break;
    }
    // fallthrough

  case RECD_COUNTER:
    if (convert) {
      RecSetRecordCounter((char *)varName, (RecCounter)value, REC_SOURCE_EXPLICIT);
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
//  Sets the variable specifed by varName to value.  varName
//   must be an RecCounter variable.  No conversion is done for
//   other types unless convert is set to ture. In the case
//   of convert is ture, type conversion is perform if applicable.
//   By default, convert is set to be false and can be overrided
//   when the function is called.
//
bool
varSetCounter(const char *varName, RecCounter value, bool convert)
{
  RecDataT varDataType = RECD_NULL;
  bool found           = true;
  int err              = REC_ERR_FAIL;

  err = RecGetRecordDataType((char *)varName, &varDataType);
  if (err != REC_ERR_OKAY) {
    return found;
  }

  switch (varDataType) {
  case RECD_COUNTER:
    RecSetRecordCounter((char *)varName, (RecCounter)value, REC_SOURCE_EXPLICIT);
    break;

  case RECD_INT:
    if (convert) {
      RecSetRecordInt((char *)varName, (RecInt)value, REC_SOURCE_EXPLICIT);
      break;
    }
    // fallthrough

  case RECD_FLOAT:
    if (convert) {
      RecSetRecordFloat((char *)varName, (RecFloat)value, REC_SOURCE_EXPLICIT);
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
//  Sets the variable specifed by varName to value.  varName
//   must be an RecInt variable.  No conversion is done for
//   other types unless convert is set to ture. In the case
//   of convert is ture, type conversion is perform if applicable.
//   By default, convert is set to be false and can be overrided
//   when the function is called.
//
bool
varSetInt(const char *varName, RecInt value, bool convert)
{
  RecDataT varDataType = RECD_NULL;
  bool found           = true;
  int err              = REC_ERR_FAIL;

  err = RecGetRecordDataType((char *)varName, &varDataType);
  if (err != REC_ERR_OKAY) {
    return found;
  }

  switch (varDataType) {
  case RECD_INT:
    RecSetRecordInt((char *)varName, (RecInt)value, REC_SOURCE_EXPLICIT);
    break;

  case RECD_COUNTER:
    if (convert) {
      RecSetRecordCounter((char *)varName, (RecCounter)value, REC_SOURCE_EXPLICIT);
      break;
    }
    // fallthrough

  case RECD_FLOAT:
    if (convert) {
      RecSetRecordFloat((char *)varName, (RecFloat)value, REC_SOURCE_EXPLICIT);
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
//  Sets the variable specifed by varName to value. value and varName
//   must be varType variables.
//
bool
varSetData(RecDataT varType, const char *varName, RecData value)
{
  RecErrT err = REC_ERR_FAIL;

  switch (varType) {
  case RECD_INT:
    err = RecSetRecordInt((char *)varName, value.rec_int, REC_SOURCE_EXPLICIT);
    break;
  case RECD_COUNTER:
    err = RecSetRecordCounter((char *)varName, value.rec_counter, REC_SOURCE_EXPLICIT);
    break;
  case RECD_FLOAT:
    err = RecSetRecordFloat((char *)varName, value.rec_float, REC_SOURCE_EXPLICIT);
    break;
  default:
    Fatal("unsupport type:%d\n", varType);
  }
  return (err == REC_ERR_OKAY);
}

// bool varDataFromName(RecDataT varType, const char *varName, RecData *value)
//
//   Sets the *value to value of the varName according varType.
//
//  return true if bufVal was succefully set
//    and false otherwise
//
bool
varDataFromName(RecDataT varType, const char *varName, RecData *value)
{
  int err;

  err = RecGetRecord_Xmalloc(varName, varType, value, true);

  return (err == REC_ERR_OKAY);
}

// bool varCounterFromName (const char*, RecFloat* )
//
//   Sets the *value to value of the varName.
//
//  return true if bufVal was succefully set
//    and false otherwise
//
bool
varCounterFromName(const char *varName, RecCounter *value)
{
  RecDataT varDataType = RECD_NULL;
  bool found           = true;
  int err              = REC_ERR_FAIL;

  err = RecGetRecordDataType((char *)varName, &varDataType);

  if (err == REC_ERR_FAIL) {
    return false;
  }

  switch (varDataType) {
  case RECD_INT: {
    RecInt tempInt = 0;
    RecGetRecordInt((char *)varName, &tempInt);
    *value = (RecCounter)tempInt;
    break;
  }
  case RECD_COUNTER: {
    *value = 0;
    RecGetRecordCounter((char *)varName, value);
    break;
  }
  case RECD_FLOAT: {
    RecFloat tempFloat = 0.0;
    RecGetRecordFloat((char *)varName, &tempFloat);
    *value = (RecCounter)tempFloat;
    break;
  }
  case RECD_STRING:
  case RECD_NULL:
  default:
    *value = -1;
    found  = false;
    break;
  }

  return found;
}

// bool varFloatFromName (const char*, RecFloat* )
//
//   Sets the *value to value of the varName.
//
//  return true if bufVal was succefully set
//    and false otherwise
//
bool
varFloatFromName(const char *varName, RecFloat *value)
{
  RecDataT varDataType = RECD_NULL;
  bool found           = true;

  int err = REC_ERR_FAIL;

  err = RecGetRecordDataType((char *)varName, &varDataType);

  if (err == REC_ERR_FAIL) {
    return false;
  }

  switch (varDataType) {
  case RECD_INT: {
    RecInt tempInt = 0;
    RecGetRecordInt((char *)varName, &tempInt);
    *value = (RecFloat)tempInt;
    break;
  }
  case RECD_COUNTER: {
    RecCounter tempCounter = 0;
    RecGetRecordCounter((char *)varName, &tempCounter);
    *value = (RecFloat)tempCounter;
    break;
  }
  case RECD_FLOAT: {
    *value = 0.0;
    RecGetRecordFloat((char *)varName, value);
    break;
  }
  case RECD_STRING:
  case RECD_NULL:
  default:
    *value = -1.0;
    found  = false;
    break;
  }

  return found;
}

// bool varIntFromName (const char*, RecInt* )
//
//   Sets the *value to value of the varName.
//
//  return true if bufVal was succefully set
//    and false otherwise
//
bool
varIntFromName(const char *varName, RecInt *value)
{
  RecDataT varDataType = RECD_NULL;
  bool found           = true;
  int err              = REC_ERR_FAIL;

  err = RecGetRecordDataType((char *)varName, &varDataType);

  if (err != REC_ERR_OKAY) {
    return false;
  }

  switch (varDataType) {
  case RECD_INT: {
    *value = 0;
    RecGetRecordInt((char *)varName, value);
    break;
  }
  case RECD_COUNTER: {
    RecCounter tempCounter = 0;
    RecGetRecordCounter((char *)varName, &tempCounter);
    *value = (RecInt)tempCounter;
    break;
  }
  case RECD_FLOAT: {
    RecFloat tempFloat = 0.0;
    RecGetRecordFloat((char *)varName, &tempFloat);
    *value = (RecInt)tempFloat;
    break;
  }
  case RECD_STRING:
  case RECD_NULL:
  default:
    *value = -1;
    found  = false;
    break;
  }

  return found;
}

// void percentStrFromFloat(MgmtFloat, char* bufVal)
//
//  Converts a float to a percent string
//
//     bufVal must point to adequate space a la sprintf
//
void
percentStrFromFloat(RecFloat val, char *bufVal)
{
  int percent;

  percent = (int)((val * 100.0) + 0.5);
  snprintf(bufVal, 4, "%d%%", percent);
}

// void commaStrFromInt(RecInt bytes, char* bufVal)
//   Converts an Int to string with commas in it
//
//     bufVal must point to adequate space a la sprintf
//
void
commaStrFromInt(RecInt bytes, char *bufVal)
{
  int len;
  int numCommas;
  char *curPtr;

  sprintf(bufVal, "%" PRId64 "", bytes);
  len = strlen(bufVal);

  // The string is too short to need commas
  if (len < 4) {
    return;
  }

  numCommas = (len - 1) / 3;
  curPtr    = bufVal + (len + numCommas);
  *curPtr   = '\0';
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
//     Converts into a string in units of megabytes
//      No unit specification is added
//
//     bufVal must point to adequate space a la sprintf
//
void
MbytesFromInt(RecInt bytes, char *bufVal)
{
  RecInt mBytes = bytes / 1048576;

  sprintf(bufVal, "%" PRId64 "", mBytes);
}

// void bytesFromInt(RecInt bytes, char* bufVal)
//
//    Converts mgmt into a string with one of
//       GB, MB, KB, B units
//
//     bufVal must point to adequate space a la sprintf
void
bytesFromInt(RecInt bytes, char *bufVal)
{
  const int64_t gb  = 1073741824;
  const long int mb = 1048576;
  const long int kb = 1024;
  int bytesP;
  double unitBytes;

  if (bytes >= gb) {
    unitBytes = bytes / (double)gb;
    snprintf(bufVal, 15, "%.1f GB", unitBytes);
  } else {
    // Reduce the precision of the bytes parameter
    //   because we know that it less than 1GB which
    //   has plenty of precision for a regular int
    //   and saves from 64 bit arithmetic which may
    //   be expensive on some processors
    bytesP = (int)bytes;
    if (bytesP >= mb) {
      unitBytes = bytes / (double)mb;
      snprintf(bufVal, 15, "%.1f MB", unitBytes);
    } else if (bytesP >= kb) {
      unitBytes = bytes / (double)kb;
      snprintf(bufVal, 15, "%.1f KB", unitBytes);
    } else {
      snprintf(bufVal, 15, "%d", bytesP);
    }
  }
}

// bool varStrFromName (const char*, char*, int)
//
//   Sets the bufVal string to the value of the local manager
//     named by varName.  bufLen is size of bufVal
//
//  return true if bufVal was succefully set
//    and false otherwise
//
//  EVIL ALERT: overviewRecord::varStrFromName is extremely
//    similar to this function except in how it gets it's
//    data.  Changes to this fuction must be propogated
//    to its twin.  Cut and Paste sucks but there is not
//    an easy way to merge the functions
//
bool
varStrFromName(const char *varNameConst, char *bufVal, int bufLen)
{
  char *varName        = nullptr;
  RecDataT varDataType = RECD_NULL;
  bool found           = true;
  int varNameLen       = 0;
  char formatOption    = '\0';
  RecData data;
  int err = REC_ERR_FAIL;

  memset(&data, 0, sizeof(RecData));

  // Check to see if there is a \ option on the end of variable
  //   \ options indicate that we need special formatting
  //   of the results.  Supported \ options are
  //
  ///  b - bytes.  Ints and Counts only.  Amounts are
  //       transformed into one of GB, MB, KB, or B
  //
  varName    = ats_strdup(varNameConst);
  varNameLen = strlen(varName);
  if (varNameLen > 3 && varName[varNameLen - 2] == '\\') {
    formatOption = varName[varNameLen - 1];

    // Now that we know the format option, terminate the string
    //   to make the option disappear
    varName[varNameLen - 2] = '\0';

    // Return not found for unknown format options
    if (formatOption != 'b' && formatOption != 'm' && formatOption != 'c' && formatOption != 'p') {
      ats_free(varName);
      return false;
    }
  }

  err = RecGetRecordDataType(varName, &varDataType);
  if (err == REC_ERR_FAIL) {
    ats_free(varName);
    return false;
  }

  switch (varDataType) {
  case RECD_INT:
    RecGetRecordInt(varName, &data.rec_int);
    if (formatOption == 'b') {
      bytesFromInt(data.rec_int, bufVal);
    } else if (formatOption == 'm') {
      MbytesFromInt(data.rec_int, bufVal);
    } else if (formatOption == 'c') {
      commaStrFromInt(data.rec_int, bufVal);
    } else {
      snprintf(bufVal, bufLen, "%" PRId64 "", data.rec_int);
    }
    break;

  case RECD_COUNTER:
    RecGetRecordCounter(varName, &data.rec_counter);
    if (formatOption == 'b') {
      bytesFromInt((MgmtInt)data.rec_counter, bufVal);
    } else if (formatOption == 'm') {
      MbytesFromInt((MgmtInt)data.rec_counter, bufVal);
    } else if (formatOption == 'c') {
      commaStrFromInt(data.rec_counter, bufVal);
    } else {
      snprintf(bufVal, bufLen, "%" PRId64 "", data.rec_counter);
    }
    break;
  case RECD_FLOAT:
    RecGetRecordFloat(varName, &data.rec_float);
    if (formatOption == 'p') {
      percentStrFromFloat(data.rec_float, bufVal);
    } else {
      snprintf(bufVal, bufLen, "%.2f", data.rec_float);
    }
    break;
  case RECD_STRING:
    RecGetRecordString_Xmalloc(varName, &data.rec_string);
    if (data.rec_string == nullptr) {
      bufVal[0] = '\0';
    } else if (strlen(data.rec_string) < (size_t)(bufLen - 1)) {
      ink_strlcpy(bufVal, data.rec_string, bufLen);
    } else {
      ink_strlcpy(bufVal, data.rec_string, bufLen);
    }
    ats_free(data.rec_string);
    break;
  default:
    found = false;
    break;
  }

  ats_free(varName);
  return found;
}

// bool MgmtData::setFromName(const char*)
//
//    Fills in class variables from the given
//      variable name
//
//    Returns true if the information could be set
//     and false otherwise
//
bool
MgmtData::setFromName(const char *varName)
{
  bool found = true;
  int err;

  err = RecGetRecordDataType((char *)varName, &this->type);

  if (err == REC_ERR_FAIL) {
    return found;
  }

  switch (this->type) {
  case RECD_INT:
    RecGetRecordInt((char *)varName, &this->data.rec_int);
    break;
  case RECD_COUNTER:
    RecGetRecordCounter((char *)varName, &this->data.rec_counter);
    break;
  case RECD_FLOAT:
    RecGetRecordFloat((char *)varName, &this->data.rec_float);
    break;
  case RECD_STRING:
    RecGetRecordString_Xmalloc((char *)varName, &this->data.rec_string);
    break;
  case RECD_NULL:
  default:
    found = false;
    break;
  }

  return found;
}

MgmtData::MgmtData()
{
  type = RECD_NULL;
  memset(&data, 0, sizeof(RecData));
}

MgmtData::~MgmtData()
{
  if (type == RECD_STRING) {
    ats_free(data.rec_string);
  }
}

// MgmtData::compareFromString(const char* str, strLen)
//
//  Compares the value of string converted to
//    data type of this_>type with value
//    held in this->data
//
bool
MgmtData::compareFromString(const char *str)
{
  RecData compData;
  bool compare = false;
  float floatDiff;

  switch (this->type) {
  case RECD_INT:
    // TODO: Add SI decimal multipliers rule ?
    if (str && recordRegexCheck("^[0-9]+$", str)) {
      compData.rec_int = ink_atoi64(str);
      if (data.rec_int == compData.rec_int) {
        compare = true;
      }
    }
    break;
  case RECD_COUNTER:
    if (str && recordRegexCheck("^[0-9]+$", str)) {
      compData.rec_counter = ink_atoi64(str);
      if (data.rec_counter == compData.rec_counter) {
        compare = true;
      }
    }
    break;
  case RECD_FLOAT:
    compData.rec_float = atof(str);
    // HACK - There are some rounding problems with
    //   floating point numbers so say we have a match if there difference
    //   is small
    floatDiff = data.rec_float - compData.rec_float;
    if (floatDiff > -0.001 && floatDiff < 0.001) {
      compare = true;
    }
    break;
  case RECD_STRING:
    if (str == nullptr || *str == '\0') {
      if (data.rec_string == nullptr) {
        compare = true;
      }
    } else {
      if ((data.rec_string != nullptr) && (strcmp(str, data.rec_string) == 0)) {
        compare = true;
      }
    }
    break;
  case RECD_NULL:
  default:
    compare = false;
    break;
  }

  return compare;
}

// void RecDataT varType(const char* varName)
//
//   Simply return the variable type
//
RecDataT
varType(const char *varName)
{
  RecDataT data_type;
  int err;

  err = RecGetRecordDataType((char *)varName, &data_type);

  if (err == REC_ERR_FAIL) {
    return RECD_NULL;
  }

  Debug("RecOp", "[varType] %s is of type %d", varName, data_type);
  return data_type;
}

// InkHashTable* processFormSubmission(char* submission)
//
//  A generic way to handle a HTML form submission.
//  Creates a hash table with name value pairs
//
//  CALLEE must deallocate the returned hash table with
//   ink_hash_table_destroy_and_free_values(InkHashTable *ht_ptr)
//

InkHashTable *
processFormSubmission(char *submission)
{
  InkHashTable *nameVal = ink_hash_table_create(InkHashTableKeyType_String);
  Tokenizer updates("&\n\r");
  Tokenizer pair("=");
  int numUpdates;
  char *name;
  char *value;
  char *submission_copy;
  int pairNum;

  if (submission == nullptr) {
    ink_hash_table_destroy(nameVal);
    return nullptr;
  }

  submission_copy = ats_strdup(submission);
  numUpdates      = updates.Initialize(submission_copy, SHARE_TOKS);

  for (int i = 0; i < numUpdates; i++) {
    pairNum = pair.Initialize(updates[i]);

    // We should have gotten either either 1 or 2 tokens
    //    One token indicates an variable being set to
    //    blank.  Two indicates the variable being set to
    //    a value.  If the submission is invalid, just forget
    //    about it.
    if (pairNum == 1 || pairNum == 2) {
      name = ats_strdup(pair[0]);
      substituteUnsafeChars(name);

      // If the value is blank, store it as a null
      if (pairNum == 1) {
        value = nullptr;
      } else {
        value = ats_strdup(pair[1]);
        substituteUnsafeChars(value);
      }

      ink_hash_table_insert(nameVal, name, value);
      ats_free(name);
    }
  }
  ats_free(submission_copy);

  return nameVal;
}

// InkHashTable* processFormSubmission_noSubstitute(char* submission)
//
//  A generic way to handle a HTML form submission.
//  Creates a hash table with name value pairs
//
//  CALLEE must deallocate the returned hash table with
//   ink_hash_table_destroy_and_free_values(InkHashTable *ht_ptr)
//
//  Note: This function will _not_ substituteUnsafeChars()
InkHashTable *
processFormSubmission_noSubstitute(char *submission)
{
  InkHashTable *nameVal = ink_hash_table_create(InkHashTableKeyType_String);
  Tokenizer updates("&\n\r");
  Tokenizer pair("=");
  int numUpdates;
  char *name;
  char *value;
  char *submission_copy;
  int pairNum;

  if (submission == nullptr) {
    ink_hash_table_destroy(nameVal);
    return nullptr;
  }

  submission_copy = ats_strdup(submission);
  numUpdates      = updates.Initialize(submission_copy, SHARE_TOKS);

  for (int i = 0; i < numUpdates; i++) {
    pairNum = pair.Initialize(updates[i]);

    // We should have gotten either either 1 or 2 tokens
    //    One token indicates an variable being set to
    //    blank.  Two indicates the variable being set to
    //    a value.  If the submission is invalid, just forget
    //    about it.
    if (pairNum == 1 || pairNum == 2) {
      name = ats_strdup(pair[0]);

      // If the value is blank, store it as a null
      if (pairNum == 1) {
        value = nullptr;
      } else {
        value = ats_strdup(pair[1]);
      }

      ink_hash_table_insert(nameVal, name, value);
      ats_free(name);
    }
  }
  ats_free(submission_copy);

  return nameVal;
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
      *write    = (char)charVal;
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
      safeCurrent += 1;
      break;
    }

    inCurrent++;
  }
  *safeCurrent = '\0';
  return safeBuf;
}

//
//
//  Sets the LocalManager variable:  proxy.node.hostname
//
//    To the fully qualified hostname for the machine
//       that we are running on
int
setHostnameVar()
{
  char ourHostName[MAXDNAME];
  char *firstDot;

  // Get Our HostName
  if (gethostname(ourHostName, MAXDNAME) < 0) {
    mgmt_fatal(errno, "[setHostnameVar] Can not determine our hostname");
  }

  res_init();
  appendDefaultDomain(ourHostName, MAXDNAME);

  // FQ is a Fully Qualified hostname (ie: proxydev.example.com)
  varSetFromStr("proxy.node.hostname_FQ", ourHostName);

  // non-FQ is just the hostname (ie: proxydev)
  firstDot = strchr(ourHostName, '.');
  if (firstDot != nullptr) {
    *firstDot = '\0';
  }
  varSetFromStr("proxy.node.hostname", ourHostName);

  return 0;
}

// void appendDefautDomain(char* hostname, int bufLength)
//
//   Appends the pasted in hostname with the default
//     domain if the hostname is an unqualified name
//
//   The default domain is obtained from the resolver libraries
//    data structure
//
//   Truncates the domain name if bufLength is too small
//
//
void
appendDefaultDomain(char *hostname, int bufLength)
{
  int len                 = strlen(hostname);
  const char msg[]        = "Nodes will be know by their unqualified host name";
  static int error_before = 0; // Race ok since effect is multple error msg

  ink_assert(len < bufLength);
  ink_assert(bufLength >= 64);

  // Ensure null termination of the result string
  hostname[bufLength - 1] = '\0';

  if (strchr(hostname, '.') == nullptr) {
    if (_res.defdname[0] != '\0') {
      if (bufLength - 2 >= (int)(strlen(hostname) + strlen(_res.defdname))) {
        ink_strlcat(hostname, ".", bufLength);
        ink_strlcat(hostname, _res.defdname, bufLength);
      } else {
        if (error_before == 0) {
          mgmt_log("%s %s\n", "[appendDefaultDomain] Domain name is too long.", msg);
          error_before++;
        }
      }
    } else {
      if (error_before == 0) {
        mgmt_log("%s %s\n", "[appendDefaultDomain] Unable to determine default domain name.", msg);
        error_before++;
      }
    }
  }
}

bool
recordValidityCheck(const char *varName, const char *value)
{
  RecCheckT check_t;
  char *pattern;

  if (RecGetRecordCheckType((char *)varName, &check_t) != REC_ERR_OKAY) {
    return false;
  }
  if (RecGetRecordCheckExpr((char *)varName, &pattern) != REC_ERR_OKAY) {
    return false;
  }

  switch (check_t) {
  case RECC_STR:
    if (recordRegexCheck(pattern, value)) {
      return true;
    }
    break;
  case RECC_INT:
    if (recordRangeCheck(pattern, value)) {
      return true;
    }
    break;
  case RECC_IP:
    if (recordIPCheck(pattern, value)) {
      return true;
    }
    break;
  case RECC_NULL:
    // skip checking
    return true;
  default:
    // unknown RecordCheckType...
    mgmt_log("[WebMgmtUtil] error, unknown RecordCheckType for record %s\n", varName);
  }

  return false;
}

bool
recordRegexCheck(const char *pattern, const char *value)
{
  pcre *regex;
  const char *error;
  int erroffset;

  regex = pcre_compile(pattern, 0, &error, &erroffset, nullptr);
  if (!regex) {
    return false;
  } else {
    int r = pcre_exec(regex, nullptr, value, strlen(value), 0, 0, nullptr, 0);

    pcre_free(regex);
    return (r != -1) ? true : false;
  }

  return false; // no-op
}

bool
recordRangeCheck(const char *pattern, const char *value)
{
  int l_limit;
  int u_limit;
  int val;
  char *p = (char *)pattern;
  Tokenizer dashTok("-");

  if (recordRegexCheck("^[0-9]+$", value)) {
    while (*p != '[') {
      p++;
    } // skip to '['
    if (dashTok.Initialize(++p, COPY_TOKS) == 2) {
      l_limit = atoi(dashTok[0]);
      u_limit = atoi(dashTok[1]);
      val     = atoi(value);
      if (val >= l_limit && val <= u_limit) {
        return true;
      }
    }
  }
  return false;
}

bool
recordIPCheck(const char *pattern, const char *value)
{
  //  regex_t regex;
  //  int result;
  bool check;
  const char *range_pattern =
    R"(\[[0-9]+\-[0-9]+\]\\\.\[[0-9]+\-[0-9]+\]\\\.\[[0-9]+\-[0-9]+\]\\\.\[[0-9]+\-[0-9]+\])";
  const char *ip_pattern = "[0-9]*[0-9]*[0-9].[0-9]*[0-9]*[0-9].[0-9]*[0-9]*[0-9].[0-9]*[0-9]*[0-9]";

  Tokenizer dotTok1(".");
  Tokenizer dotTok2(".");
  int i;

  check = true;
  if (recordRegexCheck(range_pattern, pattern) && recordRegexCheck(ip_pattern, value)) {
    if (dotTok1.Initialize((char *)pattern, COPY_TOKS) == 4 && dotTok2.Initialize((char *)value, COPY_TOKS) == 4) {
      for (i = 0; i < 4 && check; i++) {
        if (!recordRangeCheck(dotTok1[i], dotTok2[i])) {
          check = false;
        }
      }
      if (check) {
        return true;
      }
    }
  } else if (strcmp(value, "") == 0) {
    return true;
  }
  return false;
}

bool
recordRestartCheck(const char *varName)
{
  RecUpdateT update_t;

  if (RecGetRecordUpdateType((char *)varName, &update_t) != REC_ERR_OKAY) {
    return false;
  }

  if (update_t == RECU_RESTART_TS || update_t == RECU_RESTART_TM) {
    return true;
  }

  return false;
}
