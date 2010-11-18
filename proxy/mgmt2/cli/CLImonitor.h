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

/***************************************/
/****************************************************************************
 *
 *  Module: Encapsulates command line handling of displaying statistics
 *
 *
 ****************************************************************************/

#ifndef _CLI_MONITOR_H_
#define _CLI_MONITOR_H_

#include "inktomi++.h"
#include "CLIeventHandler.h"    /* CLI_DATA */

/* Containter for monitor global variables/fcns */
struct CLI_monitor
{
#define NUM_PROT_HTTP_UA_DESCS  4
#define NUM_PROT_HTTP_OS_DESCS  4
#define NUM_PROT_ICP_DESCS     11
  /*-------------------------------*/
#define NUM_PROT_DESCS         19
  static const CLI_globals::VarNameDesc mon_prot_desctable[NUM_PROT_DESCS];

#define NUM_NODE_CACHE_DESCS    3
#define NUM_NODE_INPROG_DESCS   3
#define NUM_NODE_NETWORK_DESCS  2
#define NUM_NODE_NAMERES_DESCS  2
  /*-------------------------------*/
#define NUM_NODE_DESCS         10
  static const CLI_globals::VarNameDesc mon_node_desctable[NUM_NODE_DESCS];

  /* NOTE: took out 'Link' section for cache which has 3 entries */
#define NUM_CACHE_DESCS         17
  static const CLI_globals::VarNameDesc mon_cache_desctable[NUM_CACHE_DESCS];

#define NUM_OTHER_HOSTDB_DESCS   3
#define NUM_OTHER_DNS_DESCS      3
#define NUM_OTHER_CLUSTER_DESCS  6
#define NUM_OTHER_SOCKS_DESCS    3
#define NUM_OTHER_LOG_DESCS      5
  /*-------------------------------*/
#define NUM_OTHER_DESCS         21
  static const CLI_globals::VarNameDesc mon_other_desctable[NUM_OTHER_DESCS];

  /* Called from event handlers to do the work */
  static void doMonitorProtocolStats(CLI_DATA * c_data /* IN: client data */ );

  static void doMonitorNodeStats(CLI_DATA * c_data /* IN: client data */ );

  static void doMonitorCacheStats(CLI_DATA * c_data /* IN: client data */ );

  static void doMonitorOtherStats(CLI_DATA * c_data /* IN: client data */ );

  static void doMonitorDashboard(CLI_DATA * c_data /* IN: client data */ );

};

#endif /* _CLI_MONITOR_H_ */
