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

/*
 *   createCommand.c --
 *
 *
 *    This file provides basic create command definition,
 *    for any new commands.
 *
 *    It overrides any existing commands with the new command
 *    without any warning
 *
 *
 *    Dated  : 12/11/2000.
 */

#include <tcl.h>
#include <stdlib.h>
#include "definitions.h"
#include "createArgument.h"
#include "hashtable.h"

static char **reqd_args;
char **getReqdArgs();

extern Tcl_Interp *interp;
/* This is the only interp for the whole application */

extern cli_ArgvInfo *cliGetArgvInfo();
extern int cliSetArgvInfo(cli_ArgvInfo * argtable);



int
createCommand(const char *cmdName, Tcl_CmdProc cmdFuncPtr,
              createArgumentFuncPtr argvFuncPtr, cmdTerritory cmdScope, const char *usage, const char *helpString)
{
  /* No code to support threads for Tcl */

  /* If required add thread Specific code
     here
     Hint: Use Tcl_GetThreadData

   */
  int isSafe, i;
  cli_ArgvInfo *argtable;
  char **required_args;
  cli_cmdCallbackInfo *cmdCallbackInfo;

  if ((cmdName == NULL) || (cmdFuncPtr == NULL) || (helpString == NULL)) {
    printf("Error: New command with NULL string or object procs or with no help string provided\n");
    return TCL_ERROR;
  }


  /* Used for creating internal commands */
  isSafe = Tcl_IsSafe(interp);

  argtable = (cli_ArgvInfo *) ckalloc(sizeof(cli_ArgvInfo) * 100);
  for (i = 0; i < 100; i++) {
    argtable[i].key = (char *) NULL;
    argtable[i].type = CLI_ARGV_END;
    argtable[i].range_set = false;
    argtable[i].option = CLI_ARGV_OPTIONAL;
    argtable[i].def = (char *) NULL;
    argtable[i].help = (char *) NULL;

  }

  cmdCallbackInfo = (cli_cmdCallbackInfo *) ckalloc(sizeof(cli_cmdCallbackInfo));

  cmdCallbackInfo->command_usage = usage;

  cmdCallbackInfo->parsedArgTable = (cli_parsedArgInfo *) ckalloc(sizeof(cli_parsedArgInfo) * 100);
  for (i = 0; i < 100; i++) {
    cmdCallbackInfo->parsedArgTable[i].parsed_args = CLI_PARSED_ARGV_END;
    cmdCallbackInfo->parsedArgTable[i].data = (char *) NULL;
    cmdCallbackInfo->parsedArgTable[i].arg_string = (char *) NULL;
    cmdCallbackInfo->parsedArgTable[i].arg_int = CLI_DEFAULT_INT_OR_FLOAT_VALUE;
    cmdCallbackInfo->parsedArgTable[i].arg_float = CLI_DEFAULT_INT_OR_FLOAT_VALUE;
    cmdCallbackInfo->parsedArgTable[i].arg_usage = (char *) NULL;

  }

  cmdCallbackInfo->userdata = (void *) NULL;

  /* Create a new command with cmdName */
  Tcl_CreateCommand(interp, cmdName, cmdFuncPtr, (ClientData) cmdCallbackInfo,
                    (void (*)_ANSI_ARGS_((ClientData))) NULL);

  cliSetArgvInfo(argtable);
  if (isSafe) {
    if (cmdScope == CLI_COMMAND_INTERNAL) {
      Tcl_HideCommand(interp, cmdName, cmdName);
    }
  }


  if (argvFuncPtr != NULL) {
    (*argvFuncPtr) ();
    /* Now call create Arguments so that we can fill the necessary
       data structure and call parseArgv to parse the arguments.
     */
  }

  required_args = getReqdArgs();
  cliAddCommandtoHashtable(cmdName, argtable, required_args, cmdCallbackInfo->parsedArgTable, helpString);


  return TCL_OK;

}

void
setReqdArgs(char **required_args)
{
  reqd_args = required_args;
}

char **
getReqdArgs()
{
  return reqd_args;
}

int
cmd_ok()
{
  /* if it called many times, the interperter will store only
     the latest.
   */
  Tcl_AppendElement(interp, "+OK");
  return TCL_OK;
}


int
cmd_error()
{
  /* If it called many times, the interpreter will store only
     the latest
   */
  Tcl_AppendElement(interp, "-ERROR");
  return TCL_ERROR;
}
