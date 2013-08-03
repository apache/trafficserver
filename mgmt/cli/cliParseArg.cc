/** @file

  contains the implementation for parsing CLI arguments

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

  @section Description
  cliParseArgument(int argc,char **argv,cli_ArgvInfo *argTable)
  compares the given arguments with the expected arguments and
  returns error if they are not same
  If arguments are are valid it converts string arguments to the proper type
 */

#include <tcl.h>
#include <string.h>
#include <stdlib.h>
#include "createArgument.h"
#include "definitions.h"
#include "CliDisplay.h"
#include "ink_string.h"
#include "ink_defs.h"

int checkintrange(char *range, int value);
int checkfloatrange(char *range, float value);
char **findRequired(cli_ArgvInfo * argtable);
extern Tcl_Interp *interp;

/*
 * Forward declarations for procedures defined in this file:
 */
static void PrintUsage _ANSI_ARGS_((Tcl_Interp * interp, cli_CommandInfo * commandinfo));



int
cliParseArgument(int argc, const char **argv, cli_CommandInfo * commandInfo)
{
  cli_ArgvInfo *infoPtr, *prevMatchPtr = NULL, *prevPtr = NULL;
  cli_parsedArgInfo *parsedInfoPtr, *prev_parsedInfoPtr = NULL;
  int srcIndex = 1;
  bool gotMatch = false, gotInt = false;
  int missing = 0;
  int length, i;
  char curArg[256], buf[256];
  char *endPtr;
  bool got_required;
  cli_ArgvInfo *argTable = commandInfo->argtable;
  char **reqd_args = commandInfo->reqd_args;
  cli_parsedArgInfo *parsedArgTable = commandInfo->parsedArgTable;



  argc--;                       /*first argument is command name */


/* Special option for help handled here, No need to create an -help */
  /* I assume that the first thing will always start with - */

  if (argc == 1) {
    length = strlen(argv[1]);
    if ((length >= 2) && ((strncmp(argv[1], "-help", length) == 0) || (strncmp(argv[1], "-h", length) == 0))) {
      PrintUsage(interp, commandInfo);
      return TCL_OK;
    } else if (strcmp(argv[1], "----") == 0) {
      /* Special case with readline options */
      /* Readline always sends "----" */
      return TCL_OK;
    }
  }


  /* check for required arguments */
  if (reqd_args != (char **) NULL) {
    missing = 0;
    for (i = 0; reqd_args[i] != NULL; i++) {
      got_required = false;
      srcIndex = 1;
      while (srcIndex <= argc) {
        if (!strcmp(reqd_args[i], argv[srcIndex])) {
          got_required = true;
          break;
        }
        srcIndex++;
      }
      if (got_required == false) {
        missing++;
        if (missing == 1)
          Tcl_AppendResult(interp, reqd_args[i], (char *) NULL);
        else
          Tcl_AppendResult(interp, " , ", reqd_args[i], (char *) NULL);
      }
    }

    if (missing == 1) {
      Tcl_AppendResult(interp, " is required ", (char *) NULL);
      return TCL_ERROR;
    } else if (missing > 1) {
      Tcl_AppendResult(interp, " are required ", (char *) NULL);
      return TCL_ERROR;
    }

  }
/* Initialise Parsed Argtable */
  for (i = 0; i < 100; i++) {
    parsedArgTable[i].parsed_args = CLI_PARSED_ARGV_DATA;
    parsedArgTable[i].arg_float = CLI_DEFAULT_INT_OR_FLOAT_VALUE;
    parsedArgTable[i].arg_int = CLI_DEFAULT_INT_OR_FLOAT_VALUE;

    if (parsedArgTable[i].data && (parsedArgTable[i].data != (char *) NULL)) {
      ats_free(parsedArgTable[i].data);
      parsedArgTable[i].data = (char *) NULL;

    }
    if (parsedArgTable[i].arg_string && parsedArgTable[i].arg_string != (char *) NULL) {
      ats_free(parsedArgTable[i].arg_string);
      parsedArgTable[i].arg_string = (char *) NULL;
    }
  }

/* Error checking for commandline arguments based on createargument options */
  parsedInfoPtr = parsedArgTable;
  srcIndex = 1;
  while (argc > 0) {
    ink_strlcpy(curArg, argv[srcIndex], sizeof(curArg));
    argc--;
    srcIndex++;
    length = strlen(curArg);
    gotMatch = false;
    for (infoPtr = argTable; (infoPtr->key != NULL); infoPtr++) {
      if (!strcmp(infoPtr->key, curArg)) {
        gotMatch = true;
        if (infoPtr->position != CLI_ARGV_NO_POS) {     /* Don't do position checking for
                                                           CLI_ARGV_NO_POS */
          /* check if the 1st position argument comes at 1st position */
          if ((infoPtr->position == CLI_PARENT_ARGV) && (srcIndex - 1) != 1) {
            Tcl_AppendResult(interp, "\"", curArg, "\" is at wrong place Try ", argv[0], " -help", (char *) NULL);
            return TCL_ERROR;
          }

          if (infoPtr->position != CLI_PARENT_ARGV) {
            if ((srcIndex - 1) == 1) {
              Tcl_AppendResult(interp, "\"", curArg, "\" is at wrong place Try ", argv[0], " -help", (char *) NULL);
              return TCL_ERROR;
            } else {
              if (prevMatchPtr != NULL) {
                if (prevMatchPtr->arg_ref != infoPtr->position) {
                  Tcl_AppendResult(interp, "\"", curArg, "\" is at wrong place Try ", argv[0], " -help", (char *) NULL);
                  return TCL_ERROR;
                }
              }

            }
          }
        }

        if (prevMatchPtr != NULL && prevPtr == prevMatchPtr) {
          if (prevMatchPtr->type == CLI_ARGV_OPTION_NAME_VALUE) {
            if (prev_parsedInfoPtr->arg_string) {
              ats_free(prev_parsedInfoPtr->arg_string);
              prev_parsedInfoPtr->arg_string = NULL;
            }
          }
        }

        parsedInfoPtr->arg_usage = infoPtr->help;

        /* Position checking over check for type */

        switch (infoPtr->type) {
        case CLI_ARGV_CONSTANT:
          parsedInfoPtr->parsed_args = infoPtr->arg_ref;
          if (argc != 0) {
            snprintf(buf, sizeof(buf), "Too many arguments Try %s -help", argv[0]);
            Tcl_AppendResult(interp, buf, (char *) NULL);
            return TCL_ERROR;
          }
          break;
        case CLI_ARGV_INT:
          if (argc <= 0) {
            Tcl_AppendResult(interp, "\"", curArg, "\" option requires an additional integer argument", "\n",
                             infoPtr->help, (char *) NULL);
            return TCL_ERROR;
          }
          parsedInfoPtr->arg_int = strtol(argv[srcIndex], &endPtr, 0);
          if ((endPtr == argv[srcIndex]) || (*endPtr != 0)) {
            Tcl_AppendResult(interp, infoPtr->key, " requires integer argument ", "\n", infoPtr->help, (char *) NULL);
            return TCL_ERROR;
          }
          if (infoPtr->range_set == true) {
            if (parsedInfoPtr->arg_int < infoPtr->l_range.int_r1 || parsedInfoPtr->arg_int > infoPtr->u_range.int_r2) {
              snprintf(buf, sizeof(buf), "value of %s is out of range %d - %d", infoPtr->key, infoPtr->l_range.int_r1,
                       infoPtr->u_range.int_r2);

              Tcl_AppendResult(interp, buf, (char *) NULL);
              return TCL_ERROR;
            }

          }
          srcIndex++;
          argc--;
          parsedInfoPtr->parsed_args = infoPtr->arg_ref;
          break;

        case CLI_ARGV_OPTION_INT_VALUE:
          gotInt = 0;
          if (argc > 0) {
            parsedInfoPtr->arg_int = strtol(argv[srcIndex], &endPtr, 0);
            if ((endPtr == argv[srcIndex]) || (*endPtr != 0)) {
              parsedInfoPtr->arg_int = CLI_DEFAULT_INT_OR_FLOAT_VALUE;
              parsedInfoPtr->parsed_args = infoPtr->arg_ref;
              break;
            }
            gotInt = 1;
            if (infoPtr->range_set == true) {
              if (parsedInfoPtr->arg_int < infoPtr->l_range.int_r1 || parsedInfoPtr->arg_int > infoPtr->u_range.int_r2) {
                snprintf(buf, sizeof(buf), "value of %s is out of range %d - %d", infoPtr->key, infoPtr->l_range.int_r1,
                         infoPtr->u_range.int_r2);

                Tcl_AppendResult(interp, buf, (char *) NULL);
                return TCL_ERROR;
              }
            }
            srcIndex++;
            argc--;
          }
          parsedInfoPtr->parsed_args = infoPtr->arg_ref;
          break;

        case CLI_ARGV_STRING:
          if (argc <= 0) {
            Tcl_AppendResult(interp, "\"", curArg, "\" option requires an additional argument", (char *) NULL);
            return TCL_ERROR;
          }
          parsedInfoPtr->arg_string = ats_strdup(argv[srcIndex]);
          parsedInfoPtr->parsed_args = infoPtr->arg_ref;
          srcIndex++;
          argc--;
          break;
        case CLI_ARGV_OPTION_NAME_VALUE:
          if (argc > 0) {
            parsedInfoPtr->arg_string = ats_strdup(argv[srcIndex]);
          }
          parsedInfoPtr->parsed_args = infoPtr->arg_ref;
          break;
        case CLI_ARGV_FLOAT:
          if (argc <= 0) {
            Tcl_AppendResult(interp, "\"", curArg, "\" option requires an additional floating-point argument",
                             (char *) NULL);
            return TCL_ERROR;
          }

          // Solaris is messed up, in that strtod() does not honor C99/SUSv3 mode.
          parsedInfoPtr->arg_float = strtold(argv[srcIndex], &endPtr);
          if ((endPtr == argv[srcIndex]) || (*endPtr != 0)) {
            Tcl_AppendResult(interp, infoPtr->key, " requires floating-point argument",
                             "\n", infoPtr->help, (char *) NULL);
            return TCL_ERROR;
          }
          if (infoPtr->range_set == true) {
            if (parsedInfoPtr->arg_float < infoPtr->l_range.float_r1 ||
                parsedInfoPtr->arg_float > infoPtr->u_range.float_r2) {
              snprintf(buf, sizeof(buf), "value of %s out of range %f - %f", infoPtr->key, infoPtr->l_range.float_r1,
                       infoPtr->u_range.float_r2);
              Tcl_AppendResult(interp, buf, (char *) NULL);
              return TCL_ERROR;
            }

          }
          srcIndex++;
          argc--;
          parsedInfoPtr->parsed_args = infoPtr->arg_ref;
          break;

        case CLI_ARGV_OPTION_FLOAT_VALUE:
          if (argc > 0) {
            parsedInfoPtr->arg_float = strtold(argv[srcIndex], &endPtr);
            if ((endPtr == argv[srcIndex]) || (*endPtr != 0)) {
              parsedInfoPtr->arg_float = CLI_DEFAULT_INT_OR_FLOAT_VALUE;
              parsedInfoPtr->parsed_args = infoPtr->arg_ref;
              break;
            }
            if (infoPtr->range_set == true) {
              if (parsedInfoPtr->arg_float < infoPtr->l_range.float_r1 ||
                  parsedInfoPtr->arg_float > infoPtr->u_range.float_r2) {
                snprintf(buf, sizeof(buf), "value of %s out of range %f - %f", infoPtr->key, infoPtr->l_range.float_r1,
                         infoPtr->u_range.float_r2);
                Tcl_AppendResult(interp, buf, (char *) NULL);
                return TCL_ERROR;
              }

            }
            srcIndex++;
            argc--;
          }
          parsedInfoPtr->parsed_args = infoPtr->arg_ref;
          break;

        case CLI_ARGV_FUNC:
          break;

        case CLI_ARGV_CONST_OPTION:

          parsedInfoPtr->parsed_args = infoPtr->arg_ref;
          break;
        default:
          parsedInfoPtr->parsed_args = infoPtr->arg_ref;
          break;


        }
        prevMatchPtr = infoPtr;
      }
      if (gotMatch == true)
        break;

    }
    if (gotMatch == false) {
      if (prevMatchPtr != NULL) {
        /*if prev argument is of type CLI_ARGV_CONST_OPTION then this has to be valid
           argument so return error */
        if (prevMatchPtr->type == CLI_ARGV_CONST_OPTION) {
          snprintf(buf, sizeof(buf), "unrecognized argument %s\n %s", curArg, prevMatchPtr->help);
          Tcl_AppendResult(interp, buf, (char *) NULL);
          return TCL_ERROR;
        }

        if ((prevPtr == prevMatchPtr) && (prevMatchPtr->type == CLI_ARGV_OPTION_FLOAT_VALUE)) {
          Tcl_AppendResult(interp, prevMatchPtr->key, " requires floating point argument", "\n", prevMatchPtr->help,
                           (char *) NULL);
          return TCL_ERROR;
        }

        if ((prevPtr == prevMatchPtr) && (prevMatchPtr->type == CLI_ARGV_OPTION_INT_VALUE)) {
          if (gotInt == 0) {
            Tcl_AppendResult(interp, prevMatchPtr->key, " requires integer argument", "\n", prevMatchPtr->help,
                             (char *) NULL);
            return TCL_ERROR;
          }
        }


        if (prevMatchPtr->type != CLI_ARGV_OPTION_NAME_VALUE) {
          parsedInfoPtr->data = ats_strdup(curArg);
          parsedInfoPtr->parsed_args = CLI_PARSED_ARGV_DATA;
        } else
          parsedInfoPtr--;

      } else {
        parsedInfoPtr->data = ats_strdup(curArg);
        parsedInfoPtr->parsed_args = CLI_PARSED_ARGV_DATA;
      }
    }
    prev_parsedInfoPtr = parsedInfoPtr;
    parsedInfoPtr++;
    prevPtr = infoPtr;

  }
  parsedInfoPtr->parsed_args = CLI_PARSED_ARGV_END;

  return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * PrintUsage --
 *
 *	Generate a help string describing command-line options.
 *
 * Results:
 *	The interp's result will be modified to hold a help string
 *	describing all the options in argTable, plus all those
 *	in the default table unless CLI_ARGV_NO_DEFAULTS is
 *	specified in flags.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static void
PrintUsage(Tcl_Interp * /* interp ATS_UNUSED */, cli_CommandInfo * commandInfo)
{
  cli_ArgvInfo *infoPtr;
  cli_ArgvInfo *argTable = commandInfo->argtable;
  int width, numSpaces;

  /*
   * First, compute the width of the widest option key, so that we
   * can make everything line up.
   */
  width = 4;
  char *cmdHelpString = commandInfo->helpString;
  int length = strlen(commandInfo->command_name);

  if (length > width) {
    width = length;
  }

  /* Now for arguments */

  for (infoPtr = argTable; infoPtr->key != NULL; infoPtr++) {
    length = strlen(infoPtr->key);
    if (length > width) {
      width = length;
    }
  }

  // display command name
  Cli_Printf("\n%s", commandInfo->command_name);
  numSpaces = width + 1 - strlen(commandInfo->command_name);

  while (numSpaces > 0) {
    Cli_Printf(" ");
    numSpaces--;
  }

  // display command help string
  Cli_Printf("    %s", cmdHelpString);

  for (infoPtr = argTable; infoPtr->key != NULL; infoPtr++) {
    if (infoPtr->position >= 100) {
      Cli_Printf("\n    ");
    } else {
      Cli_Printf("\n  ");
    }

    // display option name
    Cli_Printf("%s", infoPtr->key);
    numSpaces = width + 1 - strlen(infoPtr->key);

    while (numSpaces > 0) {
      Cli_Printf(" ");
      numSpaces--;
    }
    // display option parameters
    Cli_Printf("  %s", infoPtr->help);
  }

  Cli_Printf("\n\n");
}
