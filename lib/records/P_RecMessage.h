/** @file

  Private RecMessage declarations

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

#include "P_RecDefs.h"
#include "tscpp/util/MemSpan.h"

//-------------------------------------------------------------------------
// Initialization
//-------------------------------------------------------------------------

void RecMessageRegister();

//-------------------------------------------------------------------------
// Message Operations
//-------------------------------------------------------------------------

RecMessage *RecMessageAlloc(RecMessageT msg_type, int initial_size = 256);
int RecMessageFree(RecMessage *msg);

RecMessage *RecMessageMarshal_Realloc(RecMessage *msg, const RecRecord *record);
int RecMessageUnmarshalFirst(RecMessage *msg, RecMessageItr *itr, RecRecord **record);
int RecMessageUnmarshalNext(RecMessage *msg, RecMessageItr *itr, RecRecord **record);

int RecMessageSend(RecMessage *msg);
int RecMessageRegisterRecvCb(RecMessageRecvCb recv_cb, void *cookie);
void RecMessageRecvThis(ts::MemSpan);

RecMessage *RecMessageReadFromDisk(const char *fpath);
int RecMessageWriteToDisk(RecMessage *msg, const char *fpath);
