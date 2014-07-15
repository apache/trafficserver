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
 * Filename: ConfigCmd.h
 * Purpose: This file contains the CLI's "config" command definitions.
 *
 *
 ****************************************************************/

#include "mgmtapi.h"
#include <tcl.h>
#include "createArgument.h"
#include "definitions.h"

#ifndef __CONFIG_CMD_H__
#define __CONFIG_CMD_H__


// enumerated type which captures all "config" commands
typedef enum
{
  CMD_CONFIG_GET = 100,
  CMD_CONFIG_SET,
  CMD_CONFIG_SET_VALUE,
  CMD_ENABLE_STATUS,
  CMD_CONFIG_NAME,
  CMD_CONFIG_RESTART_CLUSTER,
  CMD_CONFIG_PORTS,
  CMD_CONFIG_PORTS_HTTP_SERVER,
  CMD_CONFIG_PORTS_CLUSTER,
  CMD_CONFIG_PORTS_CLUSTER_RS,
  CMD_CONFIG_PORTS_CLUSTER_MC,
  CMD_CONFIG_PORTS_CONNECT,
  CMD_CONFIG_PORTS_SOCKS_SERVER,
  CMD_CONFIG_PORTS_ICP,
  CMD_CONFIG_DATE,
  CMD_CONFIG_TIME,
  CMD_CONFIG_TIMEZONE,
  CMD_CONFIG_TIMEZONE_LIST,
  CMD_HALT,
  CMD_REBOOT,
  CMD_CONFIG_START,
  CMD_CONFIG_STOP,
  CMD_CONFIG_WRITE,
  CMD_CONFIG_WRITE_IFC_HEAD,
  CMD_CONFIG_WRITE_TS_VERSION,
  CMD_CONFIG_WRITE_BUILD_DATE,
  CMD_CONFIG_WRITE_PLATFORM,
  CMD_CONFIG_WRITE_NODES,
  CMD_CONFIG_WRITE_FEATURE,
  CMD_CONFIG_WRITE_TAR,
  CMD_CONFIG_WRITE_TAR_INFO,
  CMD_CONFIG_WRITE_FILELIST,
  CMD_CONFIG_WRITE_TAR_COMMON,
  CMD_CONFIG_WRITE_BIN_DIR,
  CMD_CONFIG_WRITE_BIN_GROUP,
  CMD_CONFIG_WRITE_BIN_COMMON,
  CMD_CONFIG_WRITE_LIB_DIR,
  CMD_CONFIG_WRITE_LIB_GROUP,
  CMD_CONFIG_WRITE_LIB_COMMON,
  CMD_CONFIG_WRITE_CONFIG_DIR,
  CMD_CONFIG_WRITE_CONFIG_GROUP,
  CMD_CONFIG_WRITE_CONFIG_COMMON,
  CMD_CONFIG_WRITE_COMMON_FILE,
  CMD_CONFIG_READ,
  CMD_CONFIG_READ_IFC_HEAD,
  CMD_CONFIG_UPGRADE_READ_URL,
  CMD_CONFIG_READ_FEATURE,
  CMD_CONFIG_READ_TAR,
  CMD_CONFIG_READ_TAR_INFO,
  CMD_CONFIG_READ_TAR_COMMON,
  CMD_CONFIG_READ_BIN_DIR,
  CMD_CONFIG_READ_BIN_GROUP,
  CMD_CONFIG_READ_BIN_COMMON,
  CMD_CONFIG_READ_LIB_DIR,
  CMD_CONFIG_READ_LIB_GROUP,
  CMD_CONFIG_READ_LIB_COMMON,
  CMD_CONFIG_READ_CONFIG_DIR,
  CMD_CONFIG_READ_CONFIG_GROUP,
  CMD_CONFIG_READ_CONFIG_COMMON,
  CMD_CONFIG_READ_COMMON_FILE,
  CMD_CONFIG_FILTER,
  CMD_CONFIG_SECURITY,
  CMD_CONFIG_SECURITY_IP,
  CMD_CONFIG_SECURITY_PASSWORD,
  CMD_CONFIG_PARENTS_STATUS,
  CMD_CONFIG_PARENTS_CACHE,
  CMD_CONFIG_PARENTS_CONFIG_FILE,
  CMD_CONFIG_REMAP,
  CMD_CONFIG_HTTP_STATUS,
  CMD_CONFIG_HTTP_KEEP_ALIVE_TIMEOUT_IN,
  CMD_CONFIG_HTTP_KEEP_ALIVE_TIMEOUT_OUT,
  CMD_CONFIG_HTTP_INACTIVE_TIMEOUT_IN,
  CMD_CONFIG_HTTP_INACTIVE_TIMEOUT_OUT,
  CMD_CONFIG_HTTP_ACTIVE_TIMEOUT_IN,
  CMD_CONFIG_HTTP_ACTIVE_TIMEOUT_OUT,
  CMD_CONFIG_HTTP_REMOVE_FROM,
  CMD_CONFIG_HTTP_REMOVE_REFERER,
  CMD_CONFIG_HTTP_REMOVE_USER,
  CMD_CONFIG_HTTP_REMOVE_COOKIE,
  CMD_CONFIG_HTTP_REMOVE_HEADER,
  CMD_CONFIG_HTTP_GLOBAL_USER_AGENT,
  CMD_CONFIG_HTTP_INSERT_IP,
  CMD_CONFIG_HTTP_REMOVE_IP,
  CMD_CONFIG_HTTP_PROXY,
  CMD_CONFIG_HTTP_FWD,
  CMD_CONFIG_HTTP_REV,
  CMD_CONFIG_HTTP_FWD_REV,
  CMD_CONFIG_ICP,
  CMD_CONFIG_ICP_MODE,
  CMD_CONFIG_ICP_MODE_RECEIVE,
  CMD_CONFIG_ICP_MODE_SENDRECEIVE,
  CMD_CONFIG_ICP_MODE_DISABLED,
  CMD_CONFIG_ICP_PORT,
  CMD_CONFIG_ICP_MCAST,
  CMD_CONFIG_ICP_QTIMEOUT,
  CMD_CONFIG_ICP_PEERS,
  CMD_CONFIG_PORT_TUNNELS_SERVER_OTHER_PORTS,
  CMD_CONFIG_SCHEDULED_UPDATE_STATUS,
  CMD_CONFIG_SCHEDULED_UPDATE_RETRY_COUNT,
  CMD_CONFIG_SCHEDULED_UPDATE_RETRY_INTERVAL,
  CMD_CONFIG_SCHEDULED_UPDATE_MAX_CONCURRENT,
  CMD_CONFIG_SCHEDULED_UPDATE_FORCE_IMMEDIATE,
  CMD_CONFIG_SCHEDULED_UPDATE_RULES,
  CMD_CONFIG_SOCKS_STATUS,
  CMD_CONFIG_SOCKS_VERSION,
  CMD_CONFIG_SOCKS_DEFAULT_SERVERS,
  CMD_CONFIG_SOCKS_ACCEPT,
  CMD_CONFIG_SOCKS_ACCEPT_PORT,
  CMD_CONFIG_CACHE,
  CMD_CONFIG_CACHE_ON,
  CMD_CONFIG_CACHE_OFF,
  CMD_CONFIG_CACHE_HTTP,
  CMD_CONFIG_CACHE_CLUSTER_BYPASS,
  CMD_CONFIG_CACHE_IGNORE_BYPASS,
  CMD_CONFIG_CACHE_MAX_OBJECT_SIZE,
  CMD_CONFIG_CACHE_MAX_ALTERNATES,
  CMD_CONFIG_CACHE_FILE,
  CMD_CONFIG_CACHE_FRESHNESS,
  CMD_CONFIG_CACHE_FRESHNESS_VERIFY,
  CMD_CONFIG_CACHE_FRESHNESS_VERIFY_WHEN_EXPIRED,
  CMD_CONFIG_CACHE_FRESHNESS_VERIFY_NO_DATE,
  CMD_CONFIG_CACHE_FRESHNESS_VERIFY_ALWALYS,
  CMD_CONFIG_CACHE_FRESHNESS_VERIFY_NEVER,
  CMD_CONFIG_CACHE_FRESHNESS_MINIMUM,
  CMD_CONFIG_CACHE_FRESHNESS_MINIMUM_EXPLICIT,
  CMD_CONFIG_CACHE_FRESHNESS_MINIMUM_LAST_MODIFIED,
  CMD_CONFIG_CACHE_FRESHNESS_MINIMUM_NOTHING,
  CMD_CONFIG_CACHE_FRESHNESS_NO_EXPIRE_LIMIT,
  CMD_CONFIG_CACHE_FRESHNESS_NO_EXPIRE_LIMIT_GREATER_THAN,
  CMD_CONFIG_CACHE_FRESHNESS_NO_EXPIRE_LIMIT_LESS_THAN,
  CMD_CONFIG_CACHE_DYNAMIC,
  CMD_CONFIG_CACHE_ALTERNATES,
  CMD_CONFIG_CACHE_VARY,
  CMD_CONFIG_CACHE_VARY_TEXT,
  CMD_CONFIG_CACHE_VARY_COOKIES_IMAGES,
  CMD_CONFIG_CACHE_VARY_OTHER,
  CMD_CONFIG_CACHE_COOKIES,
  CMD_CONFIG_CACHE_COOKIES_NONE,
  CMD_CONFIG_CACHE_COOKIES_ALL,
  CMD_CONFIG_CACHE_COOKIES_NON_TEXT,
  CMD_CONFIG_CACHE_COOKIES_NON_TEXT_EXT,
  CMD_CONFIG_CACHE_CLEAR,
  CMD_CONFIG_HOSTDB,
  CMD_CONFIG_HOSTDB_LOOKUP_TIMEOUT,
  CMD_CONFIG_HOSTDB_FOREGROUND_TIMEOUT,
  CMD_CONFIG_HOSTDB_BACKGROUND_TIMEOUT,
  CMD_CONFIG_HOSTDB_INVALID_HOST_TIMEOUT,
  CMD_CONFIG_HOSTDB_RE_DNS_ON_RELOAD,
  CMD_CONFIG_HOSTDB_CLEAR,
  CMD_CONFIG_DNS,
  CMD_CONFIG_DNS_RESOLVE_TIMEOUT,
  CMD_CONFIG_DNS_RETRIES,
  CMD_CONFIG_VIRTUALIP,
  CMD_CONFIG_VIRTUALIP_STATUS,
  CMD_CONFIG_VIRTUALIP_LIST,
  CMD_CONFIG_VIRTUALIP_ADD,
  CMD_CONFIG_VIRTUALIP_ADD_IP,
  CMD_CONFIG_VIRTUALIP_ADD_DEVICE,
  CMD_CONFIG_VIRTUALIP_ADD_SUBINTERFACE,
  CMD_CONFIG_VIRTUALIP_DELETE,
  CMD_CONFIG_LOGGING_ON,
  CMD_CONFIG_LOGGING_OFF,
  CMD_CONFIG_LOGGING_EVENT,
  CMD_CONFIG_LOGGING_EVENT_ENABLED,
  CMD_CONFIG_LOGGING_EVENT_TRANS_ONLY,
  CMD_CONFIG_LOGGING_EVENT_ERROR_ONLY,
  CMD_CONFIG_LOGGING_EVENT_DISABLED,
  CMD_CONFIG_LOGGING_MGMT_DIRECTORY,
  CMD_CONFIG_LOGGING_SPACE_LIMIT,
  CMD_CONFIG_LOGGING_SPACE_HEADROOM,
  CMD_CONFIG_LOGGING_COLLATION_STATUS,
  CMD_CONFIG_LOGGING_COLLATION_STATUS_INACTIVE,
  CMD_CONFIG_LOGGING_COLLATION_STATUS_HOST,
  CMD_CONFIG_LOGGING_COLLATION_STATUS_SEND_STANDARD,
  CMD_CONFIG_LOGGING_COLLATION_STATUS_SEND_CUSTOM,
  CMD_CONFIG_LOGGING_COLLATION_STATUS_SEND_ALL,
  CMD_CONFIG_LOGGING_COLLATION_HOST,
  CMD_CONFIG_LOGGING_COLLATION,
  CMD_CONFIG_LOGGING_COLLATION_SECRET,
  CMD_CONFIG_LOGGING_COLLATION_TAGGED,
  CMD_CONFIG_LOGGING_COLLATION_ORPHAN_LIMIT,
  CMD_CONFIG_LOGGING_AND_CUSTOM_FORMAT,
  CMD_CONFIG_LOGGING_FORMAT_SQUID,
  CMD_CONFIG_LOGGING_FORMAT_NETSCAPE_COMMON,
  CMD_CONFIG_LOGGING_FORMAT_NETSCAPE_EXT,
  CMD_CONFIG_LOGGING_FORMAT_NETSCAPE_EXT2,
  CMD_CONFIG_LOGGING_TYPE,
  CMD_CONFIG_LOGGING_TYPE_ASCII,
  CMD_CONFIG_LOGGING_TYPE_BINARY,
  CMD_CONFIG_LOGGING_FILE,
  CMD_CONFIG_LOGGING_HEADER,
  CMD_CONFIG_LOGGING_SPLITTING,
  CMD_CONFIG_LOGGING_SPLITTING_ICP,
  CMD_CONFIG_LOGGING_SPLITTING_HTTP,
  CMD_CONFIG_LOGGING_CUSTOM,
  CMD_CONFIG_LOGGING_ROLLING,
  CMD_CONFIG_LOGGING_OFFSET,
  CMD_CONFIG_LOGGING_INTERVAL,
  CMD_CONFIG_LOGGING_AUTO_DELETE,
  CMD_CONFIG_SSL,
  CMD_CONFIG_SSL_STATUS,
  CMD_CONFIG_SSL_PORT,
  CMD_CONFIG_IP_ADDRESS,
  CMD_CONFIG_HOSTNAME,
  CMD_CONFIG_NETMASK,
  CMD_CONFIG_DOMAIN,
  CMD_CONFIG_DNS_IP,
  CMD_CONFIG_DEFAULT_ROUTER,
  CMD_CONFIG_NETWORK_INT,
  CMD_CONFIG_NETWORK_STATUS,
  CMD_CONFIG_NETWORK_START,
  CMD_CONFIG_NETWORK_PROTOCOL,
  CMD_CONFIG_GATEWAY,
  CMD_CONFIG_ALARM_RESOLVE_NAME,
  CMD_CONFIG_ALARM_RESOLVE_NUMBER,
  CMD_CONFIG_ALARM_RESOLVE_ALL,
  CMD_CONFIG_ALARM_NOTIFY
} cliConfigCommand;

typedef struct DateTime
{
  char str_hh[16];
  char str_min[16];
  char str_ss[16];
  char str_dd[16];
  char str_mm[16];
  char str_yy[16];
} DateTime;

////////////////////////////////////////////////////////////////
// ConfigCmd
//
// This is the callback function for the "config" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int Cmd_Config(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);

////////////////////////////////////////////////////////////////
// ConfigCmdArgs
//
// Register "config" command arguments with the Tcl interpreter.
//
int CmdArgs_Config();

////////////////////////////////////////////////////////////////
// Cmd_ConfigGet
//
// This is the callback function for the "config:get" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int Cmd_ConfigGet(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);

////////////////////////////////////////////////////////////////
// Cmd_ConfigSet
//
// This is the callback function for the "config:set" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int Cmd_ConfigSet(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);

////////////////////////////////////////////////////////////////
// Cmd_ConfigName
//
// This is the callback function for the "config:name" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int Cmd_ConfigName(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);

////////////////////////////////////////////////////////////////
// Cmd_ConfigStart
//
// This is the callback function for the "config:start" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int Cmd_ConfigStart(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);

////////////////////////////////////////////////////////////////
// Cmd_ConfigStop
//
// This is the callback function for the "config:stop" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int Cmd_ConfigStop(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);

int Cmd_ConfigHardRestart(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);

int Cmd_ConfigRestart(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);
int CmdArgs_ConfigRestart();

////////////////////////////////////////////////////////////////
// Cmd_ConfigParents
//
// This is the callback function for the "config:parents" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int Cmd_ConfigParents(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigParents
//
// Register "config:parents" arguments with the Tcl interpreter.
//
int CmdArgs_ConfigParents();

////////////////////////////////////////////////////////////////
// Cmd_ConfigRemap
//
// This is the callback function for the "config:remap" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int Cmd_ConfigRemap(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);

////////////////////////////////////////////////////////////////
// Cmd_ConfigPorts
//
// This is the callback function for the "config:ports" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int Cmd_ConfigPorts(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigPorts
//
// Register "config:ports" arguments with the Tcl interpreter.
//
int CmdArgs_ConfigPorts();

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigPortTunnles
//
// Register "config:PortTunnles" arguments with the Tcl interpreter.
//
int CmdArgs_ConfigPortTunnels();


////////////////////////////////////////////////////////////////
// Cmd_ConfigScheduledUpdate
//
// This is the callback function for the "config:scheduled-update" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int Cmd_ConfigScheduledUpdate(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigScheduled-Update
//
// Register "config:Scheduled-Update" arguments with the Tcl interpreter.
//
int CmdArgs_ConfigScheduledUpdate();

////////////////////////////////////////////////////////////////
// Cmd_ConfigSocks
//
// This is the callback function for the "config:scheduled-update" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int Cmd_ConfigSocks(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);
////////////////////////////////////////////////////////////////
// CmdArgs_ConfigSocks
//
// Register "config:socks" arguments with the Tcl interpreter.
//
int CmdArgs_ConfigSocks();

////////////////////////////////////////////////////////////////
// Cmd_ConfigSecurity
//
// This is the callback function for the "config:security" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int Cmd_ConfigSecurity(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigSecurity
//
// Register "config:security" arguments with the Tcl interpreter.
//
int CmdArgs_ConfigSecurity();

////////////////////////////////////////////////////////////////
// Cmd_ConfigHttp
//
// This is the callback function for the "config:http" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int Cmd_ConfigHttp(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigHttp
//
// Register "config:http" arguments with the Tcl interpreter.
//
int CmdArgs_ConfigHttp();

////////////////////////////////////////////////////////////////
// Cmd_ConfigIcp
//
// This is the callback function for the "config:icp" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int Cmd_ConfigIcp(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigIcp
//
// Register "config:Icp" arguments with the Tcl interpreter.
//
int CmdArgs_ConfigIcp();

////////////////////////////////////////////////////////////////
//
// Cmd_ConfigHostdb
// This is the callback function for the "config:hostdb" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//

////////////////////////////////////////////////////////////////
// Cmd_ConfigCache
//
// This is the callback function for the "config:cache" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int Cmd_ConfigCache(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigCache
//
// Register "config:cache" arguments with the Tcl interpreter.
//
int CmdArgs_ConfigCache();


int Cmd_ConfigHostdb(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigHostdb
//
// Register "config:hostdb" arguments with the Tcl interpreter.
//
int CmdArgs_ConfigHostdb();

////////////////////////////////////////////////////////////////
// Cmd_ConfigDns
//
// This is the callback function for the "config:dns" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int Cmd_ConfigDns(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigDns
//
// Register "config:dns" arguments with the Tcl interpreter.
//
int CmdArgs_ConfigDns();

////////////////////////////////////////////////////////////////
// Cmd_ConfigVirtualip
//
// This is the callback function for the "config:virtualip" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int Cmd_ConfigVirtualip(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);
////////////////////////////////////////////////////////////////
// CmdArgs_ConfigVirtualip
//
// Register "config:virtualip" arguments with the Tcl interpreter.
//
int CmdArgs_ConfigVirtualip();

////////////////////////////////////////////////////////////////
// Cmd_ConfigLogging
//
// This is the callback function for the "config:logging" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int Cmd_ConfigLogging(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigLogging
//
// Register "config:logging" arguments with the Tcl interpreter.
//
int CmdArgs_ConfigLogging();
// Cmd_ConfigSsl
//
// This is the callback function for the "config:ssl" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int Cmd_ConfigSsl(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigSsl
//
// Register "config:ssl" arguments with the Tcl interpreter.
//
int CmdArgs_ConfigSsl();

////////////////////////////////////////////////////////////////
// Functions to implement network settings
//
int IsValidHostname(char *str);
int IsValidFQHostname(char *str);
int IsValidDomainname(char *str);
int IsValidIpAddress(char *str);
#if defined(solaris)
int getnetmask(char *mask);
#endif
char *pos_after_string(char const *haystack, char const *needle);
int StartBinary(char *abs_bin_path, char *bin_options, int isScript);
int getrouter(char *router, int len);
int getnameserver(char *nameserver, int len);
int setnameserver(char *nameserver);


////////////////////////////////////////////////////////////////
// Cmd_ConfigAlarm
//
// This is the callback function for the "config:alarm" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int Cmd_ConfigAlarm(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigAlarm
//
// Register "config:alarm" arguments with the Tcl interpreter.
//
int CmdArgs_ConfigAlarm();

////////////////////////////////////////////////////////////////
// Cmd_Enable
//
// This is the callback function for the "enable" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int Cmd_Enable(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigParents
//
// Register "config:parents" arguments with the Tcl interpreter.
//
int CmdArgs_Enable();

int cliCheckIfEnabled(char *command);

////////////////////////////////////////////////////////////////
// Cmd_Disable
//
// This is the callback function for the "enable" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int Cmd_Disable(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);

////////////////////////////////////////////////////////////////
// Cmd_Help
//
// This is the callback function for the "help" command. It displays the
// online help for the requested topic(s).
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int Cmd_Help(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);


////////////////////////////////////////////////////////////////
//
// "config" sub-command implementations
//
////////////////////////////////////////////////////////////////

// config start sub-command
int ConfigStart();

// config stop sub-command
int ConfigStop();

// config get sub-command
int ConfigGet(const char *rec_name);

// config set sub-command
int ConfigSet(const char *rec_name, const char *rec_value);

// config name sub-command
int ConfigName(const char *proxy_name);

// config ports sub-command
int ConfigPortsSet(int arg_ref, void *valuePtr);
int ConfigPortsGet(int arg_ref);

// config Date sub-command
int ConfigDate(char *datestr);

// config Time sub-command
int ConfigTime(char *timestr);

// config Timezone sub-command
int ConfigTimezone(int, int);

//config Timezone List
int ConfigTimezoneList();

// config security password sub-command
int ConfigSecurityPasswd();

// config remap sub-command
int ConfigRemap(const char *url);

// config http proxy sub-command
int ConfigHttpProxy(int arg_ref, int setvar);

// config icp mode sub-command
int ConfigIcpMode(int arg_ref, int setvar);

// config Cache Freshness Verify sub-command
int ConfigCacheFreshnessVerify(int arg_ref, int setvar);

// config Cache Freshness Minimum sub-command
int ConfigCacheFreshnessMinimum(int arg_ref, int setvar);

// config Cache FreshnessNoExpireLimit
int ConfigCacheFreshnessNoExpireLimit(TSInt min, TSInt max, int setvar);

// config Cache Vary sub-command
int ConfigCacheVary(int arg_ref, char *field, int setvar);

// config Cache Cookies sub-command
int ConfigCacheCookies(int arg_ref, int setvar);

// config Cache Clear sub-command
int ConfigCacheClear();

// config HostDB Clear sub-command
int ConfigHostdbClear();

//config virtualip list
int ConfigVirtualIpList();

//config virtualip add
int ConfigVirtualipAdd(char *ip, char *device, int subinterface, int set_var);

//config virtualip delete
int ConfigVirtualipDelete(int ip_no, int set_var);

// config Logging Event sub-command
int ConfigLoggingEvent(int arg_ref, int setvar);

// config Logging collation status sub-command
int ConfigLoggingCollationStatus(int arg_ref, int setvar);

// config Logging collation sub-command
int ConfigLoggingCollation(TSString secret, int arg_ref, TSInt orphan, int setvar);

// config Logging Format Type File sub-command
int ConfigLoggingFormatTypeFile(int arg_ref_format, int arg_ref,
                                int arg_ref_type, TSString file, TSString header, int setvar);

// config Logging splitting sub-command
int ConfigLoggingSplitting(int arg_ref_protocol, int arg_ref_on_off, int setvar);

// config Logging Custom Format sub-command
int ConfigLoggingCustomFormat(int arg_ref_on_off, int arg_ref_format, int setvar);

// config Logging rolling offset interval autodelete sub-command
int ConfigLoggingRollingOffsetIntervalAutodelete(int arg_ref_rolling, TSInt offset, TSInt num_hours,
                                                 int arg_ref_auto_delete, int setvar);

// config icp peers sub-command
int ConfigIcpPeers(char *url);

// config:alarm resolve-name
int ConfigAlarmResolveName(char *name);

// config:alarm resolve-number
int ConfigAlarmResolveNumber(int number);

// config:alarm resolve-all
int ConfigAlarmResolveAll();

// config:alarm notify
int ConfigAlarmNotify(char *stringval);

#endif // __CONFIG_CMD_H__
