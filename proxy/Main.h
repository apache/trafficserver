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

  Main.h

  This is the primary include file for the proxy cache system.


 ****************************************************************************/

#ifndef _Main_h_
#define	_Main_h_

#include "inktomi++.h"
#include "Regression.h"
#include "I_Version.h"


//
// Constants
//
#define DOMAIN_NAME_MAX       255
#define PATH_NAME_MAX         511

#define ET_CACHE ET_CALL

// TODO: consolidate location of these defaults
#define DEFAULT_ROOT_DIRECTORY            PREFIX
#define DEFAULT_LOCAL_STATE_DIRECTORY     "var/trafficserver"
#define DEFAULT_SYSTEM_CONFIG_DIRECTORY   "etc/trafficserver"
#define DEFAULT_LOG_DIRECTORY             "var/log/trafficserver"
#define DEFAULT_TS_DIRECTORY_FILE         PREFIX "/etc/traffic_server"

//
// Global Data
//
// Global Configuration
extern int use_accept_thread;
extern int accept_till_done;
//extern int ssl_accept_port_number;
//extern int ssl_enabled;
extern int http_accept_port_number;
extern int http_accept_file_descriptor;
extern int command_flag;
extern int auto_clear_hostdb_flag;
extern int auto_clear_cache_flag;
extern int lock_process;
extern int fds_limit;
extern int debug_level;
extern char cluster_host[DOMAIN_NAME_MAX + 1];
extern int cluster_port_number;
extern char proxy_name[256];

extern int remote_management_flag;
extern char management_directory[256];

inkcoreapi extern int qt_accept_file_descriptor;
inkcoreapi extern int CacheClusteringEnabled;

extern int use_mp;

// Debugging Configuration
extern char debug_host[DOMAIN_NAME_MAX + 1];
extern int debug_port;

// Default socket buffer limits
extern int default_sndbuf_size;
extern int default_rcvbuf_size;

//
// Functions
//
void init_system();
void shutdown_system();
inline bool
maintainance_mode()
{
  return (command_flag ? true : false);
}

void syslog_thr_init();

enum HttpPortTypes
{
  SERVER_PORT_DEFAULT = 0,
  SERVER_PORT_COMPRESSED,
  SERVER_PORT_BLIND_TUNNEL,
  SERVER_PORT_NCA,
  SERVER_PORT_SSL
};

struct HttpPortEntry
{
  int fd;
  HttpPortTypes type;
};

extern HttpPortEntry *http_port_attr_array;

extern Version version;
extern AppVersionInfo appVersionInfo;

struct HttpOtherPortEntry
{
  int port;
  HttpPortTypes type;
};
extern HttpOtherPortEntry *http_other_port_array;


#define TS_ReadConfigInteger            REC_ReadConfigInteger
#define TS_ReadConfigLLong              REC_ReadConfigLLong
#define TS_ReadConfigFloat              REC_ReadConfigFloat
#define TS_ReadConfigString             REC_ReadConfigString
#define TS_EstablishStaticConfigInteger REC_EstablishStaticConfigInteger
#define TS_RegisterConfigUpdateFunc     REC_RegisterConfigUpdateFunc
#define TS_ReadConfigStringAlloc        REC_ReadConfigStringAlloc
#define TS_ConfigReadInteger            REC_ConfigReadInteger
#define TS_ConfigReadString             REC_ConfigReadString


#endif /* _Main_h_ */
