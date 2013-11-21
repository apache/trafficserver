/** @file

  Implementation of CliDisplay routines for the use of TB

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


#include "libts.h"
#include "CliDisplay.h"
#include "definitions.h"
#include <string.h>


#define BUF_SIZE 1024

extern Tcl_Interp *interp;
static CliPrintLevelT CliPrintLevel = CLI_PRINT_DEFAULT;

int CliDisplayPrintf = 0;

/*****************************************************************
 * Display one of the predefined error messages.
 */
int
Cli_Error(const char *errString, ...)
{
  char buffer[BUF_SIZE];

  va_list ap;
  va_start(ap, errString);
  // XXX/amitk ugh. easy buffer overrun.
  // why isn't vsnprintf always available??
  vsprintf(buffer, errString, ap);
  va_end(ap);

  // fix for BZ27769
  //   libtrafficshell.a standalone uses Tcl_AppendResult
  //   traffic_shell binary uses printf
  if (CliDisplayPrintf) {
    printf("%s", buffer);
  } else {
    Tcl_AppendResult(interp, buffer, (char *) NULL);
  }

  return CLI_OK;
}

/****************************************************************
 * Set print level
 *   level = CLI_PRINT_DEFAULT (Cli_Printf)
 *   level = CLI_PRINT_INFO    (Cli_Printf, Cli_Info)
 *   level = CLI_PRINT_DEBUG   (Cli_Printf, Cli_Debug)
 *   level = CLI_PRINT_INFO | CLI_PRINT_DEBUG  (Cli_Printf, Cli_Debug, Cli_Info)
 */
int
Cli_SetPrintLevel(CliPrintLevelT level)
{
  CliPrintLevel = level;
  return CLI_OK;

}

/****************************************************************
 * return the current print level
 */
CliPrintLevelT
Cli_GetPrintLevel()
{
  return CliPrintLevel;
}

/****************************************************************
 * Display string
 */
int
Cli_Printf(const char *string, ...)
{
  char buffer[BUF_SIZE];
  va_list ap;

  va_start(ap, string);
  vsprintf(buffer, string, ap);
  va_end(ap);

  // fix for BZ27769
  //   libtrafficshell.a standalone uses Tcl_AppendResult
  //   traffic_shell binary uses printf
  if (CliDisplayPrintf) {
    printf("%s", buffer);
  } else {
    Tcl_AppendResult(interp, buffer, (char *) NULL);
  }

  return CLI_OK;
}

/****************************************************************
 * Display informative message
 */
int
Cli_Info(const char *string, ...)
{
  char buffer[BUF_SIZE];
  va_list ap;

  va_start(ap, string);
  if (CliPrintLevel & CLI_PRINT_INFO) {
    vsprintf(buffer, string, ap);

    // fix for BZ27769
    //   libtrafficshell.a standalone uses Tcl_AppendResult
    //   traffic_shell binary uses printf
    if (CliDisplayPrintf) {
      printf("%s", buffer);
    } else {
      Tcl_AppendResult(interp, buffer, (char *) NULL);
    }
  }
  va_end(ap);

  return CLI_OK;
}


/****************************************************************
 * Display debug statement
 */
int
Cli_Debug(const char *string, ...)
{
  char buffer[BUF_SIZE];
  va_list ap;

  // allocate enough room for "debug: " at beginning of string
  const size_t new_string_size = strlen(string) + 8;
  char *new_string = (char *)alloca(new_string_size);
  ink_strlcpy(new_string, "debug: ", new_string_size);
  ink_strlcat(new_string, string, new_string_size );

  va_start(ap, string);
  if (CliPrintLevel & CLI_PRINT_DEBUG) {
    vsprintf(buffer, new_string, ap);

    // fix for BZ27769
    //   libtrafficshell.a standalone uses Tcl_AppendResult
    //   traffic_shell binary uses printf
    if (CliDisplayPrintf) {
      printf("%s", buffer);
    } else {
      Tcl_AppendResult(interp, buffer, (char *) NULL);
    }
  }
  va_end(ap);

  return CLI_OK;
}

int
Cli_PrintEnable(const char *string, int flag)
{
  if (flag == 0)
    Cli_Printf("%soff\n", string);
  else if (flag == 1)
    Cli_Printf("%son\n", string);
  else {
    Cli_Debug(ERR_INVALID_PARAMETER);
    return CLI_ERROR;
  }

  return CLI_OK;
}

int
Cli_PrintOnOff(int flag)
{
  switch (flag) {
  case 0:
    Cli_Printf("off\n");
    break;
  case 1:
    Cli_Printf("on\n");
    break;
  default:
    Cli_Printf("?\n");
    break;
  }
  return CLI_OK;
}

int
Cli_PrintArg(int arg_index, const cli_parsedArgInfo * argtable)
{



  if (arg_index >= 0) {
    Cli_Debug("%d %d %s\n", argtable[arg_index].parsed_args,
              argtable[arg_index].arg_int, argtable[arg_index].arg_string);
    return CLI_OK;
  }
  return CLI_ERROR;
}

#undef BUF_SIZE
