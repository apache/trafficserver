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
 * Filename: UtilCmds.cc
 * Purpose: This file contains the CLI's utility commands.
 *
 *
 ****************************************************************/

#include "UtilCmds.h"
#include "createArgument.h"
#include "CliMgmtUtils.h"
#include "CliDisplay.h"
#include "ink_error.h"
#include "ink_defs.h"
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>

////////////////////////////////////////////////////////////////
// DebugCmd
//
// This is the callback function for the "debug" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
DebugCmd(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
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

  Cli_Debug("DebugCmd\n");

  for (infoPtr = argtable; (infoPtr->parsed_args != 0); infoPtr++) {

    // call appropriate function based on the 1st argument

    switch (infoPtr->parsed_args) {
    case CMD_DEBUG_ON:
      Cli_Debug("debug on sub-command\n");
      return (DebugOn());

    case CMD_DEBUG_OFF:
      Cli_Debug("debug off sub-command\n");
      return (DebugOff());
    }
  }
  return CLI_OK;
}


////////////////////////////////////////////////////////////////
// DebugCmdArgs
//
// Register "debug" command arguments with the Tcl interpreter.
//
int
DebugCmdArgs()
{
  Cli_Debug("DebugCmdArgs\n");

  createArgument("on", 1, CLI_ARGV_CONSTANT, (char *) NULL, CMD_DEBUG_ON, "Turn Debug Statements ON", (char *) NULL);

  createArgument("off", 1, CLI_ARGV_CONSTANT, (char *) NULL, CMD_DEBUG_OFF, "Turn Debug Statements OFF", (char *) NULL);

  return CLI_OK;
}

////////////////////////////////////////////////////////////////
//
// "debug" sub-command implementations
//
////////////////////////////////////////////////////////////////


// debug on sub-command
//   used to turn on debug print statements
int
DebugOn()
{
  CliPrintLevelT level = Cli_GetPrintLevel();
  level = (CliPrintLevelT) ((int) level | (int) CLI_PRINT_DEBUG);
  Cli_SetPrintLevel(level);
  return CLI_OK;
}

// debug off sub-command
//   used to turn off debug print statements
int
DebugOff()
{
  CliPrintLevelT level = Cli_GetPrintLevel();
  level = (CliPrintLevelT) ((int) level & ~(int) CLI_PRINT_DEBUG);
  Cli_SetPrintLevel(level);
  return CLI_OK;
}


////////////////////////////////////////////////////////////////
// Cmd_ConfigRoot
//
// This is the callback function for the "config:root" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigRoot(ClientData /* clientData ATS_UNUSED */, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:root") == CLI_ERROR) {
    return CMD_ERROR;
  }

  Cli_Debug("Cmd_ConfigRoot\n");

  if (getuid() == 0) {
    Cli_Printf("Already root user.\n");
    return CLI_OK;

  }
  char ts_path[1024];
  if (GetTSDirectory(ts_path,sizeof(ts_path))) {
    return CLI_ERROR;
  }

  char command[1024];
  snprintf(command, sizeof(command),
           "/bin/su - root -c \"%s/start_traffic_shell\"", ts_path);

  // start traffic_shell as root user
  // su will prompt user for root password
  int pid = fork();
  if (pid == 0) {
    if (system(command) == -1)
      exit(2);
    else
      exit(1);
  }

  waitpid(pid, NULL, 0);

  return CLI_OK;
}
