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
 *   createCommand.h --
 *
 *
 *    This file provides basic create command declartion,
 *    for any new commands.
 *
 *    It overrides any existing commands with the new command
 *
 *
 *
 *    Dated  : 12/11/2000.
 */

#ifndef CREATE_COMMAND
#define CREATE_COMMAND

#include "definitions.h"



/* First Argument  cmdName: New command name for CLI
   Second Argument cmdFuncPtr : A 'C' function to be invoked
                                for the new Command
   Third Argument argvFuncPtr : Function to attach arguments
                                Pass NULL for no arguments.
   Fourth Argument cmdScope   : Scope of the command.
                                Pass NULL, if the new command
                                needs to be visible for
                                the customer.
*/

extern int createCommand(const char *cmdName, Tcl_CmdProc * cmdFuncPtr,
                         createArgumentFuncPtr argvFuncPtr, cmdTerritory cmdScope, const char *usage, const char *helpstring);

#endif /* CREATE_COMMAND */
