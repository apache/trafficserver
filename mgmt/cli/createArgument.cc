/** @file

  This file provides basic create Argument defintion, for any new arguments.

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
#include <tcl.h>
#include <string.h>

#include "createArgument.h"
#include "definitions.h"
#include "commandOptions.h"
#include "hashtable.h"
#include "ink_string.h"

#define totalArguments 30       /* This is arbitary value */
#define NEG_BOUND -10
#define POS_BOUND 10

static cli_ArgvInfo *ArgvTable;
static cli_ArgvInfo *org_ArgvTable;
extern Tcl_Interp *interp;

cli_ArgvInfo *cliGetArgvInfo();
cli_ArgvInfo *cliGetOrgArgvInfo();
int getCommandOption(int commandoption, int *arg_type, int *arg_option);
char **findRequired(cli_ArgvInfo * argtable);
extern void setReqdArgs(char **required_args);

int
createArgument(const char *argument, int position, int commandoption,
               char *range, int argumentRef, const char *helpString, char *defValue)
{
  int arg_type = 0;
  int arg_option = 0;
  char **reqd_args;
  cli_ArgvInfo *aCliArgvTable = cliGetArgvInfo();

  size_t key_len = sizeof(char) * (strlen(argument) + 1);
  aCliArgvTable->key = (char *) ckalloc(key_len);
  ink_strlcpy(aCliArgvTable->key, argument, key_len);

  aCliArgvTable->position = position;

  getCommandOption(commandoption, &arg_type, &arg_option);
  aCliArgvTable->type = arg_type;
  if (arg_option != 0) {
    aCliArgvTable->option = arg_option;
  }


  aCliArgvTable->arg_ref = argumentRef;

  if (range != NULL) {
    aCliArgvTable->range_set = true;
    if (aCliArgvTable->type == CLI_ARGV_INT || aCliArgvTable->type == CLI_ARGV_OPTION_INT_VALUE) {
      getIntRange(range, &(aCliArgvTable->l_range.int_r1), &(aCliArgvTable->u_range.int_r2));
    }
    if (aCliArgvTable->type == CLI_ARGV_FLOAT || aCliArgvTable->type == CLI_ARGV_OPTION_FLOAT_VALUE) {
      getFloatRange(range, &(aCliArgvTable->l_range.float_r1), &(aCliArgvTable->u_range.float_r2));
    }

  }

  if (defValue != NULL) {
    size_t def_len = sizeof(char) * (strlen(defValue) + 1);
    aCliArgvTable->def = (char *) ckalloc(def_len);
    ink_strlcpy(aCliArgvTable->def, defValue, def_len);
  }


  if (helpString != NULL) {
    size_t help_len = sizeof(char) * (strlen(helpString) + 1);
    aCliArgvTable->help = (char *) ckalloc(help_len);
    ink_strlcpy(aCliArgvTable->help, helpString, help_len);
  }

  reqd_args = findRequired(cliGetOrgArgvInfo());
  setReqdArgs(reqd_args);
  return TCL_OK;
}


cli_ArgvInfo *
cliGetArgvInfo()
{
  cli_ArgvInfo *temp_ArgvTable = ArgvTable;
  ArgvTable++;
  return temp_ArgvTable;
}

int
cliSetArgvInfo(cli_ArgvInfo * argtable)
{
  ArgvTable = argtable;
  org_ArgvTable = argtable;
  return TCL_OK;
}

cli_ArgvInfo *
cliGetOrgArgvInfo()
{
  return org_ArgvTable;
}


char **
findRequired(cli_ArgvInfo * argtable)
{
  char **args;
  cli_ArgvInfo *infoPtr;
  int count_required = 0, i = 0;
  for (infoPtr = argtable; (infoPtr->key != NULL); infoPtr++) {
    if (infoPtr->option == CLI_ARGV_REQUIRED) {
      count_required++;
    }
  }
  if (count_required == 0)
    return (char **) NULL;

  args = (char **) ckalloc(sizeof(char *) * (count_required + 1));
  for (infoPtr = argtable; (infoPtr->key != NULL); infoPtr++) {
    if (infoPtr->option == CLI_ARGV_REQUIRED) {

      args[i] = ats_strdup(infoPtr->key);
      i++;
    }
  }
  args[i] = NULL;
  return args;
}


int
getCommandOption(int commandoption, int *arg_type, int *arg_option)
{
  switch (commandoption) {
  case CLI_ARGV_CONSTANT:
    *arg_type = CLI_ARGV_CONSTANT;
    *arg_option = 0;
    break;
  case CLI_ARGV_CONST_OPTION:
    *arg_type = CLI_ARGV_CONST_OPTION;
    *arg_option = 0;
    break;
  case CLI_ARGV_INT:
    *arg_type = CLI_ARGV_INT;
    *arg_option = 0;
    break;
  case CLI_ARGV_STRING:
    *arg_type = CLI_ARGV_STRING;
    *arg_option = 0;
    break;
  case CLI_ARGV_FLOAT:
    *arg_type = CLI_ARGV_FLOAT;
    *arg_option = 0;
    break;
  case CLI_ARGV_OPTION_NAME_VALUE:
    *arg_type = CLI_ARGV_OPTION_NAME_VALUE;
    *arg_option = 0;
    break;
  case CLI_ARGV_OPTION_FLOAT_VALUE:
    *arg_type = CLI_ARGV_OPTION_FLOAT_VALUE;
    *arg_option = 0;
    break;
  case CLI_ARGV_OPTION_INT_VALUE:
    *arg_type = CLI_ARGV_OPTION_INT_VALUE;
    *arg_option = 0;
    break;
  case CLI_ARGV_CONSTANT_OPTIONAL:
    *arg_type = CLI_ARGV_CONSTANT;
    *arg_option = CLI_ARGV_OPTIONAL;
    break;
  case CLI_ARGV_INT_OPTIONAL:
    *arg_type = CLI_ARGV_INT;
    *arg_option = CLI_ARGV_OPTIONAL;
    break;
  case CLI_ARGV_STRING_OPTIONAL:
    *arg_type = CLI_ARGV_STRING;
    *arg_option = CLI_ARGV_OPTIONAL;
    break;
  case CLI_ARGV_FLOAT_OPTIONAL:
    *arg_type = CLI_ARGV_FLOAT;
    *arg_option = CLI_ARGV_OPTIONAL;
    break;
  case CLI_ARGV_FUNC_OPTIONAL:
    *arg_type = CLI_ARGV_FUNC;
    *arg_option = CLI_ARGV_OPTIONAL;
    break;
  case CLI_ARGV_HELP_OPTIONAL:
    *arg_type = CLI_ARGV_HELP;
    *arg_option = CLI_ARGV_OPTIONAL;
    break;
  case CLI_ARGV_CONST_OPTION_OPTIONAL:
    *arg_type = CLI_ARGV_CONST_OPTION;
    *arg_option = CLI_ARGV_OPTIONAL;
    break;
  case CLI_ARGV_CONSTANT_REQUIRED:
    *arg_type = CLI_ARGV_CONSTANT;
    *arg_option = CLI_ARGV_REQUIRED;
    break;
  case CLI_ARGV_INT_REQUIRED:
    *arg_type = CLI_ARGV_INT;
    *arg_option = CLI_ARGV_REQUIRED;
    break;
  case CLI_ARGV_STRING_REQUIRED:
    *arg_type = CLI_ARGV_STRING;
    *arg_option = CLI_ARGV_REQUIRED;
    break;
  case CLI_ARGV_FLOAT_REQUIRED:
    *arg_type = CLI_ARGV_FLOAT;
    *arg_option = CLI_ARGV_REQUIRED;
    break;
  default:
    *arg_type = 0;
    *arg_option = 0;
    break;
  }
  return TCL_OK;
}

int
getIntRange(char *range, int *r1, int *r2)
{
  char *range_str;
  char *str;
  int bound, i = 0, len;
  char *buf;
  char *endPtr;

  buf = (char *) ckalloc(sizeof(char) * 256);
  range_str = ats_strdup(range);
  len = strlen(range_str);
  range_str[len] = 0;
  str = range_str;
  while (*str != 0) {
    if (*str == '-') {
      bound = NEG_BOUND;
      str++;
    }

    else if (*str == '+') {
      bound = POS_BOUND;
      str++;
    } else {
      Tcl_AppendResult(interp, "range not specified correctly", (char *) NULL);
      ckfree(buf);
      ckfree(range_str);
      return TCL_ERROR;
    }
    if (*str == 'r') {
      str++;
      i = 0;
      for (; (*str != '+'); str++) {
        if (*str == 0)
          break;
        buf[i] = *str;
        i++;
      }
      buf[i] = 0;
      if (bound == NEG_BOUND) {
        *r1 = strtol(buf, &endPtr, 0);
        if ((endPtr == buf) || (*endPtr != 0)) {
          Tcl_AppendResult(interp, "negative range is not correct\n", (char *) NULL);
          ckfree(buf);
          ckfree(range_str);
          return TCL_ERROR;

        }
      } else if (bound == POS_BOUND) {
        *r2 = strtol(buf, &endPtr, 0);
        if ((endPtr == buf) || (*endPtr != 0)) {
          Tcl_AppendResult(interp, "positive range is not correct\n", (char *) NULL);
          ckfree(buf);
          ckfree(range_str);
          return TCL_ERROR;

        }
      }

    } else {
      Tcl_AppendResult(interp, "range not specified correctly\n");
      ckfree(buf);
      ckfree(range_str);
      return TCL_ERROR;
    }


  }
  ckfree(buf);
  ckfree(range_str);
  return TCL_OK;
}


int
getFloatRange(char *range, float *r1, float *r2)
{
  char *range_str;
  char *str;
  int bound, i = 0, len;
  char *buf;
  char *endPtr;

  buf = (char *) ckalloc(sizeof(char) * 256);
  range_str = ats_strdup(range);
  len = strlen(range_str);
  range_str[len] = 0;
  str = range_str;
  while (*str != 0) {
    if (*str == '-') {
      bound = NEG_BOUND;
      str++;
    }

    else if (*str == '+') {
      bound = POS_BOUND;
      str++;
    } else {
      Tcl_AppendResult(interp, "range not specified correctly", (char *) NULL);
      ckfree(buf);
      ckfree(range_str);
      return TCL_ERROR;
    }
    if (*str == 'r') {
      str++;
      i = 0;
      for (; (*str != '+'); str++) {
        if (*str == 0)
          break;
        buf[i] = *str;
        i++;
      }
      buf[i] = 0;
      if (bound == NEG_BOUND) {
        // Solaris is messed up, in that strtod() does not honor C99/SUSv3 mode.
        *r1 = strtold(buf, &endPtr);
        if ((endPtr == buf) || (*endPtr != 0)) {
          Tcl_AppendResult(interp, "negative range is not correct", (char *) NULL);
          ckfree(buf);
          ckfree(range_str);
          return TCL_ERROR;

        }
      } else if (bound == POS_BOUND) {
        *r2 = strtold(buf, &endPtr);
        if ((endPtr == buf) || (*endPtr != 0)) {
          Tcl_AppendResult(interp, "positive range is not correct", (char *) NULL);
          ckfree(buf);
          ckfree(range_str);
          return TCL_ERROR;

        }
      }

    } else {
      Tcl_AppendResult(interp, "range not specified correctly");
      ckfree(buf);
      ckfree(range_str);
      return TCL_ERROR;
    }


  }
  ckfree(buf);
  ckfree(range_str);
  return TCL_OK;
}
