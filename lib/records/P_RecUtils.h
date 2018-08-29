/** @file

  Private record util declarations

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

#include "ts/Diags.h"
#include "ts/ink_atomic.h"

#include "P_RecDefs.h"

//-------------------------------------------------------------------------
// Macros
//-------------------------------------------------------------------------

#define REC_TYPE_IS_STAT(rec_type) (((rec_type) == RECT_PROCESS) || ((rec_type) == RECT_PLUGIN) || ((rec_type) == RECT_NODE))

#define REC_TYPE_IS_CONFIG(rec_type) (((rec_type) == RECT_CONFIG) || ((rec_type) == RECT_LOCAL))

//-------------------------------------------------------------------------
// RecRecord Utils
//-------------------------------------------------------------------------
void RecRecordInit(RecRecord *r);
void RecRecordFree(RecRecord *r);
RecRecord *RecAlloc(RecT rec_type, const char *name, RecDataT data_type);

//-------------------------------------------------------------------------
// RecData Utils
//-------------------------------------------------------------------------

// Reset the value of this RecData to zero.
void RecDataZero(RecDataT type, RecData *data);

void RecDataSetMax(RecDataT type, RecData *data);
void RecDataSetMin(RecDataT type, RecData *data);
bool RecDataSet(RecDataT data_type, RecData *data_dst, RecData *data_src);
bool RecDataSetFromInt64(RecDataT data_type, RecData *data_dst, int64_t data_int64);
bool RecDataSetFromFloat(RecDataT data_type, RecData *data_dst, float data_float);
bool RecDataSetFromString(RecDataT data_type, RecData *data_dst, const char *data_string);
int RecDataCmp(RecDataT type, RecData left, RecData right);
RecData RecDataAdd(RecDataT type, RecData left, RecData right);
RecData RecDataSub(RecDataT type, RecData left, RecData right);
RecData RecDataMul(RecDataT type, RecData left, RecData right);
RecData RecDataDiv(RecDataT type, RecData left, RecData right);

//-------------------------------------------------------------------------
// Logging
//-------------------------------------------------------------------------

// XXX Rec has it's own copy of the global diags pointer, so it's own
// debug macros the use that. I'm not convinced that Rec actually does
// need a separate pointer.
void _RecLog(DiagsLevel dl, const SourceLocation &loc, const char *fmt, ...);
void _RecDebug(DiagsLevel dl, const SourceLocation &loc, const char *fmt, ...);

void RecDebugOff();

#define RecLog(level, fmt, ...) _RecLog(level, MakeSourceLocation(), fmt, ##__VA_ARGS__)
#define RecDebug(level, fmt, ...) _RecDebug(level, MakeSourceLocation(), fmt, ##__VA_ARGS__)
