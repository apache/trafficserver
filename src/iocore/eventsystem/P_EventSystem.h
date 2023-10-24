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
#define _P_EventSystem_H

#include "tscore/ink_platform.h"

#include "iocore/eventsystem/EventSystem.h"

#include "iocore/eventsystem/P_Thread.h"
#include "iocore/eventsystem/P_VIO.h"
#include "iocore/eventsystem/P_IOBuffer.h"
#include "iocore/eventsystem/P_VConnection.h"
#include "iocore/eventsystem/P_Freer.h"
#include "iocore/eventsystem/P_UnixEvent.h"
#include "iocore/eventsystem/P_UnixEThread.h"
#include "iocore/eventsystem/P_ProtectedQueue.h"
#include "iocore/eventsystem/P_UnixEventProcessor.h"
#include "iocore/eventsystem/P_UnixSocketManager.h"

static constexpr ts::ModuleVersion EVENT_SYSTEM_MODULE_INTERNAL_VERSION{EVENT_SYSTEM_MODULE_PUBLIC_VERSION,
                                                                        ts::ModuleVersion::PRIVATE};
