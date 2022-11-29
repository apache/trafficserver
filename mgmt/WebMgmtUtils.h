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

#pragma once

#include "MgmtDefs.h"
#include "records/P_RecCore.h"

// Convert to byte units (GB, MB, KB)
void bytesFromInt(RecInt bytes, char *bufVal, int bufLen);

// Convert to MB
void MbytesFromInt(RecInt bytes, char *bufVal, int bufLen);

// Create comma string from int
void commaStrFromInt(RecInt bytes, char *bufVal, int bufLen);

// Create percent string from float
void percentStrFromFloat(RecFloat val, char *bufVal, int bufLen);

// Converts where applicable to specified type
bool varDataFromName(RecDataT varType, const char *varName, RecData *value);

// No conversion done.  varName must represent a value of the appropriate
//  type
// Default argument "convert" added to allow great flexibility in type checking
bool varSetInt(const char *varName, RecInt value, bool convert = false);
bool varSetCounter(const char *varName, RecCounter value, bool convert = false);
bool varSetFloat(const char *varName, RecFloat value, bool convert = false);
bool varSetData(RecDataT varType, const char *varName, RecData value);

int convertHtmlToUnix(char *buffer);
int substituteUnsafeChars(char *buffer);
char *substituteForHTMLChars(const char *buffer);
