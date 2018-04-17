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

/****************************************************************************

  Event Subsystem



**************************************************************************/
#pragma once
#define _P_EventSystem_h

#include "ts/ink_platform.h"

#include "I_EventSystem.h"

#include "P_Thread.h"
#include "P_VIO.h"
#include "P_IOBuffer.h"
#include "P_VConnection.h"
#include "P_Freer.h"
#include "P_UnixEvent.h"
#include "P_UnixEThread.h"
#include "P_ProtectedQueue.h"
#include "P_UnixEventProcessor.h"
#include "P_UnixSocketManager.h"
#undef EVENT_SYSTEM_MODULE_VERSION
#define EVENT_SYSTEM_MODULE_VERSION \
  makeModuleVersion(EVENT_SYSTEM_MODULE_MAJOR_VERSION, EVENT_SYSTEM_MODULE_MINOR_VERSION, PRIVATE_MODULE_HEADER)
