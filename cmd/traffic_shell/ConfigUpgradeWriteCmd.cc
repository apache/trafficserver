/** @file

  This file contains the CLI's "config:write" command implementation

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
#include <CliDisplay.h>
#include <CliMgmtUtils.h>
#include <ConfigUpgradeCmd.h>
#include <string.h>


CIFCWriteEntry::CIFCWriteEntry():CountOn(0)
{
  char *pathPtr;
  char *filenamePtr;
  char *versionPtr;

  pathPtr = getenv("IFCPATH");
  filenamePtr = getenv("IFCFILENAME");
  versionPtr = getenv("IFCVERSION");

  snprintf(FileName, sizeof(FileName), "%s%s", pathPtr, filenamePtr);
  snprintf(Version, sizeof(Version), "%s", versionPtr);

  KeyWord[0] = '\0';
  Input[0] = '\0';
}

CIFCWriteEntry::~CIFCWriteEntry()
{
}

// check IFCVERSION IFCPATH and IFCFILENAME setup
TSError
CIFCWriteEntry::ConfigWriteCheckIFCEnv()
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

TSError
CIFCWriteEntry::Cli_NewIFCFile()
{
  FILE *Fptr;

  Fptr = fopen(FileName, "r+");
  if (Fptr != NULL) {
    fclose(Fptr);
    return TS_ERR_OKAY;
  } else {                      /* need to create new file */
    Fptr = fopen(FileName, "w");
    if (Fptr == NULL) {
      Cli_Error("Error in creating new IFC file\n");
      return TS_ERR_WRITE_FILE;
    }

    fprintf(Fptr, "%s\n\n", IFC_BEGIN);
    fprintf(Fptr, "%s%s\n%s\n\n", IFC_HEAD, IFC_LIST_BEGIN, IFC_LIST_END);
    fprintf(Fptr, "%s%s\n%s\n\n", IFC_FEATURE, IFC_LIST_BEGIN, IFC_LIST_END);
    fprintf(Fptr, "%s%s%d\n%s\n\n", IFC_TAR, IFC_LIST_BEGIN, 0, IFC_LIST_END);
    fprintf(Fptr, "%s%s\n%s\n\n", IFC_TAR_INFO, IFC_LIST_BEGIN, IFC_LIST_END);
    fprintf(Fptr, "%s%s%d\n%s\n\n", IFC_COMMON_TAR, IFC_LIST_BEGIN, 0, IFC_LIST_END);
    fprintf(Fptr, "%s%s%d\n%s\n\n", IFC_BIN_GROUP, IFC_LIST_BEGIN, 0, IFC_LIST_END);
    fprintf(Fptr, "%s%s%d\n%s\n\n", IFC_BIN_DIR, IFC_LIST_BEGIN, 0, IFC_LIST_END);
    fprintf(Fptr, "%s%s%d\n%s\n\n", IFC_BIN_COMMON, IFC_LIST_BEGIN, 0, IFC_LIST_END);
    fprintf(Fptr, "%s%s%d\n%s\n\n", IFC_LIB_GROUP, IFC_LIST_BEGIN, 0, IFC_LIST_END);
    fprintf(Fptr, "%s%s%d\n%s\n\n", IFC_LIB_DIR, IFC_LIST_BEGIN, 0, IFC_LIST_END);
    fprintf(Fptr, "%s%s%d\n%s\n\n", IFC_LIB_COMMON, IFC_LIST_BEGIN, 0, IFC_LIST_END);
    fprintf(Fptr, "%s%s%d\n%s\n\n", IFC_CONFIG_GROUP, IFC_LIST_BEGIN, 0, IFC_LIST_END);
    fprintf(Fptr, "%s%s%d\n%s\n\n", IFC_CONFIG_DIR, IFC_LIST_BEGIN, 0, IFC_LIST_END);
    fprintf(Fptr, "%s%s%d\n%s\n\n", IFC_CONFIG_COMMON, IFC_LIST_BEGIN, 0, IFC_LIST_END);
    fprintf(Fptr, "%s%s%d\n%s\n\n", IFC_COMMON_FILE, IFC_LIST_BEGIN, 0, IFC_LIST_END);
    fprintf(Fptr, "%s\n\n", IFC_END);

    fclose(Fptr);

    return TS_ERR_OKAY;
  }
}


int
CIFCWriteEntry::ConfigWriteIFCEle()
{
  FILE *Fptr;
  char *filebuffer, *in_buffer;
  char *p1;
  char OldCountString[CONFIG_UPGRADE_INT_STRING_SIZE];
  int Count = -1;
  long size, amount_read, addsize;

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
  filebuffer[size] = '\0';

  if (size * (int) sizeof(char) != amount_read) {
    Cli_Error("Error Reading IFC File\n");
    fclose(Fptr);
    delete[]filebuffer;
    return CLI_ERROR;
  }
  // look for KeyWord
  if ((p1 = strstr(filebuffer, KeyWord)) == NULL) {
    delete[]filebuffer;
    fclose(Fptr);
    Cli_Error("Error Finding Keyword\n");
    return CLI_ERROR;
  }

  p1 += strlen(KeyWord);
  p1++;

  // writeback
  Fptr = fopen(FileName, "r+");
  fseek(Fptr, p1 - filebuffer, SEEK_SET);

  switch (CountOn) {
  case 1:
    {
      // coverity[secure_coding]
      sscanf(p1, "%d\n", &Count);
      snprintf(OldCountString, sizeof(OldCountString), "%d", Count);
      p1 += (strlen(OldCountString) + 1);
      Count++;
      addsize = strlen(Input) + CONFIG_UPGRADE_INT_STRING_SIZE + 2;
      const size_t in_buffer_size = addsize + 1;
      in_buffer = new char[in_buffer_size];
      snprintf(in_buffer, in_buffer_size, "%d\n%s\n", Count, Input);
      in_buffer[addsize] = '\0';
      if (fwrite(in_buffer, sizeof(char), strlen(in_buffer), Fptr) != strlen(in_buffer)) {
        Cli_Error("Unable to fwrite() buffer\n");
        delete[]in_buffer;
        delete[]filebuffer;
        fclose(Fptr);
        return CLI_ERROR;
      }
      delete[]in_buffer;
      break;
    }
  case 0:
    if ((fwrite("\n", sizeof(char), 1, Fptr) != 1) ||
        (fwrite(Input, sizeof(char), strlen(Input), Fptr) != strlen(Input))) {
      Cli_Error("Unable to fwrite() buffer\n");
      fclose(Fptr);
      delete[]filebuffer;
      return CLI_ERROR;
    }
    break;

  default:
    Cli_Error("Unexpected Value of CountOn\n");
    fclose(Fptr);
    delete[]filebuffer;
    return CLI_ERROR;
  }

  if (fwrite(p1, sizeof(char), size - (p1 - filebuffer), Fptr) != (size_t)(size - (p1 - filebuffer))) {
    Cli_Error("Unable to fwrite() buffer\n");
    fclose(Fptr);
    delete[]filebuffer;
    return CLI_ERROR;
  }
  fclose(Fptr);
  delete[]filebuffer;
  return CLI_OK;

}


void
CIFCWriteEntry::PrintEle()
{
  printf("%s:%d:%s\n", KeyWord, CountOn, Input);
}

int
CIFCWriteEntry::ConfigWriteIFCHead(char *ts_version, char *build_date, char *platform, int nodes)
{
  ink_strlcpy(KeyWord, IFC_HEAD, sizeof(KeyWord));
  snprintf(Input, sizeof(Input), "%s\n%s\n%s\n%d", ts_version, build_date, platform, nodes);
  CountOn = 0;
  return (ConfigWriteIFCEle());
}

int
CIFCWriteEntry::ConfigWriteIFCFeature(char *input)
{
  ink_strlcpy(KeyWord, IFC_FEATURE, sizeof(KeyWord));
  CountOn = 0;
  ink_strlcpy(Input, input, sizeof(Input));
  return (ConfigWriteIFCEle());
}

int
CIFCWriteEntry::ConfigWriteIFCTar(char *input)
{
  ink_strlcpy(KeyWord, IFC_TAR, sizeof(KeyWord));
  CountOn = 1;
  ink_strlcpy(Input, input, sizeof(Input));
  return (ConfigWriteIFCEle());
}

int
CIFCWriteEntry::ConfigWriteIFCCommonTar(char *input)
{
  ink_strlcpy(KeyWord, IFC_COMMON_TAR, sizeof(KeyWord));
  CountOn = 1;
  ink_strlcpy(Input, input, sizeof(Input));
  return (ConfigWriteIFCEle());
}

int
CIFCWriteEntry::ConfigWriteIFCTarInfo(char *tar, char *filelist)
{
  ink_strlcpy(KeyWord, IFC_TAR_INFO, sizeof(KeyWord));
  CountOn = 0;
  snprintf(Input, sizeof(Input), "%s:\n%s", tar, filelist);
  return (ConfigWriteIFCEle());
}

int
CIFCWriteEntry::ConfigWriteIFCBinGroup(char *input)
{
  ink_strlcpy(KeyWord, IFC_BIN_GROUP, sizeof(KeyWord));
  CountOn = 1;
  ink_strlcpy(Input, input, sizeof(Input));
  return (ConfigWriteIFCEle());
}

int
CIFCWriteEntry::ConfigWriteIFCBinDir(char *subdir, char *filelist)
{
  ink_strlcpy(KeyWord, IFC_BIN_DIR, sizeof(KeyWord));
  CountOn = 1;
  snprintf(Input, sizeof(Input), "%s:%s", subdir, filelist);
  return (ConfigWriteIFCEle());
}

int
CIFCWriteEntry::ConfigWriteIFCBinCommon(char *input)
{
  ink_strlcpy(KeyWord, IFC_BIN_COMMON, sizeof(KeyWord));
  CountOn = 1;
  ink_strlcpy(Input, input, sizeof(Input));
  return (ConfigWriteIFCEle());
}

int
CIFCWriteEntry::ConfigWriteIFCLibGroup(char *input)
{
  ink_strlcpy(KeyWord, IFC_LIB_GROUP, sizeof(KeyWord));
  CountOn = 1;
  ink_strlcpy(Input, input, sizeof(Input));
  return (ConfigWriteIFCEle());
}

int
CIFCWriteEntry::ConfigWriteIFCLibDir(char *subdir, char *filelist)
{
  ink_strlcpy(KeyWord, IFC_LIB_DIR, sizeof(KeyWord));
  CountOn = 1;
  snprintf(Input, sizeof(Input), "%s:%s", subdir, filelist);
  return (ConfigWriteIFCEle());
}

int
CIFCWriteEntry::ConfigWriteIFCLibCommon(char *input)
{
  ink_strlcpy(KeyWord, IFC_LIB_COMMON, sizeof(KeyWord));
  CountOn = 1;
  ink_strlcpy(Input, input, sizeof(Input));
  return (ConfigWriteIFCEle());
}

int
CIFCWriteEntry::ConfigWriteIFCConfigGroup(char *input)
{
  ink_strlcpy(KeyWord, IFC_CONFIG_GROUP, sizeof(KeyWord));
  CountOn = 1;
  ink_strlcpy(Input, input, sizeof(Input));
  return (ConfigWriteIFCEle());
}

int
CIFCWriteEntry::ConfigWriteIFCConfigDir(char *subdir, char *filelist)
{
  ink_strlcpy(KeyWord, IFC_CONFIG_DIR, sizeof(KeyWord));
  CountOn = 1;
  snprintf(Input, sizeof(Input), "%s:%s", subdir, filelist);
  return (ConfigWriteIFCEle());
}

int
CIFCWriteEntry::ConfigWriteIFCConfigCommon(char *input)
{
  ink_strlcpy(KeyWord, IFC_CONFIG_COMMON, sizeof(KeyWord));
  CountOn = 1;
  ink_strlcpy(Input, input, sizeof(Input));
  return (ConfigWriteIFCEle());
}


int
CIFCWriteEntry::ConfigWriteIFCCommonFile(char *input)
{
  ink_strlcpy(KeyWord, IFC_COMMON_FILE, sizeof(KeyWord));
  CountOn = 1;
  ink_strlcpy(Input, input, sizeof(Input));
  return (ConfigWriteIFCEle());
}

////////////////////////////////////////////////////////////////
// Cmd_ConfigWrite
//
// This is the callback function for the "config:write" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigWrite(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  /*if(cliCheckIfEnabled("config:write") == CLI_ERROR)
     {
     return CMD_ERROR;
     } */

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  Cli_Debug("Cmd_ConfigWrite argc %d\n", argc);

  if (CIFCWriteEntry::ConfigWriteCheckIFCEnv() == TS_ERR_READ_FILE) {
    Cli_Error("Set $IFCVERSION, $IFCPATH and $IFCFILENAME First\n");
    return CLI_ERROR;
  }

  CIFCWriteEntry Entry;

  if (Entry.Cli_NewIFCFile() == TS_ERR_WRITE_FILE) {
    return CLI_ERROR;
  }

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {
    switch (argtable->parsed_args) {
    case CMD_CONFIG_WRITE_IFC_HEAD:
      return (Entry.ConfigWriteIFCHead(argtable[1].arg_string,
                                       argtable[2].arg_string, argtable[3].arg_string, argtable[4].arg_int));

    case CMD_CONFIG_WRITE_FEATURE:
      return (Entry.ConfigWriteIFCFeature(argtable[0].arg_string));

    case CMD_CONFIG_WRITE_TAR:
      return (Entry.ConfigWriteIFCTar(argtable[0].arg_string));

    case CMD_CONFIG_WRITE_TAR_INFO:
      return (Entry.ConfigWriteIFCTarInfo(argtable[0].arg_string, argtable[1].arg_string));

    case CMD_CONFIG_WRITE_TAR_COMMON:
      return (Entry.ConfigWriteIFCCommonTar(argtable[0].arg_string));

    case CMD_CONFIG_WRITE_BIN_DIR:
      return (Entry.ConfigWriteIFCBinDir(argtable[0].arg_string, argtable[1].arg_string));

    case CMD_CONFIG_WRITE_BIN_GROUP:
      return (Entry.ConfigWriteIFCBinGroup(argtable[0].arg_string));

    case CMD_CONFIG_WRITE_BIN_COMMON:
      return (Entry.ConfigWriteIFCBinCommon(argtable[0].arg_string));

    case CMD_CONFIG_WRITE_LIB_DIR:
      return (Entry.ConfigWriteIFCLibDir(argtable[0].arg_string, argtable[1].arg_string));

    case CMD_CONFIG_WRITE_LIB_GROUP:
      return (Entry.ConfigWriteIFCLibGroup(argtable[0].arg_string));

    case CMD_CONFIG_WRITE_LIB_COMMON:
      return (Entry.ConfigWriteIFCLibCommon(argtable[0].arg_string));

    case CMD_CONFIG_WRITE_CONFIG_DIR:
      return (Entry.ConfigWriteIFCConfigDir(argtable[0].arg_string, argtable[1].arg_string));

    case CMD_CONFIG_WRITE_CONFIG_GROUP:
      return (Entry.ConfigWriteIFCConfigGroup(argtable[0].arg_string));

    case CMD_CONFIG_WRITE_CONFIG_COMMON:
      return (Entry.ConfigWriteIFCConfigCommon(argtable[0].arg_string));

    case CMD_CONFIG_WRITE_COMMON_FILE:
      return (Entry.ConfigWriteIFCCommonFile(argtable[0].arg_string));
    }
  }

  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigWrite0
//
// Register "config:write" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigWrite()
{
  createArgument("ifc-head", 1, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_WRITE_IFC_HEAD, "Specify the head information of ifc file", (char *) NULL);
  createArgument("ts-version", CMD_CONFIG_WRITE_IFC_HEAD, CLI_ARGV_STRING,
                 (char *) NULL, CMD_CONFIG_WRITE_TS_VERSION, "Specify the version of Traffic Server installed",
                 (char *) NULL);
  createArgument("build-date", CMD_CONFIG_WRITE_TS_VERSION, CLI_ARGV_STRING,
                 (char *) NULL, CMD_CONFIG_WRITE_BUILD_DATE, "Specify date of the Traffic Server Build", (char *) NULL);
  createArgument("platform", CMD_CONFIG_WRITE_BUILD_DATE, CLI_ARGV_STRING,
                 (char *) NULL, CMD_CONFIG_WRITE_PLATFORM, "Specify the platform of installation", (char *) NULL);
  createArgument("nodes", CMD_CONFIG_WRITE_PLATFORM, CLI_ARGV_INT,
                 (char *) NULL, CMD_CONFIG_WRITE_NODES, "Specify the number of node in the cluster", (char *) NULL);
  createArgument("feature", 1, CLI_ARGV_STRING,
                 (char *) NULL, CMD_CONFIG_WRITE_FEATURE, "Specify the feature string", (char *) NULL);
  createArgument("tar", 1, CLI_ARGV_STRING,
                 (char *) NULL, CMD_CONFIG_WRITE_TAR, "Specify the tar file list", (char *) NULL);
  createArgument("tar-common", 1, CLI_ARGV_STRING,
                 (char *) NULL, CMD_CONFIG_WRITE_TAR_COMMON, "Specify the MUST-HAVE tar files", (char *) NULL);
  createArgument("tar-info", 1, CLI_ARGV_STRING,
                 (char *) NULL, CMD_CONFIG_WRITE_TAR_INFO, "Specify the file contained in this named tar file",
                 (char *) NULL);
  createArgument("filelist", CLI_ARGV_NO_POS, CLI_ARGV_STRING,
                 (char *) NULL, CMD_CONFIG_WRITE_FILELIST, "Specify the filelist contained in this named tar file",
                 (char *) NULL);
  createArgument("bin-dir", 1, CLI_ARGV_STRING,
                 (char *) NULL, CMD_CONFIG_WRITE_BIN_DIR,
                 "Specify the subdirectories and the files in each of them in bin directory", (char *) NULL);
  createArgument("bin-group", 1, CLI_ARGV_STRING, (char *) NULL, CMD_CONFIG_WRITE_BIN_GROUP,
                 "Specify the file listed in the bin directory", (char *) NULL);
  createArgument("bin-common", 1, CLI_ARGV_STRING, (char *) NULL, CMD_CONFIG_WRITE_BIN_COMMON,
                 "Specify the MUST-HAVE bin files", (char *) NULL);
  createArgument("lib-dir", 1, CLI_ARGV_STRING, (char *) NULL, CMD_CONFIG_WRITE_LIB_DIR,
                 "Specify the subdirectories and the files in each of them in lib directory", (char *) NULL);
  createArgument("lib-group", 1, CLI_ARGV_STRING, (char *) NULL, CMD_CONFIG_WRITE_LIB_GROUP,
                 "Specify the file listed in the lib directory", (char *) NULL);
  createArgument("lib-common", 1, CLI_ARGV_STRING, (char *) NULL, CMD_CONFIG_WRITE_LIB_COMMON,
                 "Specify the MUST-HAVE lib files", (char *) NULL);
  createArgument("config-dir", 1, CLI_ARGV_STRING, (char *) NULL, CMD_CONFIG_WRITE_CONFIG_DIR,
                 "Specify the subdirectories and the files in each of them in the config directory", (char *) NULL);
  createArgument("config-group", 1, CLI_ARGV_STRING, (char *) NULL, CMD_CONFIG_WRITE_CONFIG_GROUP,
                 "Specify the file listed in the config directory", (char *) NULL);
  createArgument("config-common", 1, CLI_ARGV_STRING, (char *) NULL, CMD_CONFIG_WRITE_CONFIG_COMMON,
                 "Specify the MUST-HAVE config files", (char *) NULL);
  createArgument("common-file", 1, CLI_ARGV_STRING, (char *) NULL, CMD_CONFIG_WRITE_COMMON_FILE,
                 "Specify the MUST-HAVE files", (char *) NULL);

  return CLI_OK;
}
