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

  P_HostDB.h


 ****************************************************************************/

#pragma once

#include "tscore/ink_platform.h"

#ifdef SPLIT_DNS
#include "P_SplitDNS.h"
#endif

#include "P_EventSystem.h"

#include "I_HostDB.h"

// HostDB files
#include "P_DNS.h"
#include "P_RefCountCache.h"
#include "P_HostDBProcessor.h"

static constexpr ts::ModuleVersion HOSTDB_MODULE_INTERNAL_VERSION{HOSTDB_MODULE_PUBLIC_VERSION, ts::ModuleVersion::PRIVATE};

Ptr<HostDBInfo> probe(Ptr<ProxyMutex> mutex, CryptoHash const &hash, bool ignore_timeout);

void make_crypto_hash(CryptoHash &hash, const char *hostname, int len, int port, const char *pDNSServers, HostDBMark mark);
