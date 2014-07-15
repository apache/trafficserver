/** @file

  This file contains the CLI's "config" command implementation.

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
 
  @section description createArgument("timezone",1,CLI_ARGV_OPTION_INT_VALUE,
		  (char*)NULL, CMD_CONFIG_TIMEZONE, "Time Zone",
                  (char*)NULL);
 */


#include "libts.h"
#include "I_Layout.h"
#include "mgmtapi.h"
#include "ShowCmd.h"
#include "ConfigCmd.h"
#include "createArgument.h"
#include "CliMgmtUtils.h"
#include "CliDisplay.h"
// TODO: Remove all those system defines
//       and protecect them via #ifdef HAVE_FOO_H
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/wait.h>
#include <errno.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


bool enable_restricted_commands = false;

int
u_getch(void)
{
  static signed int returned = (-1), fd;
  static struct termios new_io_settings, old_io_settings;
  fd = fileno(stdin);
  tcgetattr(fd, &old_io_settings);
  new_io_settings = old_io_settings;
  new_io_settings.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(fd, TCSANOW, &new_io_settings);
  returned = getchar();
  tcsetattr(fd, TCSANOW, &old_io_settings);
  return returned;
}

////////////////////////////////////////////////////////////////
// Cmd_Enable
//
// This is the callback
// function for the "enable" command.
// TODO: This currently doesn't do anything, these commands are
//       always available.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_Enable(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */

  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable, *infoPtr;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  infoPtr = argtable;

  /* Add "enable status" to check the status of enable/disable */
  if (argc == 2) {
    switch (infoPtr->parsed_args) {
    case CMD_ENABLE_STATUS:
      if (enable_restricted_commands == true) {
        Cli_Printf("on\n");
        return CMD_OK;
      } else {
        Cli_Printf("off\n");
        return CMD_ERROR;
      }
    default:
      Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
      return CMD_ERROR;
    }
  }

  if (enable_restricted_commands == true) {
    Cli_Printf("Already Enabled\n");
    return CMD_OK;
  }

  // TODO: replace this assert with appropriate authentication
  ink_release_assert(enable_restricted_commands);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// CmdArgs_Enable
//
// Register "enable" arguments with the Tcl interpreter.
//
int
CmdArgs_Enable()
{

  createArgument("status", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_ENABLE_STATUS, "Check Enable Status", (char *) NULL);
  return CLI_OK;
}

////////////////////////////////////////////////////////////////
// Cmd_Disable
//
// This is the callback
// function for the "disable" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_Disable(ClientData /* clientData ATS_UNUSED */, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (getuid() == 0) {
    Cli_Printf("root user cannot \"disable\"\n");
    return 0;
  }

  enable_restricted_commands = false;
  return 0;
}



////////////////////////////////////////////////////////////////
// Cmd_Config
//
// This is the callback function for the "config" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_Config(ClientData /* clientData ATS_UNUSED */, Tcl_Interp * interp, int argc, const char *argv[])
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

  Cli_Debug("Cmd_Config\n");
  Tcl_Eval(interp, "info commands config* ");

  size_t cmdinfo_len = strlen(Tcl_GetStringResult(interp)) + 2;
  cmdinfo = (char *)alloca(sizeof(char) * cmdinfo_len);
  ink_strlcpy(cmdinfo, Tcl_GetStringResult(interp), cmdinfo_len);
  size_t temp_len = strlen(cmdinfo) + 20;
  temp = (char *)alloca(sizeof(char) * temp_len);
  ink_strlcpy(temp, "lsort \"", temp_len);
  ink_strlcat(temp, cmdinfo, temp_len);
  ink_strlcat(temp, "\"", temp_len);

  Tcl_Eval(interp, temp);
  ink_strlcpy(cmdinfo, Tcl_GetStringResult(interp), cmdinfo_len);
  i = i + strlen("config ");
  while (cmdinfo[i] != 0) {
    if (cmdinfo[i] == ' ') {
      cmdinfo[i] = '\n';
    }
    i++;
  }
  cmdinfo[i] = '\n';
  i++;
  cmdinfo[i] = 0;
  Cli_Printf("Following are the available config commands\n");
  Cli_Printf(cmdinfo + strlen("config "));

  return CLI_OK;

}


////////////////////////////////////////////////////////////////
// CmdArgs_Config
//
// Register "config" command arguments with the Tcl interpreter.
//
int
CmdArgs_Config()
{
  Cli_Debug("CmdArgs_Config\n");

  return CLI_OK;
}

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
int
Cmd_ConfigGet(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:get") == CLI_ERROR) {
    return CMD_ERROR;
  }
  cli_cmdCallbackInfo *cmdCallbackInfo;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;

  Cli_Debug("Cmd_ConfigGet argc %d\n", argc);

  if (argc == 2) {
    return (ConfigGet(argv[1]));
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

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
int
Cmd_ConfigSet(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:set") == CLI_ERROR) {
    return CMD_ERROR;
  }
  cli_cmdCallbackInfo *cmdCallbackInfo;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;

  Cli_Debug("Cmd_ConfigSet argc %d\n", argc);

  if (argc == 3) {
    return (ConfigSet(argv[1], argv[2]));
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

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
int
Cmd_ConfigName(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:name") == CLI_ERROR) {
    return CMD_ERROR;
  }
  cli_cmdCallbackInfo *cmdCallbackInfo;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;

  Cli_Debug("Cmd_ConfigName argc %d\n", argc);

  return (ConfigName(argv[1]));

  // coverity[unreachable]
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

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
int
Cmd_ConfigStart(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:start") == CLI_ERROR) {
    return CMD_ERROR;
  }

  cli_cmdCallbackInfo *cmdCallbackInfo;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;

  Cli_Debug("Cmd_ConfigStart argc %d\n", argc);

  if (argc == 1) {
    return (ConfigStart());
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

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
int
Cmd_ConfigStop(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;
  if (cliCheckIfEnabled("config:stop") == CLI_ERROR) {
    return CMD_ERROR;
  }

  cli_cmdCallbackInfo *cmdCallbackInfo;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  Cli_Debug("Cmd_ConfigStop argc %d\n", argc);

  if (argc == 1) {
    return (ConfigStop());
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// Cmd_ConfigHardRestart
//
// This is the callback function for the "config:hard-restart" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigHardRestart(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;
  if (cliCheckIfEnabled("config:hard-restart") == CLI_ERROR) {
    return CMD_ERROR;
  }

  cli_cmdCallbackInfo *cmdCallbackInfo;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  Cli_Debug("Cmd_ConfigHardRestart argc %d\n", argc);

  if (argc == 1) {
    return (TSHardRestart());
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// Cmd_ConfigRestart
//
// This is the callback function for the "config:restart" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigRestart(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;
  if (cliCheckIfEnabled("config:restart") == CLI_ERROR) {
    return CMD_ERROR;
  }

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  Cli_Debug("Cmd_ConfigRestart argc %d\n", argc);

  if (argc == 1) {
    return (TSRestart(false));
  } else if (argc == 2) {
    if (argtable[0].parsed_args == CMD_CONFIG_RESTART_CLUSTER) {
      return (TSRestart(true));
    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigRestart
//
// Register "config:restart" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigRestart()
{
  createArgument("cluster", 1, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_RESTART_CLUSTER, "Restart the entire cluster", (char *) NULL);

  return 0;
}


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
int
Cmd_ConfigParents(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:parents") == CLI_ERROR) {
    return CMD_ERROR;
  }
  Cli_Debug("Cmd_ConfigParents argc %d\n", argc);

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable, *infoPtr;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  infoPtr = argtable;

  if (argc == 1) {
    Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
    return CMD_ERROR;
  }

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {
    switch (infoPtr->parsed_args) {
    case CMD_CONFIG_PARENTS_STATUS:
      return (Cli_RecordOnOff_Action((argc == 3),
                                     "proxy.config.http.parent_proxy_routing_enable", argtable->arg_string));

    case CMD_CONFIG_PARENTS_CACHE:
      return (Cli_RecordString_Action((argc == 3), "proxy.config.http.parent_proxies", argtable->arg_string));

    case CMD_CONFIG_PARENTS_CONFIG_FILE:
      return (Cli_ConfigFileURL_Action(TS_FNAME_PARENT_PROXY, "parent.config", argtable->arg_string));
    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigParents
//
// Register "config:parents" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigParents()
{

  createArgument("status", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_PARENTS_STATUS, "Parenting <on|off>", (char *) NULL);
  createArgument("name", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_PARENTS_CACHE, "Specify cache parent", (char *) NULL);
  createArgument("rules", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_PARENTS_CONFIG_FILE, "Specify config file", (char *) NULL);
  return CLI_OK;
}


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
int
Cmd_ConfigRemap(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:remap") == CLI_ERROR) {
    return CMD_ERROR;
  }
  cli_cmdCallbackInfo *cmdCallbackInfo;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  Cli_Debug("Cmd_ConfigRemap argc %d\n", argc);

  if (argc == 2) {
    return (ConfigRemap(argv[1]));
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}


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
int
Cmd_ConfigPorts(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:ports") == CLI_ERROR) {
    return CMD_ERROR;
  }

  Cli_Debug("Cmd_ConfigPorts argc %d\n", argc);

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;

  Cli_Debug("Cmd_ConfigPorts argc %d\n", argc);

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {

    if (argc == 2) {            // get
      return (ConfigPortsGet(argtable[0].parsed_args));
    } else {                    // set
      switch (argtable->parsed_args) {
      case CMD_CONFIG_PORTS_CONNECT:
        return (ConfigPortsSet(argtable[0].parsed_args, argtable[0].data));
        break;
      case CMD_CONFIG_PORTS_HTTP_SERVER:
      case CMD_CONFIG_PORTS_CLUSTER:
      case CMD_CONFIG_PORTS_CLUSTER_RS:
      case CMD_CONFIG_PORTS_CLUSTER_MC:
      case CMD_CONFIG_PORTS_SOCKS_SERVER:
      case CMD_CONFIG_PORTS_ICP:
        return (ConfigPortsSet(argtable[0].parsed_args, &argtable[0].arg_int));
      }
    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX,
            "\n\nconfig:ports <http-server | http-other | webui | cluster-rs | cluster-mc | \n  ssl | \n socks-server | icp > \n <port | ports list>\n");
  return CMD_ERROR;

}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigPorts
//
// Register "config:ports" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigPorts()
{
  createArgument("http-server", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_PORTS_HTTP_SERVER, "Set Ports for http-server", (char *) NULL);
  createArgument("cluster", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_PORTS_CLUSTER, "Set Ports for cluster", (char *) NULL);
  createArgument("cluster-rs", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_PORTS_CLUSTER_RS, "Set Ports for cluster-rs", (char *) NULL);
  createArgument("cluster-mc", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_PORTS_CLUSTER_MC, "Set Ports for cluster-mc", (char *) NULL);
  createArgument("connect", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_PORTS_CONNECT, "Set Ports for allowed CONNECT", (char *) NULL);
  createArgument("socks-server", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_PORTS_SOCKS_SERVER, "Set Ports for socks-server", (char *) NULL);
  return 0;
}

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
int
Cmd_ConfigSecurity(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:security") == CLI_ERROR) {
    return CMD_ERROR;
  }
  Cli_Debug("Cmd_ConfigSecurity argc %d\n", argc);


  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;

  if (argtable[0].parsed_args != CLI_PARSED_ARGV_END) {
    switch (argtable[0].parsed_args) {
    case CMD_CONFIG_SECURITY_IP:
      return (Cli_ConfigFileURL_Action(TS_FNAME_IP_ALLOW, "ip_allow.config", argtable->arg_string));

    case CMD_CONFIG_SECURITY_PASSWORD:
      return (ConfigSecurityPasswd());
    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigSecurity
//
// Register "config:security" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigSecurity()
{
  createArgument("ip-allow", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_SECURITY_IP, "Clients allowed to connect to proxy <url>", (char *) NULL);
  createArgument("password", 1, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_SECURITY_PASSWORD, "Change Admin Password", (char *) NULL);
  return 0;
}

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
int
Cmd_ConfigHttp(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  int setvar = 0;
  if (cliCheckIfEnabled("config:http") == CLI_ERROR) {
    return CMD_ERROR;
  }

  Cli_Debug("Cmd_ConfigHttp argc %d\n", argc);

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;

  if (argc == 3) {
    setvar = 1;
  }

  if (argc > 3) {
    Cli_Error("Too many arguments\n");
    Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
    return CMD_ERROR;
  }

  Cli_PrintArg(0, argtable);
  Cli_PrintArg(1, argtable);

  int action = (argc == 3) ? RECORD_SET : RECORD_GET;

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {
    switch (argtable->parsed_args) {
    case CMD_CONFIG_HTTP_STATUS:
      return (Cli_RecordOnOff_Action(action, "proxy.config.http.cache.http", argtable->arg_string));

    case CMD_CONFIG_HTTP_KEEP_ALIVE_TIMEOUT_IN:
      return (Cli_RecordInt_Action(action, "proxy.config.http.keep_alive_no_activity_timeout_in", argtable->arg_int));

    case CMD_CONFIG_HTTP_KEEP_ALIVE_TIMEOUT_OUT:
      return (Cli_RecordInt_Action(action, "proxy.config.http.keep_alive_no_activity_timeout_out", argtable->arg_int));

    case CMD_CONFIG_HTTP_INACTIVE_TIMEOUT_IN:
      return (Cli_RecordInt_Action(action, "proxy.config.http.transaction_no_activity_timeout_in", argtable->arg_int));

    case CMD_CONFIG_HTTP_INACTIVE_TIMEOUT_OUT:
      return (Cli_RecordInt_Action(action, "proxy.config.http.transaction_no_activity_timeout_out", argtable->arg_int));

    case CMD_CONFIG_HTTP_ACTIVE_TIMEOUT_IN:
      return (Cli_RecordInt_Action(action, "proxy.config.http.transaction_active_timeout_in", argtable->arg_int));

    case CMD_CONFIG_HTTP_ACTIVE_TIMEOUT_OUT:
      return (Cli_RecordInt_Action(action, "proxy.config.http.transaction_active_timeout_out", argtable->arg_int));

    case CMD_CONFIG_HTTP_REMOVE_FROM:
      return (Cli_RecordOnOff_Action(action, "proxy.config.http.anonymize_remove_from", argtable->arg_string));

    case CMD_CONFIG_HTTP_REMOVE_REFERER:
      return (Cli_RecordOnOff_Action(action, "proxy.config.http.anonymize_remove_referer", argtable->arg_string));

    case CMD_CONFIG_HTTP_REMOVE_USER:
      return (Cli_RecordOnOff_Action(action, "proxy.config.http.anonymize_remove_user_agent", argtable->arg_string));

    case CMD_CONFIG_HTTP_REMOVE_COOKIE:
      return (Cli_RecordOnOff_Action(action, "proxy.config.http.anonymize_remove_cookie", argtable->arg_string));

    case CMD_CONFIG_HTTP_REMOVE_HEADER:
      return (Cli_RecordString_Action(action, "proxy.config.http.anonymize_other_header_list", argtable->arg_string));

    case CMD_CONFIG_HTTP_GLOBAL_USER_AGENT:
      return (Cli_RecordString_Action(action, "proxy.config.http.global_user_agent_header", argtable->arg_string));

    case CMD_CONFIG_HTTP_INSERT_IP:
      return (Cli_RecordOnOff_Action(action, "proxy.config.http.anonymize_insert_client_ip", argtable->arg_string));

    case CMD_CONFIG_HTTP_REMOVE_IP:
      return (Cli_RecordOnOff_Action(action, "proxy.config.http.anonymize_remove_client_ip", argtable->arg_string));

    case CMD_CONFIG_HTTP_PROXY:
      return (ConfigHttpProxy(argtable[1].parsed_args, setvar));
    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigHttp
//
// Register "config:http" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigHttp()
{
  createArgument("status", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_HTTP_STATUS, "HTTP proxying <on | off>", (char *) NULL);

  createArgument("keep-alive-timeout-in", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_HTTP_KEEP_ALIVE_TIMEOUT_IN, "Keep alive timeout inbound <seconds>",
                 (char *) NULL);
  createArgument("keep-alive-timeout-out", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_HTTP_KEEP_ALIVE_TIMEOUT_OUT, "Keep alive timeout outbound <seconds>",
                 (char *) NULL);
  createArgument("inactive-timeout-in", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_HTTP_INACTIVE_TIMEOUT_IN, "Inactive timeout inbound <seconds>",
                 (char *) NULL);
  createArgument("inactive-timeout-out", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_HTTP_INACTIVE_TIMEOUT_OUT, "Inactive timeout outbound <seconds>",
                 (char *) NULL);
  createArgument("active-timeout-in", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_HTTP_ACTIVE_TIMEOUT_IN, "Active timeout inbound <seconds>", (char *) NULL);
  createArgument("active-timeout-out", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_HTTP_ACTIVE_TIMEOUT_OUT, "Active timeout outbound <seconds>", (char *) NULL);

  createArgument("remove-from", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_HTTP_REMOVE_FROM, "Remove \"From:\" header <on|off>", (char *) NULL);
  createArgument("remove-referer", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_HTTP_REMOVE_REFERER, "Remove \"Referer:\" header <on|off>", (char *) NULL);
  createArgument("remove-user", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_HTTP_REMOVE_USER, "Remove \"User:\" header <on|off>", (char *) NULL);
  createArgument("remove-cookie", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_HTTP_REMOVE_COOKIE, "Remove \"Cookie:\" header <on|off>", (char *) NULL);
  createArgument("remove-header", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_HTTP_REMOVE_HEADER, "String of headers to be removed <string>",
                 (char *) NULL);

  createArgument("global-user-agent", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_HTTP_GLOBAL_USER_AGENT, "User-Agent to send to Origin <string>",
                 (char *) NULL);

  createArgument("insert-ip", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_HTTP_INSERT_IP, "Insert client IP into header <on|off>", (char *) NULL);
  createArgument("remove-ip", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_HTTP_REMOVE_IP, "Remove client IP from header <on|off>", (char *) NULL);
  createArgument("proxy", 1, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_HTTP_PROXY, "Proxy Mode <fwd | rev | fwd-rev>", (char *) NULL);
  createArgument("fwd", CMD_CONFIG_HTTP_PROXY, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_HTTP_FWD, "Specify proxy mode to be forward", (char *) NULL);
  createArgument("rev", CMD_CONFIG_HTTP_PROXY, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_HTTP_REV, "Specify proxy mode to be reverse", (char *) NULL);
  createArgument("fwd-rev", CMD_CONFIG_HTTP_PROXY, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_HTTP_FWD_REV, "Specify proxy mode to be both", (char *) NULL);
  return 0;
}

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
int
Cmd_ConfigIcp(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:icp") == CLI_ERROR) {
    return CMD_ERROR;
  }
  Cli_Debug("Cmd_ConfigIcp argc %d\n", argc);

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;

  int action = (argc == 3) ? RECORD_SET : RECORD_GET;

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {
    switch (argtable->parsed_args) {
    case CMD_CONFIG_ICP_MODE:
      return (ConfigIcpMode(argtable[1].parsed_args, action));

    case CMD_CONFIG_ICP_PORT:
      return (Cli_RecordInt_Action(action, "proxy.config.icp.icp_port", argtable->arg_int));

    case CMD_CONFIG_ICP_MCAST:
      return (Cli_RecordOnOff_Action(action, "proxy.config.icp.multicast_enabled", argtable->arg_string));

    case CMD_CONFIG_ICP_QTIMEOUT:
      return (Cli_RecordInt_Action(action, "proxy.config.icp.query_timeout", argtable->arg_int));

    case CMD_CONFIG_ICP_PEERS:
      return (Cli_ConfigFileURL_Action(TS_FNAME_ICP_PEER, "icp.config", argtable->arg_string));
    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigIcp
//
// Register "config:Icp" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigIcp()
{
  createArgument("mode", 1, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_ICP_MODE, "Mode <disabled | receive | send-receive>", (char *) NULL);

  createArgument("receive", CMD_CONFIG_ICP_MODE, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_ICP_MODE_RECEIVE, "Specify receive mode for icp", (char *) NULL);

  createArgument("send-receive", CMD_CONFIG_ICP_MODE, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_ICP_MODE_SENDRECEIVE, "Specify send & receive mode for icp", (char *) NULL);

  createArgument("disabled", CMD_CONFIG_ICP_MODE, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_ICP_MODE_DISABLED, "icp mode disabled", (char *) NULL);

  createArgument("port", 1, CLI_ARGV_OPTION_INT_VALUE, (char *) NULL, CMD_CONFIG_ICP_PORT, "Port <int>", (char *) NULL);
  createArgument("multicast", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_ICP_MCAST, "Multicast <on|off>", (char *) NULL);
  createArgument("query-timeout", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_ICP_QTIMEOUT, "Query Timeout <seconds>", (char *) NULL);
  createArgument("peers", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_ICP_PEERS, "URL for ICP Peers config file <url>", (char *) NULL);
  return 0;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigPortTunnles
//
// Register "config:PortTunnles" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigPortTunnels()
{

  createArgument("server-other-ports", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_PORT_TUNNELS_SERVER_OTHER_PORTS, "Set the tunnel port number <int>",
                 (char *) NULL);
  return 0;
}



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
int
Cmd_ConfigScheduledUpdate(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:scheduled-update") == CLI_ERROR) {
    return CMD_ERROR;
  }
  Cli_Debug("Cmd_ConfigScheduledUpdate argc %d\n", argc);

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;

  int action = (argc == 3) ? RECORD_SET : RECORD_GET;

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {
    switch (argtable->parsed_args) {
    case CMD_CONFIG_SCHEDULED_UPDATE_STATUS:
      return (Cli_RecordOnOff_Action(action, "proxy.config.update.enabled", argtable->arg_string));

    case CMD_CONFIG_SCHEDULED_UPDATE_RETRY_COUNT:
      return (Cli_RecordInt_Action(action, "proxy.config.update.retry_count", argtable->arg_int));

    case CMD_CONFIG_SCHEDULED_UPDATE_RETRY_INTERVAL:
      return (Cli_RecordInt_Action(action, "proxy.config.update.retry_interval", argtable->arg_int));

    case CMD_CONFIG_SCHEDULED_UPDATE_MAX_CONCURRENT:
      return (Cli_RecordInt_Action(action, "proxy.config.update.concurrent_updates", argtable->arg_int));

    case CMD_CONFIG_SCHEDULED_UPDATE_FORCE_IMMEDIATE:
      return (Cli_RecordOnOff_Action(action, "proxy.config.update.force", argtable->arg_string));

    case CMD_CONFIG_SCHEDULED_UPDATE_RULES:
      return (Cli_ConfigFileURL_Action(TS_FNAME_UPDATE_URL, "update.config", argtable->arg_string));
    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigScheduled-Update
//
// Register "config:Scheduled-Update" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigScheduledUpdate()
{
  createArgument("status", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_SCHEDULED_UPDATE_STATUS, "Set scheduled-update status <on | off>",
                 (char *) NULL);
  createArgument("retry-count", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_SCHEDULED_UPDATE_RETRY_COUNT, "Set retry-count <int>", (char *) NULL);
  createArgument("retry-interval", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_SCHEDULED_UPDATE_RETRY_INTERVAL, "Set retry-interval <sec>", (char *) NULL);
  createArgument("max-concurrent", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_SCHEDULED_UPDATE_MAX_CONCURRENT, "Set maximum concurrent updates",
                 (char *) NULL);
  createArgument("force-immediate", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_SCHEDULED_UPDATE_FORCE_IMMEDIATE, "Set force-immediate <on | off>",
                 (char *) NULL);
  createArgument("rules", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_SCHEDULED_UPDATE_RULES, "Update update.config file from url <string>",
                 (char *) NULL);

  return 0;
}


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
int
Cmd_ConfigSocks(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:socks") == CLI_ERROR) {
    return CMD_ERROR;
  }
  Cli_Debug("Cmd_ConfigSocks argc %d\n", argc);
  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;

  int action = (argc == 3) ? RECORD_SET : RECORD_GET;

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {
    switch (argtable->parsed_args) {
    case CMD_CONFIG_SOCKS_STATUS:
      return (Cli_RecordOnOff_Action(action, "proxy.config.socks.socks_needed", argtable->arg_string));

    case CMD_CONFIG_SOCKS_VERSION:
      return (Cli_RecordInt_Action(action, "proxy.config.socks.socks_version", argtable->arg_int));

    case CMD_CONFIG_SOCKS_DEFAULT_SERVERS:
      return (Cli_RecordString_Action(action, "proxy.config.socks.default_servers", argtable->arg_string));

    case CMD_CONFIG_SOCKS_ACCEPT:
      return (Cli_RecordOnOff_Action(action, "proxy.config.socks.accept_enabled", argtable->arg_string));

    case CMD_CONFIG_SOCKS_ACCEPT_PORT:
      return (Cli_RecordInt_Action(action, "proxy.config.socks.accept_port", argtable->arg_int));
    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigScheduled-Update
//
// Register "config:Scheduled-Update" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigSocks()
{
  createArgument("status", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_SOCKS_STATUS, "Set socks status <on | off>", (char *) NULL);

  createArgument("version", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_SOCKS_VERSION, "Set version <int>", (char *) NULL);

  createArgument("default-servers", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_SOCKS_DEFAULT_SERVERS, "Set default-servers <string>", (char *) NULL);

  createArgument("accept", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_SOCKS_ACCEPT, "Set accept <on | off>", (char *) NULL);

  createArgument("accept-port", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_SOCKS_ACCEPT_PORT, "Set server accept-port <int>", (char *) NULL);

  return 0;
}


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
int
Cmd_ConfigCache(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:cache") == CLI_ERROR) {
    return CMD_ERROR;
  }
  Cli_Debug("Cmd_ConfigCache argc %d\n", argc);

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;

  int action = 0;

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {
    switch (argtable->parsed_args) {
    case CMD_CONFIG_CACHE_HTTP:
      return (Cli_RecordOnOff_Action((argc == 3), "proxy.config.http.cache.http", argtable->arg_string));

    case CMD_CONFIG_CACHE_CLUSTER_BYPASS:
      return (Cli_RecordOnOff_Action((argc == 3),
                                     "proxy.config.http.cache.cluster_cache_local", argtable->arg_string));
    case CMD_CONFIG_CACHE_IGNORE_BYPASS:
      return (Cli_RecordOnOff_Action((argc == 3),
                                     "proxy.config.http.cache.ignore_client_no_cache", argtable->arg_string));

    case CMD_CONFIG_CACHE_MAX_OBJECT_SIZE:
      return (Cli_RecordInt_Action((argc == 3), "proxy.config.cache.max_doc_size", argtable->arg_int));

    case CMD_CONFIG_CACHE_MAX_ALTERNATES:
      return (Cli_RecordInt_Action((argc == 3), "proxy.config.cache.limits.http.max_alts", argtable->arg_int));

    case CMD_CONFIG_CACHE_FILE:
      return (Cli_ConfigFileURL_Action(TS_FNAME_CACHE_OBJ, "cache.config", argtable->arg_string));

    case CMD_CONFIG_CACHE_FRESHNESS:
      if (argtable[1].parsed_args != CLI_PARSED_ARGV_END) {
        switch (argtable[1].parsed_args) {
        case CMD_CONFIG_CACHE_FRESHNESS_VERIFY:
          if (argc == 4) {
            action = RECORD_SET;
          }
          return (ConfigCacheFreshnessVerify(argtable[2].parsed_args, action));

        case CMD_CONFIG_CACHE_FRESHNESS_MINIMUM:
          if (argc == 4) {
            action = RECORD_SET;
          }
          return (ConfigCacheFreshnessMinimum(argtable[2].parsed_args, action));

        case CMD_CONFIG_CACHE_FRESHNESS_NO_EXPIRE_LIMIT:
          if (argtable[2].parsed_args != CLI_PARSED_ARGV_END) {
            if ((argtable[2].parsed_args ==
                 CMD_CONFIG_CACHE_FRESHNESS_NO_EXPIRE_LIMIT_GREATER_THAN) &&
                (argtable[3].parsed_args == CMD_CONFIG_CACHE_FRESHNESS_NO_EXPIRE_LIMIT_LESS_THAN) && (argc == 7)) {
              action = RECORD_SET;
            } else {
              Cli_Printf("\n config:cache freshness no-expire-limit greater-than <value> less-than<value>\n");
              return CMD_ERROR;
            }
          }
          Cli_Debug("greater than %d, less than %d \n", argtable[2].arg_int, argtable[3].arg_int);
          return (ConfigCacheFreshnessNoExpireLimit(argtable[2].arg_int, argtable[3].arg_int, action));

        }
      }
      Cli_Printf("\n config:cache freshness <verify | minimum | no-expire-limit> \n");
      return CMD_ERROR;
    case CMD_CONFIG_CACHE_DYNAMIC:
      return (Cli_RecordOnOff_Action((argc == 3),
                                     "proxy.config.http.cache.cache_urls_that_look_dynamic", argtable->arg_string));

    case CMD_CONFIG_CACHE_ALTERNATES:
      return (Cli_RecordOnOff_Action((argc == 3),
                                     "proxy.config.http.cache.enable_default_vary_headers", argtable->arg_string));

    case CMD_CONFIG_CACHE_VARY:
      if (argtable[1].arg_string) {
        action = RECORD_SET;
      }
      return (ConfigCacheVary(argtable[1].parsed_args, argtable[1].arg_string, action));

    case CMD_CONFIG_CACHE_COOKIES:
      if (argc == 3) {
        action = RECORD_SET;
      }
      return (ConfigCacheCookies(argtable[1].parsed_args, action));
    case CMD_CONFIG_CACHE_CLEAR:
      return (ConfigCacheClear());
    }
  }

  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigCache
//
// Register "config:cache" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigCache()
{
  createArgument("http", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_CACHE_HTTP, "HTTP Protocol caching <on|off>", (char *) NULL);
  createArgument("ignore-bypass", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_CACHE_IGNORE_BYPASS, "Ignore Bypass <on|off>", (char *) NULL);
  createArgument("max-object-size", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_CACHE_MAX_OBJECT_SIZE, "Maximum object size <bytes>", (char *) NULL);
  createArgument("max-alternates", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_CACHE_MAX_ALTERNATES, "Maximum alternates <int>", (char *) NULL);
  createArgument("file", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_CACHE_FILE, "Load cache.config file from url <string>", (char *) NULL);
  createArgument("freshness", 1, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_CACHE_FRESHNESS, "Freshness parameters <verify | minimum | no-expire-limit>",
                 (char *) NULL);
  createArgument("verify", CMD_CONFIG_CACHE_FRESHNESS, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_CACHE_FRESHNESS_VERIFY,
                 "Freshness verify <when-expired | no-date | always | never> ", (char *) NULL);

  createArgument("when-expired", CMD_CONFIG_CACHE_FRESHNESS_VERIFY, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_CACHE_FRESHNESS_VERIFY_WHEN_EXPIRED,
                 "Set freshness verify to be when-expired", (char *) NULL);

  createArgument("no-date", CMD_CONFIG_CACHE_FRESHNESS_VERIFY, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_CACHE_FRESHNESS_VERIFY_NO_DATE,
                 "Set freshness verify to be no-date", (char *) NULL);

  createArgument("always", CMD_CONFIG_CACHE_FRESHNESS_VERIFY, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_CACHE_FRESHNESS_VERIFY_ALWALYS,
                 "Set freshness verify to be always", (char *) NULL);

  createArgument("never", CMD_CONFIG_CACHE_FRESHNESS_VERIFY, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_CACHE_FRESHNESS_VERIFY_NEVER,
                 "Set the freshness verify to be never", (char *) NULL);

  createArgument("minimum", CMD_CONFIG_CACHE_FRESHNESS, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_CACHE_FRESHNESS_MINIMUM,
                 "Set freshness minimum <explicit | last-modified | nothing>", (char *) NULL);

  createArgument("explicit", CMD_CONFIG_CACHE_FRESHNESS_MINIMUM, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_CACHE_FRESHNESS_MINIMUM_EXPLICIT,
                 "Set the Freshness Minimum to be explicit", (char *) NULL);

  createArgument("last-modified", CMD_CONFIG_CACHE_FRESHNESS_MINIMUM, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_CACHE_FRESHNESS_MINIMUM_LAST_MODIFIED,
                 "Set the Freshness Minimum to be last modified", (char *) NULL);

  createArgument("nothing", CMD_CONFIG_CACHE_FRESHNESS_MINIMUM, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_CACHE_FRESHNESS_MINIMUM_NOTHING,
                 "Specify the Freshness minimum to be nothing", (char *) NULL);

  createArgument("no-expire-limit", CMD_CONFIG_CACHE_FRESHNESS, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_CACHE_FRESHNESS_NO_EXPIRE_LIMIT,
                 "Set the Freshness no-expire-limit time", (char *) NULL);

  createArgument("greater-than", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_CACHE_FRESHNESS_NO_EXPIRE_LIMIT_GREATER_THAN,
                 "Set the minimum Freshness no-expire-limit time", (char *) NULL);

  createArgument("less-than", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_CACHE_FRESHNESS_NO_EXPIRE_LIMIT_LESS_THAN,
                 "Set the maximum Freshness no-expire-limit time", (char *) NULL);

  createArgument("dynamic", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_CACHE_DYNAMIC, "Set Dynamic <on|off>", (char *) NULL);

  createArgument("alternates", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_CACHE_ALTERNATES, "Set Alternates <on|off>", (char *) NULL);
  createArgument("vary", 1, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_CACHE_VARY, "Set vary <text | images | other> <field>", (char *) NULL);
  createArgument("text", CMD_CONFIG_CACHE_VARY, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_CACHE_VARY_TEXT, "Set vary text's value", (char *) NULL);
  createArgument("images", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_CACHE_VARY_COOKIES_IMAGES, "Set vary images' value", (char *) NULL);
  createArgument("other", CMD_CONFIG_CACHE_VARY, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_CACHE_VARY_OTHER, "Set vary other's value", (char *) NULL);

  createArgument("cookies", 1, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_CACHE_COOKIES, "Set cookies <none | all | images | non-text | non-text-ext>",
                 (char *) NULL);
  createArgument("none", CMD_CONFIG_CACHE_COOKIES, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_CACHE_COOKIES_NONE, "No cookies", (char *) NULL);
  createArgument("all", CMD_CONFIG_CACHE_COOKIES, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_CACHE_COOKIES_ALL, "All cookies", (char *) NULL);
  createArgument("non-text", CMD_CONFIG_CACHE_COOKIES, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_CACHE_COOKIES_NON_TEXT, "Non-text cookies", (char *) NULL);
  createArgument("non-text-ext", CMD_CONFIG_CACHE_COOKIES, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_CACHE_COOKIES_NON_TEXT_EXT, "Non-text-ext cookies", (char *) NULL);
  createArgument("clear", 1, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_CACHE_CLEAR, "Clear the cache and start Traffic Server", (char *) NULL);
  return 0;
}




////////////////////////////////////////////////////////////////
// Cmd_ConfigHostdb
//
// This is the callback function for the "config:hostdb" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigHostdb(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:hostdb") == CLI_ERROR) {
    return CMD_ERROR;
  }
  Cli_Debug("Cmd_ConfigHostdb argc %d\n", argc);

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;

  int action = (argc == 3) ? RECORD_SET : RECORD_GET;

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {
    switch (argtable->parsed_args) {
    case CMD_CONFIG_HOSTDB_LOOKUP_TIMEOUT:
      return (Cli_RecordInt_Action(action, "proxy.config.hostdb.lookup_timeout", argtable->arg_int));

    case CMD_CONFIG_HOSTDB_FOREGROUND_TIMEOUT:
      return (Cli_RecordInt_Action(action, "proxy.config.hostdb.timeout", argtable->arg_int));

    case CMD_CONFIG_HOSTDB_BACKGROUND_TIMEOUT:
      return (Cli_RecordInt_Action(action, "proxy.config.hostdb.verify_after", argtable->arg_int));

    case CMD_CONFIG_HOSTDB_INVALID_HOST_TIMEOUT:
      return (Cli_RecordInt_Action(action, "proxy.config.hostdb.fail.timeout", argtable->arg_int));

    case CMD_CONFIG_HOSTDB_RE_DNS_ON_RELOAD:
      return (Cli_RecordOnOff_Action(action, "proxy.config.hostdb.re_dns_on_reload", argtable->arg_string));
    case CMD_CONFIG_HOSTDB_CLEAR:
      return (ConfigHostdbClear());
    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigHostdb
//
// Register "config:Hostdb" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigHostdb()
{
  createArgument("lookup-timeout", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_HOSTDB_LOOKUP_TIMEOUT, "Lookup Timeout <seconds>", (char *) NULL);
  createArgument("foreground-timeout", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_HOSTDB_FOREGROUND_TIMEOUT, "Foreground Timeout <minutes>", (char *) NULL);
  createArgument("background-timeout", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_HOSTDB_BACKGROUND_TIMEOUT, "Background Timeout <minutes>", (char *) NULL);
  createArgument("invalid-host-timeout", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_HOSTDB_INVALID_HOST_TIMEOUT, "Invalid Host Timeout <minutes>",
                 (char *) NULL);
  createArgument("re-dns-on-reload", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_HOSTDB_RE_DNS_ON_RELOAD, "Re-DNS on Reload Timeout <on|off>", (char *) NULL);
  createArgument("clear", 1, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_HOSTDB_CLEAR, "Clear the HostDB and start Traffic Server", (char *) NULL);
  return 0;
}

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
int
Cmd_ConfigDns(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:dns") == CLI_ERROR) {
    return CMD_ERROR;
  }
  Cli_Debug("Cmd_ConfigDns argc %d\n", argc);

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;

  int action = (argc == 3) ? RECORD_SET : RECORD_GET;

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {
    switch (argtable->parsed_args) {
    case CMD_CONFIG_DNS_RESOLVE_TIMEOUT:
      return (Cli_RecordInt_Action(action, "proxy.config.dns.lookup_timeout", argtable->arg_int));

    case CMD_CONFIG_DNS_RETRIES:
      return (Cli_RecordInt_Action(action, "proxy.config.dns.retries", argtable->arg_int));
    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigDns
//
// Register "config:dns" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigDns()
{
  createArgument("resolve-timeout", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_DNS_RESOLVE_TIMEOUT, "Resolve timeout <int>", (char *) NULL);
  createArgument("retries", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_DNS_RETRIES, "Number of retries <int>", (char *) NULL);

  return 0;
}

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
int
Cmd_ConfigVirtualip(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:virtualip") == CLI_ERROR) {
    return CMD_ERROR;
  }
  Cli_Debug("Cmd_ConfigCache argc %d\n", argc);

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;

  int setvar = 0;

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {
    switch (argtable->parsed_args) {
    case CMD_CONFIG_VIRTUALIP_STATUS:
      return (Cli_RecordOnOff_Action((argc == 3), "proxy.config.vmap.enabled", argtable->arg_string));

    case CMD_CONFIG_VIRTUALIP_LIST:
      return (ConfigVirtualIpList());

    case CMD_CONFIG_VIRTUALIP_ADD:
      if (argc == 8) {
        setvar = 1;
      }
      Cli_PrintArg(0, argtable);
      Cli_PrintArg(1, argtable);
      Cli_PrintArg(2, argtable);
      Cli_PrintArg(3, argtable);
      if (ConfigVirtualipAdd(argtable[1].arg_string, argtable[2].arg_string, argtable[3].arg_int, setvar) == CLI_OK) {
        return CMD_OK;
      } else {
        return CMD_ERROR;
      }

    case CMD_CONFIG_VIRTUALIP_DELETE:
      if (argc == 3) {
        setvar = 1;
      }
      if (ConfigVirtualipDelete(argtable[0].arg_int, setvar) == CLI_OK) {
        return CMD_OK;
      } else {
        return CMD_ERROR;
      }
    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigVirtualip
//
// Register "config:virtualip" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigVirtualip()
{

  createArgument("status", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_VIRTUALIP_STATUS, "Virtual IP <on | off>", (char *) NULL);
  createArgument("list", 1, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_VIRTUALIP_LIST, "List virtual IP addresses", (char *) NULL);
  createArgument("add", 1, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_VIRTUALIP_ADD, "add ip <x.x.x.x> device <string> sub-intf <int>",
                 (char *) NULL);
  createArgument("ip", CMD_CONFIG_VIRTUALIP_ADD, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_VIRTUALIP_ADD_IP, "Virtual IP Address <x.x.x.x>", (char *) NULL);
  createArgument("device", CMD_CONFIG_VIRTUALIP_ADD_IP, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_VIRTUALIP_ADD_DEVICE, "Virtual IP device <string>", (char *) NULL);
  createArgument("sub-intf", CMD_CONFIG_VIRTUALIP_ADD_DEVICE, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_VIRTUALIP_ADD_SUBINTERFACE, "Virtual IP sub interface <integer>",
                 (char *) NULL);
  createArgument("delete", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_VIRTUALIP_DELETE, "Delete Virtual IP <integer>", (char *) NULL);
  return CLI_OK;
}


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
int
Cmd_ConfigLogging(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:logging") == CLI_ERROR) {
    return CMD_ERROR;
  }
  Cli_Debug("Cmd_ConfigCache argc %d\n", argc);

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;

  int setvar = 0;

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {
    switch (argtable->parsed_args) {
    case CMD_CONFIG_LOGGING_EVENT:
      if (argc == 3) {
        setvar = 1;
      }
      return (ConfigLoggingEvent(argtable[1].parsed_args, setvar));
    case CMD_CONFIG_LOGGING_MGMT_DIRECTORY:
      return (Cli_RecordString_Action((argc == 3), "proxy.config.log.logfile_dir", argtable->arg_string));

    case CMD_CONFIG_LOGGING_SPACE_LIMIT:
      return (Cli_RecordInt_Action((argc == 3), "proxy.config.log.max_space_mb_for_logs", argtable->arg_int));

    case CMD_CONFIG_LOGGING_SPACE_HEADROOM:
      return (Cli_RecordInt_Action((argc == 3), "proxy.config.log.max_space_mb_headroom", argtable->arg_int));

    case CMD_CONFIG_LOGGING_COLLATION_STATUS:
      if (argc == 3) {
        setvar = 1;
      }
      return (ConfigLoggingCollationStatus(argtable[1].parsed_args, setvar));
    case CMD_CONFIG_LOGGING_COLLATION_HOST:
      return (Cli_RecordString_Action((argc == 3), "proxy.config.log.collation_host", argtable->arg_string));

    case CMD_CONFIG_LOGGING_COLLATION:
      if (argc == 8) {
        setvar = 1;
      }
      Cli_PrintArg(1, argtable);
      Cli_PrintArg(2, argtable);
      Cli_PrintArg(3, argtable);
      Cli_PrintArg(4, argtable);
      return (ConfigLoggingCollation(argtable[1].arg_string, argtable[3].parsed_args, argtable[4].arg_int, setvar));
    case CMD_CONFIG_LOGGING_AND_CUSTOM_FORMAT:
      if (argc == 10) {
        setvar = 1;
      }
      Cli_PrintArg(1, argtable);
      Cli_PrintArg(2, argtable);
      Cli_PrintArg(3, argtable);
      Cli_PrintArg(4, argtable);
      Cli_PrintArg(5, argtable);
      Cli_PrintArg(6, argtable);
      return (ConfigLoggingFormatTypeFile(argtable[1].parsed_args,
                                          argtable[2].parsed_args,
                                          argtable[4].parsed_args,
                                          argtable[5].arg_string, argtable[6].arg_string, setvar));
    case CMD_CONFIG_LOGGING_SPLITTING:
      if (argc == 4) {
        setvar = 1;
      }
      Cli_PrintArg(1, argtable);
      Cli_PrintArg(2, argtable);
      return (ConfigLoggingSplitting(argtable[1].parsed_args, argtable[2].parsed_args, setvar));

    case CMD_CONFIG_LOGGING_CUSTOM:
      if (argc == 5) {
        setvar = 1;
      }
      Cli_PrintArg(1, argtable);
      Cli_PrintArg(2, argtable);
      Cli_PrintArg(3, argtable);
      return (ConfigLoggingCustomFormat(argtable[1].parsed_args, argtable[3].parsed_args, setvar));

    case CMD_CONFIG_LOGGING_ROLLING:
      if (argc == 9) {
        setvar = 1;
      }
      Cli_PrintArg(1, argtable);
      Cli_PrintArg(2, argtable);
      Cli_PrintArg(3, argtable);
      Cli_PrintArg(4, argtable);
      Cli_PrintArg(5, argtable);
      return (ConfigLoggingRollingOffsetIntervalAutodelete(argtable[1].parsed_args,
                                                           argtable[2].arg_int,
                                                           argtable[3].arg_int, argtable[5].parsed_args, setvar));

    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigLogging
//
// Register "config:logging" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigLogging()
{

  createArgument("on", CLI_ARGV_NO_POS, CLI_ARGV_REQUIRED,
                 (char *) NULL, CMD_CONFIG_LOGGING_ON, "Enable logging", (char *) NULL);
  createArgument("off", CLI_ARGV_NO_POS, CLI_ARGV_REQUIRED,
                 (char *) NULL, CMD_CONFIG_LOGGING_OFF, "Disable logging", (char *) NULL);
  createArgument("event", 1, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_LOGGING_EVENT, "Events <enabled | trans-only | error-only | disabled>",
                 (char *) NULL);
  createArgument("enabled", CMD_CONFIG_LOGGING_EVENT, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_LOGGING_EVENT_ENABLED, "Event logging enabled", (char *) NULL);
  createArgument("trans-only", CMD_CONFIG_LOGGING_EVENT, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_LOGGING_EVENT_TRANS_ONLY, "Event logging for transactions only",
                 (char *) NULL);
  createArgument("error-only", CMD_CONFIG_LOGGING_EVENT, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_LOGGING_EVENT_ERROR_ONLY, "Event logging for errors only", (char *) NULL);
  createArgument("disabled", CMD_CONFIG_LOGGING_EVENT, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_LOGGING_EVENT_DISABLED, "Event logging is disabled", (char *) NULL);
  createArgument("mgmt-directory", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_LOGGING_MGMT_DIRECTORY, "Logging MGMT directory <string>", (char *) NULL);
  createArgument("space-limit", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_LOGGING_SPACE_LIMIT, "Space limit for logs <mb>", (char *) NULL);
  createArgument("space-headroom", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_LOGGING_SPACE_HEADROOM, "Space for headroom <mb>", (char *) NULL);
  createArgument("collation-status", 1, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_LOGGING_COLLATION_STATUS,
                 "Collation status <inactive | host | send-standard |\n" "                   send-custom | send-all>",
                 (char *) NULL);
  createArgument("inactive", CMD_CONFIG_LOGGING_COLLATION_STATUS, CLI_ARGV_CONSTANT, (char *) NULL,
                 CMD_CONFIG_LOGGING_COLLATION_STATUS_INACTIVE, "No collation", (char *) NULL);
  createArgument("host", CMD_CONFIG_LOGGING_COLLATION_STATUS, CLI_ARGV_CONSTANT, (char *) NULL,
                 CMD_CONFIG_LOGGING_COLLATION_STATUS_HOST, "Be a collation host (receiver)", (char *) NULL);
  createArgument("send-standard", CMD_CONFIG_LOGGING_COLLATION_STATUS, CLI_ARGV_OPTION_NAME_VALUE, (char *) NULL,
                 CMD_CONFIG_LOGGING_COLLATION_STATUS_SEND_STANDARD, "Send standard logs", (char *) NULL);
  createArgument("send-custom", CMD_CONFIG_LOGGING_COLLATION_STATUS, CLI_ARGV_OPTION_NAME_VALUE, (char *) NULL,
                 CMD_CONFIG_LOGGING_COLLATION_STATUS_SEND_CUSTOM, "Send custom logs", (char *) NULL);
  createArgument("send-all", CMD_CONFIG_LOGGING_COLLATION_STATUS, CLI_ARGV_OPTION_NAME_VALUE, (char *) NULL,
                 CMD_CONFIG_LOGGING_COLLATION_STATUS_SEND_ALL, "Send all logs", (char *) NULL);
  createArgument("collation-host", 1, CLI_ARGV_OPTION_NAME_VALUE, (char *) NULL, CMD_CONFIG_LOGGING_COLLATION_HOST,
                 "Specify the collation host <string>", (char *) NULL);

  createArgument("collation", 1, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_LOGGING_COLLATION, "Collation parameters secret <secret> tagged <on | off>\n"
                 "                   orphan-limit <orphan>", (char *) NULL);

  createArgument("secret", CMD_CONFIG_LOGGING_COLLATION, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_LOGGING_COLLATION_SECRET, "Collation secret is <string>", (char *) NULL);

  createArgument("tagged", CLI_ARGV_NO_POS, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_LOGGING_COLLATION_TAGGED, "Collation tagged is <on | off>", (char *) NULL);

  createArgument("orphan-limit", CLI_ARGV_NO_POS, CLI_ARGV_INT,
                 (char *) NULL, CMD_CONFIG_LOGGING_COLLATION_ORPHAN_LIMIT,
                 "Collation orphan limit size <mb>", (char *) NULL);

  createArgument("format", CLI_ARGV_NO_POS, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_LOGGING_AND_CUSTOM_FORMAT,
                 "Logging format <squid | netscape-common | netscape-ext |\n"
                 "                   netscape-ext2>", (char *) NULL);

  createArgument("squid", CMD_CONFIG_LOGGING_AND_CUSTOM_FORMAT, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_LOGGING_FORMAT_SQUID, "Squid <on | off>", (char *) NULL);
  createArgument("netscape-common", CMD_CONFIG_LOGGING_AND_CUSTOM_FORMAT, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_LOGGING_FORMAT_NETSCAPE_COMMON, "Netscape Common <on | off>", (char *) NULL);
  createArgument("netscape-ext", CMD_CONFIG_LOGGING_AND_CUSTOM_FORMAT, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_LOGGING_FORMAT_NETSCAPE_EXT, "Netscape Extended <on | off>", (char *) NULL);
  createArgument("netscape-ext2", CMD_CONFIG_LOGGING_AND_CUSTOM_FORMAT, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_LOGGING_FORMAT_NETSCAPE_EXT2,
                 "Netscape Extended 2 <on | off>", (char *) NULL);

  createArgument("type", CLI_ARGV_NO_POS, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_LOGGING_TYPE, "Logging type <ascii | binary>", (char *) NULL);
  createArgument("ascii", CMD_CONFIG_LOGGING_TYPE, CLI_ARGV_REQUIRED,
                 (char *) NULL, CMD_CONFIG_LOGGING_TYPE_ASCII, "ASCII log files", (char *) NULL);
  createArgument("binary", CMD_CONFIG_LOGGING_TYPE, CLI_ARGV_REQUIRED,
                 (char *) NULL, CMD_CONFIG_LOGGING_TYPE_BINARY, "Binary log files", (char *) NULL);

  createArgument("file", CLI_ARGV_NO_POS, CLI_ARGV_STRING,
                 (char *) NULL, CMD_CONFIG_LOGGING_FILE, "Log file name <string>", (char *) NULL);

  createArgument("header", CLI_ARGV_NO_POS, CLI_ARGV_STRING,
                 (char *) NULL, CMD_CONFIG_LOGGING_HEADER, "Log file header <string>", (char *) NULL);

  createArgument("splitting", 1, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_LOGGING_SPLITTING,
                 "Splitting of logs for protocols <icp | http>", (char *) NULL);
  createArgument("icp", CMD_CONFIG_LOGGING_SPLITTING, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_LOGGING_SPLITTING_ICP, "Split ICP <on | off>", (char *) NULL);
  createArgument("http", CMD_CONFIG_LOGGING_SPLITTING, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_LOGGING_SPLITTING_HTTP, "Split of HTTP <on | off>", (char *) NULL);

  createArgument("custom", 1, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_LOGGING_CUSTOM,
                 "Custom Logging <on | off>", (char *) NULL);

  createArgument("rolling", 1, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_LOGGING_ROLLING,
                 "Log file rolling <on | off> offset <hour>\n"
                 "                   interval <num-hours> auto-delete <on | off>", (char *) NULL);

  createArgument("offset", CLI_ARGV_NO_POS, CLI_ARGV_INT,
                 (char *) NULL, CMD_CONFIG_LOGGING_OFFSET, "Rolling offset <hour> (24hour format)", (char *) NULL);

  createArgument("interval", CLI_ARGV_NO_POS, CLI_ARGV_INT,
                 (char *) NULL, CMD_CONFIG_LOGGING_INTERVAL, "Rolling interval <seconds>", (char *) NULL);
  createArgument("auto-delete", CLI_ARGV_NO_POS, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_LOGGING_AUTO_DELETE, "Auto delete <on | off>", (char *) NULL);
  return 0;


}

////////////////////////////////////////////////////////////////
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
int
Cmd_ConfigSsl(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{

  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:ssl") == CLI_ERROR) {
    return CMD_ERROR;
  }
  Cli_Debug("Cmd_ConfigSsl argc %d\n", argc);

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;

  int action = (argc == 3) ? RECORD_SET : RECORD_GET;

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {
    switch (argtable->parsed_args) {
    case CMD_CONFIG_SSL_STATUS:
      return (Cli_RecordOnOff_Action(action, "proxy.config.ssl.enabled", argtable->arg_string));

    case CMD_CONFIG_SSL_PORT:
      return (Cli_RecordInt_Action(action, "proxy.config.ssl.server_port", argtable->arg_int));
    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}



////////////////////////////////////////////////////////////////
// CmdArgs_ConfigSsl
//
// Register "config:ssl" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigSsl()
{
  createArgument("status", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_SSL_STATUS, "SSL <on | off>", (char *) NULL);

  createArgument("ports", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_SSL_PORT, "SSL port <int>", (char *) NULL);
  return 0;
}


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
int
Cmd_ConfigAlarm(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:alarm") == CLI_ERROR) {
    return CMD_ERROR;
  }
  Cli_Debug("Cmd_ConfigAlarm argc %d\n", argc);

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {
    switch (argtable->parsed_args) {
    case CMD_CONFIG_ALARM_RESOLVE_NAME:
      if (argc < 3) {
        return (ShowAlarms());
      }
      return (ConfigAlarmResolveName(argtable->arg_string));
    case CMD_CONFIG_ALARM_RESOLVE_NUMBER:
      if (argc < 3) {
        return (ShowAlarms());
      }
      return (ConfigAlarmResolveNumber(argtable->arg_int));
    case CMD_CONFIG_ALARM_RESOLVE_ALL:
      return (ConfigAlarmResolveAll());
    case CMD_CONFIG_ALARM_NOTIFY:
      Cli_Debug("Cmd_ConfigAlarm \"%s\"\n", argtable->arg_string);
      return (ConfigAlarmNotify(argtable->arg_string));
    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}



////////////////////////////////////////////////////////////////
// CmdArgs_ConfigAlarm
//
// Register "config:alarm" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigAlarm()
{
  createArgument("resolve-name", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_ALARM_RESOLVE_NAME, "Resolve by name <string>", (char *) NULL);

  createArgument("resolve-number", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_ALARM_RESOLVE_NUMBER, "Resolve by number from list <int>", (char *) NULL);

  createArgument("resolve-all", CLI_ARGV_NO_POS, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_ALARM_RESOLVE_ALL, "Resolve all alarms", (char *) NULL);

  createArgument("notify", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_ALARM_NOTIFY, "Alarm notification <on | off>", (char *) NULL);
  return CLI_OK;
}


////////////////////////////////////////////////////////////////
//
// "config" sub-command implementations
//
////////////////////////////////////////////////////////////////


// config start sub-command
int
ConfigStart()
{
  TSProxyStateT state = TSProxyStateGet();

  switch (state) {
  case TS_PROXY_ON:
    // do nothing, proxy is already on
    Cli_Error(ERR_PROXY_STATE_ALREADY, "on");
    break;
  case TS_PROXY_OFF:
  case TS_PROXY_UNDEFINED:
    if (TSProxyStateSet(TS_PROXY_ON, TS_CACHE_CLEAR_OFF)) {
      Cli_Error(ERR_PROXY_STATE_SET, "on");
      return CLI_ERROR;
    }
    break;
  }
  return CLI_OK;
}

// config stop sub-command
int
ConfigStop()
{
  TSProxyStateT state = TSProxyStateGet();

  switch (state) {
  case TS_PROXY_OFF:
    // do nothing, proxy is already off
    Cli_Error(ERR_PROXY_STATE_ALREADY, "off");
    break;
  case TS_PROXY_ON:
  case TS_PROXY_UNDEFINED:
    if (TSProxyStateSet(TS_PROXY_OFF, TS_CACHE_CLEAR_OFF)) {
      Cli_Error(ERR_PROXY_STATE_SET, "off");
      return CLI_ERROR;
    }
    break;
  }
  return CLI_OK;
}

// config get sub-command
//   used to get the value of any config variable in records.config
int
ConfigGet(const char *rec_name)
{
  Cli_Debug("ConfigGet: rec_name %s\n", rec_name);

  TSError status;
  TSRecordEle rec_val;

  status = Cli_RecordGet(rec_name, &rec_val);

  if (status) {
    return status;
  }
  // display the result

  switch (rec_val.rec_type) {
  case TS_REC_INT:            // TS64 aka "long long"
    Cli_Printf("%s = %d\n", rec_name, (int) rec_val.int_val);
    break;
  case TS_REC_COUNTER:        // TS64 aka "long long"
    Cli_Printf("%s = %d\n", rec_name, (int) rec_val.counter_val);
    break;
  case TS_REC_FLOAT:          // float
    Cli_Printf("%s = %f\n", rec_name, rec_val.float_val);
    break;
  case TS_REC_STRING:         // char*
    Cli_Printf("%s = \"%s\"\n", rec_name, rec_val.string_val);
    break;
  case TS_REC_UNDEFINED:      // what's this???
    Cli_Printf("%s = UNDEFINED\n", rec_name);
    break;
  }

  return CLI_OK;
}

// config set sub-command
//   used to set the value of any variable in records.config

int
ConfigSet(const char *rec_name, const char *value)
{
  Cli_Debug("ConfigSet: rec_name %s value %s\n", rec_name, value);

  TSError status;
  TSActionNeedT action_need;

  status = Cli_RecordSet(rec_name, value, &action_need);
  if (status) {
    return status;
  }

  return (Cli_ConfigEnactChanges(action_need));
}

// config name sub-command
//   used to set or display the value of proxy.config.proxy_name

int
ConfigName(const char *proxy_name)
{
  TSError status = TS_ERR_OKAY;
  TSString str_val = NULL;
  TSActionNeedT action_need;

  if (proxy_name) {             // set the name

    Cli_Debug("ConfigName: set name proxy_name %s\n", proxy_name);

    status = Cli_RecordSetString("proxy.config.proxy_name", (TSString) proxy_name, &action_need);

    if (status) {
      return status;
    }

    return (Cli_ConfigEnactChanges(action_need));

  }

  else {                        // display the name
    Cli_Debug("ConfigName: get name\n");

    status = Cli_RecordGetString("proxy.config.proxy_name", &str_val);
    if (status) {
      return status;
    }

    if (str_val) {
      Cli_Printf("%s\n", str_val);
    } else {
      Cli_Printf("none\n");
    }
  }

  return CLI_OK;

}

// config ports sub-command
//   used to set the value of port(s)

int
ConfigPortsSet(int arg_ref, void *valuePtr)
{

  switch (arg_ref) {
  case CMD_CONFIG_PORTS_CONNECT:
    Cli_Debug("ConfigPortsSet: arg_ref %d value %s\n", arg_ref, (char *) valuePtr);
    break;
  default:
    Cli_Debug("ConfigPortsSet: arg_ref %d value %d\n", arg_ref, *(TSInt *) valuePtr);
  }

  TSError status = TS_ERR_OKAY;
  TSActionNeedT action_need = TS_ACTION_UNDEFINED;

  Cli_Debug("ConfigPorts: set\n");
  switch (arg_ref) {
  case CMD_CONFIG_PORTS_HTTP_SERVER:
    status = Cli_RecordSetInt("proxy.config.http.server_port", *(TSInt *) valuePtr, &action_need);
    break;
  case CMD_CONFIG_PORTS_CLUSTER:
    status = Cli_RecordSetInt("proxy.config.cluster.cluster_port", *(TSInt *) valuePtr, &action_need);
    break;
  case CMD_CONFIG_PORTS_CLUSTER_RS:
    status = Cli_RecordSetInt("proxy.config.cluster.rsport", *(TSInt *) valuePtr, &action_need);
    break;
  case CMD_CONFIG_PORTS_CLUSTER_MC:
    status = Cli_RecordSetInt("proxy.config.cluster.mcport", *(TSInt *) valuePtr, &action_need);
    break;
  case CMD_CONFIG_PORTS_CONNECT:
    status = Cli_RecordSetString("proxy.config.http.connect_ports", (TSString) valuePtr, &action_need);
    break;
  case CMD_CONFIG_PORTS_SOCKS_SERVER:
    status = Cli_RecordSetInt("proxy.config.socks.socks_server_port", *(TSInt *) valuePtr, &action_need);
    break;
  case CMD_CONFIG_PORTS_ICP:
    status = Cli_RecordSetInt("proxy.config.icp.icp_port", *(TSInt *) valuePtr, &action_need);
    break;
  }

  if (status) {
    return status;
  }

  return (Cli_ConfigEnactChanges(action_need));

}

// config ports sub-command
//   used to display the value of port(s)

int
ConfigPortsGet(int arg_ref)
{
  TSError status = TS_ERR_OKAY;
  TSInt int_val = -1;
  TSString str_val = NULL;

  Cli_Debug("ConfigPortsGet: get\n");

  switch (arg_ref) {
  case CMD_CONFIG_PORTS_HTTP_SERVER:
    status = Cli_RecordGetInt("proxy.config.http.server_port", &int_val);
    if (status) {
      return status;
    }
    Cli_Printf("%d\n", int_val);
    break;
  case CMD_CONFIG_PORTS_CLUSTER:
    status = Cli_RecordGetInt("proxy.config.cluster.cluster_port", &int_val);
    if (status) {
      return status;
    }
    Cli_Printf("%d\n", int_val);
    break;
  case CMD_CONFIG_PORTS_CLUSTER_RS:
    status = Cli_RecordGetInt("proxy.config.cluster.rsport", &int_val);
    if (status) {
      return status;
    }
    Cli_Printf("%d\n", int_val);
    break;
  case CMD_CONFIG_PORTS_CLUSTER_MC:
    status = Cli_RecordGetInt("proxy.config.cluster.mcport", &int_val);
    if (status) {
      return status;
    }
    Cli_Printf("%d\n", int_val);
    break;
  case CMD_CONFIG_PORTS_CONNECT:
    status = Cli_RecordGetString("proxy.config.http.connect_ports", &str_val);
    if (status) {
      return status;
    }
    if (str_val) {
      Cli_Printf("%s\n", str_val);
    } else {
      Cli_Printf("none\n");
    }
    break;
  case CMD_CONFIG_PORTS_SOCKS_SERVER:
    status = Cli_RecordGetInt("proxy.config.socks.socks_server_port", &int_val);
    if (status) {
      return status;
    }
    Cli_Printf("%d\n", int_val);
    break;
  case CMD_CONFIG_PORTS_ICP:
    status = Cli_RecordGetInt("proxy.config.icp.icp_port", &int_val);
    if (status) {
      return status;
    }
    Cli_Printf("%d\n", int_val);
    break;
  default:
    Cli_Error(ERR_COMMAND_SYNTAX,
              "\n\nconfig:ports <http-server | http-other | webui | cluster-rs | cluster-mc | \n ssl | \n socks-server | icp > \n <port | ports list>\n");

    return CLI_ERROR;
  }
  return CLI_OK;
}

int
ConfigSecurityPasswd()
{
  Cli_Debug("ConfigSecurityPasswd\n");
  Cli_Printf("This command is currently a no-op");
  return CLI_OK;
}

// config remap sub-command
int
ConfigRemap(const char *url)
{
  Cli_Debug("ConfigRemap: url %s\n", url);

  return (Cli_SetConfigFileFromUrl(TS_FNAME_REMAP, url));
}

// config http proxy sub-command
int
ConfigHttpProxy(int arg_ref, int setvar)
{
  Cli_Debug("ConfigHttpProxy: proxy %d\n", arg_ref);

  TSInt rmp_val = 0;
  TSInt rev_val = 0;
  TSActionNeedT action_need = TS_ACTION_UNDEFINED;
  TSError status = TS_ERR_OKAY;

  switch (setvar) {
  case 0:                      //get

    status = Cli_RecordGetInt("proxy.config.reverse_proxy.enabled", &rev_val);
    if (status) {
      return status;
    }
    status = Cli_RecordGetInt("proxy.config.url_remap.remap_required", &rmp_val);
    if (status) {
      return status;
    }
    if ((rev_val) && (rmp_val)) {
      Cli_Printf("rev\n");
    }
    if ((rev_val) && !(rmp_val)) {
      Cli_Printf("fwd-rev\n");
    }
    if (!rev_val) {
      Cli_Printf("fwd\n");
    }
    return CLI_OK;

  case 1:                      //set
    {
      switch (arg_ref) {
      case CMD_CONFIG_HTTP_FWD:
        status = Cli_RecordSetInt("proxy.config.reverse_proxy.enabled", (TSInt) 0, &action_need);
        if (status) {
          return status;
        }
        return (Cli_ConfigEnactChanges(action_need));
      case CMD_CONFIG_HTTP_REV:
        status = Cli_RecordSetInt("proxy.config.reverse_proxy.enabled", (TSInt) 1, &action_need);
        if (status) {
          return status;
        }
        status = Cli_RecordSetInt("proxy.config.url_remap.remap_required", (TSInt) 1, &action_need);

        if (status) {
          return status;
        }
        return (Cli_ConfigEnactChanges(action_need));
      case CMD_CONFIG_HTTP_FWD_REV:
        status = Cli_RecordSetInt("proxy.config.reverse_proxy.enabled", (TSInt) 1, &action_need);
        if (status) {
          return status;
        }
        status = Cli_RecordSetInt("proxy.config.url_remap.remap_required", (TSInt) 0, &action_need);
        if (status) {
          return status;
        }
        return (Cli_ConfigEnactChanges(action_need));
      }
      return CLI_ERROR;
    }
  default:
    return CLI_ERROR;
  }
}

// config icp mode sub-command
int
ConfigIcpMode(int arg_ref, int setvar)
{
  if (setvar) {
    int mode_num = -1;
    Cli_Debug("ConfigIcpMode: mode %d\n", arg_ref);

    // convert string into mode number
    if (arg_ref == CMD_CONFIG_ICP_MODE_DISABLED) {
      mode_num = 0;
    } else if (arg_ref == CMD_CONFIG_ICP_MODE_RECEIVE) {
      mode_num = 1;
    } else if (arg_ref == CMD_CONFIG_ICP_MODE_SENDRECEIVE) {
      mode_num = 2;
    } else {
      mode_num = -1;
    }

    Cli_Debug("ConfigIcpMode: mode_num %d\n", mode_num);

    if (mode_num == -1) {
      return CLI_ERROR;
    }

    TSActionNeedT action_need = TS_ACTION_UNDEFINED;
    TSError status = Cli_RecordSetInt("proxy.config.icp.enabled",
                                       mode_num, &action_need);
    if (status) {
      return status;
    }

    return (Cli_ConfigEnactChanges(action_need));

  } else {
    TSInt value_in = -1;
    TSError status = Cli_RecordGetInt("proxy.config.icp.enabled", &value_in);

    if (status) {
      return status;
    }

    switch (value_in) {
    case 0:
      Cli_Printf("disabled\n");
      break;
    case 1:
      Cli_Printf("receive\n");
      break;
    case 2:
      Cli_Printf("send-receive\n");
      break;
    default:
      Cli_Printf("?\n");
      break;
    }

    return CLI_OK;
  }
}

// config Cache Freshness Verify sub-command
int
ConfigCacheFreshnessVerify(int arg_ref, int setvar)
{


  Cli_Debug(" ConfigCacheFreshnessVerify: %d set?%d\n", arg_ref, setvar);

  TSInt int_val = 0;
  TSActionNeedT action_need = TS_ACTION_UNDEFINED;
  TSError status = TS_ERR_OKAY;

  switch (setvar) {
  case 0:                      //get

    status = Cli_RecordGetInt("proxy.config.http.cache.when_to_revalidate", &int_val);
    if (status) {
      return status;
    }
    switch (int_val) {
    case 0:
      Cli_Printf("when-expired\n");
      break;
    case 1:
      Cli_Printf("no-date\n");
      break;
    case 2:
      Cli_Printf("always\n");
      break;
    case 3:
      Cli_Printf("never\n");
      break;
    }
    return CLI_OK;

  case 1:                      //set
    {
      switch (arg_ref) {
      case CMD_CONFIG_CACHE_FRESHNESS_VERIFY_WHEN_EXPIRED:
        int_val = 0;
        break;
      case CMD_CONFIG_CACHE_FRESHNESS_VERIFY_NO_DATE:
        int_val = 1;
        break;
      case CMD_CONFIG_CACHE_FRESHNESS_VERIFY_ALWALYS:
        int_val = 2;
        break;
      case CMD_CONFIG_CACHE_FRESHNESS_VERIFY_NEVER:
        int_val = 3;
        break;
      default:
        Cli_Printf("ERROR in Argument\n");
      }
      status = Cli_RecordSetInt("proxy.config.http.cache.when_to_revalidate", (TSInt) int_val, &action_need);
      if (status) {
        return status;
      }
      return (Cli_ConfigEnactChanges(action_need));
    }

  default:
    return CLI_ERROR;
  }
}

// config Cache Freshness Minimum sub-command
int
ConfigCacheFreshnessMinimum(int arg_ref, int setvar)
{

  Cli_Debug("ConfigCacheFreshnessMinimum: %d set?%d\n", arg_ref, setvar);

  TSInt int_val = 0;
  TSActionNeedT action_need = TS_ACTION_UNDEFINED;
  TSError status = TS_ERR_OKAY;

  switch (setvar) {
  case 0:                      //get

    status = Cli_RecordGetInt("proxy.config.http.cache.required_headers", &int_val);
    if (status) {
      return status;
    }
    switch (int_val) {
    case 0:
      Cli_Printf("nothing\n");
      break;
    case 1:
      Cli_Printf("last-modified\n");
      break;
    case 2:
      Cli_Printf("explicit\n");
      break;
    }
    return CLI_OK;

  case 1:                      //set
    {
      switch (arg_ref) {
      case CMD_CONFIG_CACHE_FRESHNESS_MINIMUM_NOTHING:
        int_val = 0;
        break;
      case CMD_CONFIG_CACHE_FRESHNESS_MINIMUM_LAST_MODIFIED:
        int_val = 1;
        break;
      case CMD_CONFIG_CACHE_FRESHNESS_MINIMUM_EXPLICIT:
        int_val = 2;
        break;
      default:
        Cli_Printf("ERROR in arg\n");
      }
      status = Cli_RecordSetInt("proxy.config.http.cache.required_headers", (TSInt) int_val, &action_need);
      if (status) {
        return status;
      }
      return (Cli_ConfigEnactChanges(action_need));
    }
  default:
    return CLI_ERROR;
  }
}

// config Cache FreshnessNoExpireLimit
int
ConfigCacheFreshnessNoExpireLimit(TSInt min, TSInt max, int setvar)
{

  Cli_Debug(" ConfigCacheFreshnessNoExpireLimit: greater than %d \n", min);
  Cli_Debug(" ConfigCacheFreshnessNoExpireLimit: less than %d\n", max);
  Cli_Debug(" set?%d\n", setvar);
  TSInt min_val = 0;
  TSInt max_val = 0;
  TSActionNeedT action_need = TS_ACTION_UNDEFINED;
  TSError status = TS_ERR_OKAY;

  switch (setvar) {
  case 0:                      //get
    status = Cli_RecordGetInt("proxy.config.http.cache.heuristic_min_lifetime", &min_val);
    if (status) {
      return status;
    }
    status = Cli_RecordGetInt("proxy.config.http.cache.heuristic_max_lifetime", &max_val);
    if (status) {
      return status;
    }

    Cli_Printf("greater than -- %d \n", min_val);
    Cli_Printf("less than ----- %d\n", max_val);
    return CLI_OK;
  case 1:
    status = Cli_RecordSetInt("proxy.config.http.cache.heuristic_min_lifetime", (TSInt) min, &action_need);
    if (status) {
      return status;
    }
    status = Cli_RecordSetInt("proxy.config.http.cache.heuristic_max_lifetime", (TSInt) max, &action_need);
    if (status) {
      return status;
    }
    return (Cli_ConfigEnactChanges(action_need));

  default:
    return CLI_ERROR;
  }
}

// config Cache Vary sub-command
int
ConfigCacheVary(int arg_ref, char *field, int setvar)
{

  Cli_Debug(" ConfigCacheVary: %d set?%d\n", arg_ref, setvar);
  Cli_Debug(" field: %s\n", field);
  TSString str_val = NULL;
  TSActionNeedT action_need = TS_ACTION_UNDEFINED;
  TSError status = TS_ERR_OKAY;

  switch (setvar) {
  case 0:                      //get

    switch (arg_ref) {
    case CMD_CONFIG_CACHE_VARY_TEXT:
      status = Cli_RecordGetString("proxy.config.http.cache.vary_default_text", &str_val);
      break;

    case CMD_CONFIG_CACHE_VARY_COOKIES_IMAGES:
      status = Cli_RecordGetString("proxy.config.http.cache.vary_default_images", &str_val);
      break;

    case CMD_CONFIG_CACHE_VARY_OTHER:
      status = Cli_RecordGetString("proxy.config.http.cache.vary_default_other", &str_val);
      break;
    default:
      Cli_Printf(" config:cache vary <text | images | other > <field>\n");
    }
    if (status) {
      return status;
    }

    if (str_val)
      Cli_Printf("%s\n", str_val);
    else
      Cli_Printf("none\n");

    return CLI_OK;

  case 1:                      //set
    {
      switch (arg_ref) {
      case CMD_CONFIG_CACHE_VARY_TEXT:
        status = Cli_RecordSetString("proxy.config.http.cache.vary_default_text", (TSString) field, &action_need);
        break;

      case CMD_CONFIG_CACHE_VARY_COOKIES_IMAGES:
        status = Cli_RecordSetString("proxy.config.http.cache.vary_default_images", (TSString) field, &action_need);
        break;

      case CMD_CONFIG_CACHE_VARY_OTHER:
        status = Cli_RecordSetString("proxy.config.http.cache.vary_default_other", (TSString) field, &action_need);
        break;
      default:
        Cli_Printf("ERROR in arg\n");
      }

      if (status) {
        return status;
      }
      return (Cli_ConfigEnactChanges(action_need));
    }
  default:
    return CLI_ERROR;
  }

}

// config Cache Cookies sub-command
int
ConfigCacheCookies(int arg_ref, int setvar)
{

  Cli_Debug("ConfigCacheCookies: %d set?%d\n", arg_ref, setvar);

  TSInt int_val = 0;
  TSActionNeedT action_need = TS_ACTION_UNDEFINED;
  TSError status = TS_ERR_OKAY;

  switch (setvar) {
  case 0:                      //get

    status = Cli_RecordGetInt("proxy.config.http.cache.cache_responses_to_cookies", &int_val);
    if (status) {
      return status;
    }
    switch (int_val) {
    case 0:
      Cli_Printf("none\n");
      break;
    case 1:
      Cli_Printf("all\n");
      break;
    case 2:
      Cli_Printf("images\n");
      break;
    case 3:
      Cli_Printf("non-text\n");
      break;
    case 4:
      Cli_Printf("non-text-extended\n");
      break;
    default:
      Cli_Printf("ERR: invalid value fetched\n");
    }
    return CLI_OK;

  case 1:                      //set
    {
      switch (arg_ref) {
      case CMD_CONFIG_CACHE_COOKIES_NONE:
        int_val = 0;
        break;
      case CMD_CONFIG_CACHE_COOKIES_ALL:
        int_val = 1;
        break;
      case CMD_CONFIG_CACHE_VARY_COOKIES_IMAGES:
        int_val = 2;
        break;
      case CMD_CONFIG_CACHE_COOKIES_NON_TEXT:
        int_val = 3;
        break;
      case CMD_CONFIG_CACHE_COOKIES_NON_TEXT_EXT:
        int_val = 4;
        break;
      default:
        Cli_Printf("ERROR in arg\n");
      }
      status = Cli_RecordSetInt("proxy.config.http.cache.cache_responses_to_cookies", (TSInt) int_val, &action_need);
      if (status) {
        return status;
      }
      return (Cli_ConfigEnactChanges(action_need));
    }
  default:
    return CLI_ERROR;
  }

}

// config Cache Clear sub-command
int
ConfigCacheClear()
{

  Cli_Debug("ConfigCacheClear");

  TSProxyStateT state;
  TSError status = TS_ERR_OKAY;

  state = TSProxyStateGet();
  switch (state) {
  case TS_PROXY_ON:
    Cli_Printf("Traffic Server is running.\nClear Cache failed.\n");
    return CLI_ERROR;

  case TS_PROXY_OFF:
    status = TSProxyStateSet(TS_PROXY_ON, TS_CACHE_CLEAR_ON);
    if (status) {
      return status;
    }
    return status;
  case TS_PROXY_UNDEFINED:
    Cli_Printf("Error %d: Problem clearing Cache.\n", state);
    return CLI_ERROR;
  default:
    Cli_Printf("Unexpected Proxy State\n");
    return CLI_ERROR;
  }

}


// config HostDB Clear sub-command
int
ConfigHostdbClear()
{

  Cli_Debug("ConfigHostDBClear\n");

  TSProxyStateT state = TS_PROXY_UNDEFINED;
  TSError status = TS_ERR_OKAY;

  state = TSProxyStateGet();
  Cli_Debug("Proxy State %d\n", state);
  switch (state) {
  case TS_PROXY_ON:
    Cli_Printf("Traffic Server is running.\nClear HostDB failed.\n");
    return CLI_ERROR;

  case TS_PROXY_OFF:
    status = TSProxyStateSet(TS_PROXY_ON, TS_CACHE_CLEAR_HOSTDB);
    if (status) {
      return status;
    }
    return status;
  case TS_PROXY_UNDEFINED:
    Cli_Printf("Error %d: Problem clearing HostDB.\n", state);
    return CLI_ERROR;
  default:
    Cli_Printf("Unexpected Proxy State\n");
    return CLI_ERROR;
  }

}

//config virtualip list
int
ConfigVirtualIpList()
{

  Cli_Debug("ConfigVirtualIpList\n");

  TSCfgContext VipCtx;
  int EleCount, i;
  TSVirtIpAddrEle *VipElePtr;

  VipCtx = TSCfgContextCreate(TS_FNAME_VADDRS);
  if (TSCfgContextGet(VipCtx) != TS_ERR_OKAY) {
    Cli_Printf("ERROR READING FILE\n");
    return CLI_ERROR;
  }
  EleCount = TSCfgContextGetCount(VipCtx);
  if (EleCount == 0) {
    Cli_Printf("\nNo Virtual IP addresses specified\n");
  } else {
    Cli_Printf("\nVirtual IP addresses specified\n" "------------------------------\n", EleCount);
    for (i = 0; i < EleCount; i++) {
      VipElePtr = (TSVirtIpAddrEle *) TSCfgContextGetEleAt(VipCtx, i);
      Cli_Printf("%d) %s %s %d\n", i, VipElePtr->ip_addr, VipElePtr->intr, VipElePtr->sub_intr);
    }
  }
  Cli_Printf("\n");
  TSCfgContextDestroy(VipCtx);

  return CLI_OK;
}

//config virtualip add
int
ConfigVirtualipAdd(char *ip, char *device, int subinterface, int setvar)
{

  Cli_Debug("ConfigVirtualipAdd: %s %s %d set? %d\n", ip, device, subinterface, setvar);

  TSActionNeedT action_need = TS_ACTION_UNDEFINED;
  TSError status = TS_ERR_OKAY;
  TSCfgContext VipCtx;
  int size;
  TSVirtIpAddrEle *VipElePtr;

  switch (setvar) {
  case 0:                      //get
    return (ConfigVirtualIpList());

  case 1:                      //set

    VipElePtr = TSVirtIpAddrEleCreate();

    if (!VipElePtr)
      return CLI_ERROR;

    size = strlen(ip);
    VipElePtr->ip_addr = new char[size + 1];
    ink_strlcpy(VipElePtr->ip_addr, ip, size + 1);
    size = strlen(device);
    VipElePtr->intr = new char[size + 1];
    ink_strlcpy(VipElePtr->intr, device, size + 1);
    VipElePtr->sub_intr = subinterface;
    VipCtx = TSCfgContextCreate(TS_FNAME_VADDRS);
    if (TSCfgContextGet(VipCtx) != TS_ERR_OKAY)
      Cli_Printf("ERROR READING FILE\n");
    status = TSCfgContextAppendEle(VipCtx, (TSCfgEle *) VipElePtr);
    if (status) {
      Cli_Printf("ERROR %d: Failed to add entry to config file.\n", status);
      return status;
    }

    status = TSCfgContextCommit(VipCtx, &action_need, NULL);

    if (status) {
      Cli_Printf("\nERROR %d: Failed to commit changes to config file.\n"
                 "         Check parameters for correctness and try again.\n", status);
      return status;
    }
    return (Cli_ConfigEnactChanges(action_need));

  default:
    return CLI_ERROR;
  }
}

//config virtualip delete
int
ConfigVirtualipDelete(int ip_no, int setvar)
{

  Cli_Debug("ConfigVirtualipDelete: %d set? %d\n", ip_no, setvar);

  TSActionNeedT action_need = TS_ACTION_UNDEFINED;
  TSError status = TS_ERR_OKAY;
  TSCfgContext VipCtx;
  int EleCount;

  switch (setvar) {
  case 0:                      //get
    return (ConfigVirtualIpList());

  case 1:                      //set

    VipCtx = TSCfgContextCreate(TS_FNAME_VADDRS);
    if (TSCfgContextGet(VipCtx) != TS_ERR_OKAY) {
      Cli_Printf("ERROR READING FILE\n");
      return CLI_ERROR;
    }
    EleCount = TSCfgContextGetCount(VipCtx);
    if (EleCount == 0) {
      Cli_Printf("No Virual IP's to delete.\n");
      return CLI_ERROR;
    }
    if (ip_no<0 || ip_no>= EleCount) {
      if (EleCount == 1)
        Cli_Printf("ERROR: Invalid parameter %d, expected integer 0\n", ip_no);
      else
        Cli_Printf("ERROR: Invalid parameter %d, expected integer between 0 and %d\n", ip_no, EleCount - 1);

      return CLI_ERROR;
    }
    status = TSCfgContextRemoveEleAt(VipCtx, ip_no);
    if (status) {
      return status;
    }
    status = TSCfgContextCommit(VipCtx, &action_need, NULL);

    if (status) {
      Cli_Printf("\nERROR %d: Failed to commit changes to config file.\n"
                 "         Check parameters for correctness and try again.\n", status);
      return status;
    }

    return (Cli_ConfigEnactChanges(action_need));

  default:
    return CLI_ERROR;
  }
}

int
IsValidIpAddress(char *str)
{
  char buf[50], *endPtr;
  int count = 0, gotfield = 0;
  int num;
  while (*str != 0) {
    if (!isdigit(*str) && *str != '.') {
      return CLI_ERROR;
    }

    if (isdigit(*str)) {
      buf[count] = *str;
      count++;
    }
    str++;
    if (*str == '.' || *str == 0) {
      if (count > 3) {
        return CLI_ERROR;
      }
      buf[count] = 0;
      num = strtol(buf, &endPtr, 0);
      if ((endPtr == buf) || (*endPtr != 0)) {
        return CLI_ERROR;
      }
      if (num > 255) {
        return CLI_ERROR;
      }
      gotfield++;
      count = 0;
      if (*str == 0)
        break;
      else
        str++;

    }
  }
  if (gotfield != 4) {
    return CLI_ERROR;
  }
  return CLI_OK;
}

int
IsValidHostname(char *str)
{
  while (*str != 0) {
    if (!isalnum(*str) && *str != '-' && *str != '_') {
      return CLI_ERROR;
    }
    str++;
  }
  return CLI_OK;
}

int
IsValidFQHostname(char *str)
{
  while (*str != 0) {
    if (!isalnum(*str) && *str != '-' && *str != '_' && *str != '.') {
      return CLI_ERROR;
    }
    str++;
  }
  return CLI_OK;
}

int
IsValidDomainname(char *str)
{
  while (*str != 0) {
    if (!isalnum(*str) && *str != '-' && *str != '_' && *str != '.') {
      return CLI_ERROR;
    }
    str++;
  }
  return CLI_OK;
}


char *
pos_after_string(char *haystack, const char *needle)
{
  char *retval;

  retval = strstr(haystack, needle);

  if (retval != (char *) NULL)
    retval += strlen(needle);

  return retval;
}


int
StartBinary(char *abs_bin_path, char *bin_options, int isScript)
{
  char ebuf[1024];
  unsigned char ret_value = false;
  char output[1024];


  // Before we do anything lets check for the existence of
  // the binary along with it's execute permmissions
  if (access(abs_bin_path, F_OK) < 0) {
    // Error can't find binary
    snprintf(ebuf, 70, "Cannot find executable %s\n", abs_bin_path);
    Cli_Error(ebuf);
    goto done;
  }
  // binary exists, check permissions
  else if (access(abs_bin_path, R_OK | X_OK) < 0) {
    // Error don't have proper permissions
    snprintf(ebuf, 70, "Cannot execute %s\n", abs_bin_path);
    Cli_Error(ebuf);
    goto done;
  }

  memset(&output, 0, 1024);
  if (bin_options) {
    if (isScript)
      snprintf(output, 1024, "/bin/sh -x %s %s", abs_bin_path, bin_options);
    else
      snprintf(output, 1024, "%s %s", abs_bin_path, bin_options);
    ret_value = (system(output) / 256) % (UCHAR_MAX + 1);
  } else {
    ret_value = (system(abs_bin_path) / 256) % (UCHAR_MAX + 1);
  }


done:
  return ret_value;
}

// config Logging Event sub-command
int
ConfigLoggingEvent(int arg_ref, int setvar)
{

  Cli_Debug("ConfigLoggingEvent: %d set?%d\n", arg_ref, setvar);

  TSInt int_val = 0;
  TSActionNeedT action_need = TS_ACTION_UNDEFINED;
  TSError status = TS_ERR_OKAY;

  switch (setvar) {
  case 0:                      //get

    status = Cli_RecordGetInt("proxy.config.log.logging_enabled", &int_val);
    if (status) {
      return status;
    }
    switch (int_val) {
    case 0:
      Cli_Printf("disabled\n");
      break;
    case 1:
      Cli_Printf("error-only\n");
      break;
    case 2:
      Cli_Printf("trans-only\n");
      break;
    case 3:
      Cli_Printf("enabled\n");
      break;
    default:
      Cli_Printf("ERR: invalid value fetched\n");
    }
    return CLI_OK;

  case 1:                      //set
    {
      switch (arg_ref) {
      case CMD_CONFIG_LOGGING_EVENT_ENABLED:
        int_val = 3;
        break;
      case CMD_CONFIG_LOGGING_EVENT_TRANS_ONLY:
        int_val = 2;
        break;
      case CMD_CONFIG_LOGGING_EVENT_ERROR_ONLY:
        int_val = 1;
        break;
      case CMD_CONFIG_LOGGING_EVENT_DISABLED:
        int_val = 0;
        break;
      default:
        Cli_Printf("ERROR in arg\n");
      }
      status = Cli_RecordSetInt("proxy.config.log.logging_enabled", (TSInt) int_val, &action_need);
      if (status) {
        return status;
      }
      return (Cli_ConfigEnactChanges(action_need));
    }

  default:
    return CLI_ERROR;
  }

}

// config Logging collation status sub-command
int
ConfigLoggingCollationStatus(int arg_ref, int setvar)
{


  Cli_Debug("ConfigLoggingCollationStatus: %d set?%d\n", arg_ref, setvar);

  TSInt int_val = 0;
  TSActionNeedT action_need = TS_ACTION_UNDEFINED;
  TSError status = TS_ERR_OKAY;

  switch (setvar) {
  case 0:                      //get

    status = Cli_RecordGetInt("proxy.local.log.collation_mode", &int_val);
    if (status) {
      return status;
    }
    switch (int_val) {
    case 0:
      Cli_Printf("inactive\n");
      break;
    case 1:
      Cli_Printf("host\n");
      break;
    case 2:
      Cli_Printf("send-standard\n");
      break;
    case 3:
      Cli_Printf("send-custom\n");
      break;
    case 4:
      Cli_Printf("send-all\n");
      break;
    default:
      Cli_Printf("ERR: invalid value fetched\n");
    }
    return CLI_OK;

  case 1:                      //set
    {
      switch (arg_ref) {
      case CMD_CONFIG_LOGGING_COLLATION_STATUS_INACTIVE:
        int_val = 0;
        break;
      case CMD_CONFIG_LOGGING_COLLATION_STATUS_HOST:
        int_val = 1;
        break;
      case CMD_CONFIG_LOGGING_COLLATION_STATUS_SEND_STANDARD:
        int_val = 2;
        break;
      case CMD_CONFIG_LOGGING_COLLATION_STATUS_SEND_CUSTOM:
        int_val = 3;
        break;
      case CMD_CONFIG_LOGGING_COLLATION_STATUS_SEND_ALL:
        int_val = 4;
        break;
      default:
        Cli_Printf("ERROR in arg\n");
      }
      Cli_Debug("ConfigLoggingCollationStatus: %d set?%d\n", int_val, setvar);
      status = Cli_RecordSetInt("proxy.local.log.collation_mode", (TSInt) int_val, &action_need);
      if (status) {
        return status;
      }
      return (Cli_ConfigEnactChanges(action_need));
    }

  default:
    return CLI_ERROR;
  }
}

// config Logging collation sub-command
int
ConfigLoggingCollation(TSString secret, int arg_ref, TSInt orphan, int setvar)
{

  Cli_Debug(" LoggingCollation %s %d %d\n", secret, orphan, arg_ref);
  Cli_Debug(" set? %d\n", setvar);

  TSString str_val = NULL;
  TSInt int_val = 0;
  TSActionNeedT action_need = TS_ACTION_UNDEFINED;
  TSError status = TS_ERR_OKAY;

  switch (setvar) {
  case 0:                      //get

    status = Cli_RecordGetString("proxy.config.log.collation_secret", &str_val);
    if (status) {
      return status;
    }
    if (str_val)
      Cli_Printf("%s\n", str_val);
    else
      Cli_Printf("none\n");

    status = Cli_RecordGetInt("proxy.config.log.collation_host_tagged", &int_val);
    if (status) {
      return status;
    }

    if (Cli_PrintEnable("", int_val) == CLI_ERROR) {
      return CLI_ERROR;
    }

    status = Cli_RecordGetInt("proxy.config.log.collation_port", &int_val);
    if (status) {
      return status;
    }
    Cli_Printf("%d MB\n", int_val);

    return CLI_OK;

  case 1:                      //set
    {

      status = Cli_RecordSetString("proxy.config.log.collation_secret", (TSString) secret, &action_need);
      if (status) {
        return status;
      }
      switch (arg_ref) {
      case CMD_CONFIG_LOGGING_ON:
        int_val = 1;
        break;
      case CMD_CONFIG_LOGGING_OFF:
        int_val = 0;
        break;
      default:
        Cli_Printf("ERROR in arg\n");
      }

      status = Cli_RecordSetInt("proxy.config.log.collation_host_tagged", (TSInt) int_val, &action_need);
      if (status) {
        return status;
      }

      status = Cli_RecordSetInt("proxy.config.log.collation_port", (TSInt) orphan, &action_need);
      if (status) {
        return status;
      }
    }
    return (Cli_ConfigEnactChanges(action_need));

  default:
    return CLI_ERROR;
  }
}


// config Logging Format Type File sub-command
int
ConfigLoggingFormatTypeFile(int arg_ref_format, int arg_ref,
                            int arg_ref_type, TSString file, TSString header, int setvar)
{

  Cli_Debug(" LoggingFormatTypeFile %d %d %d %s %s set?%d\n",
            arg_ref_format, arg_ref, arg_ref_type, file, header, setvar);

  TSString str_val = NULL;
  TSInt int_val = 0;
  TSActionNeedT action_need = TS_ACTION_UNDEFINED;
  TSError status = TS_ERR_OKAY;

  switch (setvar) {
  case 0:                      //get

    switch (arg_ref_format) {
    case CMD_CONFIG_LOGGING_FORMAT_SQUID:
      status = Cli_RecordGetInt("proxy.config.log.squid_log_enabled", &int_val);
      if (status) {
        return status;
      }
      if (Cli_PrintEnable("", int_val) == CLI_ERROR) {
        return CLI_ERROR;
      }
      status = Cli_RecordGetInt("proxy.config.log.squid_log_is_ascii", &int_val);
      if (status) {
        return status;
      }
      switch (int_val) {
      case 0:
        Cli_Printf("binary\n");
        break;
      case 1:
        Cli_Printf("ascii\n");
        break;
      }
      status = Cli_RecordGetString("proxy.config.log.squid_log_name", &str_val);
      if (status) {
        return status;
      }
      if (str_val) {
        Cli_Printf("%s\n", str_val);
      } else {
        Cli_Printf("none\n");
      }

      status = Cli_RecordGetString("proxy.config.log.squid_log_header", &str_val);
      if (status) {
        return status;
      }
      if (str_val) {
        Cli_Printf("%s\n", str_val);
      } else {
        Cli_Printf("none\n");
      }
      return CLI_OK;

    case CMD_CONFIG_LOGGING_FORMAT_NETSCAPE_COMMON:
      status = Cli_RecordGetInt("proxy.config.log.common_log_enabled", &int_val);
      if (status) {
        return status;
      }
      if (Cli_PrintEnable("", int_val) == CLI_ERROR) {
        return CLI_ERROR;
      }
      status = Cli_RecordGetInt("proxy.config.log.common_log_is_ascii", &int_val);
      if (status) {
        return status;
      }
      switch (int_val) {
      case 0:
        Cli_Printf("binary\n");
        break;
      case 1:
        Cli_Printf("ascii\n");
        break;
      }
      status = Cli_RecordGetString("proxy.config.log.common_log_name", &str_val);
      if (status) {
        return status;
      }
      Cli_Printf("%s\n", str_val);
      status = Cli_RecordGetString("proxy.config.log.common_log_header", &str_val);
      if (status) {
        return status;
      }
      Cli_Printf("%s\n", str_val);
      return CLI_OK;
    case CMD_CONFIG_LOGGING_FORMAT_NETSCAPE_EXT:
      status = Cli_RecordGetInt("proxy.config.log.extended_log_enabled", &int_val);
      if (status) {
        return status;
      }
      if (Cli_PrintEnable("", int_val) == CLI_ERROR) {
        return CLI_ERROR;
      }
      status = Cli_RecordGetInt("proxy.config.log.extended_log_is_ascii", &int_val);
      if (status) {
        return status;
      }
      switch (int_val) {
      case 0:
        Cli_Printf("binary\n");
        break;
      case 1:
        Cli_Printf("ascii\n");
        break;
      }
      status = Cli_RecordGetString("proxy.config.log.extended_log_name", &str_val);
      if (status) {
        return status;
      }
      Cli_Printf("%s\n", str_val);
      status = Cli_RecordGetString("proxy.config.log.extended_log_header", &str_val);
      if (status) {
        return status;
      }
      Cli_Printf("%s\n", str_val);
      return CLI_OK;

    case CMD_CONFIG_LOGGING_FORMAT_NETSCAPE_EXT2:
      status = Cli_RecordGetInt("proxy.config.log.extended2_log_enabled", &int_val);
      if (status) {
        return status;
      }
      if (Cli_PrintEnable("", int_val) == CLI_ERROR) {
        return CLI_ERROR;
      }
      status = Cli_RecordGetInt("proxy.config.log.extended2_log_is_ascii", &int_val);
      if (status) {
        return status;
      }
      switch (int_val) {
      case 0:
        Cli_Printf("binary\n");
        break;
      case 1:
        Cli_Printf("ascii\n");
        break;
      }
      status = Cli_RecordGetString("proxy.config.log.extended2_log_name", &str_val);
      if (status) {
        return status;
      }
      Cli_Printf("%s\n", str_val);
      status = Cli_RecordGetString("proxy.config.log.extended2_log_header", &str_val);
      if (status) {
        return status;
      }
      Cli_Printf("%s\n", str_val);
      return CLI_OK;
    }
    break;

  case 1:                      //set
    {
      switch (arg_ref_format) {
      case CMD_CONFIG_LOGGING_FORMAT_SQUID:
        switch (arg_ref) {
        case CMD_CONFIG_LOGGING_ON:
          int_val = 1;
          break;
        case CMD_CONFIG_LOGGING_OFF:
          int_val = 0;
          break;
        default:
          Cli_Printf("ERROR in arg\n");
          return CLI_ERROR;
        }

        status = Cli_RecordSetInt("proxy.config.log.squid_log_enabled", (TSInt) int_val, &action_need);
        if (status) {
          return status;
        }

        switch (arg_ref_type) {
        case CMD_CONFIG_LOGGING_TYPE_ASCII:
          int_val = 1;
          break;
        case CMD_CONFIG_LOGGING_TYPE_BINARY:
          int_val = 0;
          break;
        default:
          Cli_Printf("ERROR in arg\n");
          return CLI_ERROR;
        }
        status = Cli_RecordSetInt("proxy.config.log.squid_log_is_ascii", (TSInt) int_val, &action_need);
        if (status) {
          return status;
        }

        status = Cli_RecordSetString("proxy.config.log.squid_log_name", (TSString) file, &action_need);

        if (status) {
          return status;
        }

        status = Cli_RecordSetString("proxy.config.log.squid_log_header", (TSString) header, &action_need);
        if (status) {
          return status;
        }
        return (Cli_ConfigEnactChanges(action_need));
      case CMD_CONFIG_LOGGING_FORMAT_NETSCAPE_COMMON:

        switch (arg_ref) {
        case CMD_CONFIG_LOGGING_ON:
          int_val = 1;
          break;
        case CMD_CONFIG_LOGGING_OFF:
          int_val = 0;
          break;
        default:
          Cli_Printf("ERROR in arg\n");
          return CLI_ERROR;
        }

        status = Cli_RecordSetInt("proxy.config.log.common_log_enabled", (TSInt) int_val, &action_need);
        if (status) {
          return status;
        }

        switch (arg_ref_type) {
        case CMD_CONFIG_LOGGING_TYPE_ASCII:
          int_val = 1;
          break;
        case CMD_CONFIG_LOGGING_TYPE_BINARY:
          int_val = 0;
          break;
        default:
          Cli_Printf("ERROR in arg\n");
          return CLI_ERROR;
        }

        status = Cli_RecordSetInt("proxy.config.log.common_log_is_ascii", (TSInt) int_val, &action_need);
        if (status) {
          return status;
        }

        status = Cli_RecordSetString("proxy.config.log.common_log_name", (TSString) file, &action_need);

        if (status) {
          return status;
        }

        status = Cli_RecordSetString("proxy.config.log.common_log_header", (TSString) header, &action_need);
        if (status) {
          return status;
        }
        return (Cli_ConfigEnactChanges(action_need));

      case CMD_CONFIG_LOGGING_FORMAT_NETSCAPE_EXT:


        switch (arg_ref) {
        case CMD_CONFIG_LOGGING_ON:
          int_val = 1;
          break;
        case CMD_CONFIG_LOGGING_OFF:
          int_val = 0;
          break;
        default:
          Cli_Printf("ERROR in arg\n");
          return CLI_ERROR;
        }

        status = Cli_RecordSetInt("proxy.config.log.extended_log_enabled", (TSInt) int_val, &action_need);

        if (status) {
          return status;
        }
        switch (arg_ref_type) {
        case CMD_CONFIG_LOGGING_TYPE_ASCII:
          int_val = 1;
          break;
        case CMD_CONFIG_LOGGING_TYPE_BINARY:
          int_val = 0;
          break;
        default:
          Cli_Printf("ERROR in arg\n");
          return CLI_ERROR;
        }
        status = Cli_RecordSetInt("proxy.config.log.extended_log_is_ascii", (TSInt) int_val, &action_need);
        if (status) {
          return status;
        }

        status = Cli_RecordSetString("proxy.config.log.extended_log_name", (TSString) file, &action_need);

        if (status) {
          return status;
        }

        status = Cli_RecordSetString("proxy.config.log.extended_log_header", (TSString) header, &action_need);
        if (status) {
          return status;
        }
        return (Cli_ConfigEnactChanges(action_need));
      case CMD_CONFIG_LOGGING_FORMAT_NETSCAPE_EXT2:

        switch (arg_ref) {
        case CMD_CONFIG_LOGGING_ON:
          int_val = 1;
          break;
        case CMD_CONFIG_LOGGING_OFF:
          int_val = 0;
          break;
        default:
          Cli_Printf("ERROR in arg\n");
          return CLI_ERROR;
        }

        status = Cli_RecordSetInt("proxy.config.log.extended2_log_enabled", (TSInt) int_val, &action_need);
        if (status) {
          return status;
        }
        switch (arg_ref_type) {
        case CMD_CONFIG_LOGGING_TYPE_ASCII:
          int_val = 1;
          break;
        case CMD_CONFIG_LOGGING_TYPE_BINARY:
          int_val = 0;
          break;
        default:
          Cli_Printf("ERROR in arg\n");
          return CLI_ERROR;
        }
        status = Cli_RecordSetInt("proxy.config.log.extended2_log_is_ascii", (TSInt) int_val, &action_need);
        if (status) {
          return status;
        }

        status = Cli_RecordSetString("proxy.config.log.extended2_log_name", (TSString) file, &action_need);

        if (status) {
          return status;
        }

        status = Cli_RecordSetString("proxy.config.log.extended2_log_header", (TSString) header, &action_need);
        if (status) {
          return status;
        }
        return (Cli_ConfigEnactChanges(action_need));

      }
    }
  }
  return CLI_OK;
}

// config Logging splitting sub-command
int
ConfigLoggingSplitting(int arg_ref_protocol, int arg_ref_on_off, int setvar)
{

  Cli_Debug("ConfigLoggingSplitting %d %d set?%d\n", arg_ref_protocol, arg_ref_on_off, setvar);

  TSInt int_val;
  TSActionNeedT action_need = TS_ACTION_UNDEFINED;
  TSError status = TS_ERR_OKAY;

  switch (setvar) {
  case 0:                      //get

    switch (arg_ref_protocol) {
    case CMD_CONFIG_LOGGING_SPLITTING_ICP:
      status = Cli_RecordGetInt("proxy.config.log.separate_icp_logs", &int_val);
      if (status) {
        return status;
      }
      if (Cli_PrintEnable("", int_val) == CLI_ERROR) {
        return CLI_ERROR;
      }
      return CLI_OK;
    case CMD_CONFIG_LOGGING_SPLITTING_HTTP:
      status = Cli_RecordGetInt("proxy.config.log.separate_host_logs", &int_val);
      if (status) {
        return status;
      }
      if (Cli_PrintEnable("", int_val) == CLI_ERROR) {
        return CLI_ERROR;
      }
      return CLI_OK;
    default:
      Cli_Printf("Error in Arg\n");
      return CLI_ERROR;
    }
    return CLI_ERROR;

  case 1:
    {
      switch (arg_ref_on_off) {
      case CMD_CONFIG_LOGGING_ON:
        int_val = 1;
        break;
      case CMD_CONFIG_LOGGING_OFF:
        int_val = 0;
        break;
      default:
        Cli_Printf("ERROR in arg\n");
        return CLI_ERROR;
      }

      switch (arg_ref_protocol) {
      case CMD_CONFIG_LOGGING_SPLITTING_ICP:
        status = Cli_RecordSetInt("proxy.config.log.separate_icp_logs", int_val, &action_need);
        if (status) {
          return status;
        }
        return (Cli_ConfigEnactChanges(action_need));

      case CMD_CONFIG_LOGGING_SPLITTING_HTTP:
        status = Cli_RecordSetInt("proxy.config.log.separate_host_logs", int_val, &action_need);
        if (status) {
          return status;
        }
        return (Cli_ConfigEnactChanges(action_need));
      }
      return CLI_ERROR;
    }

  default:
    return CLI_ERROR;
  }
}

// config Logging Custom Format sub-command
int
ConfigLoggingCustomFormat(int arg_ref_on_off, int arg_ref_format, int setvar)
{

  Cli_Debug("ConfigLoggingSplitting %d %d set?%d\n", arg_ref_on_off, arg_ref_format, setvar);

  TSInt int_val = 0;
  TSActionNeedT action_need = TS_ACTION_UNDEFINED;
  TSError status = TS_ERR_OKAY;

  switch (setvar) {
  case 0:                      //get

    status = Cli_RecordGetInt("proxy.config.log.custom_logs_enabled", &int_val);
    if (status) {
      return status;
    }
    if (Cli_PrintEnable("", int_val) == CLI_ERROR) {
      return CLI_ERROR;
    }
    Cli_Printf("xml\n");
    return CLI_OK;
  case 1:
    {
      switch (arg_ref_on_off) {
      case CMD_CONFIG_LOGGING_ON:
        int_val = 1;
        break;
      case CMD_CONFIG_LOGGING_OFF:
        int_val = 0;
        break;
      default:
        Cli_Printf("ERROR in arg\n");
        return CLI_ERROR;
      }

      status = Cli_RecordSetInt("proxy.config.log.custom_logs_enabled", int_val, &action_need);
      if (status) {
        return status;
      }
      return (Cli_ConfigEnactChanges(action_need));
    }

  default:
    return CLI_ERROR;
  }
}


// config Logging rolling offset interval autodelete sub-command
int
ConfigLoggingRollingOffsetIntervalAutodelete(int arg_ref_rolling,
                                             TSInt offset, TSInt num_hours, int arg_ref_auto_delete, int setvar)
{

  Cli_Debug("ConfigLoggingRollingOffsetIntervalAutodelete %d %d\n", arg_ref_rolling, offset);
  Cli_Debug("%d\n", num_hours);
  Cli_Debug("%d\n", arg_ref_auto_delete);
  Cli_Debug("set?%d\n", setvar);

  TSInt int_val = 0;
  TSActionNeedT action_need = TS_ACTION_UNDEFINED;
  TSError status = TS_ERR_OKAY;

  switch (setvar) {
  case 0:                      //get

    status = Cli_RecordGetInt("proxy.config.log.rolling_enabled", &int_val);
    if (status) {
      return status;
    }
    if (Cli_PrintEnable("", int_val) == CLI_ERROR) {
      return CLI_ERROR;
    }
    status = Cli_RecordGetInt("proxy.config.log.rolling_offset_hr", &int_val);
    if (status) {
      return status;
    }
    Cli_Printf("%d\n", int_val);
    status = Cli_RecordGetInt("proxy.config.log.rolling_interval_sec", &int_val);
    if (status) {
      return status;
    }
    Cli_Printf("%d\n", int_val);
    status = Cli_RecordGetInt("proxy.config.log.auto_delete_rolled_files", &int_val);
    if (status) {
      return status;
    }
    if (Cli_PrintEnable("", int_val) == CLI_ERROR) {
      return CLI_ERROR;
    }
    return CLI_OK;

  case 1:
    {
      switch (arg_ref_rolling) {
      case CMD_CONFIG_LOGGING_ON:
        int_val = 1;
        break;
      case CMD_CONFIG_LOGGING_OFF:
        int_val = 0;
        break;
      default:
        Cli_Printf("ERROR in arg\n");
        return CLI_ERROR;
      }

      status = Cli_RecordSetInt("proxy.config.log.rolling_enabled", int_val, &action_need);
      if (status) {
        return status;
      }
      status = Cli_RecordSetInt("proxy.config.log.rolling_offset_hr", offset, &action_need);
      if (status) {
        return status;
      }
      status = Cli_RecordSetInt("proxy.config.log.rolling_interval_sec", num_hours, &action_need);
      if (status) {
        return status;
      }

      switch (arg_ref_auto_delete) {
      case CMD_CONFIG_LOGGING_ON:
        int_val = 1;
        break;
      case CMD_CONFIG_LOGGING_OFF:
        int_val = 0;
        break;
      default:
        Cli_Printf("ERROR in arg\n");
        return CLI_ERROR;
      }

      status = Cli_RecordSetInt("proxy.config.log.auto_delete_rolled_files", int_val, &action_need);
      if (status) {
        return status;
      }
      return (Cli_ConfigEnactChanges(action_need));

    }

  default:
    return CLI_ERROR;
  }
}

// config:alarm resolve-name
int
ConfigAlarmResolveName(char *name)
{
  bool active;
  TSError status;

  // determine if the event is active
  status = TSEventIsActive(name, &active);
  if (status != TS_ERR_OKAY) {
    // unable to retrieve active/inactive status for alarm
    Cli_Error(ERR_ALARM_STATUS, name);
    return CLI_ERROR;
  }

  if (!active) {
    // user tried to resolve a non-existent alarm
    Cli_Error(ERR_ALARM_RESOLVE_INACTIVE, name);
    return CLI_ERROR;
  }
  // alarm is active, resolve it
  status = TSEventResolve(name);
  if (status != TS_ERR_OKAY) {
    Cli_Error(ERR_ALARM_RESOLVE, name);
    return CLI_ERROR;
  }
  // successfully resolved alarm
  return CLI_OK;
}

// config:alarm resolve-number
int
ConfigAlarmResolveNumber(int number)
{
  TSList events;
  TSError status;
  int count, i;
  char *name = 0;

  events = TSListCreate();
  status = TSActiveEventGetMlt(events);
  if (status != TS_ERR_OKAY) {
    Cli_Error(ERR_ALARM_LIST);
    TSListDestroy(events);
    return CLI_ERROR;
  }

  count = TSListLen(events);
  if (number > count) {
    // number is too high
    Cli_Error(ERR_ALARM_RESOLVE_NUMBER, number);
    TSListDestroy(events);
    return CLI_ERROR;
  }

  for (i = 0; i < number; i++) {
    name = (char *) TSListDequeue(events);
  }

  // try to resolve the alarm
  TSListDestroy(events);
  return (ConfigAlarmResolveName(name));
}

// config:alarm resolve-all
int
ConfigAlarmResolveAll()
{
  TSList events;
  TSError status;
  int count, i;
  char *name;

  events = TSListCreate();
  status = TSActiveEventGetMlt(events);
  if (status != TS_ERR_OKAY) {
    Cli_Error(ERR_ALARM_LIST);
    TSListDestroy(events);
    return CLI_ERROR;
  }

  count = TSListLen(events);
  if (count == 0) {
    // no alarms to resolve
    Cli_Printf("No Alarms to resolve\n");
    TSListDestroy(events);
    return CLI_ERROR;
  }

  for (i = 0; i < count; i++) {
    name = (char *) TSListDequeue(events);
    status = TSEventResolve(name);
    if (status != TS_ERR_OKAY) {
      Cli_Error(ERR_ALARM_RESOLVE, name);
    }
  }

  TSListDestroy(events);
  return CLI_OK;
}

// config:alarm notify
int
ConfigAlarmNotify(char *string_val)
{
  if (string_val != NULL) {
    if (strcmp(string_val, "on") == 0) {
      AlarmCallbackPrint = 1;
      return CLI_OK;
    } else if (strcmp(string_val, "off") == 0) {
      AlarmCallbackPrint = 0;
      return CLI_OK;
    }
  } else {
    switch (AlarmCallbackPrint) {
    case 0:
      Cli_Printf("off\n");
      break;
    case 1:
      Cli_Printf("on\n");
      break;
    default:
      Cli_Printf("undefined\n");
      break;
    }
    return CLI_OK;
  }
  return CLI_ERROR;
}

int
find_value(const char *pathname, const char *key, char *value, int value_len, const char *delim, int no)
{
  int find = 0;

#if defined(linux) || defined(darwin) || defined(freebsd) || defined(solaris) \
 || defined(openbsd)
  char buffer[1024];
  char *pos;
  char *open_quot, *close_quot;
  FILE *fp;
  int counter = 0;

  value[0] = 0;
  // coverity[fs_check_call]
  if (access(pathname, R_OK)) {
    return find;
  }
  // coverity[toctou]
  if ((fp = fopen(pathname, "r")) != NULL) {
    ATS_UNUSED_RETURN(fgets(buffer, 1024, fp));
    while (!feof(fp)) {
      if (strstr(buffer, key) != NULL) {
        if (counter != no) {
          counter++;
        } else {
          find = 1;
          if ((pos = strstr(buffer, delim)) != NULL) {
            pos++;
            if ((open_quot = strchr(pos, '"')) != NULL) {
              pos = open_quot + 1;
              close_quot = strrchr(pos, '"');
              *close_quot = '\0';
            }
            ink_strlcpy(value, pos, value_len);

            if (value[strlen(value) - 1] == '\n') {
              value[strlen(value) - 1] = '\0';
            }
          }

          break;
        }
      }
      ATS_UNUSED_RETURN(fgets(buffer, 80, fp));
    }
    fclose(fp);
  }
#endif
  return find;
}
