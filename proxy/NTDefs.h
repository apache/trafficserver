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

#ifndef _NT_DEFS_H_
#define _NT_DEFS_H_

#ifndef BUILD_MACHINE
#define BUILD_MACHINE "NT"
#endif

#ifndef BUILD_PERSON
#define BUILD_PERSON  "BOBCAT"
#endif

#define COMPANY_REG_KEY "SOFTWARE\\Inktomi"
#define TS_ROOT_REG_KEY "TrafficServer\\5.2"
#define TS_BASE_REG_KEY "TSBase"
#define TS_CATEGORY_MSG_KEY "CategoryMessageFile"
#define TS_CATEGORY_COUNT_KEY "CategoryCount"

#define TCP_SRVS_REG_KEY "SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters"
#define MAX_USER_PORT "MaxUserPort"
#define OPTIMAL_MAX_USER_PORT 0xfffe

#define TCP_WINDOW_SIZE "TcpWindowSize"
#define OPTIMAL_TCP_WINDOW_SZ 0x4470

#define TCP_TIMED_WAIT_DELAY "TcpTimedWaitDelay"
#define OPTIMAL_TCP_TIMED_WAIT_DELAY 0xb4


// If this location is changed, then perfdll/TrafficServer.reg or the
// install shield editor needs to be changed as well.
#define TS_PERFMON_SVC_REG_KEY "SYSTEM\\CurrentControlSet\\Services\\TrafficServer\\Performance"
#define TS_EVENT_LOG_SRC_KEY "SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application\\TrafficServer"
#define TS_EVENT_LOG_MESSAGE_FILE "%SystemRoot%\\System32\\TSMsgs.DLL"

#define TS_EXIT_STR "Inktomi Traffic Server Process %d Please Exit"
#define TS_ABORT_STR "Inktomi Traffic Server Process %d Please Abort"
#define TS_DUMP_STATE_STR "Inktomi Traffic Server Process %d Please Dump State"

//***************** shared between manager and server *****************//
#define STR_IOC_PORT        "IOCPort"
#define STR_PIPE_NAME       "\\\\.\\pipe\\INKTrafficServer"
#define STR_MANAGER_ID      "ManagerProcId"
#define STR_FDLIST_SZ       "FDListSize"

#define ENV_BLOCK_SZ        256
#define NPIPE_BUFFER_SZ     (MAX_PROXY_SERVER_PORTS+1)*1024
#define DEF_WAIT_FOR_PIPE   10*1000     // 10 Sec
//********************************************************************//

#ifdef MGMT_SERVICE

#define TS_MGR_SERVICE_NAME         "InktomiTrafficManager"
#define TS_MGR_SERVICE_DISPLAY_NAME "Inktomi Traffic Manager"

// These are the attributes for creating the manager service.
//
// LPCTSTR lpServiceName = TS_MGR_SERVICE_NAME          // pointer to name of service to start
// LPCTSTR lpDisplayName = TS_MGR_SERVICE_DISPLAY_NAME  // pointer to display name
// DWORD dwDesiredAccess = SERVICE_ALL_ACCESS           // type of access to service
// DWORD dwServiceType   = SERVICE_WIN32_OWN_PROCESS    // type of service
// DWORD dwStartType     = SERVICE_AUTO_START           // when to start service
// DWORD dwErrorControl  = SERVICE_ERROR_NORMAL         // severity if service fails to start

#endif // MGMT_SERVICE

#ifdef COP_SERVICE

#define TS_COP_SERVICE_NAME         "InktomiTrafficCop"
#define TS_COP_SERVICE_DISPLAY_NAME "Inktomi Traffic Cop"
#define TS_COP_SERVICE_DESCRIPTION  "Monitors the health of Traffic Server and Traffic Manager"

// These are the attributes for creating the cop service.
//
// LPCTSTR lpServiceName = TS_COP_SERVICE_NAME          // pointer to name of service to start
// LPCTSTR lpDisplayName = TS_COP_SERVICE_DISPLAY_NAME  // pointer to display name
// DWORD dwDesiredAccess = SERVICE_ALL_ACCESS           // type of access to service
// DWORD dwServiceType   = SERVICE_WIN32_OWN_PROCESS    // type of service
// DWORD dwStartType     = SERVICE_AUTO_START           // when to start service
// DWORD dwErrorControl  = SERVICE_ERROR_NORMAL         // severity if service fails to start

#endif // COP_SERVICE

#endif
