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
 * Legal values for the type field of a createArgv:
 *
 */
#include <tcl.h>

#ifndef DEFINITIONS
#define DEFINITIONS

#define CLI_OK    TCL_OK

#define CLI_ERROR TCL_ERROR
#define CMD_OK cmd_ok()
#define CMD_ERROR cmd_error()

#define CLI_ARGV_CONSTANT                   0x1
#define CLI_ARGV_INT                        0x2
#define CLI_ARGV_STRING                     0x4
#define CLI_ARGV_FLOAT                      0x8
#define CLI_ARGV_FUNC                       0x10
#define CLI_ARGV_HELP                       0x20
#define CLI_ARGV_CONST_OPTION               0x40
#define CLI_ARGV_OPTION_FLOAT_VALUE         0x80
#define CLI_ARGV_OPTION_INT_VALUE           0x90
#define CLI_ARGV_OPTION_NAME_VALUE          0x100
#define CLI_ARGV_END                        0x200
#define CLI_PARSED_ARGV_END                 0x1000
#define CLI_PARSED_ARGV_DATA                0x1001
#define CLI_PARENT_ARGV                     1
#define CLI_ARGV_NO_POS                     -1

#define CLI_ARGV_OPTIONAL                   0x400
#define CLI_ARGV_REQUIRED                   0x800

#define CLI_DEFAULT_INT_OR_FLOAT_VALUE               -32768

typedef enum
{
  CLI_COMMAND_INTERNAL = 0x300,
  CLI_COMMAND_EXTERNAL = 0x700
} cmdTerritory;

typedef int (commandFunctionptr) (ClientData clientData, Tcl_Interp * interp, int argc, char *argv[]);
typedef int (createArgumentFuncPtr) ();

int cmd_ok(void);
int cmd_error(void);

extern int AlarmCallbackPrint;

int processArgForCommand(Tcl_Interp * interp, int argc, const char *argv[]);
int processHelpCommand(int argc, const char *argv[]);

#endif /*DEFINITIONS */
