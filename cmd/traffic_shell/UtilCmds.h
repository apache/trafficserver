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
 * Filename: UtilCmds.h
 * Purpose: This file contains the CLI's utility commands.
 *
 *
 ****************************************************************/


/* This includes a commited hacked version of tcl which has an external dependancy
   on cli-specific code and thus, this tcl build cannot be used with other
   code, and normal non-hacked tcl cannot be used with cli.  Even more beutiful
   is the fact that the hacks are no-where documented.  I managed to scrape
   them up and role them into a patch which can be found in
   $CVSROOT/contrib/tcl-tclx/common/patches for reference in case someone
   ever should feel like comming up with something a little more well thought out.
   Oh, did I mention also that apperently, no thought was put toward how to make
   this work with more than one platform, or is that obvious just by looking
   at the structure?
*/
//#include "inktcl/include/tcl.h"
/* There is is in all it's glory :( */


#include "createArgument.h"
#include "definitions.h"

#ifndef __UTIL_CMDS_H__
#define __UTIL_CMDS_H__

// enumerated type which captures all "debug" command options
typedef enum
{
  CMD_DEBUG_ON = 1,
  CMD_DEBUG_OFF
} cliDebugCommand;

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
int DebugCmd(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);

////////////////////////////////////////////////////////////////
// DebugCmdArgs
//
// Register "debug" command arguments with the Tcl interpreter.
//
int DebugCmdArgs();

////////////////////////////////////////////////////////////////
//
// "debug" sub-command implementations
//
////////////////////////////////////////////////////////////////

// debug on sub-command
int DebugOn();

// debug off sub-command
int DebugOff();

int Cmd_ConfigRoot(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);

#endif // __UTIL_CMDS_H__
