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

#ifndef _WEB_MGMT_UTILS_
#define _WEB_MGMT_UTILS_

#include "MgmtDefs.h"

/****************************************************************************
 *
 *  WebMgmtUtils.h - Functions for interfacing to management records
 *
 *
 *
 ****************************************************************************/

#include "ts/ink_hash_table.h"
#include "ts/TextBuffer.h"
#include "ExpandingArray.h"

#include "P_RecCore.h"

// class MgmtData - stores information from local manager
//    variables in its native type
//
#include "P_RecCore.h"
class MgmtData
{
public:
  MgmtData();
  ~MgmtData();
  bool compareFromString(const char *str);
  bool setFromName(const char *varName);
  RecDataT type;
  RecData data;
};

// Convert to byte units (GB, MB, KB)
void bytesFromInt(RecInt bytes, char *bufVal);

// Convert to MB
void MbytesFromInt(RecInt bytes, char *bufVal);

// Create comma string from int
void commaStrFromInt(RecInt bytes, char *bufVal);

// Create percent string from float
void percentStrFromFloat(RecFloat val, char *bufVal);

// All types converted to/from strings where appropriate
bool varStrFromName(const char *varName, char *bufVal, int bufLen);
bool varSetFromStr(const char *varName, const char *value);

// Converts where applicable to specified type
bool varIntFromName(const char *varName, RecInt *value);
bool varFloatFromName(const char *varName, RecFloat *value);
bool varCounterFromName(const char *varName, RecCounter *value);
bool varDataFromName(RecDataT varType, const char *varName, RecData *value);

// No conversion done.  varName must represnt a value of the appropriate
//  type
// Default arguement "convert" added to allow great flexiblity in type checking
bool varSetInt(const char *varName, RecInt value, bool convert = false);
bool varSetCounter(const char *varName, RecCounter value, bool convert = false);
bool varSetFloat(const char *varName, RecFloat value, bool convert = false);
bool varSetData(RecDataT varType, const char *varName, RecData value);

// Return the type of the variable named
RecDataT varType(const char *varName);

int convertHtmlToUnix(char *buffer);
int substituteUnsafeChars(char *buffer);
char *substituteForHTMLChars(const char *buffer);

// Produce a hash table based on a HTML form submission
//
//  CALLEE deallocates hashtable
InkHashTable *processFormSubmission(char *submission);
InkHashTable *processFormSubmission_noSubstitute(char *submission);

// Shutdown the proxy
bool ProxyShutdown();
int setHostnameVar();
void appendDefaultDomain(char *hostname, int bufLength);

// Some scaling constants
#define BYTES_TO_MB_SCALE (1 / (1024 * 1024.0))
#define MBIT_TO_KBIT_SCALE (1000.0)
#define SECOND_TO_MILLISECOND_SCALE (1000.0)
#define PCT_TO_INTPCT_SCALE (100.0)

bool recordValidityCheck(const char *varName, const char *value);
bool recordRegexCheck(const char *pattern, const char *value);
bool recordRangeCheck(const char *pattern, const char *value);
bool recordIPCheck(const char *pattern, const char *value);
bool recordRestartCheck(const char *varName);

void fileCheckSum(char *buffer, int size, char *checksum, const size_t checksumSize);

// file management
int getFilesInDirectory(char *managedDir, ExpandingArray *fileList);
char *newPathString(const char *s1, const char *s2);

#endif
