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

/****************************************************************
 * Filename: ShowCmd.h
 * Purpose: This file contains the CLI's "show" command definitions.
 *
 *
 ****************************************************************/

#include <tcl.h>
#include "createArgument.h"
#include "definitions.h"

#ifndef __SHOW_CMD_H__
#define __SHOW_CMD_H__

// enumerated type which captures all "show" commands
typedef enum
{
  CMD_SHOW_STATUS = 1,
  CMD_SHOW_VERSION,
  CMD_SHOW_PORTS,
  CMD_SHOW_CLUSTER,
  CMD_SHOW_SECURITY,
  CMD_SHOW_HTTP,
  CMD_SHOW_ICP,
  CMD_SHOW_ICP_PEER,
  CMD_SHOW_PROXY,
  CMD_SHOW_CACHE,
  CMD_SHOW_CACHE_RULES,
  CMD_SHOW_CACHE_STORAGE,
  CMD_SHOW_VIRTUAL_IP,
  CMD_SHOW_HOSTDB,
  CMD_SHOW_DNS_RESOLVER,
  CMD_SHOW_LOGGING,
  CMD_SHOW_SSL,
  CMD_SHOW_FILTER,
  CMD_SHOW_PARENTS,
  CMD_SHOW_PARENT_RULES,
  CMD_SHOW_REMAP,
  CMD_SHOW_SOCKS,
  CMD_SHOW_SOCKS_RULES,
  CMD_SHOW_PORT_TUNNELS,
  CMD_SHOW_SCHEDULED_UPDATE,
  CMD_SHOW_UPDATE_RULES,
  CMD_SHOW_PROXY_STATS,
  CMD_SHOW_HTTP_TRANS_STATS,
  CMD_SHOW_HTTP_STATS,
  CMD_SHOW_ICP_STATS,
  CMD_SHOW_CACHE_STATS,
  CMD_SHOW_HOSTDB_STATS,
  CMD_SHOW_DNS_STATS,
  CMD_SHOW_LOGGING_STATS
} cliShowCommand;

////////////////////////////////////////////////////////////////
// ShowCmd
//
// This is the callback function for the "show" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int ShowCmd(ClientData clientData, Tcl_Interp * interp, int argc, char *argv[]);

////////////////////////////////////////////////////////////////
// ShowCmdArgs
//
// Register "show" command arguments with the Tcl interpreter.
//
int ShowCmdArgs();

int Cmd_Show(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);
int Cmd_ShowStatus(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);
int Cmd_ShowVersion(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);
int Cmd_ShowPorts(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);
int Cmd_ShowCluster(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);
int Cmd_ShowSecurity(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);
int Cmd_ShowHttp(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);
int Cmd_ShowIcp(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);
int CmdArgs_ShowIcp();
int Cmd_ShowProxy(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);
int Cmd_ShowCache(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);
int CmdArgs_ShowCache();
int Cmd_ShowVirtualIp(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);
int Cmd_ShowHostDb(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);
int Cmd_ShowDnsResolver(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);
int Cmd_ShowLogging(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);
int Cmd_ShowSsl(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);
int Cmd_ShowParents(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);
int CmdArgs_ShowParents();
int Cmd_ShowRemap(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);
int Cmd_ShowSocks(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);
int CmdArgs_ShowSocks();
int Cmd_ShowScheduledUpdate(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);
int CmdArgs_ShowScheduledUpdate();
int Cmd_ShowProxyStats(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);
int Cmd_ShowHttpTransStats(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);
int Cmd_ShowHttpStats(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);
int Cmd_ShowIcpStats(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);
int Cmd_ShowCacheStats(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);
int Cmd_ShowHostDbStats(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);
int Cmd_ShowDnsStats(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);
int Cmd_ShowLoggingStats(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);
int Cmd_ShowAlarms(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);
int Cmd_ShowRadius(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);
int Cmd_ShowNtlm(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);
int Cmd_ShowNtlmStats(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);
int CmdArgs_None();

int getnameserver(char *nameserver, int len);
int getrouter(char *router, int len);

////////////////////////////////////////////////////////////////
//
// "show" sub-command implementations
//
////////////////////////////////////////////////////////////////

// show status sub-command
int ShowStatus();

// show version sub-command
int ShowVersion();

// show ports sub-command
int ShowPorts();

// show cluster sub-command
int ShowCluster();

// show security sub-command
int ShowSecurity();

// show http sub-command
int ShowHttp();

// show icp sub-command
int ShowIcp();

// show icp peer sub-command
int ShowIcpPeer();

// show proxy sub-command
int ShowProxy();

// show cache sub-command
int ShowCache();

// show cache rules sub-command
int ShowCacheRules();

// show cache storage sub-command
int ShowCacheStorage();

// show virtual-ip sub-command
int ShowVirtualIp();

// show hostdb sub-command
int ShowHostDb();

// show dns-resolver sub-command
int ShowDnsResolver();

// show logging sub-command
int ShowLogging();

// show ssl sub-command
int ShowSsl();

// show parents sub-command
int ShowParents();

// show:parent rules sub-command
int ShowParentRules();

// show remap sub-command
int ShowRemap();

// show socks sub-command
int ShowSocks();

// show:socks rules sub-command
int ShowSocksRules();

// show scheduled-update sub-command
int ShowScheduledUpdate();

// show:scheduled-update rules sub-command
int ShowScheduledUpdateRules();

////////////////////////////////////////////////////////////////
// statistics sub-commands
//

// show proxy-stats sub-command
int ShowProxyStats();

// show proxy-stats sub-command
int ShowHttpTransStats();

// show proxy-stats sub-command
int ShowHttpStats();

// show proxy-stats sub-command
int ShowIcpStats();

// show proxy-stats sub-command
int ShowCacheStats();

// show proxy-stats sub-command
int ShowHostDbStats();

// show proxy-stats sub-command
int ShowDnsStats();

// show proxy-stats sub-command
int ShowLoggingStats();

// show:alarms sub-command
int ShowAlarms();

#endif // __SHOW_CMD_H__
