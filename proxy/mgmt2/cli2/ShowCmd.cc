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
 * Filename: ShowCmd.cc
 * Purpose: This file contains the "show" command implementation.
 *
 * 
 ****************************************************************/

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "ShowCmd.h"
#include "ConfigCmd.h"
#include "createArgument.h"
#include "CliMgmtUtils.h"
#include "CliDisplay.h"
#include "ink_defs.h"
#include "ink_string.h"
#include <SysAPI.h>

////////////////////////////////////////////////////////////////
// Cmd_Show
//
// This is the callback function for the "show" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_Show(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  char *cmdinfo, *temp;
  int i = 0;
  Cli_Debug("Cmd_Show\n");
  Tcl_Eval(interp, "info commands show* ");

  int cmdinfo_size = sizeof(char) * (strlen(interp->result) + 2);
  cmdinfo = (char *) malloc(cmdinfo_size);
  ink_strncpy(cmdinfo, interp->result, cmdinfo_size);
  int temp_size = sizeof(char) * (strlen(cmdinfo) + 20);
  temp = (char *) malloc(temp_size);
  ink_strncpy(temp, "lsort \"", temp_size);
  strncat(temp, cmdinfo, temp_size - strlen(temp) - 1);
  strncat(temp, "\"", temp_size - strlen(temp) - 1);
  Tcl_Eval(interp, temp);
  ink_strncpy(cmdinfo, interp->result, cmdinfo_size);
  i = i + strlen("show ");
  while (cmdinfo[i] != 0) {
    if (cmdinfo[i] == ' ') {
      cmdinfo[i] = '\n';
    }
    i++;
  }
  cmdinfo[i] = '\n';
  i++;
  cmdinfo[i] = 0;
  Cli_Printf("Following are the available show commands\n");
  Cli_Printf(cmdinfo + strlen("show "));
  free(cmdinfo);
  free(temp);
  return 0;

}

////////////////////////////////////////////////////////////////
// Cmd_ShowStatus
//
// This is the callback function for the "show:status" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowStatus(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowStatus\n");

  return (ShowStatus());
}

////////////////////////////////////////////////////////////////
// Cmd_ShowVersion
//
// This is the callback function for the "show:version" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowVersion(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowVersion\n");

  return (ShowVersion());
}

////////////////////////////////////////////////////////////////
// Cmd_ShowPorts
//
// This is the callback function for the "show:ports" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowPorts(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowPorts\n");

  return (ShowPorts());
}


////////////////////////////////////////////////////////////////
// Cmd_ShowCluster
//
// This is the callback function for the "show:security" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowCluster(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowCluster\n");

  return (ShowCluster());
}

////////////////////////////////////////////////////////////////
// Cmd_ShowSecurity
//
// This is the callback function for the "show:security" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowSecurity(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowSecurity\n");

  return (ShowSecurity());
}

////////////////////////////////////////////////////////////////
// Cmd_ShowHttp
//
// This is the callback function for the "show:http" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowHttp(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowHttp\n");

  return (ShowHttp());
}

////////////////////////////////////////////////////////////////
// Cmd_ShowNntp
//
// This is the callback function for the "show:nntp" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowNntp(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  INKString plugin_name = NULL;
  INKError status = INK_ERR_OKAY;
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowNntp\n");
  status = Cli_RecordGetString("proxy.config.nntp.plugin_name", &plugin_name);
  if (status != INK_ERR_OKAY) {
    return status;
  }
  if (Cli_CheckPluginStatus(plugin_name) != CLI_OK) {
    Cli_Printf("NNTP is not installed.\n\n");
    return CMD_ERROR;
  };

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;

  if (argtable[0].parsed_args != CLI_PARSED_ARGV_END) {
    if (argtable[0].parsed_args == CMD_SHOW_NNTP_CONFIG) {
      return (ShowNntpConfig());
    }
#if 0
    if (argtable[0].parsed_args == CMD_SHOW_NNTP_SERVERS) {
      return (ShowNntpServers());
    } else if (argtable[0].parsed_args == CMD_SHOW_NNTP_ACCESS) {
      return (ShowNntpAccess());
    }
#endif
    Cli_Error(ERR_INVALID_COMMAND);
    return CMD_ERROR;
  }
  return (ShowNntp());
}

int
CmdArgs_ShowNntp()
{
  createArgument("config-xml", 1, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_SHOW_NNTP_CONFIG, "NNTP Configuration", (char *) NULL);
#if 0
  createArgument("servers", 1, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_SHOW_NNTP_SERVERS, "NNTP Server Configuration", (char *) NULL);
  createArgument("access", 1, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_SHOW_NNTP_ACCESS, "NNTP Access Configuration", (char *) NULL);
#endif
  return 0;
}

////////////////////////////////////////////////////////////////
// Cmd_ShowFtp
//
// This is the callback function for the "show:ftp" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowFtp(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowFtp\n");

  return (ShowFtp());
}

////////////////////////////////////////////////////////////////
// Cmd_ShowIcp
//
// This is the callback function for the "show:icp" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowIcp(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowIcp argc %d\n", argc);

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;

  if (argtable[0].parsed_args != CLI_PARSED_ARGV_END) {
    if (argtable[0].parsed_args == CMD_SHOW_ICP_PEER) {
      return (ShowIcpPeer());
    }
    Cli_Error(ERR_INVALID_COMMAND);
    return CMD_ERROR;
  }
  return (ShowIcp());
}

////////////////////////////////////////////////////////////////
// CmdArgs_ShowIcp
//
// Register "show:icp" arguments with the Tcl interpreter.
//
int
CmdArgs_ShowIcp()
{
  createArgument("peers", 1, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_SHOW_ICP_PEER, "ICP Peer Configuration", (char *) NULL);

  return 0;
}

////////////////////////////////////////////////////////////////
// Cmd_ShowProxy
//
// This is the callback function for the "show:proxy" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowProxy(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowProxy\n");

  return (ShowProxy());
}

////////////////////////////////////////////////////////////////
// Cmd_ShowCache
//
// This is the callback function for the "show:cache" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowCache(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowCache\n");

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;

  if (argtable[0].parsed_args != CLI_PARSED_ARGV_END) {
    if (argtable[0].parsed_args == CMD_SHOW_CACHE_RULES) {
      return (ShowCacheRules());
    } else if (argtable[0].parsed_args == CMD_SHOW_CACHE_STORAGE) {
      return (ShowCacheStorage());
    }

    Cli_Error(ERR_INVALID_COMMAND);
    return CMD_ERROR;
  }
  return (ShowCache());
}

////////////////////////////////////////////////////////////////
// CmdArgs_ShowCache
//
// Register "show:cache" arguments with the Tcl interpreter.
//
int
CmdArgs_ShowCache()
{
  createArgument("rules", 1, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_SHOW_CACHE_RULES, "Rules from cache.config", (char *) NULL);
  createArgument("storage", 1, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_SHOW_CACHE_STORAGE, "Rules from storage.config", (char *) NULL);

  return 0;
}

////////////////////////////////////////////////////////////////
// Cmd_ShowVirtualIp
//
// This is the callback function for the "show:virtual-ip" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowVirtualIp(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowVirtualIp\n");

  return (ShowVirtualIp());
}

////////////////////////////////////////////////////////////////
// Cmd_ShowHostDb
//
// This is the callback function for the "show:hostdb" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowHostDb(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowHostDb\n");

  return (ShowHostDb());
}

////////////////////////////////////////////////////////////////
// Cmd_ShowDnsResolver
//
// This is the callback function for the "show:dns-resolver" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowDnsResolver(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowDnsResolver\n");

  return (ShowDnsResolver());
}

////////////////////////////////////////////////////////////////
// Cmd_ShowLogging
//
// This is the callback function for the "show:logging" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowLogging(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowLogging\n");

  return (ShowLogging());
}

////////////////////////////////////////////////////////////////
// Cmd_ShowSsl
//
// This is the callback function for the "show:ssl" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowSsl(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowSsl\n");

  return (ShowSsl());
}

////////////////////////////////////////////////////////////////
// Cmd_ShowFilter
//
// This is the callback function for the "show:filter" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowFilter(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowFilter\n");

  return (ShowFilter());
}

////////////////////////////////////////////////////////////////
// Cmd_ShowParents
//
// This is the callback function for the "show:parents" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowParents(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowParents\n");

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;

  if (argtable[0].parsed_args != CLI_PARSED_ARGV_END) {
    if (argtable[0].parsed_args == CMD_SHOW_PARENT_RULES) {
      return (ShowParentRules());
    }

    Cli_Error(ERR_INVALID_COMMAND);
    return CMD_ERROR;
  }

  return (ShowParents());
}

////////////////////////////////////////////////////////////////
// CmdArgs_ShowParents
//
// Register "show:parents" arguments with the Tcl interpreter.
//
int
CmdArgs_ShowParents()
{
  createArgument("rules", 1, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_SHOW_PARENT_RULES, "Display parent.config rules file", (char *) NULL);
  return 0;
}

////////////////////////////////////////////////////////////////
// Cmd_ShowRemap
//
// This is the callback function for the "show:remap" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowRemap(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowRemap\n");

  return (ShowRemap());
}


////////////////////////////////////////////////////////////////
// Cmd_ShowSnmp
//
// This is the callback function for the "show:snmp" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowSnmp(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowSnmp\n");

  return (ShowSnmp());
}

////////////////////////////////////////////////////////////////
// Cmd_ShowLdap
//
// This is the callback function for the "show:ldap" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowLdap(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowLdap\n");

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;

  if (argtable[0].parsed_args != CLI_PARSED_ARGV_END) {
    if (argtable[0].parsed_args == CMD_SHOW_LDAP_RULES) {
      return (ShowLdapRules());
    }

    Cli_Error(ERR_INVALID_COMMAND);
    return CMD_ERROR;
  }

  return (ShowLdap());
}

////////////////////////////////////////////////////////////////
// CmdArgs_ShowLdap
//
// Register "show:ldap" arguments with the Tcl interpreter.
//
int
CmdArgs_ShowLdap()
{
  createArgument("rules", 1, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_SHOW_LDAP_RULES, "Display filter.config rules file (used for LDAP configuration)",
                 (char *) NULL);
  return 0;
}


////////////////////////////////////////////////////////////////
// Cmd_ShowLdapstats
//
// This is the callback function for the "show:ldap-stats" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowLdapStats(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowLdapStats\n");

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;

  return (ShowLdapStats());

}


////////////////////////////////////////////////////////////////
// Cmd_ShowSocks
//
// This is the callback function for the "show:socks" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowSocks(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowSocks\n");

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;

  if (argtable[0].parsed_args != CLI_PARSED_ARGV_END) {
    if (argtable[0].parsed_args == CMD_SHOW_SOCKS_RULES) {
      return (ShowSocksRules());
    }

    Cli_Error(ERR_INVALID_COMMAND);
    return CMD_ERROR;
  }

  return (ShowSocks());
}

////////////////////////////////////////////////////////////////
// CmdArgs_ShowSocks
//
// Register "show:socks" arguments with the Tcl interpreter.
//
int
CmdArgs_ShowSocks()
{
  createArgument("rules", 1, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_SHOW_SOCKS_RULES, "Display socks.config rules file", (char *) NULL);
  return 0;
}

////////////////////////////////////////////////////////////////
// Cmd_ShowPortTunnels
//
// This is the callback function for the "show:port-tunnels" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowPortTunnels(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowPortTunnels\n");

  return (ShowPortTunnels());
}

////////////////////////////////////////////////////////////////
// Cmd_ShowScheduledUpdate
//
// This is the callback function for the "show:scheduled-update" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowScheduledUpdate(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowScheduledUpdate\n");

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;

  if (argtable[0].parsed_args != CLI_PARSED_ARGV_END) {
    if (argtable[0].parsed_args == CMD_SHOW_UPDATE_RULES) {
      return (ShowScheduledUpdateRules());
    }

    Cli_Error(ERR_INVALID_COMMAND);
    return CMD_ERROR;
  }

  return (ShowScheduledUpdate());
}

////////////////////////////////////////////////////////////////
// CmdArgs_ShowScheduledUpdate
//
// Register "show:scheduled-update" arguments with the Tcl interpreter.
//
int
CmdArgs_ShowScheduledUpdate()
{
  createArgument("rules", 1, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_SHOW_UPDATE_RULES, "Display update.config rules file", (char *) NULL);
  return 0;
}

////////////////////////////////////////////////////////////////
// Cmd_ShowProxyStats
//
// This is the callback function for the "show:proxy-stats" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowProxyStats(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowProxyStats\n");

  return (ShowProxyStats());
}

////////////////////////////////////////////////////////////////
// Cmd_ShowHttpTransStats
//
// This is the callback function for the "show:http-trans-stats" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowHttpTransStats(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowHttpTransStats\n");

  return (ShowHttpTransStats());
}

////////////////////////////////////////////////////////////////
// Cmd_ShowHttpStats
//
// This is the callback function for the "show:http-stats" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowHttpStats(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowHttpStats\n");

  return (ShowHttpStats());
}

////////////////////////////////////////////////////////////////
// Cmd_ShowNntpStats
//
// This is the callback function for the "show:nntp-stats" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowNntpStats(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  INKString plugin_name = NULL;
  INKError status = INK_ERR_OKAY;
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowNntpStats\n");

  status = Cli_RecordGetString("proxy.config.nntp.plugin_name", &plugin_name);
  if (status != INK_ERR_OKAY) {
    return status;
  }
  if (Cli_CheckPluginStatus(plugin_name) != CLI_OK) {
    Cli_Printf("NNTP is not installed.\n\n");
    return CMD_ERROR;
  };

  return (ShowNntpStats());
}

////////////////////////////////////////////////////////////////
// Cmd_ShowFtpStats
//
// This is the callback function for the "show:ftp-stats" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowFtpStats(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowFtpStats\n");

  return (ShowFtpStats());
}

////////////////////////////////////////////////////////////////
// Cmd_ShowIcpStats
//
// This is the callback function for the "show:icp-stats" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowIcpStats(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowIcpStats\n");

  return (ShowIcpStats());
}

////////////////////////////////////////////////////////////////
// Cmd_ShowCacheStats
//
// This is the callback function for the "show:cache-stats" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowCacheStats(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowCacheStats\n");

  return (ShowCacheStats());
}

////////////////////////////////////////////////////////////////
// Cmd_ShowHostDbStats
//
// This is the callback function for the "show:hostdb-stats" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowHostDbStats(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowHostDbStats\n");

  return (ShowHostDbStats());
}

////////////////////////////////////////////////////////////////
// Cmd_ShowDnsStats
//
// This is the callback function for the "show:dns-stats" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowDnsStats(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowDnsStats\n");

  return (ShowDnsStats());
}

////////////////////////////////////////////////////////////////
// Cmd_ShowLoggingStats
//
// This is the callback function for the "show:logging-stats" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowLoggingStats(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowLoggingStats\n");

  return (ShowLoggingStats());
}

////////////////////////////////////////////////////////////////
// Cmd_ShowAlarms
//
// This is the callback function for the "show:alarms" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowAlarms(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  Cli_Debug("Cmd_ShowAlarms\n");

  return (ShowAlarms());
}

////////////////////////////////////////////////////////////////
// Cmd_ShowNtlmStats
//
int
Cmd_ShowRadius(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  INKInt radius_status = -1;
  INKString pri_host = NULL;
  INKInt pri_port = -1;
  INKString sec_host = NULL;
  INKInt sec_port = -1;

  Cli_RecordGetInt("proxy.config.radius.auth.enabled", &radius_status);
  Cli_RecordGetString("proxy.config.radius.proc.radius.primary_server.name", &pri_host);
  Cli_RecordGetInt("proxy.config.radius.proc.radius.primary_server.auth_port", &pri_port);
  Cli_RecordGetString("proxy.config.radius.proc.radius.secondary_server.name", &sec_host);
  Cli_RecordGetInt("proxy.config.radius.proc.radius.secondary_server.auth_port", &sec_port);

  Cli_Printf("\n");
  Cli_Printf("Radius Authentication -------- %s\n", radius_status == 1 ? "on" : "off");
  Cli_Printf("Primary Hostname ------------- %s\n", pri_host);
  Cli_Printf("Primary Port ----------------- %d\n", pri_port);
  Cli_Printf("Secondary Hostname ----------- %s\n", sec_host);
  Cli_Printf("Secondary Port --------------- %d\n", sec_port);
  Cli_Printf("\n");

  return CLI_OK;
}

////////////////////////////////////////////////////////////////
// Cmd_ShowNtlm
int
Cmd_ShowNtlm(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  INKInt ntlm_status = -1;
  INKString domain_controller = NULL;
  INKString nt_domain = NULL;
  INKInt load_balancing = -1;

  Cli_RecordGetInt("proxy.config.ntlm.auth.enabled", &ntlm_status);
  Cli_RecordGetString("proxy.config.ntlm.dc.list", &domain_controller);
  Cli_RecordGetString("proxy.config.ntlm.nt_domain", &nt_domain);
  Cli_RecordGetInt("proxy.config.ntlm.dc.load_balance", &load_balancing);

  Cli_Printf("\n");
  Cli_Printf("NTLM Authentication ------ %s\n", ntlm_status == 1 ? "on" : "off");
  Cli_Printf("Domain Controller(s) ----- %s\n", domain_controller);
  Cli_Printf("NT Domain ---------------- %s\n", nt_domain);
  Cli_Printf("Load Balancing ----------- %s\n", load_balancing == 1 ? "on" : "off");
  Cli_Printf("\n");

  return CLI_OK;
}

////////////////////////////////////////////////////////////////
// Cmd_ShowNtlmStats
//
// This is the callback function for the "show:ntlm-stats" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowNtlmStats(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  INKInt cache_hits = -1;
  INKInt cache_misses = -1;
  INKInt server_errors = -1;
  INKInt auth_denied = -1;
  INKInt auth_cancelled = -1;

  Cli_RecordGetInt("proxy.process.ntlm.cache.hits", &cache_hits);
  Cli_RecordGetInt("proxy.process.ntlm.cache.misses", &cache_misses);
  Cli_RecordGetInt("proxy.process.ntlm.server.errors", &server_errors);
  Cli_RecordGetInt("proxy.process.ntlm.denied.authorizations", &auth_denied);
  Cli_RecordGetInt("proxy.process.ntlm.cancelled.authentications", &auth_cancelled);

  Cli_Printf("\n");
  Cli_Printf("Cache Hits ----------------- %d\n", cache_hits);
  Cli_Printf("Cache Misses --------------- %d\n", cache_misses);
  Cli_Printf("Server Errors -------------- %d\n", server_errors);
  Cli_Printf("Authorization Denied ------- %d\n", auth_denied);
  Cli_Printf("Authentication Cancelled --- %d\n", auth_cancelled);
  Cli_Printf("\n");

  return CLI_OK;
}

////////////////////////////////////////////////////////////////
// Cmd_ShowNetwork
//
// This is the callback function for the "show:network" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ShowNetwork(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  char hostname[256], ipaddr[32], netmask[32], domainname[256], router[32], dns_ip[32];
  char value[80];

  Cli_Debug("Cmd_ShowNetwork\n");

  memset(hostname, 0, 256);
  memset(ipaddr, 0, 32);
  memset(netmask, 0, 32);
  memset(domainname, 0, 32);
  memset(router, 0, 32);
  memset(dns_ip, 0, 32);

  Net_GetHostname(hostname, sizeof(hostname));
  Cli_Printf("\nHostname ---------------- %s\n", strlen(hostname) ? hostname : "not set");

  Net_GetDefaultRouter(value, sizeof(value));
  Cli_Printf("Default Gateway --------- %s\n", value);

  Net_GetDomain(value, sizeof(value));
  Cli_Printf("Search Domain ----------- %s\n", strlen(value) ? value : "none");

  Net_GetDNS_Servers(value, sizeof(value));
  Cli_Printf("DNS IP Addresses--------- %s\n", strlen(value) ? value : "none");

  char interface[80];
  int num_interfaces = Net_GetNetworkIntCount();
  for (int i = 0; i < num_interfaces; i++) {
    if (Net_GetNetworkInt(i, interface, sizeof(interface))) {
      Cli_Printf("No information for NIC %d\n", i);
      continue;
    }
    Cli_Printf("\nNIC %s\n", interface);

    Net_GetNIC_Status(interface, value, sizeof(value));
    Cli_Printf("  Status ---------------- %s\n", value);

    Net_GetNIC_Start(interface, value, sizeof(value));
    Cli_Printf("  Start on Boot --------- %s\n", value);

    Net_GetNIC_Protocol(interface, value, sizeof(value));
    Cli_Printf("  Start Protocol -------- %s\n", value);

    Net_GetNIC_IP(interface, value, sizeof(value));
    Cli_Printf("  IP Address ------------ %s\n", value);

    Net_GetNIC_Netmask(interface, value, sizeof(value));
    Cli_Printf("  Netmask --------------- %s\n", value);

    Net_GetNIC_Gateway(interface, value, sizeof(value));
    Cli_Printf("  Gateway --------------- %s\n", value);
  }
  Cli_Printf("\n");
  return CLI_OK;
}


////////////////////////////////////////////////////////////////
// CmdArgs_None
//
// Register a command with no arguments with the Tcl interpreter.
//
int
CmdArgs_None()
{
  return 0;
}

////////////////////////////////////////////////////////////////
//
// "show" sub-command implementations
//
////////////////////////////////////////////////////////////////


// show status sub-command
int
ShowStatus()
{
  INKProxyStateT state = INKProxyStateGet();
  Cli_Printf("\n");
  switch (state) {
  case INK_PROXY_ON:
    Cli_Printf("Proxy -- on\n");
    break;
  case INK_PROXY_OFF:
    Cli_Printf("Proxy -- off\n");
    break;
  case INK_PROXY_UNDEFINED:
    Cli_Printf("Proxy status undefined\n");
    break;
  }
  Cli_Printf("\n");

  return CLI_OK;
}

// show version sub-command
int
ShowVersion()
{
  INKError status = INK_ERR_OKAY;
  INKString ts_version = NULL;
  INKString tm_version = NULL;

  status = Cli_RecordGetString("proxy.process.version.server.short", &ts_version);
  status = Cli_RecordGetString("proxy.node.version.manager.short", &tm_version);

  Cli_Printf("\n");
  Cli_Printf("traffic_server version --- %s\n" "traffic_manager version -- %s\n", ts_version, tm_version);
  Cli_Printf("\n");

  return status;
}

// show ports sub-command
int
ShowPorts()
{
  INKInt http_server = -1;
  INKString http_other = NULL;
  INKInt web_interface = -1;
  INKInt overseer = -1;
  INKInt cluster = -1;
  INKInt cluster_rs = -1;
  INKInt cluster_mc = -1;
  INKInt nntp_server = -1;
  INKInt ftp_server = -1;
  INKInt socks_server = -1;
  INKInt icp = -1;
  INKString ssl = NULL;

  // retrieve values

  Cli_RecordGetInt("proxy.config.http.server_port", &http_server);
  Cli_RecordGetString("proxy.config.http.server_other_ports", &http_other);
  Cli_RecordGetInt("proxy.config.admin.web_interface_port", &web_interface);
  Cli_RecordGetInt("proxy.config.admin.overseer_port", &overseer);
  Cli_RecordGetInt("proxy.config.cluster.cluster_port", &cluster);
  Cli_RecordGetInt("proxy.config.cluster.rsport", &cluster_rs);
  Cli_RecordGetInt("proxy.config.cluster.mcport", &cluster_mc);
  Cli_RecordGetInt("proxy.config.nntp.server_port", &nntp_server);
  Cli_RecordGetInt("proxy.config.ftp.proxy_server_port", &ftp_server);
  Cli_RecordGetString("proxy.config.http.ssl_ports", &ssl);
  Cli_RecordGetInt("proxy.config.socks.socks_server_port", &socks_server);
  Cli_RecordGetInt("proxy.config.icp.icp_port", &icp);

  // display results
  Cli_Printf("\n");
  Cli_Printf("HTTP Server Port ------- %d\n", http_server);
  Cli_Printf("HTTP Other Ports ------- %s\n", (http_other != NULL) ? http_other : "none");
  Cli_Printf("Web Interface Port ----- %d\n", web_interface);
  Cli_Printf("Overseer Port ---------- %d\n", overseer);
  Cli_Printf("Cluster Port ----------- %d\n", cluster);
  Cli_Printf("Cluster RS Port -------- %d\n", cluster_rs);
  Cli_Printf("Cluster MC Port -------- %d\n", cluster_mc);
  Cli_Printf("NNTP Server Port ------- %d\n", nntp_server);
  Cli_Printf("FTP Proxy Server Port -- %d\n", ftp_server);
  Cli_Printf("SSL Ports -------------- %s\n", (ssl != NULL) ? ssl : "none");
  Cli_Printf("SOCKS Server Port ------ %d\n", socks_server);
  Cli_Printf("ICP Port --------------- %d\n", icp);
  Cli_Printf("\n");

  return CLI_OK;
}

// show cluster sub-command
int
ShowCluster()
{
  INKInt cluster = -1;
  INKInt cluster_rs = -1;
  INKInt cluster_mc = -1;

  Cli_RecordGetInt("proxy.config.cluster.cluster_port", &cluster);
  Cli_RecordGetInt("proxy.config.cluster.rsport", &cluster_rs);
  Cli_RecordGetInt("proxy.config.cluster.mcport", &cluster_mc);

  Cli_Printf("\n");
  Cli_Printf("Cluster Port ----------- %d\n", cluster);
  Cli_Printf("Cluster RS Port -------- %d\n", cluster_rs);
  Cli_Printf("Cluster MC Port -------- %d\n", cluster_mc);
  Cli_Printf("\n");

  return CLI_OK;
}

// show security sub-command
int
ShowSecurity()
{
  INKInt web_interface = -1;
  INKInt overseer = -1;

  Cli_Printf("\n");
  Cli_RecordGetInt("proxy.config.admin.web_interface_port", &web_interface);
  Cli_RecordGetInt("proxy.config.admin.overseer_port", &overseer);

  Cli_Printf("Web Interface Port ----- %d\n", web_interface);
  Cli_Printf("Overseer Port ---------- %d\n", overseer);
  Cli_Printf("\n");
  Cli_Printf("Traffic Server Access\n" "-------------------\n");
  INKError status = Cli_DisplayRules(INK_FNAME_IP_ALLOW);

  if (status) {
    return status;
  }

  Cli_Printf("\n");
  Cli_Printf("Traffic Manager Access\n" "-------------------\n");
  status = Cli_DisplayRules(INK_FNAME_MGMT_ALLOW);

  return status;
}

// show http sub-command
int
ShowHttp()
{
  // declare and initialize variables

  INKInt http_enabled = -1;
  INKInt keepalive_timeout_in = -1;
  INKInt keepalive_timeout_out = -1;
  INKInt inactivity_timeout_in = -1;
  INKInt inactivity_timeout_out = -1;
  INKInt activity_timeout_in = -1;
  INKInt activity_timeout_out = -1;
  INKInt max_alts = -1;
  INKInt remove_from = -1;
  INKInt remove_referer = -1;
  INKInt remove_user_agent = -1;
  INKInt remove_cookie = -1;
  INKString other_header_list = NULL;
  INKInt insert_client_ip = -1;
  INKInt remove_client_ip = -1;
  INKInt http_server = -1;
  INKString http_other = NULL;
  INKString global_user_agent = NULL;

  // retrieve values

  Cli_RecordGetInt("proxy.config.http.cache.http", &http_enabled);
  Cli_RecordGetInt("proxy.config.http.keep_alive_no_activity_timeout_in", &keepalive_timeout_in);
  Cli_RecordGetInt("proxy.config.http.keep_alive_no_activity_timeout_out", &keepalive_timeout_out);
  Cli_RecordGetInt("proxy.config.http.transaction_no_activity_timeout_in", &inactivity_timeout_in);
  Cli_RecordGetInt("proxy.config.http.transaction_no_activity_timeout_out", &inactivity_timeout_out);
  Cli_RecordGetInt("proxy.config.http.transaction_active_timeout_in", &activity_timeout_in);
  Cli_RecordGetInt("proxy.config.http.transaction_active_timeout_out", &activity_timeout_out);
  Cli_RecordGetInt("proxy.config.cache.limits.http.max_alts", &max_alts);
  Cli_RecordGetInt("proxy.config.http.anonymize_remove_from", &remove_from);
  Cli_RecordGetInt("proxy.config.http.anonymize_remove_referer", &remove_referer);
  Cli_RecordGetInt("proxy.config.http.anonymize_remove_user_agent", &remove_user_agent);
  Cli_RecordGetInt("proxy.config.http.anonymize_remove_cookie", &remove_cookie);
  Cli_RecordGetString("proxy.config.http.anonymize_other_header_list", &other_header_list);
  Cli_RecordGetInt("proxy.config.http.anonymize_insert_client_ip", &insert_client_ip);
  Cli_RecordGetInt("proxy.config.http.anonymize_remove_client_ip", &remove_client_ip);
  Cli_RecordGetInt("proxy.config.http.server_port", &http_server);
  Cli_RecordGetString("proxy.config.http.server_other_ports", &http_other);

  Cli_RecordGetString("proxy.config.http.global_user_agent_header", &global_user_agent);

  // display results
  Cli_Printf("\n");
  Cli_Printf("HTTP Caching ------------------ %s\n", (http_enabled == 1) ? "on" : "off");
  Cli_Printf("HTTP Server Port -------------- %d\n", http_server);
  Cli_Printf("HTTP Other Ports -------------- %s\n", (http_other != NULL) ? http_other : "none");
  Cli_Printf("Keep-Alive Timeout Inbound ---- %d s\n", keepalive_timeout_in);
  Cli_Printf("Keep-Alive Timeout Outbound --- %d s\n", keepalive_timeout_out);
  Cli_Printf("Inactivity Timeout Inbound ---- %d s\n", inactivity_timeout_in);
  Cli_Printf("Inactivity Timeout Outbound --- %d s\n", inactivity_timeout_out);
  Cli_Printf("Activity Timeout Inbound ------ %d s\n", activity_timeout_in);
  Cli_Printf("Activity Timeout Outbound ----- %d s\n", activity_timeout_out);
  Cli_Printf("Maximum Number of Alternates -- %d\n", max_alts);

  if (remove_from == 1 || remove_referer == 1 || remove_user_agent == 1 || remove_cookie == 1) {
    Cli_Printf("Remove the following common headers -- \n");
    if (remove_from == 1) {
      Cli_Printf("From\n");
    }
    if (remove_referer == 1) {
      Cli_Printf("Referer\n");
    }
    if (remove_user_agent == 1) {
      Cli_Printf("User-Agent\n");
    }
    if (remove_cookie == 1) {
      Cli_Printf("Cookie\n");
    }
  }
  if (other_header_list != NULL && strlen(other_header_list)) {
    Cli_Printf("Remove additional headers ----- " "%s\n", other_header_list);
  }
  if (insert_client_ip == 1) {
    Cli_Printf("Insert Client IP Address into Header\n");
  }
  if (remove_client_ip == 1) {
    Cli_Printf("Remove Client IP Address from Header\n");
  }
  if (global_user_agent) {
    Cli_Printf("Set User-Agent header to %s\n", global_user_agent);
  }

  Cli_Printf("\n");

  return CLI_OK;
}

// show nntp sub-command
int
ShowNntp()
{
  // declare and initialize variables

  INKInt nntp_enabled = -1;
  INKInt nntp_server = -1;
  INKInt nntp_server_port = 119;
  INKString connect_msg_allow = NULL;
  INKString connect_msg_disallow = NULL;
  INKInt nntp_posting = -1;
  INKInt nntp_access_control = -1;
  INKInt nntp_v2_auth = -1;
  INKInt nntp_run_local_auth_server = -1;
  INKInt nntp_clustering = -1;
  INKInt nntp_allow_feeds = -1;
  INKInt nntp_access_logs = -1;
  INKInt nntp_bg_posting = -1;
  INKInt nntp_obey_control_cancel = -1;
  INKInt nntp_obey_control_newgroup = -1;
  INKInt nntp_obey_control_rmgroup = -1;
  INKInt nntp_inactivity_timeout = 600;
  INKInt nntp_check_newgrp_every = 86400;
  INKInt nntp_check_cancelled_articles_every = 3600;
  INKInt nntp_check_parent_server_every = 300;
  INKInt nntp_check_cluster_every = 60;
  INKInt nntp_check_pull_groups_every = 600;
  INKString nntp_auth_server_hostname = NULL;
  INKInt nntp_auth_server_port = 0;
  INKInt nntp_local_authserver_timeout = 50000;
  INKInt nntp_client_speed_throttle = 0;

  // Since so much code is ifdef'ed out, we get tons of warnings here.
  // Lets avoid those for now ... /leif
  NOWARN_UNUSED(nntp_server);
  NOWARN_UNUSED(nntp_server_port);
  NOWARN_UNUSED(connect_msg_allow);
  NOWARN_UNUSED(connect_msg_disallow);
  NOWARN_UNUSED(nntp_posting);
  NOWARN_UNUSED(nntp_access_control);
  NOWARN_UNUSED(nntp_v2_auth);
  NOWARN_UNUSED(nntp_run_local_auth_server);
  NOWARN_UNUSED(nntp_clustering);
  NOWARN_UNUSED(nntp_allow_feeds);
  NOWARN_UNUSED(nntp_access_logs);
  NOWARN_UNUSED(nntp_bg_posting);
  NOWARN_UNUSED(nntp_check_cluster_every);
  NOWARN_UNUSED(nntp_auth_server_hostname);
  NOWARN_UNUSED(nntp_auth_server_port);
  NOWARN_UNUSED(nntp_local_authserver_timeout);
  NOWARN_UNUSED(nntp_client_speed_throttle);

  // retrieve values

  Cli_RecordGetInt("proxy.config.nntp.cache_enabled", &nntp_enabled);
#if 0
  Cli_RecordGetInt("proxy.config.nntp.enabled", &nntp_server);
  Cli_RecordGetInt("proxy.config.nntp.server_port", &nntp_server_port);
  Cli_RecordGetString("proxy.config.nntp.posting_ok_message", &connect_msg_allow);
  Cli_RecordGetString("proxy.config.nntp.posting_not_ok_message", &connect_msg_disallow);
  Cli_RecordGetInt("proxy.config.nntp.posting_enabled", &nntp_posting);
  Cli_RecordGetInt("proxy.config.nntp.access_control_enabled", &nntp_access_control);
  Cli_RecordGetInt("proxy.config.nntp.v2_authentication", &nntp_v2_auth);
  Cli_RecordGetInt("proxy.config.nntp.run_local_authentication_server", &nntp_run_local_auth_server);
  Cli_RecordGetInt("proxy.config.nntp.cluster_enabled", &nntp_clustering);
  Cli_RecordGetInt("proxy.config.nntp.feed_enabled", &nntp_allow_feeds);
  Cli_RecordGetInt("proxy.config.nntp.logging_enabled", &nntp_access_logs);
  Cli_RecordGetInt("proxy.config.nntp.background_posting_enabled", &nntp_bg_posting);
#endif
  Cli_RecordGetInt("proxy.config.nntp.obey_control_cancel", &nntp_obey_control_cancel);
  Cli_RecordGetInt("proxy.config.nntp.obey_control_newgroup", &nntp_obey_control_newgroup);
  Cli_RecordGetInt("proxy.config.nntp.obey_control_rmgroup", &nntp_obey_control_rmgroup);
  Cli_RecordGetInt("proxy.config.nntp.inactivity_timeout", &nntp_inactivity_timeout);
  Cli_RecordGetInt("proxy.config.nntp.check_newgroups_every", &nntp_check_newgrp_every);
  Cli_RecordGetInt("proxy.config.nntp.check_cancels_every", &nntp_check_cancelled_articles_every);
  Cli_RecordGetInt("proxy.config.nntp.group_check_parent_every", &nntp_check_parent_server_every);
  Cli_RecordGetInt("proxy.config.nntp.check_pull_every", &nntp_check_pull_groups_every);
#if 0
  Cli_RecordGetInt("proxy.config.nntp.group_check_cluster_every", &nntp_check_cluster_every);
  Cli_RecordGetString("proxy.config.nntp.authorization_hostname", &nntp_auth_server_hostname);
  Cli_RecordGetInt("proxy.config.nntp.authorization_port", &nntp_auth_server_port);
  Cli_RecordGetInt("proxy.config.nntp.authorization_server_timeout", &nntp_local_authserver_timeout);
  Cli_RecordGetInt("proxy.config.nntp.client_speed_throttle", &nntp_client_speed_throttle);
#endif
  // display results

  Cli_Printf("\n");
  Cli_PrintEnable("NNTP Caching --------------------------- ", nntp_enabled);
#if 0
  Cli_PrintEnable("NNTP Server  --------------------------- ", nntp_server);
  if (nntp_enabled == 1)
    Cli_Printf("NNTP Server Port ----------------------- %d\n", nntp_server_port);
  if (connect_msg_allow)
    Cli_Printf("Connect Message ------------------------ %s\n", connect_msg_allow);
  if (connect_msg_disallow)
    Cli_Printf("Connect Message ------------------------ %s\n", connect_msg_disallow);
#endif
  Cli_Printf("NNTP Options:\n");
#if 0
  Cli_PrintEnable("  Posting ------------------------------ ", nntp_posting);
  Cli_PrintEnable("  Access Control ----------------------- ", nntp_access_control);
  Cli_PrintEnable("  V2 Authentication -------------------- ", nntp_v2_auth);
  Cli_PrintEnable("  Local Auth. Server ------------------- ", nntp_run_local_auth_server);
  Cli_PrintEnable("  Clustering --------------------------- ", nntp_clustering);
  Cli_PrintEnable("  Allow feeds -------------------------- ", nntp_allow_feeds);
  Cli_PrintEnable("  Access Logs -------------------------- ", nntp_access_logs);
  Cli_PrintEnable("  Background Posting ------------------- ", nntp_bg_posting);
#endif
  Cli_PrintEnable("  Obey Cancel Control ------------------ ", nntp_obey_control_cancel);
  Cli_PrintEnable("  Obey NewGroups Control --------------- ", nntp_obey_control_newgroup);
  Cli_PrintEnable("  Obey RmGroups Control ---------------- ", nntp_obey_control_rmgroup);
  Cli_PrintEnable("Inactivity Timeout --------------------- ", nntp_inactivity_timeout);
  Cli_Printf("Check for New Groups Every ------------- %d s\n", nntp_check_newgrp_every);
  Cli_Printf("Check for Cancelled Articles Every------ %d s\n", nntp_check_cancelled_articles_every);
  Cli_Printf("Check Parent NNTP Server Every---------- %d s\n", nntp_check_parent_server_every);
  Cli_Printf("Check Pull Groups Every ---------------- %d s\n", nntp_check_pull_groups_every);
#if 0
  Cli_Printf("Check Cluster Every -------------------- %d s\n", nntp_check_cluster_every);
  if (nntp_auth_server_hostname) {
    Cli_Printf("Authentication Server Host ------------- %s\n", nntp_auth_server_hostname);
    Cli_Printf("Authentication Server Port ------------- %d\n", nntp_auth_server_port);
  }
  Cli_Printf("Local Authentication Server Timeout ---- %d ms\n", nntp_local_authserver_timeout);
  Cli_Printf("Client Speed Throttle ------------------ %d bytes/s\n", nntp_client_speed_throttle);
#endif
  Cli_Printf("\n");

  return CLI_OK;
}

// show nntp config sub-command
int
ShowNntpConfig()
{
  INKError status = INK_ERR_OKAY;
  Cli_Printf("\n");
  // display nntp_config.xml
  Cli_Printf("nntp_config.xml\n" "-------------------\n");
  status = Cli_DisplayRules(INK_FNAME_NNTP_CONFIG_XML);
  Cli_Printf("\n");
  return CLI_OK;
}

#if 0
// show nntp servers sub-command
int
ShowNntpServers()
{
  INKError status = INK_ERR_OKAY;
  Cli_Printf("\n");
  // display rules from nntp_servers.config
  Cli_Printf("nntp_servers.config rules\n" "-------------------\n");
  status = Cli_DisplayRules(INK_FNAME_NNTP_SERVERS);
  Cli_Printf("\n");

  return status;
}

// show nntp access sub-command
int
ShowNntpAccess()
{
  INKError status = INK_ERR_OKAY;
  Cli_Printf("\n");
  // display rules from nntp_access.config
  Cli_Printf("nntp_access.config rules\n" "-------------------\n");
  status = Cli_DisplayRules(INK_FNAME_NNTP_ACCESS);
  Cli_Printf("\n");

  return status;
}
#endif

// show ftp sub-command
int
ShowFtp()
{
  // declare and initialize variables

  INKInt cache_ftp = 0;
  INKInt document_lifetime = -1;
  INKInt data_connection_mode = -1;
  INKInt control_connection_timeout = -1;
  INKString passwd = NULL;

  // retrieve value

  Cli_RecordGetInt("proxy.config.http.cache.ftp", &cache_ftp);
  Cli_RecordGetInt("proxy.config.http.ftp.cache.document_lifetime", &document_lifetime);
  Cli_RecordGetInt("proxy.config.ftp.data_connection_mode", &data_connection_mode);
  Cli_RecordGetInt("proxy.config.ftp.control_connection_timeout", &control_connection_timeout);
  Cli_RecordGetString("proxy.config.http.ftp.anonymous_passwd", &passwd);
  // display results
  Cli_Printf("\n");

  if (Cli_PrintEnable("FTP Caching ----------------------- ", cache_ftp) == CLI_ERROR) {
    return CLI_ERROR;
  }
  Cli_Printf("FTP Cached Objects Expired After -- %d s\n", document_lifetime);

  switch (data_connection_mode) {
  case 1:
    Cli_Printf("FTP Connection Mode --------------- %s\n", "PASV/PORT");
    break;
  case 2:
    Cli_Printf("FTP Connection Mode --------------- %s\n", "PORT ONLY");
    break;
  case 3:
    Cli_Printf("FTP Connection Mode --------------- %s\n", "PASV ONLY");
    break;
  default:
    Cli_Printf("FTP Connection Mode --------------- %s\n", "NOT SET");

  }

  Cli_Printf("FTP Inactivity Timeout ------------ %d s\n", control_connection_timeout);
  Cli_Printf("Anonymous FTP password ------------ %s\n", passwd);
  Cli_Printf("\n");

  return CLI_OK;
}

// show icp sub-command
int
ShowIcp()
{
  // declare and initialize variables

  INKInt icp_enabled = 0;
  INKInt icp_port = -1;
  INKInt multicast_enabled = 0;
  INKInt query_timeout = 2;

  // retrieve value

  Cli_RecordGetInt("proxy.config.icp.enabled", &icp_enabled);
  Cli_RecordGetInt("proxy.config.icp.icp_port", &icp_port);
  Cli_RecordGetInt("proxy.config.icp.multicast_enabled", &multicast_enabled);
  Cli_RecordGetInt("proxy.config.icp.query_timeout", &query_timeout);

  // display results
  Cli_Printf("\n");

  Cli_PrintEnable("ICP Mode Enabled ------- ", icp_enabled);
  Cli_Printf("ICP Port --------------- %d\n", icp_port);
  Cli_PrintEnable("ICP Multicast Enabled -- ", multicast_enabled);
  Cli_Printf("ICP Query Timeout ------ %d s\n", query_timeout);
  Cli_Printf("\n");

  return CLI_OK;
}

// show icp peer sub-command
int
ShowIcpPeer()
{
  // display rules from icp.config
  Cli_Printf("\n");
  Cli_Printf("icp.config Rules\n" "-------------------\n");
  INKError status = Cli_DisplayRules(INK_FNAME_ICP_PEER);
  Cli_Printf("\n");

  return status;
}

// show proxy sub-command
int
ShowProxy()
{

  INKString proxy_name = NULL;

  Cli_RecordGetString("proxy.config.proxy_name", &proxy_name);
  Cli_Printf("\n");
  Cli_Printf("Name -- %s\n", proxy_name);
  Cli_Printf("\n");

  return CLI_OK;
}

// show cache sub-command
int
ShowCache()
{
  // declare and initialize variables

  INKInt cache_http = -1;
  INKInt cache_ftp = -1;
  INKInt cache_nntp = -1;
  INKInt cache_bypass = -1;
  INKInt max_doc_size = -1;
  INKInt when_to_reval = -1;
  INKInt reqd_headers = -1;
  INKInt min_life = -1;
  INKInt max_life = -1;
  INKInt doc_life = -1;
  INKInt dynamic_urls = -1;
  INKInt alternates = -1;
  INKString vary_def_text = "NONE";
  INKString vary_def_image = "NONE";
  INKString vary_def_other = "NONE";
  INKInt cookies = -1;

  // retrieve values

  Cli_RecordGetInt("proxy.config.http.cache.http", &cache_http);
  Cli_RecordGetInt("proxy.config.http.cache.ftp", &cache_ftp);
  Cli_RecordGetInt("proxy.config.nntp.cache_enabled", &cache_nntp);
  Cli_RecordGetInt("proxy.config.http.cache.ignore_client_no_cache", &cache_bypass);
  Cli_RecordGetInt("proxy.config.cache.max_doc_size", &max_doc_size);
  Cli_RecordGetInt("proxy.config.http.cache.when_to_revalidate", &when_to_reval);
  Cli_RecordGetInt("proxy.config.http.cache.required_headers", &reqd_headers);
  Cli_RecordGetInt("proxy.config.http.cache.heuristic_min_lifetime", &min_life);
  Cli_RecordGetInt("proxy.config.http.cache.heuristic_max_lifetime", &max_life);
  Cli_RecordGetInt("proxy.config.http.ftp.cache.document_lifetime", &doc_life);
  Cli_RecordGetInt("proxy.config.http.cache.cache_urls_that_look_dynamic", &dynamic_urls);
  Cli_RecordGetInt("proxy.config.http.cache.enable_default_vary_headers", &alternates);
  Cli_RecordGetString("proxy.config.http.cache.vary_default_text", &vary_def_text);
  Cli_RecordGetString("proxy.config.http.cache.vary_default_images", &vary_def_image);
  Cli_RecordGetString("proxy.config.http.cache.vary_default_other", &vary_def_other);

  Cli_RecordGetInt("proxy.config.http.cache.cache_responses_to_cookies", &cookies);


  // display results
  Cli_Printf("\n");

  Cli_PrintEnable("HTTP Caching --------------------------- ", cache_http);

  Cli_PrintEnable("FTP Caching ---------------------------- ", cache_ftp);

  Cli_PrintEnable("NNTP Caching --------------------------- ", cache_nntp);

  Cli_PrintEnable("Ignore User Requests To Bypass Cache --- ", cache_bypass);

  if (max_doc_size == 0)
    Cli_Printf("Maximum HTTP/FTP Object Size ----------- NONE\n");
  else
    Cli_Printf("Maximum HTTP/FTP Object Size ----------- %d\n", max_doc_size);

  Cli_Printf("Freshness\n");
  Cli_Printf("  Verify Freshness By Checking --------- ");

  switch (when_to_reval) {
  case 0:
    Cli_Printf("When The Object Has Expired\n");
    break;
  case 1:
    Cli_Printf("When The Object Has No Expiry Date\n");
    break;
  case 2:
    Cli_Printf("Always\n");
    break;
  case 3:
    Cli_Printf("Never\n");
    break;
  default:
    Cli_Printf("unknown\n");
    break;
  }

  Cli_Printf("  Minimum Information to be Cacheable -- ");

  switch (reqd_headers) {
  case 0:
    Cli_Printf("Nothing\n");
    break;
  case 1:
    Cli_Printf("A Last Modified Time\n");
    break;
  case 2:
    Cli_Printf("An Explicit Lifetime\n");
    break;
  default:
    Cli_Printf("unknown\n");
    break;
  }

  Cli_Printf("  If Object has no Expiration Date: \n" "    Leave it in Cache for at least ----- %d s\n", min_life);
  Cli_Printf("    but no more than ------------------- %d s\n", max_life);
  Cli_Printf("  FTP Cached Objects Expire After ------ %d s\n", doc_life);

  Cli_Printf("Variable Content\n");

  Cli_PrintEnable("  Cache Responses to URLs that contain\n    \"?\",\";\",\"cgi\" or end in \".asp\" ----- ",
                  dynamic_urls);

  Cli_PrintEnable("  Alternates Enabled ------------------- ", alternates);

  Cli_Printf("  Vary on HTTP Header Fields: \n");
  Cli_Printf("    Text ------------------------------- %s\n", vary_def_text);
  Cli_Printf("    Images ----------------------------- %s\n", vary_def_image);
  Cli_Printf("    Other ------------------------------ %s\n", vary_def_other);

  Cli_Printf("  Cache responses to requests containing cookies for:\n");

  switch (cookies) {
  case 0:
    Cli_Printf("    No Content-types\n");
    break;
  case 1:
    Cli_Printf("    All Content-types\n");
    break;
  case 2:
    Cli_Printf("    Only Image-content Types\n");
    break;
  case 3:
    Cli_Printf("    Content Types which are not Text\n");
    break;
  case 4:
    Cli_Printf("    Content Types which are not Text with some exceptions\n");
    break;
  }
  Cli_Printf("\n");

  return CLI_OK;
}

// show cache rules sub-command
int
ShowCacheRules()
{
  // display rules from cache.config
  Cli_Printf("\n");

  Cli_Printf("cache.config rules\n" "-------------------\n");
  INKError status = Cli_DisplayRules(INK_FNAME_CACHE_OBJ);

  Cli_Printf("\n");

  return status;
}

// show cache storage sub-command
int
ShowCacheStorage()
{
  // display rules from storage.config
  Cli_Printf("storage.config rules\n");

  INKError status = Cli_DisplayRules(INK_FNAME_STORAGE);

  return status;
}


// show virtual-ip sub-command
int
ShowVirtualIp()
{
  INKCfgContext VipCtx;
  int EleCount, i;
  INKVirtIpAddrEle *VipElePtr;

  VipCtx = INKCfgContextCreate(INK_FNAME_VADDRS);
  if (INKCfgContextGet(VipCtx) != INK_ERR_OKAY)
    Cli_Printf("ERROR READING FILE\n");
  EleCount = INKCfgContextGetCount(VipCtx);
  Cli_Printf("\n");
  Cli_Printf("%d Elements in Record\n", EleCount);
  Cli_Printf("\n");
  for (i = 0; i < EleCount; i++) {
    VipElePtr = (INKVirtIpAddrEle *) INKCfgContextGetEleAt(VipCtx, i);
    Cli_Printf("%d %s %s %d\n", i, VipElePtr->ip_addr, VipElePtr->intr, VipElePtr->sub_intr);
  }
  Cli_Printf("\n");
  return CLI_OK;
}

// show hostdb sub-command
int
ShowHostDb()
{

  // declare and initialize variables

  INKInt lookup_timeout = -1;
  INKInt timeout = -1;
  INKInt verify_after = -1;
  INKInt fail_timeout = -1;
  INKInt re_dns_on_reload = 0;
  INKInt dns_lookup_timeout = -1;
  INKInt dns_retries = -1;

  // retrieve value

  Cli_RecordGetInt("proxy.config.hostdb.lookup_timeout", &lookup_timeout);
  Cli_RecordGetInt("proxy.config.hostdb.timeout", &timeout);
  Cli_RecordGetInt("proxy.config.hostdb.verify_after", &verify_after);
  Cli_RecordGetInt("proxy.config.hostdb.fail.timeout", &fail_timeout);
  Cli_RecordGetInt("proxy.config.hostdb.re_dns_on_reload", &re_dns_on_reload);
  Cli_RecordGetInt("proxy.config.dns.lookup_timeout", &dns_lookup_timeout);
  Cli_RecordGetInt("proxy.config.dns.retries", &dns_retries);

  // display results
  Cli_Printf("\n");

  Cli_Printf("Lookup Timeout ----------- %d s\n", lookup_timeout);
  Cli_Printf("Foreground Timeout ------- %d s\n", timeout);
  Cli_Printf("Background Timeout ------- %d s\n", verify_after);
  Cli_Printf("Invalid Host Timeout ----- %d s\n", fail_timeout);
  if (Cli_PrintEnable("Re-DNS on Reload --------- ", re_dns_on_reload) == CLI_ERROR) {
    return CLI_ERROR;
  }
  Cli_Printf("Resolve Attempt Timeout -- %d s\n", dns_lookup_timeout);
  Cli_Printf("Number of retries -------- %d \n", dns_retries);
  Cli_Printf("\n");

  return CLI_OK;
}

// show dns-resolver sub-command
int
ShowDnsResolver()
{
  // declare and initialize variables

  INKInt dns_search_default_domains = 0;
  INKInt http_enable_url_expandomatic = 0;

  // retrieve value

  Cli_RecordGetInt("proxy.config.dns.search_default_domains", &dns_search_default_domains);
  Cli_RecordGetInt("proxy.config.http.enable_url_expandomatic", &http_enable_url_expandomatic);

  // display results
  Cli_Printf("\n");

  if (Cli_PrintEnable("Local Domain Expansion -- ", dns_search_default_domains) == CLI_ERROR) {
    return CLI_ERROR;
  }
  if (Cli_PrintEnable(".com Domain Expansion --- ", http_enable_url_expandomatic) == CLI_ERROR) {
    return CLI_ERROR;
  }
  Cli_Printf("\n");

  return CLI_OK;
}

// show logging sub-command
int
ShowLogging()
{
  // declare and initialize variables

  INKInt logging_enabled = 0;
  INKInt log_space = -1;
  INKInt headroom_space = -1;
  INKInt collation_mode = 0;
  INKString collation_host = "None";
  INKInt collation_port = -1;
  INKString collation_secret = NULL;
  INKInt host_tag = 0;
  INKInt orphan_space = -1;

  INKInt squid_log = 0;
  INKInt is_ascii = 1;
  INKString file_name = NULL;
  INKString file_header = NULL;

  INKInt common_log = 0;
  INKInt common_is_ascii = 0;
  INKString common_file_name = NULL;
  INKString common_file_header = NULL;

  INKInt extended_log = 0;
  INKInt extended_is_ascii = 0;
  INKString extended_file_name = NULL;
  INKString extended_file_header = NULL;

  INKInt extended2_log = 0;
  INKInt extended2_is_ascii = 0;
  INKString extended2_file_name = NULL;
  INKString extended2_file_header = NULL;

  INKInt nntp_log = 0;
  INKInt icp_log = 0;
  INKInt http_host_log = 0;
  INKInt custom_log = 0;
  INKInt xml_log = 0;
  INKInt rolling = 0;
  INKInt roll_offset_hr = -1;
  INKInt roll_interval = -1;
  INKInt auto_delete = 0;
  // retrieve value

  Cli_RecordGetInt("proxy.config.log2.logging_enabled", &logging_enabled);
  Cli_RecordGetInt("proxy.config.log2.max_space_mb_for_logs", &log_space);
  Cli_RecordGetInt("proxy.config.log2.max_space_mb_headroom", &headroom_space);
  Cli_RecordGetInt("proxy.local.log2.collation_mode", &collation_mode);
  Cli_RecordGetString("proxy.config.log2.collation_host", &collation_host);
  Cli_RecordGetInt("proxy.config.log2.collation_port", &collation_port);
  Cli_RecordGetString("proxy.config.log2.collation_secret", &collation_secret);
  Cli_RecordGetInt("proxy.config.log2.collation_host_tagged", &host_tag);
  Cli_RecordGetInt("proxy.config.log2.max_space_mb_for_orphan_logs", &orphan_space);

  Cli_RecordGetInt("proxy.config.log2.squid_log_enabled", &squid_log);
  Cli_RecordGetInt("proxy.config.log2.squid_log_is_ascii", &is_ascii);
  Cli_RecordGetString("proxy.config.log2.squid_log_name", &file_name);
  Cli_RecordGetString("proxy.config.log2.squid_log_header", &file_header);

  Cli_RecordGetInt("proxy.config.log2.common_log_enabled", &common_log);
  Cli_RecordGetInt("proxy.config.log2.common_log_is_ascii", &common_is_ascii);
  Cli_RecordGetString("proxy.config.log2.common_log_name", &common_file_name);
  Cli_RecordGetString("proxy.config.log2.common_log_header", &common_file_header);

  Cli_RecordGetInt("proxy.config.log2.extended_log_enabled", &extended_log);
  Cli_RecordGetInt("proxy.config.log2.extended_log_is_ascii", &extended_is_ascii);
  Cli_RecordGetString("proxy.config.log2.extended_log_name", &extended_file_name);
  Cli_RecordGetString("proxy.config.log2.extended_log_header", &extended_file_header);

  Cli_RecordGetInt("proxy.config.log2.extended2_log_enabled", &extended2_log);
  Cli_RecordGetInt("proxy.config.log2.extended2_log_is_ascii", &extended2_is_ascii);
  Cli_RecordGetString("proxy.config.log2.extended2_log_name", &extended2_file_name);
  Cli_RecordGetString("proxy.config.log2.extended2_log_header", &extended2_file_header);

  Cli_RecordGetInt("proxy.config.log2.separate_nntp_logs", &nntp_log);
  Cli_RecordGetInt("proxy.config.log2.separate_icp_logs", &icp_log);
  Cli_RecordGetInt("proxy.config.log2.separate_host_logs", &http_host_log);
  Cli_RecordGetInt("proxy.config.log2.separate_host_logs", &custom_log);
  Cli_RecordGetInt("proxy.config.log2.xml_logs_config", &xml_log);

  Cli_RecordGetInt("proxy.config.log2.rolling_enabled", &rolling);
  Cli_RecordGetInt("proxy.config.log2.rolling_offset_hr", &roll_offset_hr);
  Cli_RecordGetInt("proxy.config.log2.rolling_interval_sec", &roll_interval);
  Cli_RecordGetInt("proxy.config.log2.auto_delete_rolled_files", &auto_delete);

  // display results
  Cli_Printf("\n");

  Cli_Printf("Logging Mode ----------------------------- ");
  switch (logging_enabled) {
  case 0:
    Cli_Printf("no logging\n");
    break;
  case 1:
    Cli_Printf("errors only\n");
    break;
  case 2:
    Cli_Printf("transactions only\n");
    break;
  case 3:
    Cli_Printf("errors and transactions\n");
    break;
  default:
    Cli_Printf("invalid mode\n");
    break;
  }

  Cli_Printf("\nManagement\n");
  Cli_Printf("  Log Space Limit ------------------------ %d MB\n", log_space);
  Cli_Printf("  Log Space Headroom --------------------- %d MB\n", headroom_space);

  Cli_PrintEnable("\nLog Collation ---------------------------- ", collation_mode);
  Cli_Printf("  Host ----------------------------------- %s\n", collation_host);
  Cli_Printf("  Port ----------------------------------- %d\n", collation_port);
  Cli_Printf("  Secret --------------------------------- %s\n", collation_secret);
  Cli_PrintEnable("  Host Tagged ---------------------------- ", host_tag);
  Cli_Printf("  Space Limit for Orphan Files ----------- %d MB\n", orphan_space);

  Cli_PrintEnable("\nSquid Format ----------------------------- ", squid_log);
  if (is_ascii == 1)
    Cli_Printf("  File Type ------------------------------ %s\n", "ASCII");
  else if (is_ascii == 0)
    Cli_Printf("  File Type ------------------------------ %s\n", "BINARY");
  else
    Cli_Debug(ERR_INVALID_PARAMETER);
  Cli_Printf("  File Name ------------------------------ %s\n", file_name);
  Cli_Printf("  File Header ---------------------------- %s\n", file_header);

  Cli_PrintEnable("\nNetscape Common -------------------------- ", common_log);
  if (common_is_ascii == 1)
    Cli_Printf("  File Type ------------------------------ %s\n", "ASCII");
  else if (common_is_ascii == 0)
    Cli_Printf("  File Type ------------------------------ %s\n", "BINARY");
  else
    Cli_Debug(ERR_INVALID_PARAMETER);
  Cli_Printf("  File Name ------------------------------ %s\n", common_file_name);
  Cli_Printf("  File Header ---------------------------- %s\n", common_file_header);

  Cli_PrintEnable("\nNetscape Extended ------------------------ ", extended_log);
  if (extended_is_ascii == 1)
    Cli_Printf("  File Type ------------------------------ %s\n", "ASCII");
  else if (extended_is_ascii == 0)
    Cli_Printf("  File Type ------------------------------ %s\n", "BINARY");
  else
    Cli_Debug(ERR_INVALID_PARAMETER);
  Cli_Printf("  File Name ------------------------------ %s\n", extended_file_name);
  Cli_Printf("  File Header ---------------------------- %s\n", extended_file_header);

  Cli_PrintEnable("\nNetscape Extended2 ----------------------- ", extended2_log);
  if (extended2_is_ascii == 1)
    Cli_Printf("  File Type ------------------------------ %s\n", "ASCII");
  else if (extended2_is_ascii == 0)
    Cli_Printf("  File Type ------------------------------ %s\n", "BINARY");
  else
    Cli_Debug(ERR_INVALID_PARAMETER);
  Cli_Printf("  File Name   ---------------------------- %s\n", extended2_file_name);
  Cli_Printf("  File Header ---------------------------- %s\n", extended2_file_header);

  Cli_Printf("\nSplitting\n");
  Cli_PrintEnable("  NNTP Log Splitting --------------------- ", nntp_log);
  Cli_PrintEnable("  ICP Log Splitting ---------------------- ", icp_log);
  Cli_PrintEnable("  HTTP Host Log Splitting ---------------- ", http_host_log);
  Cli_PrintEnable("\nCustom Logs ------------------------------ ", custom_log);
  if (xml_log == 0)
    Cli_Printf("Custom Log Definition Format ------------- %s\n", "Traditional");
  Cli_PrintEnable("\nRolling ---------------------------------- ", rolling);
  Cli_Printf("  Roll Offset Hour ----------------------- %d\n", roll_offset_hr);
  Cli_Printf("  Roll Interval -------------------------- %d s\n", roll_interval);
  Cli_PrintEnable("  Auto-delete rolled files (low space) --- ", auto_delete);
  Cli_Printf("\n");

  return CLI_OK;
}

// show ssl sub-command
int
ShowSsl()
{
  // declare and initialize variables

  INKString ssl_ports = NULL;

  // retrieve value

  Cli_RecordGetString("proxy.config.http.ssl_ports", &ssl_ports);

  // display results
  Cli_Printf("\n");
  Cli_Printf("Restrict SSL Connections to Ports -- %s\n", ssl_ports);
  Cli_Printf("\n");

  return CLI_OK;
}

// show filter sub-command
int
ShowFilter()
{
  // display rules from filter.config
  Cli_Printf("\n");

  Cli_Printf("filter.config rules\n" "-------------------\n");
  INKError status = Cli_DisplayRules(INK_FNAME_FILTER);
  Cli_Printf("\n");

  return status;
}

// show parents sub-command
int
ShowParents()
{
  INKError status = INK_ERR_OKAY;
  INKInt parent_enabled = -1;
  INKString parent_cache = NULL;

  Cli_RecordGetInt("proxy.config.http.parent_proxy_routing_enable", &parent_enabled);
  Cli_RecordGetString("proxy.config.http.parent_proxies", &parent_cache);
  Cli_Printf("\n");
  Cli_Printf("Parent Caching -- %s\n", (parent_enabled == 1) ? "on" : "off");
  Cli_Printf("Parent Cache ---- %s\n", parent_cache);
  Cli_Printf("\n");

  return status;
}

// show:parent rules sub-command
int
ShowParentRules()
{
  // display rules from parent.config
  Cli_Printf("\n");

  Cli_Printf("parent.config rules\n" "-------------------\n");
  INKError status = Cli_DisplayRules(INK_FNAME_PARENT_PROXY);
  Cli_Printf("\n");

  return status;
}

// show remap sub-command
int
ShowRemap()
{
  // display rules from remap.config
  Cli_Printf("\n");

  Cli_Printf("remap.config rules\n" "-------------------\n");
  INKError status = Cli_DisplayRules(INK_FNAME_REMAP);
  Cli_Printf("\n");

  return status;
}


// show snmp sub-command
int
ShowSnmp()
{

  // declare and initialize variables

  INKInt snmp_master_agent_enabled = 0;

  // retrieve value

  Cli_RecordGetInt("proxy.config.snmp.master_agent_enabled", &snmp_master_agent_enabled);

  // display results
  Cli_Printf("\n");

  if (Cli_PrintEnable("SNMP Agent -- ", snmp_master_agent_enabled) == CLI_ERROR) {
    return CLI_ERROR;
  }
  Cli_Printf("\n");

  return CLI_OK;
}

// show ldap sub-command
int
ShowLdap()
{
  // declare and initialize variables

  INKInt ldap_enable = 0;
  INKInt cache_size = -1;
  INKInt ttl_value = -1;
  INKInt auth_fail = 0;
  INKString ServerName = NULL;
  INKInt ServerPort = 0;
  INKString BaseDN = NULL;

  // retrieve values

  Cli_RecordGetInt("proxy.config.ldap.auth.enabled", &ldap_enable);
  Cli_RecordGetInt("proxy.config.ldap.cache.size", &cache_size);
  Cli_RecordGetInt("proxy.config.ldap.auth.ttl_value", &ttl_value);
  Cli_RecordGetInt("proxy.config.ldap.auth.purge_cache_on_auth_fail", &auth_fail);
  Cli_RecordGetString("proxy.config.ldap.proc.ldap.server.name", &ServerName);
  Cli_RecordGetInt("proxy.config.ldap.proc.ldap.server.port", &ServerPort);
  Cli_RecordGetString("proxy.config.ldap.proc.ldap.base.dn", &BaseDN);


  // display results
  Cli_Printf("\n");

  Cli_PrintEnable("LDAP ---------------------- ", ldap_enable);
  Cli_Printf("Cache Size ---------------- %d\n", cache_size);
  Cli_Printf("TTL Value ----------------- %d ms\n", ttl_value);
  Cli_PrintEnable("Purge Cache On Auth Fail -- ", auth_fail);
  Cli_Printf("Server Name --------------- %s\n", ServerName);
  Cli_Printf("Server Port --------------- %d\n", ServerPort);
  Cli_Printf("Base DN ------------------- %s\n", BaseDN);
  Cli_Printf("\n");

  return CLI_OK;
}

// show:ldap rules sub-command
int
ShowLdapRules()
{
  // display rules from filter.config
  //   since filter.config is now used for ldap configuration
  Cli_Printf("\n");

  Cli_Printf("filter.config rules\n" "-------------------\n");
  INKError status = Cli_DisplayRules(INK_FNAME_FILTER);
  Cli_Printf("\n");

  return status;
}

// show ldap sub-command
int
ShowLdapStats()
{
  // declare and initialize variables


  INKInt Cache_Hits = 0;
  INKInt Cache_Misses = 0;
  INKInt Server_Errors = 0;
  INKInt Authorization_Denied = 0;
  INKInt Authorization_Timeouts = 0;
  INKInt Authorization_Cancelled = 0;

  // retrieve values

  Cli_RecordGetInt("proxy.process.ldap.cache.hits", &Cache_Hits);
  Cli_RecordGetInt("proxy.process.ldap.cache.misses", &Cache_Misses);
  Cli_RecordGetInt("proxy.process.ldap.server.errors", &Server_Errors);
  Cli_RecordGetInt("proxy.process.ldap.denied.authorizations", &Authorization_Denied);
  Cli_RecordGetInt("proxy.process.ldap.auth.timed_out", &Authorization_Timeouts);
  Cli_RecordGetInt("proxy.process.ldap.cancelled.authentications", &Authorization_Cancelled);


  // display results
  Cli_Printf("\n");
  Cli_Printf("Cache Hits---------------- %d\n", Cache_Hits);
  Cli_Printf("Cache Misses-------------- %d\n", Cache_Misses);
  Cli_Printf("Server Errors------------- %d\n", Server_Errors);
  Cli_Printf("Authorization Denied------ %d\n", Authorization_Denied);
  Cli_Printf("Authorization Timeouts---- %d\n", Authorization_Timeouts);
  Cli_Printf("Authorization Cancelled--- %d\n", Authorization_Cancelled);
  Cli_Printf("\n");

  return CLI_OK;
}


// show socks sub-command
int
ShowSocks()
{
  // declare and initialize variables

  INKInt socks_enabled = 0;
  INKInt version = -1;
  INKString default_servers = NULL;
  INKInt accept_enabled = -1;
  INKInt accept_port = -1;

  // retrieve values

  Cli_RecordGetInt("proxy.config.socks.socks_needed", &socks_enabled);
  Cli_RecordGetInt("proxy.config.socks.socks_version", &version);
  Cli_RecordGetString("proxy.config.socks.default_servers", &default_servers);
  Cli_RecordGetInt("proxy.config.socks.accept_enabled", &accept_enabled);
  Cli_RecordGetInt("proxy.config.socks.accept_port", &accept_port);

  // display results
  Cli_Printf("\n");
  Cli_PrintEnable("SOCKS -------------------- ", socks_enabled);
  Cli_Printf("SOCKS Version ------------ %d\n", version);
  Cli_Printf("SOCKS Default Servers ---- %s\n", default_servers);
  Cli_PrintEnable("SOCKS Accept Enabled ----- ", accept_enabled);
  Cli_Printf("SOCKS Accept Port -------- %d\n", accept_port);
  Cli_Printf("\n");

  return CLI_OK;
}

// show:socks rules sub-command
int
ShowSocksRules()
{
  // display rules from socks.config
  Cli_Printf("\n");

  Cli_Printf("socks.config rules\n" "-------------------\n");
  INKError status = Cli_DisplayRules(INK_FNAME_SOCKS);
  Cli_Printf("\n");

  return status;
}

// show port-tunnels sub-command
int
ShowPortTunnels()
{
  INKString str_val = NULL;
  INKError status = INK_ERR_OKAY;

  status = Cli_RecordGetString("proxy.config.http.server_other_ports", &str_val);
  if (status) {
    return status;
  }
  Cli_Printf("\n");
  Cli_Printf("server-other-ports -- %s\n", str_val);
  Cli_Printf("\n");
  Cli_Printf("To view the corresponding rule of the remap.config file in the following format\n");
  Cli_Printf("map tunnel://<proxy_ip>:<port_num>/tunnel://<dest_server>:<dest_port>\n");
  Cli_Printf("Use show:remap\n");
  Cli_Printf("\n");
  return CLI_OK;
}

// show scheduled-update sub-command
int
ShowScheduledUpdate()
{
  // variable declaration
  INKInt enabled = 0;
  INKInt force = 0;
  INKInt retry_count = -1;
  INKInt retry_interval = -1;
  INKInt concurrent_updates = 0;

  // get value
  Cli_RecordGetInt("proxy.config.update.enabled", &enabled);
  Cli_RecordGetInt("proxy.config.update.retry_count", &retry_count);
  Cli_RecordGetInt("proxy.config.update.retry_interval", &retry_interval);
  Cli_RecordGetInt("proxy.config.update.concurrent_updates", &concurrent_updates);
  Cli_RecordGetInt("proxy.config.update.force", &force);
  //display values

  Cli_Printf("\n");

  if (Cli_PrintEnable("Scheduled Update ------------- ", enabled) == CLI_ERROR) {
    return CLI_ERROR;
  }

  Cli_Printf("Update Error Retry Count ----- %d\n", retry_count);
  Cli_Printf("Update Error Retry Interval -- %d s\n", retry_interval);
  Cli_Printf("Maximum Concurrent Updates --- %d\n", concurrent_updates);

  if (Cli_PrintEnable("Force Immediate Update ------- ", force) == CLI_ERROR) {
    return CLI_ERROR;
  }
  Cli_Printf("\n");

  return CLI_OK;
}

// show:scheduled-update rules sub-command
int
ShowScheduledUpdateRules()
{
  // display rules from socks.config
  Cli_Printf("\n");

  Cli_Printf("update.config rules\n" "-------------------\n");
  INKError status = Cli_DisplayRules(INK_FNAME_UPDATE_URL);
  Cli_Printf("\n");

  return status;
}

////////////////////////////////////////////////////////////////
// statistics sub-commands
//

// show proxy-stats sub-command
int
ShowProxyStats()
{

  INKFloat cache_hit_ratio = -1.0;
  INKFloat bandwidth_hit_ratio = -1.0;
  INKFloat percent_free = -1.0;
  INKInt current_server_connection = -1;
  INKInt current_client_connection = -1;
  INKInt current_cache_connection = -1;
  INKFloat client_throughput_out = -1.0;
  INKFloat xacts_per_second = -1.0;


  //get value
  Cli_RecordGetFloat("proxy.node.cache_hit_ratio", &cache_hit_ratio);
  Cli_RecordGetFloat("proxy.node.bandwidth_hit_ratio", &bandwidth_hit_ratio);
  Cli_RecordGetFloat("proxy.node.cache.percent_free", &percent_free);
  Cli_RecordGetInt("proxy.node.current_server_connections", &current_server_connection);
  Cli_RecordGetInt("proxy.node.current_client_connections", &current_client_connection);
  Cli_RecordGetInt("proxy.node.current_cache_connections", &current_cache_connection);
  Cli_RecordGetFloat("proxy.node.client_throughput_out", &client_throughput_out);
  Cli_RecordGetFloat("proxy.node.user_agent_xacts_per_second", &xacts_per_second);


  //display values
  Cli_Printf("\n");

  Cli_Printf("Document Hit Rate -------- %f %%\t *\n", 100 * cache_hit_ratio);
  Cli_Printf("Bandwidth Saving --------- %f %%\t *\n", 100 * bandwidth_hit_ratio);
  Cli_Printf("Cache Percent Free ------- %f %%\n", 100 * percent_free);
  Cli_Printf("Open Server Connections -- %d\n", current_server_connection);
  Cli_Printf("Open Client Connections -- %d\n", current_client_connection);
  Cli_Printf("Open Cache Connections --- %d\n", current_cache_connection);
  Cli_Printf("Client Throughput -------- %f MBit/Sec\n", client_throughput_out);
  Cli_Printf("Transaction Per Second --- %f\n", xacts_per_second);
  Cli_Printf("\n* Value represents 10 second average.\n");
  Cli_Printf("\n");

  return CLI_OK;
}

// show http-trans-stats sub-command
int
ShowHttpTransStats()
{
  //varialbles
  INKFloat frac_avg_10s_hit_fresh = -1.0;
  INKInt msec_avg_10s_hit_fresh = -1;
  INKFloat frac_avg_10s_hit_revalidated = -1.0;
  INKInt msec_avg_10s_hit_revalidated = -1;
  INKFloat frac_avg_10s_miss_cold = -1.0;
  INKInt msec_avg_10s_miss_cold = -1;
  INKFloat frac_avg_10s_miss_not_cachable = -1.0;
  INKInt msec_avg_10s_miss_not_cachable = -1;
  INKFloat frac_avg_10s_miss_changed = -1.0;
  INKInt msec_avg_10s_miss_changed = -1;
  INKFloat frac_avg_10s_miss_client_no_cache = -1.0;
  INKInt msec_avg_10s_miss_client_no_cache = -1;
  INKFloat frac_avg_10s_errors_connect_failed = -1.0;
  INKInt msec_avg_10s_errors_connect_failed = -1;
  INKFloat frac_avg_10s_errors_other = -1.0;
  INKInt msec_avg_10s_errors_other = -1;
  INKFloat frac_avg_10s_errors_aborts = -1.0;
  INKInt msec_avg_10s_errors_aborts = -1;
  INKFloat frac_avg_10s_errors_possible_aborts = -1.0;
  INKInt msec_avg_10s_errors_possible_aborts = -1;
  INKFloat frac_avg_10s_errors_early_hangups = -1.0;
  INKInt msec_avg_10s_errors_early_hangups = -1;
  INKFloat frac_avg_10s_errors_empty_hangups = -1.0;
  INKInt msec_avg_10s_errors_empty_hangups = -1;
  INKFloat frac_avg_10s_errors_pre_accept_hangups = -1.0;
  INKInt msec_avg_10s_errors_pre_accept_hangups = -1;
  INKFloat frac_avg_10s_other_unclassified = -1.0;
  INKInt msec_avg_10s_other_unclassified = -1;

  //get values
  Cli_RecordGetFloat("proxy.node.http.transaction_frac_avg_10s.hit_fresh", &frac_avg_10s_hit_fresh);
  Cli_RecordGetFloat("proxy.node.http.transaction_frac_avg_10s.hit_revalidated", &frac_avg_10s_hit_revalidated);
  Cli_RecordGetFloat("proxy.node.http.transaction_frac_avg_10s.miss_cold", &frac_avg_10s_miss_cold);
  Cli_RecordGetFloat("proxy.node.http.transaction_frac_avg_10s.miss_not_cacheable", &frac_avg_10s_miss_not_cachable);
  Cli_RecordGetFloat("proxy.node.http.transaction_frac_avg_10s.miss_changed", &frac_avg_10s_miss_changed);

  Cli_RecordGetFloat("proxy.node.http.transaction_frac_avg_10s.miss_client_no_cache",
                     &frac_avg_10s_miss_client_no_cache);
  Cli_RecordGetFloat("proxy.node.http.transaction_frac_avg_10s.errors.connect_failed",
                     &frac_avg_10s_errors_connect_failed);
  Cli_RecordGetFloat("proxy.node.http.transaction_frac_avg_10s.errors.other", &frac_avg_10s_errors_other);
  Cli_RecordGetFloat("proxy.node.http.transaction_frac_avg_10s.errors.aborts", &frac_avg_10s_errors_aborts);
  Cli_RecordGetFloat("proxy.node.http.transaction_frac_avg_10s.errors.possible_aborts",
                     &frac_avg_10s_errors_possible_aborts);
  Cli_RecordGetFloat("proxy.node.http.transaction_frac_avg_10s.errors.early_hangups",
                     &frac_avg_10s_errors_early_hangups);
  Cli_RecordGetFloat("proxy.node.http.transaction_frac_avg_10s.errors.empty_hangups",
                     &frac_avg_10s_errors_empty_hangups);
  Cli_RecordGetFloat("proxy.node.http.transaction_frac_avg_10s.errors.pre_accept_hangups",
                     &frac_avg_10s_errors_pre_accept_hangups);
  Cli_RecordGetFloat("proxy.node.http.transaction_frac_avg_10s.other.unclassified", &frac_avg_10s_other_unclassified);

  Cli_RecordGetInt("proxy.node.http.transaction_msec_avg_10s.hit_fresh", &msec_avg_10s_hit_fresh);
  Cli_RecordGetInt("proxy.node.http.transaction_msec_avg_10s.hit_revalidated", &msec_avg_10s_hit_revalidated);
  Cli_RecordGetInt("proxy.node.http.transaction_msec_avg_10s.miss_cold", &msec_avg_10s_miss_cold);
  Cli_RecordGetInt("proxy.node.http.transaction_msec_avg_10s.miss_not_cacheable", &msec_avg_10s_miss_not_cachable);
  Cli_RecordGetInt("proxy.node.http.transaction_msec_avg_10s.miss_changed", &msec_avg_10s_miss_changed);

  Cli_RecordGetInt("proxy.node.http.transaction_msec_avg_10s.miss_client_no_cache", &msec_avg_10s_miss_client_no_cache);
  Cli_RecordGetInt("proxy.node.http.transaction_msec_avg_10s.errors.connect_failed",
                   &msec_avg_10s_errors_connect_failed);
  Cli_RecordGetInt("proxy.node.http.transaction_msec_avg_10s.errors.other", &msec_avg_10s_errors_other);
  Cli_RecordGetInt("proxy.node.http.transaction_msec_avg_10s.errors.aborts", &msec_avg_10s_errors_aborts);
  Cli_RecordGetInt("proxy.node.http.transaction_msec_avg_10s.errors.possible_aborts",
                   &msec_avg_10s_errors_possible_aborts);
  Cli_RecordGetInt("proxy.node.http.transaction_msec_avg_10s.errors.early_hangups", &msec_avg_10s_errors_early_hangups);
  Cli_RecordGetInt("proxy.node.http.transaction_msec_avg_10s.errors.empty_hangups", &msec_avg_10s_errors_empty_hangups);
  Cli_RecordGetInt("proxy.node.http.transaction_msec_avg_10s.errors.pre_accept_hangups",
                   &msec_avg_10s_errors_pre_accept_hangups);
  Cli_RecordGetInt("proxy.node.http.transaction_msec_avg_10s.other.unclassified", &msec_avg_10s_other_unclassified);

  //display 
  Cli_Printf("\n");
  Cli_Printf("HTTP Transaction Frequency and Speeds\n");
  Cli_Printf("Transaction Type              Frequency   Speed(ms)\n");
  Cli_Printf("--Hits--\n");
  Cli_Printf("Fresh ----------------------- %f %%  %d\n", 100 * frac_avg_10s_hit_fresh, msec_avg_10s_hit_fresh);
  Cli_Printf("Stale Revalidated ----------- %f %%  %d\n", 100 * frac_avg_10s_hit_revalidated,
             msec_avg_10s_hit_revalidated);
  Cli_Printf("--Misses--\n");
  Cli_Printf("Now Cached ------------------ %f %%  %d\n", 100 * frac_avg_10s_miss_cold, msec_avg_10s_miss_cold);
  Cli_Printf("Server No Cache ------------- %f %%  %d\n", 100 * frac_avg_10s_miss_not_cachable,
             msec_avg_10s_miss_not_cachable);
  Cli_Printf("Stale Reloaded -------------- %f %%  %d\n", 100 * frac_avg_10s_miss_changed, msec_avg_10s_miss_changed);
  Cli_Printf("Client No Cache ------------- %f %%  %d\n", 100 * frac_avg_10s_miss_client_no_cache,
             msec_avg_10s_miss_client_no_cache);
  Cli_Printf("--Errors--\n");
  Cli_Printf("Connection Failures --------- %f %%  %d\n", 100 * frac_avg_10s_errors_connect_failed,
             msec_avg_10s_errors_connect_failed);
  Cli_Printf("Other Errors ---------------- %f %%  %d\n", 100 * frac_avg_10s_errors_other, msec_avg_10s_errors_other);
  Cli_Printf("--Aborted Transactions--\n");
  Cli_Printf("Client Aborts --------------- %f %%  %d\n", 100 * frac_avg_10s_errors_aborts, msec_avg_10s_errors_aborts);
  Cli_Printf("Questionable Client Aborts -- %f %%  %d\n", 100 * frac_avg_10s_errors_possible_aborts,
             msec_avg_10s_errors_possible_aborts);
  Cli_Printf("Partial Request Hangups ----- %f %%  %d\n", 100 * frac_avg_10s_errors_early_hangups,
             msec_avg_10s_errors_possible_aborts);
  Cli_Printf("Pre-Request Hangups --------- %f %%  %d\n", 100 * frac_avg_10s_errors_empty_hangups,
             msec_avg_10s_errors_empty_hangups);
  Cli_Printf("Pre-Connect Hangups --------- %f %%  %d\n", 100 * frac_avg_10s_errors_pre_accept_hangups,
             msec_avg_10s_errors_pre_accept_hangups);
  Cli_Printf("--Other Transactions--\n");
  Cli_Printf("Unclassified ---------------- %f %%  %d\n", 100 * frac_avg_10s_other_unclassified,
             msec_avg_10s_other_unclassified);
  Cli_Printf("\n");

  return CLI_OK;
}

// show http-stats sub-command
int
ShowHttpStats()
{
  //variable declaration
  INKInt user_agent_response_document_total_size = -1;
  INKInt user_agent_response_header_total_size = -1;
  INKInt current_client_connections = -1;
  INKInt current_client_transactions = -1;
  INKInt origin_server_response_document_total_size = -1;
  INKInt origin_server_response_header_total_size = -1;
  INKInt current_server_connections = -1;
  INKInt current_server_transactions = -1;
  //get value
  Cli_RecordGetInt("proxy.process.http.user_agent_response_document_total_size",
                   &user_agent_response_document_total_size);
  Cli_RecordGetInt("proxy.process.http.user_agent_response_header_total_size", &user_agent_response_header_total_size);
  Cli_RecordGetInt("proxy.process.http.current_client_connections", &current_client_connections);
  Cli_RecordGetInt("proxy.process.http.current_client_transactions", &current_client_transactions);
  Cli_RecordGetInt("proxy.process.http.origin_server_response_document_total_size",
                   &origin_server_response_document_total_size);
  Cli_RecordGetInt("proxy.process.http.origin_server_response_header_total_size",
                   &origin_server_response_header_total_size);
  Cli_RecordGetInt("proxy.process.http.current_server_connections", &current_server_connections);
  Cli_RecordGetInt("proxy.process.http.current_server_transactions", &current_server_transactions);

  //display value
  Cli_Printf("\n");
  Cli_Printf("--Client--\n");
  Cli_Printf("Total Document Bytes ----- %d MB\n", user_agent_response_document_total_size / (1024 * 1024));
  Cli_Printf("Total Header Bytes ------- %d MB\n", user_agent_response_header_total_size / (1024 * 1024));
  Cli_Printf("Total Connections -------- %d\n", current_client_connections);
  Cli_Printf("Transactins In Progress -- %d\n", current_client_transactions);
  Cli_Printf("--Server--\n");
  Cli_Printf("Total Document Bytes ----- %d MB\n", origin_server_response_document_total_size / (1024 * 1024));
  Cli_Printf("Total Header Bytes ------- %d MB\n", origin_server_response_header_total_size / (1024 * 1024));
  Cli_Printf("Total Connections -------- %d\n", current_server_connections);
  Cli_Printf("Transactins In Progress -- %d\n", current_server_transactions);
  Cli_Printf("\n");

  return CLI_OK;
}

// show proxy-stats sub-command
int
ShowNntpStats()
{
  INKInt client_open_connections = -1;
  INKInt client_bytes_read = -1;
  INKInt client_bytes_written = -1;
  INKInt server_open_connections = -1;
  INKInt server_bytes_read = -1;
  INKInt server_bytes_written = -1;
  INKInt article_hits = -1;
  INKInt article_misses = -1;
  INKInt overview_hits = -1;
  INKInt overview_refreshes = -1;
  INKInt group_hits = -1;
  INKInt group_refreshes = -1;
  INKInt posts = -1;
  INKInt post_bytes = -1;
  INKInt pull_bytes = -1;
  INKInt feed_bytes = -1;

  // get values

  Cli_RecordGetInt("proxy.process.nntp.client_connections_currently_open", &client_open_connections);
  Cli_RecordGetInt("proxy.process.nntp.client_bytes_read", &client_bytes_read);
  Cli_RecordGetInt("proxy.process.nntp.client_bytes_written", &client_bytes_written);
  Cli_RecordGetInt("proxy.process.nntp.server_connections_currently_open", &server_open_connections);
  Cli_RecordGetInt("proxy.process.nntp.server_bytes_read", &server_bytes_read);
  Cli_RecordGetInt("proxy.process.nntp.server_bytes_written", &server_bytes_written);
  Cli_RecordGetInt("proxy.process.nntp.article_hits", &article_hits);
  Cli_RecordGetInt("proxy.process.nntp.article_misses", &article_misses);
  Cli_RecordGetInt("proxy.process.nntp.overview_hits", &overview_hits);
  Cli_RecordGetInt("proxy.process.nntp.overview_refreshes", &overview_refreshes);
  Cli_RecordGetInt("proxy.process.nntp.group_hits", &group_hits);
  Cli_RecordGetInt("proxy.process.nntp.group_refreshes", &group_refreshes);
  Cli_RecordGetInt("proxy.process.nntp.posts", &posts);
  Cli_RecordGetInt("proxy.process.nntp.post_bytes", &post_bytes);
  Cli_RecordGetInt("proxy.process.nntp.pull_bytes", &pull_bytes);
  Cli_RecordGetInt("proxy.process.nntp.feed_bytes", &feed_bytes);

  // display results
  Cli_Printf("\n");
  Cli_Printf("--Client--\n");
  Cli_Printf("Open Connections ---- %d\n", client_open_connections);
  Cli_Printf("Bytes Read ---------- %d\n", client_bytes_read);
  Cli_Printf("Bytes Written ------- %d\n", client_bytes_written);
  Cli_Printf("--Server--\n");
  Cli_Printf("Open Connections ---- %d\n", server_open_connections);
  Cli_Printf("Bytes Read ---------- %d\n", server_bytes_read);
  Cli_Printf("Bytes Written ------- %d\n", server_bytes_written);
  Cli_Printf("--Operations--\n");
  Cli_Printf("Article Hits -------- %d\n", article_hits);
  Cli_Printf("Article Misses ------ %d\n", article_misses);
  Cli_Printf("Overview Hits ------- %d\n", overview_hits);
  Cli_Printf("Overview Refreshes -- %d\n", overview_refreshes);
  Cli_Printf("Group Hits ---------- %d\n", group_hits);
  Cli_Printf("Group Refreshes ----- %d\n", group_refreshes);
  Cli_Printf("Posts --------------- %d\n", posts);
  Cli_Printf("Post Bytes ---------- %d\n", post_bytes);
  Cli_Printf("Pull Bytes ---------- %d\n", pull_bytes);
  Cli_Printf("Feed Bytes ---------- %d\n", feed_bytes);
  Cli_Printf("\n");

  return CLI_OK;
}

// show proxy-stats sub-command
int
ShowFtpStats()
{
  //variable declaration

  INKInt connections_currently_open = -1;
  INKInt connections_successful_pasv = -1;
  INKInt connections_failed_pasv = -1;
  INKInt connections_successful_port = -1;
  INKInt connections_failed_port = -1;

  //get value   
  Cli_RecordGetInt("proxy.process.ftp.connections_currently_open", &connections_currently_open);
  Cli_RecordGetInt("proxy.process.ftp.connections_successful_pasv", &connections_successful_pasv);
  Cli_RecordGetInt("proxy.process.ftp.connections_failed_pasv", &connections_failed_pasv);
  Cli_RecordGetInt("proxy.process.ftp.connections_successful_port", &connections_successful_port);
  Cli_RecordGetInt("proxy.process.ftp.connections_failed_port", &connections_failed_port);

  //display values
  Cli_Printf("\n");
  Cli_Printf("Open Connections ------------ %d\n", connections_currently_open);
  Cli_Printf("PASV Connections Successes -- %d\n", connections_successful_pasv);
  Cli_Printf("PASV Connections Failure ---- %d\n", connections_failed_pasv);
  Cli_Printf("PORT Connections Successes -- %d\n", connections_successful_port);
  Cli_Printf("PORT Connections Failure ---- %d\n", connections_failed_port);
  Cli_Printf("\n");

  return CLI_OK;
}

// show proxy-stats sub-command
int
ShowIcpStats()
{
  //variable declaration

  INKInt icp_query_requests = -1;
  INKInt total_udp_send_queries = -1;
  INKInt icp_query_hits = -1;
  INKInt icp_query_misses = -1;
  INKInt icp_remote_responses = -1;
  INKFloat total_icp_response_time = -1.0;
  INKFloat total_icp_request_time = -1.0;
  INKInt icp_remote_query_requests = -1;
  INKInt cache_lookup_success = -1;
  INKInt cache_lookup_fail = -1;
  INKInt query_response_write = -1;

  //get values
  Cli_RecordGetInt("proxy.process.icp.icp_query_requests", &icp_query_requests);
  Cli_RecordGetInt("proxy.process.icp.total_udp_send_queries", &total_udp_send_queries);
  Cli_RecordGetInt("proxy.process.icp.icp_query_hits", &icp_query_hits);
  Cli_RecordGetInt("proxy.process.icp.icp_query_misses", &icp_query_misses);
  Cli_RecordGetInt("proxy.process.icp.icp_remote_responses", &icp_remote_responses);
  Cli_RecordGetFloat("proxy.process.icp.total_icp_response_time", &total_icp_response_time);
  Cli_RecordGetFloat("proxy.process.icp.total_icp_request_time", &total_icp_request_time);
  Cli_RecordGetInt("proxy.process.icp.icp_remote_query_requests", &icp_remote_query_requests);
  Cli_RecordGetInt("proxy.process.icp.cache_lookup_success", &cache_lookup_success);
  Cli_RecordGetInt("proxy.process.icp.cache_lookup_fail", &cache_lookup_fail);
  Cli_RecordGetInt("proxy.process.icp.query_response_write", &query_response_write);

  //display values
  Cli_Printf("\n");
  Cli_Printf("--Queries Originating From This Node--\n");
  Cli_Printf("Query Requests ----------------------------- %d\n", icp_query_requests);
  Cli_Printf("Query Messages Sent ------------------------ %d\n", total_udp_send_queries);
  Cli_Printf("Peer Hit Messages Received ----------------- %d\n", icp_query_hits);
  Cli_Printf("Peer Miss Messages Received ---------------- %d\n", icp_query_misses);
  Cli_Printf("Total Responses Received ------------------- %d\n", icp_remote_responses);
  Cli_Printf("Average ICP Message Response Time ---------- %f ms\n", total_icp_response_time);
  Cli_Printf("Average ICP Request Time ------------------- %f ms\n", total_icp_request_time);
  Cli_Printf("\n");
  Cli_Printf("--Queries Originating from ICP Peers--\n");
  Cli_Printf("Query Messages Received -------------------- %d\n", icp_remote_query_requests);
  Cli_Printf("Remote Query Hits -------------------------- %d\n", cache_lookup_success);
  Cli_Printf("Remote Query Misses ------------------------ %d\n", cache_lookup_fail);
  Cli_Printf("Successful Response Message Sent to Peers -- %d\n", query_response_write);
  Cli_Printf("\n");

  return CLI_OK;
}

// show proxy-stats sub-command
int
ShowCacheStats()
{
  //variable delaration
  INKInt bytes_used = -1;
  INKInt bytes_total = -1;
  INKInt ram_cache_total_bytes = -1;
  INKInt ram_cache_bytes_used = -1;
  INKInt ram_cache_hits = -1;
  INKInt ram_cache_misses = -1;
  INKInt lookup_active = -1;
  INKInt lookup_success = -1;
  INKInt lookup_failure = -1;
  INKInt read_active = -1;
  INKInt read_success = -1;
  INKInt read_failure = -1;
  INKInt write_active = -1;
  INKInt write_success = -1;
  INKInt write_failure = -1;
  INKInt update_active = -1;
  INKInt update_success = -1;
  INKInt update_failure = -1;
  INKInt remove_active = -1;
  INKInt remove_success = -1;
  INKInt remove_failure = -1;

  //get values

  Cli_RecordGetInt("proxy.process.cache.bytes_used", &bytes_used);
  Cli_RecordGetInt("proxy.process.cache.bytes_total", &bytes_total);
  Cli_RecordGetInt("proxy.process.cache.ram_cache.total_bytes", &ram_cache_total_bytes);
  Cli_RecordGetInt("proxy.process.cache.ram_cache.bytes_used", &ram_cache_bytes_used);
  Cli_RecordGetInt("proxy.process.cache.ram_cache.hits", &ram_cache_hits);
  Cli_RecordGetInt("proxy.process.cache.ram_cache.misses", &ram_cache_misses);
  Cli_RecordGetInt("proxy.process.cache.lookup.active", &lookup_active);
  Cli_RecordGetInt("proxy.process.cache.lookup.success", &lookup_success);
  Cli_RecordGetInt("proxy.process.cache.lookup.failure", &lookup_failure);
  Cli_RecordGetInt("proxy.process.cache.read.active", &read_active);
  Cli_RecordGetInt("proxy.process.cache.read.success", &read_success);
  Cli_RecordGetInt("proxy.process.cache.read.failure", &read_failure);
  Cli_RecordGetInt("proxy.process.cache.write.active", &write_active);
  Cli_RecordGetInt("proxy.process.cache.write.success", &write_success);
  Cli_RecordGetInt("proxy.process.cache.write.failure", &write_failure);
  Cli_RecordGetInt("proxy.process.cache.update.active", &update_active);
  Cli_RecordGetInt("proxy.process.cache.update.success", &update_success);
  Cli_RecordGetInt("proxy.process.cache.update.failure", &update_failure);
  Cli_RecordGetInt("proxy.process.cache.remove.active", &remove_active);
  Cli_RecordGetInt("proxy.process.cache.remove.success", &remove_success);
  Cli_RecordGetInt("proxy.process.cache.remove.failure", &remove_failure);

  //display values  
  Cli_Printf("\n");
  Cli_Printf("Bytes Used --- %d GB\n", bytes_used / (1024 * 1024 * 1024));
  Cli_Printf("Cache Size --- %d GB\n", bytes_total / (1024 * 1024 * 1024));
  Cli_Printf("--RAM Cache--\n");
  Cli_Printf("Total Bytes -- %d\n", ram_cache_total_bytes);
  Cli_Printf("Bytes Used --- %d\n", ram_cache_bytes_used);
  Cli_Printf("Hits --------- %d\n", ram_cache_hits);
  Cli_Printf("Misses ------- %d\n", ram_cache_misses);
  Cli_Printf("--Lookups--\n");
  Cli_Printf("In Progress -- %d\n", lookup_active);
  Cli_Printf("Hits --------- %d\n", lookup_success);
  Cli_Printf("Misses ------- %d\n", lookup_failure);
  Cli_Printf("--Reads--\n");
  Cli_Printf("In Progress -- %d\n", read_active);
  Cli_Printf("Hits --------- %d\n", read_success);
  Cli_Printf("Misses ------- %d\n", read_failure);
  Cli_Printf("--Writes--\n");
  Cli_Printf("In Progress -- %d\n", write_active);
  Cli_Printf("Hits --------- %d\n", write_success);
  Cli_Printf("Misses ------- %d\n", write_failure);
  Cli_Printf("--Updates--\n");
  Cli_Printf("In Progress -- %d\n", update_active);
  Cli_Printf("Hits --------- %d\n", update_success);
  Cli_Printf("Misses ------- %d\n", update_failure);
  Cli_Printf("--Removes--\n");
  Cli_Printf("In Progress -- %d\n", remove_active);
  Cli_Printf("Hits --------- %d\n", remove_success);
  Cli_Printf("Misses ------- %d\n", remove_failure);
  Cli_Printf("\n");

  return CLI_OK;
}

// show proxy-stats sub-command
int
ShowHostDbStats()
{
  //variables 
  INKFloat hit_ratio = -1.0;
  INKFloat lookups_per_second = -1.0;

  //get values
  Cli_RecordGetFloat("proxy.node.hostdb.hit_ratio", &hit_ratio);
  Cli_RecordGetFloat("proxy.node.dns.lookups_per_second", &lookups_per_second);

  //display values
  Cli_Printf("\n");
  Cli_Printf("Host Database hit Rate -- %f %% *\n", 100 * hit_ratio);
  Cli_Printf("DNS Lookups Per Second -- %f\n", lookups_per_second);
  Cli_Printf("\n* Value reprensents 10 second average.\n");
  Cli_Printf("\n");

  return CLI_OK;
}

// show proxy-stats sub-command
int
ShowDnsStats()
{
  INKFloat lookups_per_second = -1.0;

  Cli_RecordGetFloat("proxy.node.dns.lookups_per_second", &lookups_per_second);
  Cli_Printf("\n");
  Cli_Printf("DNS Lookups Per Second -- %f\n", lookups_per_second);
  Cli_Printf("\n");

  return CLI_OK;
}

// show proxy-stats sub-command
int
ShowLoggingStats()
{
  INKCounter log_file_open = -1;
  INKInt log_files_space_used = -1;
  INKCounter event_log_access = -1;
  INKCounter event_log_access_skip = -1;
  INKCounter event_log_error = -1;

  Cli_RecordGetCounter("proxy.process.log2.log_files_open", &log_file_open);
  Cli_RecordGetInt("proxy.process.log2.log_files_space_used", &log_files_space_used);
  Cli_RecordGetCounter("proxy.process.log2.event_log_access", &event_log_access);
  Cli_RecordGetCounter("proxy.process.log2.event_log_access_skip", &event_log_access_skip);
  Cli_RecordGetCounter("proxy.process.log2.event_log_error", &event_log_error);

  Cli_Printf("\n");
  Cli_Printf("Current Open Log Files ----------- %d\n", log_file_open);
  Cli_Printf("Space Used For Log Files --------- %d\n", log_files_space_used);
  Cli_Printf("Number of Access Events Logged --- %d\n", event_log_access);
  Cli_Printf("Number of Access Events Skipped -- %d\n", event_log_access_skip);
  Cli_Printf("Number of Error Events Logged ---- %d\n", event_log_error);
  Cli_Printf("\n");

  return CLI_OK;
}

// show:alarms sub-command
int
ShowAlarms()
{
  INKList events;
  int count, i;
  char *name;

  events = INKListCreate();
  INKError status = INKActiveEventGetMlt(events);
  if (status != INK_ERR_OKAY) {
    if (events) {
      INKListDestroy(events);
    }
    Cli_Error(ERR_ALARM_LIST);
    return CLI_ERROR;
  }

  count = INKListLen(events);
  if (count > 0) {
    Cli_Printf("\nActive Alarms\n");
    for (i = 0; i < count; i++) {
      name = (char *) INKListDequeue(events);
      Cli_Printf("  %d. %s\n", i + 1, name);
    }
    Cli_Printf("\n");
  } else {
    Cli_Printf("\nNo active alarms.\n\n");
  }

  INKListDestroy(events);

  return CLI_OK;
}
