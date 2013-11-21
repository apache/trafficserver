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
 * Filename: CliDisplay.h
 * Purpose: This file contains the CLI display routines.
 *
 *
 ****************************************************************/

#include <stdarg.h>
#include <stdio.h>
#include "createArgument.h"

#ifndef __CLI_ERROR_HANDLE_H__
#define __CLI_ERROR_HANDLE_H__

/* CLI print levels
 */
typedef enum
{
  CLI_PRINT_DEFAULT = 0x0,
  CLI_PRINT_INFO = 0x2,
  CLI_PRINT_DEBUG = 0x4
} CliPrintLevelT;

/* All possible error strings are defined below.
 *    In the future, this could be changed to a message_id/lookup mechanism.
 */

#define ERR_RECORD_GET         "INKRecordGet: failed to retrieve %s\n"
#define ERR_RECORD_GET_INT     "INKRecordGetInt: failed to retrieve %s\n"
#define ERR_RECORD_GET_COUNTER "INKRecordGetCounter: failed to retrieve %s\n"
#define ERR_RECORD_GET_FLOAT   "INKRecordGetFloat: failed to retrieve %s\n"
#define ERR_RECORD_GET_STRING  "INKRecordGetString: failed to retrieve %s\n"
#define ERR_RECORD_SET         "INKRecordSet: failed to set %s value %s\n"
#define ERR_RECORD_SET_INT     "INKRecordSetInt: failed to set %s value %d\n"
#define ERR_RECORD_SET_FLOAT   "INKRecordSetFloat: failed to set %s value %f\n"
#define ERR_RECORD_SET_STRING  "INKRecordSetString: failed to set %s value %s\n"


#define ERR_COMMAND_SYNTAX    "\nCommand Syntax: \n%s\n\n"
#define ERR_REQ_ACTION_UNDEF  "Undefined Action Required before Changes Take Effect\n"
#define ERR_TOO_MANY_ARGS     "\nToo many arguments specified.\n"

#define ERR_INVALID_COMMAND   "wrong # args: should be \n"
#define ERR_INVALID_PARAMETER "Invalid Parameter\n"
#define ERR_MISSING_PARAMETER "Missing Parameter\n"

#define ERR_PROXY_STATE_ALREADY "Proxy is already %s\n" // on/off
#define ERR_PROXY_STATE_SET     "Unable to set Proxy %s\n"      // on/off

#define ERR_CONFIG_FILE_READ  "Error Reading Rules File %d\n"
#define ERR_CONFIG_FILE_WRITE "Error Writing Rules File %d\n"
#define ERR_READ_FROM_URL     "Error Reading File from URL %s\n"

#define ERR_ALARM_LIST        "Error Retrieving Alarm List\n"
#define ERR_ALARM_STATUS      "Error Determining Active/Inactive status of alarm %s\n"
#define ERR_ALARM_RESOLVE_INACTIVE "Error: Attempt to resolve inactive alarm %s\n"
#define ERR_ALARM_RESOLVE     "Errur: Unable to resolve alarm %s\n"
#define ERR_ALARM_RESOLVE_NUMBER "Error: Alarm number non-existent\n"

int Cli_Error(const char *errString, ...);
int Cli_SetPrintLevel(CliPrintLevelT level);
CliPrintLevelT Cli_GetPrintLevel();
int Cli_Printf(const char *string, ...);
int Cli_Debug(const char *string, ...);
int Cli_PrintEnable(const char *string, int flag);
int Cli_PrintOnOff(int flag);
int Cli_PrintArg(int arg_index, const cli_parsedArgInfo * argtable);
#endif // __CLI_ERROR_HANDLE_H__
