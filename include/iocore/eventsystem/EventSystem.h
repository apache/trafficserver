/** @file

  Event subsystem

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
#define _I_EventSystem_h

#include "tscore/Version.h"
#include "tscore/ink_platform.h"
#include "ts/apidefs.h"

#include "iocore/eventsystem/IOBuffer.h"
#include "iocore/eventsystem/PipeIOBuffer.h"
#include "iocore/eventsystem/Action.h"
#include "iocore/eventsystem/Continuation.h"
#include "iocore/eventsystem/EThread.h"
#include "iocore/eventsystem/Event.h"
#include "iocore/eventsystem/EventProcessor.h"

#include "iocore/eventsystem/Lock.h"
#include "iocore/eventsystem/PriorityEventQueue.h"
#include "iocore/eventsystem/Processor.h"
#include "iocore/eventsystem/ProtectedQueue.h"
#include "iocore/eventsystem/Thread.h"
#include "iocore/eventsystem/UnixSocket.h"
#include "iocore/eventsystem/VIO.h"
#include "iocore/eventsystem/VConnection.h"
#include "records/RecProcess.h"
#include "iocore/eventsystem/RecProcess.h"

static constexpr ts::ModuleVersion EVENT_SYSTEM_MODULE_PUBLIC_VERSION(1, 0, ts::ModuleVersion::PUBLIC);

void ink_event_system_init(ts::ModuleVersion version);
