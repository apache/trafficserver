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

#ifndef _P_HostDB_h_
#define _P_HostDB_h_

#include "ts/ink_platform.h"

#ifdef SPLIT_DNS
#include "P_SplitDNS.h"
#include "P_Cluster.h"
#endif

#include "P_EventSystem.h"

#include "I_HostDB.h"

// HostDB files
#include "P_DNS.h"
#include "P_MultiCache.h"
#include "P_HostDBProcessor.h"

#undef HOSTDB_MODULE_VERSION
#define HOSTDB_MODULE_VERSION makeModuleVersion(HOSTDB_MODULE_MAJOR_VERSION, HOSTDB_MODULE_MINOR_VERSION, PRIVATE_MODULE_HEADER)
HostDBInfo *probe(ProxyMutex *mutex, HostDBMD5 const &md5, bool ignore_timeout);

void make_md5(INK_MD5 &md5, const char *hostname, int len, int port, char const *pDNSServers, HostDBMark mark);
#endif
