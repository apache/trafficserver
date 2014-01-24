/** @file

  This file contains the "show" command implementation.

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
Cmd_Show(ClientData /* clientData ATS_UNUSED */, Tcl_Interp * interp, int argc, const char *argv[])
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

  int cmdinfo_size = sizeof(char) * (strlen(Tcl_GetStringResult(interp)) + 2);
  cmdinfo = (char *)alloca(cmdinfo_size);
  ink_strlcpy(cmdinfo, Tcl_GetStringResult(interp), cmdinfo_size);
  int temp_size = sizeof(char) * (strlen(cmdinfo) + 20);
  temp = (char *)alloca(temp_size);
  ink_strlcpy(temp, "lsort \"", temp_size);
  ink_strlcat(temp, cmdinfo, temp_size);
  ink_strlcat(temp, "\"", temp_size);
  Tcl_Eval(interp, temp);
  ink_strlcpy(cmdinfo, Tcl_GetStringResult(interp), cmdinfo_size);
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
Cmd_ShowStatus(ClientData /* clientData ATS_UNUSED */, Tcl_Interp * interp, int argc, const char *argv[])
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
Cmd_ShowVersion(ClientData /* clientData ATS_UNUSED */, Tcl_Interp * interp, int argc, const char *argv[])
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
Cmd_ShowPorts(ClientData /* clientData ATS_UNUSED */, Tcl_Interp * interp, int argc, const char *argv[])
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
Cmd_ShowCluster(ClientData /* clientData ATS_UNUSED */, Tcl_Interp * interp, int argc, const char *argv[])
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
Cmd_ShowSecurity(ClientData /* clientData ATS_UNUSED */, Tcl_Interp * interp, int argc, const char *argv[])
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
Cmd_ShowHttp(ClientData /* clientData ATS_UNUSED */, Tcl_Interp * interp, int argc, const char *argv[])
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
Cmd_ShowProxy(ClientData /* clientData ATS_UNUSED */, Tcl_Interp * interp, int argc, const char *argv[])
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
Cmd_ShowVirtualIp(ClientData /* clientData ATS_UNUSED */, Tcl_Interp * interp, int argc, const char *argv[])
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
Cmd_ShowHostDb(ClientData /* clientData ATS_UNUSED */, Tcl_Interp * interp, int argc, const char *argv[])
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
Cmd_ShowDnsResolver(ClientData /* clientData ATS_UNUSED */, Tcl_Interp * interp, int argc, const char *argv[])
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
Cmd_ShowLogging(ClientData /* clientData ATS_UNUSED */, Tcl_Interp * interp, int argc, const char *argv[])
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
Cmd_ShowSsl(ClientData /* clientData ATS_UNUSED */, Tcl_Interp * interp, int argc, const char *argv[])
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
Cmd_ShowRemap(ClientData /* clientData ATS_UNUSED */, Tcl_Interp * interp, int argc, const char *argv[])
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
Cmd_ShowProxyStats(ClientData /* clientData ATS_UNUSED */, Tcl_Interp * interp, int argc, const char *argv[])
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
Cmd_ShowHttpTransStats(ClientData /* clientData ATS_UNUSED */, Tcl_Interp * interp, int argc, const char *argv[])
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
Cmd_ShowHttpStats(ClientData /* clientData ATS_UNUSED */, Tcl_Interp * interp, int argc, const char *argv[])
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
Cmd_ShowIcpStats(ClientData /* clientData ATS_UNUSED */, Tcl_Interp * interp, int argc, const char *argv[])
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
Cmd_ShowCacheStats(ClientData /* clientData ATS_UNUSED */, Tcl_Interp * interp, int argc, const char *argv[])
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
Cmd_ShowHostDbStats(ClientData /* clientData ATS_UNUSED */, Tcl_Interp * interp, int argc, const char *argv[])
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
Cmd_ShowDnsStats(ClientData /* clientData ATS_UNUSED */, Tcl_Interp * interp, int argc, const char *argv[])
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
Cmd_ShowLoggingStats(ClientData /* clientData ATS_UNUSED */, Tcl_Interp * interp, int argc, const char *argv[])
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
Cmd_ShowAlarms(ClientData /* clientData ATS_UNUSED */, Tcl_Interp * interp, int argc, const char *argv[])
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
  TSProxyStateT state = TSProxyStateGet();
  Cli_Printf("\n");
  switch (state) {
  case TS_PROXY_ON:
    Cli_Printf("Proxy -- on\n");
    break;
  case TS_PROXY_OFF:
    Cli_Printf("Proxy -- off\n");
    break;
  case TS_PROXY_UNDEFINED:
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
  TSError status = TS_ERR_OKAY;
  TSString ts_version = NULL;
  TSString tm_version = NULL;

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
  TSString http_ports = NULL;
  TSInt cluster = -1;
  TSInt cluster_rs = -1;
  TSInt cluster_mc = -1;
  TSInt socks_server = -1;
  TSInt icp = -1;
  TSString connect = NULL;

  // retrieve values

  Cli_RecordGetString("proxy.config.http.server_ports", &http_ports);
  Cli_RecordGetInt("proxy.config.cluster.cluster_port", &cluster);
  Cli_RecordGetInt("proxy.config.cluster.rsport", &cluster_rs);
  Cli_RecordGetInt("proxy.config.cluster.mcport", &cluster_mc);
  Cli_RecordGetString("proxy.config.http.connect_ports", &connect);
  Cli_RecordGetInt("proxy.config.socks.socks_server_port", &socks_server);
  Cli_RecordGetInt("proxy.config.icp.icp_port", &icp);

  // display results
  Cli_Printf("\n");
  Cli_Printf("HTTP Ports ------------- %s\n", (http_ports != NULL) ? http_ports : "none");
  Cli_Printf("Cluster Port ----------- %d\n", cluster);
  Cli_Printf("Cluster RS Port -------- %d\n", cluster_rs);
  Cli_Printf("Cluster MC Port -------- %d\n", cluster_mc);
  Cli_Printf("Allowed CONNECT Ports -- %s\n", (connect != NULL) ? connect : "none");
  Cli_Printf("SOCKS Server Port ------ %d\n", socks_server);
  Cli_Printf("ICP Port --------------- %d\n", icp);
  Cli_Printf("\n");

  return CLI_OK;
}

// show cluster sub-command
int
ShowCluster()
{
  TSInt cluster = -1;
  TSInt cluster_rs = -1;
  TSInt cluster_mc = -1;

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
  Cli_Printf("\n");
  Cli_Printf("Traffic Server Access\n" "-------------------\n");
  TSError status = Cli_DisplayRules(TS_FNAME_IP_ALLOW);

  return status;
}

// show http sub-command
int
ShowHttp()
{
  // declare and initialize variables

  TSInt http_enabled = -1;
  TSInt keepalive_timeout_in = -1;
  TSInt keepalive_timeout_out = -1;
  TSInt inactivity_timeout_in = -1;
  TSInt inactivity_timeout_out = -1;
  TSInt activity_timeout_in = -1;
  TSInt activity_timeout_out = -1;
  TSInt max_alts = -1;
  TSInt remove_from = -1;
  TSInt remove_referer = -1;
  TSInt remove_user_agent = -1;
  TSInt remove_cookie = -1;
  TSString other_header_list = NULL;
  TSInt insert_client_ip = -1;
  TSInt remove_client_ip = -1;
  TSInt http_server = -1;
  TSString http_other = NULL;
  TSString global_user_agent = NULL;

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

// show icp sub-command
int
ShowIcp()
{
  // declare and initialize variables

  TSInt icp_enabled = 0;
  TSInt icp_port = -1;
  TSInt multicast_enabled = 0;
  TSInt query_timeout = 2;

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
  TSError status = Cli_DisplayRules(TS_FNAME_ICP_PEER);
  Cli_Printf("\n");

  return status;
}

// show proxy sub-command
int
ShowProxy()
{

  TSString proxy_name = NULL;

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

  TSInt cache_http = -1;
  TSInt cache_bypass = -1;
  TSInt max_doc_size = -1;
  TSInt when_to_reval = -1;
  TSInt reqd_headers = -1;
  TSInt min_life = -1;
  TSInt max_life = -1;
  TSInt dynamic_urls = -1;
  TSInt alternates = -1;
  const char *vary_def_text = "NONE";
  const char *vary_def_image = "NONE";
  const char *vary_def_other = "NONE";
  TSInt cookies = -1;

  // retrieve values

  Cli_RecordGetInt("proxy.config.http.cache.http", &cache_http);
  Cli_RecordGetInt("proxy.config.cache.max_doc_size", &max_doc_size);
  Cli_RecordGetInt("proxy.config.http.cache.when_to_revalidate", &when_to_reval);
  Cli_RecordGetInt("proxy.config.http.cache.required_headers", &reqd_headers);
  Cli_RecordGetInt("proxy.config.http.cache.heuristic_min_lifetime", &min_life);
  Cli_RecordGetInt("proxy.config.http.cache.heuristic_max_lifetime", &max_life);
  Cli_RecordGetInt("proxy.config.http.cache.cache_urls_that_look_dynamic", &dynamic_urls);
  Cli_RecordGetInt("proxy.config.http.cache.enable_default_vary_headers", &alternates);
  Cli_RecordGetString("proxy.config.http.cache.vary_default_text", (char**)&vary_def_text);
  Cli_RecordGetString("proxy.config.http.cache.vary_default_images", (char**)&vary_def_image);
  Cli_RecordGetString("proxy.config.http.cache.vary_default_other", (char**)&vary_def_other);

  Cli_RecordGetInt("proxy.config.http.cache.cache_responses_to_cookies", &cookies);


  // display results
  Cli_Printf("\n");

  Cli_PrintEnable("HTTP Caching --------------------------- ", cache_http);

  Cli_PrintEnable("Ignore User Requests To Bypass Cache --- ", cache_bypass);

  if (max_doc_size == 0)
    Cli_Printf("Maximum HTTP Object Size ----------- NONE\n");
  else
    Cli_Printf("Maximum HTTP Object Size ----------- %d\n", max_doc_size);

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
  TSError status = Cli_DisplayRules(TS_FNAME_CACHE_OBJ);

  Cli_Printf("\n");

  return status;
}

// show cache storage sub-command
int
ShowCacheStorage()
{
  // display rules from storage.config
  Cli_Printf("storage.config rules\n");

  TSError status = Cli_DisplayRules(TS_FNAME_STORAGE);

  return status;
}


// show virtual-ip sub-command
int
ShowVirtualIp()
{
  TSCfgContext VipCtx;
  int EleCount, i;
  TSVirtIpAddrEle *VipElePtr;

  VipCtx = TSCfgContextCreate(TS_FNAME_VADDRS);
  if (TSCfgContextGet(VipCtx) != TS_ERR_OKAY)
    Cli_Printf("ERROR READING FILE\n");
  EleCount = TSCfgContextGetCount(VipCtx);
  Cli_Printf("\n");
  Cli_Printf("%d Elements in Record\n", EleCount);
  Cli_Printf("\n");
  for (i = 0; i < EleCount; i++) {
    VipElePtr = (TSVirtIpAddrEle *) TSCfgContextGetEleAt(VipCtx, i);
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

  TSInt lookup_timeout = -1;
  TSInt timeout = -1;
  TSInt verify_after = -1;
  TSInt fail_timeout = -1;
  TSInt re_dns_on_reload = 0;
  TSInt dns_lookup_timeout = -1;
  TSInt dns_retries = -1;

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

  TSInt dns_search_default_domains = 0;
  TSInt http_enable_url_expandomatic = 0;

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

  TSInt logging_enabled = 0;
  TSInt log_space = -1;
  TSInt headroom_space = -1;
  TSInt collation_mode = 0;
  const char *collation_host = "None";
  TSInt collation_port = -1;
  TSString collation_secret = NULL;
  TSInt host_tag = 0;
  TSInt preproc_threads = 0;
  TSInt orphan_space = -1;

  TSInt squid_log = 0;
  TSInt is_ascii = 1;
  TSString file_name = NULL;
  TSString file_header = NULL;

  TSInt common_log = 0;
  TSInt common_is_ascii = 0;
  TSString common_file_name = NULL;
  TSString common_file_header = NULL;

  TSInt extended_log = 0;
  TSInt extended_is_ascii = 0;
  TSString extended_file_name = NULL;
  TSString extended_file_header = NULL;

  TSInt extended2_log = 0;
  TSInt extended2_is_ascii = 0;
  TSString extended2_file_name = NULL;
  TSString extended2_file_header = NULL;

  TSInt icp_log = 0;
  TSInt http_host_log = 0;
  TSInt custom_log = 0;
  TSInt xml_log = 0;
  TSInt rolling = 0;
  TSInt roll_offset_hr = -1;
  TSInt roll_interval = -1;
  TSInt auto_delete = 0;
  // retrieve value

  Cli_RecordGetInt("proxy.config.log.logging_enabled", &logging_enabled);
  Cli_RecordGetInt("proxy.config.log.max_space_mb_for_logs", &log_space);
  Cli_RecordGetInt("proxy.config.log.max_space_mb_headroom", &headroom_space);
  Cli_RecordGetInt("proxy.local.log.collation_mode", &collation_mode);
  Cli_RecordGetString("proxy.config.log.collation_host", (char**)&collation_host);
  Cli_RecordGetInt("proxy.config.log.collation_port", &collation_port);
  Cli_RecordGetString("proxy.config.log.collation_secret", &collation_secret);
  Cli_RecordGetInt("proxy.config.log.collation_host_tagged", &host_tag);
  Cli_RecordGetInt("proxy.config.log.max_space_mb_for_orphan_logs", &orphan_space);
  Cli_RecordGetInt("proxy.config.log.collation_preproc_threads", &preproc_threads);

  Cli_RecordGetInt("proxy.config.log.squid_log_enabled", &squid_log);
  Cli_RecordGetInt("proxy.config.log.squid_log_is_ascii", &is_ascii);
  Cli_RecordGetString("proxy.config.log.squid_log_name", &file_name);
  Cli_RecordGetString("proxy.config.log.squid_log_header", &file_header);

  Cli_RecordGetInt("proxy.config.log.common_log_enabled", &common_log);
  Cli_RecordGetInt("proxy.config.log.common_log_is_ascii", &common_is_ascii);
  Cli_RecordGetString("proxy.config.log.common_log_name", &common_file_name);
  Cli_RecordGetString("proxy.config.log.common_log_header", &common_file_header);

  Cli_RecordGetInt("proxy.config.log.extended_log_enabled", &extended_log);
  Cli_RecordGetInt("proxy.config.log.extended_log_is_ascii", &extended_is_ascii);
  Cli_RecordGetString("proxy.config.log.extended_log_name", &extended_file_name);
  Cli_RecordGetString("proxy.config.log.extended_log_header", &extended_file_header);

  Cli_RecordGetInt("proxy.config.log.extended2_log_enabled", &extended2_log);
  Cli_RecordGetInt("proxy.config.log.extended2_log_is_ascii", &extended2_is_ascii);
  Cli_RecordGetString("proxy.config.log.extended2_log_name", &extended2_file_name);
  Cli_RecordGetString("proxy.config.log.extended2_log_header", &extended2_file_header);

  Cli_RecordGetInt("proxy.config.log.separate_icp_logs", &icp_log);
  Cli_RecordGetInt("proxy.config.log.separate_host_logs", &http_host_log);
  Cli_RecordGetInt("proxy.config.log.separate_host_logs", &custom_log);

  Cli_RecordGetInt("proxy.config.log.rolling_enabled", &rolling);
  Cli_RecordGetInt("proxy.config.log.rolling_offset_hr", &roll_offset_hr);
  Cli_RecordGetInt("proxy.config.log.rolling_interval_sec", &roll_interval);
  Cli_RecordGetInt("proxy.config.log.auto_delete_rolled_files", &auto_delete);

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
  Cli_PrintEnable("  Preproc Threads ------------------------ ", preproc_threads);
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

  TSString connect_ports = NULL;

  // retrieve value

  Cli_RecordGetString("proxy.config.http.connect_ports", &connect_ports);

  // display results
  Cli_Printf("\n");
  Cli_Printf("Restrict CONNECT connections to Ports -- %s\n", connect_ports);
  Cli_Printf("\n");

  return CLI_OK;
}


// show parents sub-command
int
ShowParents()
{
  TSError status = TS_ERR_OKAY;
  TSInt parent_enabled = -1;
  TSString parent_cache = NULL;

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
  TSError status = Cli_DisplayRules(TS_FNAME_PARENT_PROXY);
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
  TSError status = Cli_DisplayRules(TS_FNAME_REMAP);
  Cli_Printf("\n");

  return status;
}

// show socks sub-command
int
ShowSocks()
{
  // declare and initialize variables

  TSInt socks_enabled = 0;
  TSInt version = -1;
  TSString default_servers = NULL;
  TSInt accept_enabled = -1;
  TSInt accept_port = -1;

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
  TSError status = Cli_DisplayRules(TS_FNAME_SOCKS);
  Cli_Printf("\n");

  return status;
}

// show scheduled-update sub-command
int
ShowScheduledUpdate()
{
  // variable declaration
  TSInt enabled = 0;
  TSInt force = 0;
  TSInt retry_count = -1;
  TSInt retry_interval = -1;
  TSInt concurrent_updates = 0;

  // get value
  Cli_RecordGetInt("proxy.config.update.enabled", &enabled);
  Cli_RecordGetInt("proxy.config.update.retry_count", &retry_count);
  Cli_RecordGetInt("proxy.config.update.retry_interval", &retry_interval);
  Cli_RecordGetInt("proxy.config.update.concurrent_updates", &concurrent_updates);
  Cli_RecordGetInt("proxy.config.update.force", &force);
  //display values

  // display rules from socks.config
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
  Cli_Printf("\n");

  Cli_Printf("update.config rules\n" "-------------------\n");
  TSError status = Cli_DisplayRules(TS_FNAME_UPDATE_URL);
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

  TSFloat cache_hit_ratio = -1.0;
  TSFloat cache_hit_mem_ratio = -1.0;
  TSFloat bandwidth_hit_ratio = -1.0;
  TSFloat percent_free = -1.0;
  TSInt current_server_connection = -1;
  TSInt current_client_connection = -1;
  TSInt current_cache_connection = -1;
  TSFloat client_throughput_out = -1.0;
  TSFloat xacts_per_second = -1.0;


  //get value
  Cli_RecordGetFloat("proxy.node.cache_hit_ratio", &cache_hit_ratio);
  Cli_RecordGetFloat("proxy.node.cache_hit_mem_ratio", &cache_hit_mem_ratio);
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
  Cli_Printf("Ram cache Hit Rate ------- %f %%\t *\n", 100 * cache_hit_mem_ratio);
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
  TSFloat frac_avg_10s_hit_fresh = -1.0;
  TSInt msec_avg_10s_hit_fresh = -1;
  TSFloat frac_avg_10s_hit_revalidated = -1.0;
  TSInt msec_avg_10s_hit_revalidated = -1;
  TSFloat frac_avg_10s_miss_cold = -1.0;
  TSInt msec_avg_10s_miss_cold = -1;
  TSFloat frac_avg_10s_miss_not_cachable = -1.0;
  TSInt msec_avg_10s_miss_not_cachable = -1;
  TSFloat frac_avg_10s_miss_changed = -1.0;
  TSInt msec_avg_10s_miss_changed = -1;
  TSFloat frac_avg_10s_miss_client_no_cache = -1.0;
  TSInt msec_avg_10s_miss_client_no_cache = -1;
  TSFloat frac_avg_10s_errors_connect_failed = -1.0;
  TSInt msec_avg_10s_errors_connect_failed = -1;
  TSFloat frac_avg_10s_errors_other = -1.0;
  TSInt msec_avg_10s_errors_other = -1;
  TSFloat frac_avg_10s_errors_aborts = -1.0;
  TSInt msec_avg_10s_errors_aborts = -1;
  TSFloat frac_avg_10s_errors_possible_aborts = -1.0;
  TSInt msec_avg_10s_errors_possible_aborts = -1;
  TSFloat frac_avg_10s_errors_early_hangups = -1.0;
  TSInt msec_avg_10s_errors_early_hangups = -1;
  TSFloat frac_avg_10s_errors_empty_hangups = -1.0;
  TSInt msec_avg_10s_errors_empty_hangups = -1;
  TSFloat frac_avg_10s_errors_pre_accept_hangups = -1.0;
  TSInt msec_avg_10s_errors_pre_accept_hangups = -1;
  TSFloat frac_avg_10s_other_unclassified = -1.0;
  TSInt msec_avg_10s_other_unclassified = -1;

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
             msec_avg_10s_errors_early_hangups);
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
  TSInt user_agent_response_document_total_size = -1;
  TSInt user_agent_response_header_total_size = -1;
  TSInt current_client_connections = -1;
  TSInt current_client_transactions = -1;
  TSInt origin_server_response_document_total_size = -1;
  TSInt origin_server_response_header_total_size = -1;
  TSInt current_server_connections = -1;
  TSInt current_server_transactions = -1;
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
ShowIcpStats()
{
  //variable declaration

  TSInt icp_query_requests = -1;
  TSInt total_udp_send_queries = -1;
  TSInt icp_query_hits = -1;
  TSInt icp_query_misses = -1;
  TSInt icp_remote_responses = -1;
  TSFloat total_icp_response_time = -1.0;
  TSFloat total_icp_request_time = -1.0;
  TSInt icp_remote_query_requests = -1;
  TSInt cache_lookup_success = -1;
  TSInt cache_lookup_fail = -1;
  TSInt query_response_write = -1;

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
  TSInt bytes_used = -1;
  TSInt bytes_total = -1;
  TSInt ram_cache_total_bytes = -1;
  TSInt ram_cache_bytes_used = -1;
  TSInt ram_cache_hits = -1;
  TSInt ram_cache_misses = -1;
  TSInt lookup_active = -1;
  TSInt lookup_success = -1;
  TSInt lookup_failure = -1;
  TSInt read_active = -1;
  TSInt read_success = -1;
  TSInt read_failure = -1;
  TSInt write_active = -1;
  TSInt write_success = -1;
  TSInt write_failure = -1;
  TSInt update_active = -1;
  TSInt update_success = -1;
  TSInt update_failure = -1;
  TSInt remove_active = -1;
  TSInt remove_success = -1;
  TSInt remove_failure = -1;

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
  Cli_Printf("Total Bytes -- %" PRId64 "\n", ram_cache_total_bytes);
  Cli_Printf("Bytes Used --- %" PRId64 "\n", ram_cache_bytes_used);
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
  TSFloat hit_ratio = -1.0;
  TSFloat lookups_per_second = -1.0;

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
  TSFloat lookups_per_second = -1.0;

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
  TSCounter log_file_open = -1;
  TSInt log_files_space_used = -1;
  TSCounter event_log_access = -1;
  TSCounter event_log_access_skip = -1;
  TSCounter event_log_error = -1;

  Cli_RecordGetCounter("proxy.process.log.log_files_open", &log_file_open);
  Cli_RecordGetInt("proxy.process.log.log_files_space_used", &log_files_space_used);
  Cli_RecordGetCounter("proxy.process.log.event_log_access", &event_log_access);
  Cli_RecordGetCounter("proxy.process.log.event_log_access_skip", &event_log_access_skip);
  Cli_RecordGetCounter("proxy.process.log.event_log_error", &event_log_error);

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
  TSList events;
  int count, i;
  char *name;

  events = TSListCreate();
  TSError status = TSActiveEventGetMlt(events);
  if (status != TS_ERR_OKAY) {
    if (events) {
      TSListDestroy(events);
    }
    Cli_Error(ERR_ALARM_LIST);
    return CLI_ERROR;
  }

  count = TSListLen(events);
  if (count > 0) {
    Cli_Printf("\nActive Alarms\n");
    for (i = 0; i < count; i++) {
      name = (char *) TSListDequeue(events);
      Cli_Printf("  %d. %s\n", i + 1, name);
    }
    Cli_Printf("\n");
  } else {
    Cli_Printf("\nNo active alarms.\n\n");
  }

  TSListDestroy(events);

  return CLI_OK;
}
