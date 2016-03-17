/** @file

  Private record process declarations

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

#ifndef _P_REC_PROCESS_H_
#define _P_REC_PROCESS_H_

// Must include 'P_EventSystem.h' before 'I_EventSystem.h' (which is
// included in 'I_RecProcess.h') to prevent multiple-symbol-definition
// complaints if the caller uses both 'P_EventSystem.h' and this 'P_'
// file.
#include "P_EventSystem.h"

#include "I_RecProcess.h"
#include "P_RecDefs.h"

//-------------------------------------------------------------------------
// Protected Interface
//-------------------------------------------------------------------------

int RecExecRawStatSyncCbs();

#endif
