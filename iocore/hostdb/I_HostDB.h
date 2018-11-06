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

  I_HostDB.h


 ****************************************************************************/

#pragma once

#include "I_EventSystem.h"
#include "I_Net.h"
#include "I_Cache.h"

#include "I_HostDBProcessor.h"

// TS-1925: switch from MMH to MD5 hash; bumped to version 2
// switch from MD5 to SHA256 hash; bumped to version 3
// 2.1: Switched to mark RR elements.
static constexpr ts::ModuleVersion HOSTDB_MODULE_PUBLIC_VERSION(3, 1);
