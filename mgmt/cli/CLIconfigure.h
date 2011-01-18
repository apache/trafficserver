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
 *  Module: Encapsulates command line configuration
 *
 *
 ****************************************************************************/

#ifndef _CLI_CONFIGURE_H_
#define _CLI_CONFIGURE_H_

#include "libts.h"
#include "CLIeventHandler.h"    /* CLI_DATA */

/* Container for command line configuration global variables/fcns */
struct CLI_configure
{

  /* Server Configuration */
#define NUM_SERVER_TRAFFIC_DESCS      4
#define NUM_SERVER_WEB_DESCS          2
#define NUM_SERVER_VIP_DESCS          1
#define NUM_SERVER_AUTOC_DESCS        1
#define NUM_SERVER_THROTTLE_DESCS     1
#define NUM_SERVER_SNMP_DESCS         1
#define NUM_SERVER_CRP_DESCS          4
  /*------------------------------------*/
#define NUM_SERVER_DESCS              14
  static const CLI_globals::VarNameDesc conf_server_desctable[NUM_SERVER_DESCS];

  /* Protocol Configuration */
#define NUM_CONF_PROTOCOLS_HTTP_DESCS      15
  /*------------------------------------------*/
#define NUM_CONF_PROTOCOLS_DESCS           15
  static const CLI_globals::VarNameDesc conf_protocols_desctable[NUM_CONF_PROTOCOLS_DESCS];

  /* Cache Configuration */
#define NUM_CONF_CACHE_ACT_DESCS      2
#define NUM_CONF_CACHE_STORAGE_DESCS  2
#define NUM_CONF_CACHE_FRESH_DESCS    4
#define NUM_CONF_CACHE_VARC_DESCS     6
  /*------------------------------------------*/
#define NUM_CONF_CACHE_DESCS         14
  static const CLI_globals::VarNameDesc conf_cache_desctable[NUM_CONF_CACHE_DESCS];

  /* Security Configuration */
#define NUM_CONF_SECURITY_ACCESS_DESCS    3
#define NUM_CONF_SECURITY_FIREW_DESCS     4
  /*------------------------------------------*/
#define NUM_CONF_SECURITY_DESCS           7
  static const CLI_globals::VarNameDesc conf_security_desctable[NUM_CONF_SECURITY_DESCS];

  /* Routing Configuration */
#define NUM_CONF_ROUT_PARENT_DESCS     2
#define NUM_CONF_ROUT_ICP_DESCS        4
#define NUM_CONF_ROUT_REVP_DESCS       3
  /*------------------------------------------*/
#define NUM_CONF_ROUT_DESCS            9
  static const CLI_globals::VarNameDesc conf_rout_desctable[NUM_CONF_ROUT_DESCS];

  /* HostDB Configuration */
#define NUM_CONF_HOSTDB_MG_DESCS        5
#define NUM_CONF_HOSTDB_DNS_DESCS       2
  /*------------------------------------------*/
#define NUM_CONF_HOSTDB_DESCS           7
  static const CLI_globals::VarNameDesc conf_hostdb_desctable[NUM_CONF_HOSTDB_DESCS];

  /* Logging Configuration */
#define NUM_CONF_LOGGING_EVENT_DESCS     1
#define NUM_CONF_LOGGING_LMG_DESCS       3
#define NUM_CONF_LOGGING_LC_DESCS        5
#define NUM_CONF_LOGGING_SQUID_DESCS     4
#define NUM_CONF_LOGGING_NSCPC_DESCS     4
#define NUM_CONF_LOGGING_NSCPE_DESCS     4
#define NUM_CONF_LOGGING_NSCPE2_DESCS    4
#define NUM_CONF_LOGGING_CUSTOM_DESCS    1
#define NUM_CONF_LOGGING_ROLL_DESCS      4
#define NUM_CONF_LOGGING_SPLIT_DESCS     1
  /*------------------------------------------*/
#define NUM_CONF_LOGGING_DESCS          31
  static const CLI_globals::VarNameDesc conf_logging_desctable[NUM_CONF_LOGGING_DESCS];

  /* Snapshots Configuration */
  /*------------------------------------------*/
#define NUM_CONF_SNAPSHOTS_DESCS          1
  static const CLI_globals::VarNameDesc conf_snapshots_desctable[NUM_CONF_SNAPSHOTS_DESCS];

  /* Called from event handlers to do the work */
  static void doConfigureServer(CLI_DATA * c_data /* IN: client data */ );

  static void doConfigureProtocols(CLI_DATA * c_data /* IN: client data */ );

  static void doConfigureCache(CLI_DATA * c_data /* IN: client data */ );

  static void doConfigureSecurity(CLI_DATA * c_data /* IN: client data */ );

  static void doConfigureRouting(CLI_DATA * c_data /* IN: client data */ );

  static void doConfigureHostDB(CLI_DATA * c_data /* IN: client data */ );

  static void doConfigureLogging(CLI_DATA * c_data /* IN: client data */ );

  static void doConfigureSnapshots(CLI_DATA * c_data /* IN: client data */ );

};


#endif /*  _CLI_CONFIGURE_H_ */
