/** @file

  This file contains the CLI's "config:read" command implementation

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
#include <ConfigCmd.h>
#include <CliDisplay.h>
#include <CliMgmtUtils.h>
#include <ConfigUpgradeCmd.h>
#include <string.h>
#include <mgmtapi.h>


extern Tcl_Interp *interp;
CIFCReadEntry::CIFCReadEntry():
CountOn(0), Count(0)
{
  KeyWord[0] = 0;
  Output[0] = 0;
  const char *pathPtr = getenv("IFCPATH");
  const char *filenamePtr = getenv("IFCFILENAME");
  const char *versionPtr = getenv("IFCVERSION");

  snprintf(FileName, sizeof(FileName), "%s%s", pathPtr, filenamePtr);
  snprintf(Version, sizeof(Version), "%s", versionPtr);
}

CIFCReadEntry::~CIFCReadEntry()
{
}

// check IFCVERSION IFCPATH and IFCFILENAME setup
TSError
CIFCReadEntry::ConfigReadCheckIFCEnv()
{
  char *pathPtr;
  char *filenamePtr;
  char *versionPtr;

  pathPtr = getenv("IFCPATH");
  filenamePtr = getenv("IFCFILENAME");
  versionPtr = getenv("IFCVERSION");

  if (pathPtr == NULL || filenamePtr == NULL || versionPtr == NULL ||
      strlen(pathPtr) == 0 || strlen(filenamePtr) == 0 || strlen(versionPtr) == 0) {
    return TS_ERR_READ_FILE;
  }

  return TS_ERR_OKAY;
}


int
CIFCReadEntry::ConfigReadPrintIFCEle()
{
  Tcl_AppendResult(interp, Output, (char *) NULL);
  return CLI_OK;
}

int
CIFCReadEntry::ConfigReadIFCEle()
{
  FILE *Fptr;
  char *filebuffer;
  char *p1, *p2;
  char CountString[CONFIG_UPGRADE_INT_STRING_SIZE];
  long size, amount_read;

  if ((Fptr = fopen(FileName, "r")) == NULL) {
    Cli_Error("ERROR Open IFC File to read\n");
    return CLI_ERROR;
  }

  fseek(Fptr, 0, SEEK_END);
  size = ftell(Fptr);
  if (size <= 0) {
    fclose(Fptr);
    Cli_Error("Error Reading IFC File\n");
    return CLI_ERROR;
  }

  filebuffer = new char[size + 1];
  fseek(Fptr, 0, SEEK_SET);
  amount_read = fread((void *) filebuffer, sizeof(char), size, Fptr);
  fclose(Fptr);
  Fptr = NULL;
  filebuffer[size] = '\0';

  if (size * (int) sizeof(char) != amount_read) {
    Cli_Error("Error Reading IFC File\n");
    delete[]filebuffer;
    return CLI_ERROR;
  }
  // look for KeyWord
  if ((p1 = strstr(filebuffer, KeyWord)) == NULL) {
    delete[]filebuffer;
    Cli_Error("Error Finding Keyword\n");
    return CLI_ERROR;
  }

  p1 += strlen(KeyWord);
  p1++;

  // look for the end tag
  if ((p2 = strstr(p1, IFC_LIST_END)) == NULL) {
    delete[]filebuffer;
    Cli_Error("Error Finding End Keyword\n");
    return CLI_ERROR;
  }

  switch (CountOn) {
  case 1:

    // coverity[secure_coding]
    sscanf(p1, "%d\n", &Count);
    snprintf(CountString, sizeof(CountString), "%d", Count);
    p1 += (strlen(CountString));
    //fall through

  case 0:
    p1++;
    snprintf(Output, p2 - p1, "%s", p1);
    //memset(Output + (p2-p1), 1, 0);
    delete[]filebuffer;
    return (ConfigReadPrintIFCEle());

  default:

    Cli_Error("Unexpected Value of CountOn\n");
    delete[]filebuffer;
    return CLI_ERROR;
  }
}


int
CIFCReadEntry::ConfigReadIFCHead()
{
  ink_strlcpy(KeyWord, IFC_HEAD, sizeof(KeyWord));
  CountOn = 0;
  return (ConfigReadIFCEle());
}

int
CIFCReadEntry::ConfigReadIFCFeature()
{
  ink_strlcpy(KeyWord, IFC_FEATURE, sizeof(KeyWord));
  CountOn = 0;
  return (ConfigReadIFCEle());
}

int
CIFCReadEntry::ConfigReadIFCTar()
{
  ink_strlcpy(KeyWord, IFC_TAR, sizeof(KeyWord));
  CountOn = 1;
  return (ConfigReadIFCEle());
}

int
CIFCReadEntry::ConfigReadIFCCommonTar()
{
  ink_strlcpy(KeyWord, IFC_COMMON_TAR, sizeof(KeyWord));
  CountOn = 1;
  return (ConfigReadIFCEle());
}

int
CIFCReadEntry::ConfigReadIFCTarInfo()
{
  ink_strlcpy(KeyWord, IFC_TAR_INFO, sizeof(KeyWord));
  CountOn = 0;
  return (ConfigReadIFCEle());
}

int
CIFCReadEntry::ConfigReadIFCBinGroup()
{
  ink_strlcpy(KeyWord, IFC_BIN_GROUP, sizeof(KeyWord));
  CountOn = 1;
  return (ConfigReadIFCEle());
}

int
CIFCReadEntry::ConfigReadIFCBinDir()
{
  ink_strlcpy(KeyWord, IFC_BIN_DIR, sizeof(KeyWord));
  CountOn = 1;
  return (ConfigReadIFCEle());
}

int
CIFCReadEntry::ConfigReadIFCBinCommon()
{
  ink_strlcpy(KeyWord, IFC_BIN_COMMON, sizeof(KeyWord));
  CountOn = 1;
  return (ConfigReadIFCEle());
}

int
CIFCReadEntry::ConfigReadIFCLibGroup()
{
  ink_strlcpy(KeyWord, IFC_LIB_GROUP, sizeof(KeyWord));
  CountOn = 1;
  return (ConfigReadIFCEle());
}

int
CIFCReadEntry::ConfigReadIFCLibDir()
{
  ink_strlcpy(KeyWord, IFC_LIB_DIR, sizeof(KeyWord));
  CountOn = 1;
  return (ConfigReadIFCEle());
}

int
CIFCReadEntry::ConfigReadIFCLibCommon()
{
  ink_strlcpy(KeyWord, IFC_LIB_COMMON, sizeof(KeyWord));
  CountOn = 1;
  return (ConfigReadIFCEle());
}

int
CIFCReadEntry::ConfigReadIFCConfigGroup()
{
  ink_strlcpy(KeyWord, IFC_CONFIG_GROUP, sizeof(KeyWord));
  CountOn = 1;
  return (ConfigReadIFCEle());
}

int
CIFCReadEntry::ConfigReadIFCConfigDir()
{
  ink_strlcpy(KeyWord, IFC_CONFIG_DIR, sizeof(KeyWord));
  CountOn = 1;
  return (ConfigReadIFCEle());
}

int
CIFCReadEntry::ConfigReadIFCConfigCommon()
{
  ink_strlcpy(KeyWord, IFC_CONFIG_COMMON, sizeof(KeyWord));
  CountOn = 1;
  return (ConfigReadIFCEle());
}


int
CIFCReadEntry::ConfigReadIFCCommonFile()
{
  ink_strlcpy(KeyWord, IFC_COMMON_FILE, sizeof(KeyWord));
  CountOn = 1;
  return (ConfigReadIFCEle());
}


////////////////////////////////////////////////////////////////
// Cmd_ConfigRead
//
// This is the callback function for the "config:read" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigRead(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  /*   if(cliCheckIfEnabled("config:read") == CLI_ERROR) {
     return CMD_ERROR;
     }
   */
  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  Cli_Debug("Cmd_ConfigRead argc %d\n", argc);

  if (CIFCWriteEntry::ConfigWriteCheckIFCEnv() == TS_ERR_READ_FILE) {
    Cli_Error("Set $IFCVERSION, $IFCPATH and $IFCFILENAME First\n");
    return CLI_ERROR;
  }

  CIFCReadEntry Entry;

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {

    switch (argtable->parsed_args) {

    case CMD_CONFIG_READ_IFC_HEAD:
      return (Entry.ConfigReadIFCHead());

    case CMD_CONFIG_READ_FEATURE:
      return (Entry.ConfigReadIFCFeature());

    case CMD_CONFIG_READ_TAR:
      return (Entry.ConfigReadIFCTar());

    case CMD_CONFIG_READ_TAR_INFO:
      return (Entry.ConfigReadIFCTarInfo());

    case CMD_CONFIG_READ_TAR_COMMON:
      return (Entry.ConfigReadIFCCommonTar());

    case CMD_CONFIG_READ_BIN_DIR:
      return (Entry.ConfigReadIFCBinDir());

    case CMD_CONFIG_READ_BIN_GROUP:
      return (Entry.ConfigReadIFCBinGroup());

    case CMD_CONFIG_READ_BIN_COMMON:
      return (Entry.ConfigReadIFCBinCommon());

    case CMD_CONFIG_READ_LIB_DIR:
      return (Entry.ConfigReadIFCLibDir());

    case CMD_CONFIG_READ_LIB_GROUP:
      return (Entry.ConfigReadIFCLibGroup());

    case CMD_CONFIG_READ_LIB_COMMON:
      return (Entry.ConfigReadIFCLibCommon());

    case CMD_CONFIG_READ_CONFIG_DIR:
      return (Entry.ConfigReadIFCConfigDir());

    case CMD_CONFIG_READ_CONFIG_GROUP:
      return (Entry.ConfigReadIFCConfigGroup());

    case CMD_CONFIG_READ_CONFIG_COMMON:
      return (Entry.ConfigReadIFCConfigCommon());

    case CMD_CONFIG_READ_COMMON_FILE:
      return (Entry.ConfigReadIFCCommonFile());
    }
  }

  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;

}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigRead
//
// Register "config:read" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigRead()
{
  createArgument("ifc-head", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_READ_IFC_HEAD, "Read the head information of ifc file", (char *) NULL);
  createArgument("feature", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_READ_FEATURE, "Read the feature string", (char *) NULL);
  createArgument("tar", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_READ_TAR, "Read the tar file list", (char *) NULL);
  createArgument("tar-common", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_READ_TAR_COMMON, "Read the MUST-HAVE tar files", (char *) NULL);
  createArgument("tar-info", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_READ_TAR_INFO, "Read the file contained in this named tar file",
                 (char *) NULL);
  createArgument("bin-dir", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_READ_BIN_DIR,
                 "Read the subdirectories and the files in each of them in bin directory", (char *) NULL);
  createArgument("bin-group", 1, CLI_ARGV_OPTION_NAME_VALUE, (char *) NULL, CMD_CONFIG_READ_BIN_GROUP,
                 "Read the file listed in the bin directory", (char *) NULL);
  createArgument("bin-common", 1, CLI_ARGV_OPTION_NAME_VALUE, (char *) NULL, CMD_CONFIG_READ_BIN_COMMON,
                 "Read the MUST-HAVE bin files", (char *) NULL);
  createArgument("lib-dir", 1, CLI_ARGV_OPTION_NAME_VALUE, (char *) NULL, CMD_CONFIG_READ_LIB_DIR,
                 "Read the subdirectories and the files in each of them in lib directory", (char *) NULL);
  createArgument("lib-group", 1, CLI_ARGV_OPTION_NAME_VALUE, (char *) NULL, CMD_CONFIG_READ_LIB_GROUP,
                 "Read the file listed in the lib directory", (char *) NULL);
  createArgument("lib-common", 1, CLI_ARGV_OPTION_NAME_VALUE, (char *) NULL, CMD_CONFIG_READ_LIB_COMMON,
                 "Read the MUST-HAVE lib files", (char *) NULL);
  createArgument("config-dir", 1, CLI_ARGV_OPTION_NAME_VALUE, (char *) NULL, CMD_CONFIG_READ_CONFIG_DIR,
                 "Read the subdirectories and the files in each of them in the config directory", (char *) NULL);
  createArgument("config-group", 1, CLI_ARGV_OPTION_NAME_VALUE, (char *) NULL, CMD_CONFIG_READ_CONFIG_GROUP,
                 "Read the file listed in the config directory", (char *) NULL);
  createArgument("config-common", 1, CLI_ARGV_OPTION_NAME_VALUE, (char *) NULL, CMD_CONFIG_READ_CONFIG_COMMON,
                 "Read the MUST-HAVE config files", (char *) NULL);
  createArgument("common-file", 1, CLI_ARGV_OPTION_NAME_VALUE, (char *) NULL, CMD_CONFIG_READ_COMMON_FILE,
                 "Read the MUST-HAVE files", (char *) NULL);

  return CLI_OK;
}


////////////////////////////////////////////////////////////////
// Cmd_ConfigSaveUrl
//
// This is the callback function for the "config:saveUrl" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigSaveUrl(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* Call to processArgForCommand must appear at the beginning
     of each command's cb function.
   */

  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

/*   if(cliCheckIfEnabled("config:save-url") == CLI_ERROR) {
     return CMD_ERROR;
   }
*/
  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable, *infoPtr;
  char *Url = NULL;
  char *outputFile = NULL;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  Cli_Debug("Cmd_ConfigSaveUrl argc %d\n", argc);
  char **header = 0;            /* lv: useless variable - never changed */
  int headerSize;
  char **body = 0;              /* lv: useless variable - never changed */
  int bodySize;

  for (infoPtr = argtable; infoPtr->parsed_args != CLI_PARSED_ARGV_END; infoPtr++) {
    switch (infoPtr->parsed_args) {

    case CMD_CONFIG_UPGRADE_READ_URL:
      Url = argtable->arg_string;
      infoPtr++;
      outputFile = argtable->arg_string;
      break;
    }
  }
  if (Url == NULL || outputFile == NULL) {
    return CMD_ERROR;
  } else {
    if (TSReadFromUrl(Url, header, &headerSize, body, &bodySize) == TS_ERR_FAIL) {
      return CMD_ERROR;
    }

  }

  return CMD_OK;
}


////////////////////////////////////////////////////////////////
// CmdArgs_ConfigSaveUrl
//
// Register "config:read" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigSaveUrl()
{
  createArgument("url", 1, CLI_ARGV_STRING, (char *) NULL, CMD_CONFIG_UPGRADE_READ_URL, "Read the url", (char *) NULL);

  return CMD_OK;
}
