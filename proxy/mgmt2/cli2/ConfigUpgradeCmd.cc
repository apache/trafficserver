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
 * Filename: ConfigUpgradeCmd.cc
 * Purpose: This file contains the CLI's "config:write/read/install/upgrade"
   command implementation.
 *
 ****************************************************************/


#include <ConfigCmd.h>
#include <CliDisplay.h>
#include <CliMgmtUtils.h>
#include <CliDisplay.h>
#include <CliMgmtUtils.h>
#include <ConfigUpgradeCmd.h>
#include <string.h>


CIFCEntry::CIFCEntry(char *keyword, int counton, const char *string, ...)
{

  char Buffer[CONFIG_UPGRADE_BUF_SIZE];

  KeyWord = new char[strlen(keyword) + 1];
  sprintf(KeyWord, keyword);
  CountOn = counton;

  va_start(ap, string);
  vsprintf(Buffer, string, ap);
  va_end(ap);
  Input = new char[strlen(Buffer) + 1];
  sprintf(Input, Buffer);

  Version = NULL;
  FileName = NULL;

}

CIFCEntry::~CIFCEntry()
{

  if (Version != NULL)
    delete[]Version;
  if (FileName != NULL)
    delete[]FileName;
  if (KeyWord != NULL)
    delete[]KeyWord;
  if (Input != NULL)
    delete[]Input;

}

// check IFCVERSION IFCPATH and IFCFILENAME setup
INKError
CIFCEntry::ConfigWriteCheckIFCEnv()
{

  char *pathPtr;
  char *filenamePtr;
  char *versionPtr;


  pathPtr = getenv("IFCPATH");
  filenamePtr = getenv("IFCFILENAME");
  versionPtr = getenv("IFCVERSION");

  if (pathPtr == NULL || filenamePtr == NULL || versionPtr == NULL ||
      strlen(pathPtr) == 0 || strlen(filenamePtr) == 0 || strlen(versionPtr) == 0) {
    return INK_ERR_READ_FILE;
  }

  FileName = new char[strlen(pathPtr) + strlen(filenamePtr) + 1];
  sprintf(FileName, "%s%s", pathPtr, filenamePtr);

  Version = new char[strlen(versionPtr) + 1];
  sprintf(Version, "%s", versionPtr);

  return INK_ERR_OKAY;
}

INKError
CIFCEntry::Cli_NewIFCFile()
{

  FILE *Fptr;

  Fptr = fopen(FileName, "r+");
  if (Fptr != NULL) {
    fclose(Fptr);
    return INK_ERR_OKAY;
  } else {                      /* need to create new file */
    Fptr = fopen(FileName, "w");
    if (Fptr == NULL) {
      Cli_Error("Error in creating new IFC file\n");
      return INK_ERR_WRITE_FILE;
    }
    fprintf(Fptr, "############################\n");
    fprintf(Fptr, "#IFCHEAD FORMAT\n");
    fprintf(Fptr, "#\n");
    fprintf(Fptr, "#<TRAFFIC SERVER VERSION>\n");
    fprintf(Fptr, "#<BUILD DATE>\n");
    fprintf(Fptr, "#<PLATFORM>\n");
    fprintf(Fptr, "#<NUMBER OF NODES>\n");
    fprintf(Fptr, "\n");
    fprintf(Fptr, "IfcHead\n");
    fprintf(Fptr, "\n");

    fprintf(Fptr, "############################\n");
    fprintf(Fptr, "#FEATURE FORMAT\n");
    fprintf(Fptr, "#\n");
    fprintf(Fptr, "#<FEATURE STRING LIST>\n");
    fprintf(Fptr, "\n");
    fprintf(Fptr, "Feature\n");
    fprintf(Fptr, "\n");

    fprintf(Fptr, "############################\n");
    fprintf(Fptr, "#TAR FORMAT\n");
    fprintf(Fptr, "#\n");
    fprintf(Fptr, "#<NUMBER OF TAR FILES>\n");
    fprintf(Fptr, "#<LIST OF TAR FILES>\n");
    fprintf(Fptr, "\n");
    fprintf(Fptr, "Tar\n");
    fprintf(Fptr, "0\n");
    fprintf(Fptr, "\n");

    fprintf(Fptr, "############################\n");
    fprintf(Fptr, "#COMMONTAR FORMAT\n");
    fprintf(Fptr, "#\n");
    fprintf(Fptr, "#<NUMBER OF TAR FILES>\n");
    fprintf(Fptr, "#<LIST OF TAR FILES>\n");
    fprintf(Fptr, "\n");
    fprintf(Fptr, "CommonTar\n");
    fprintf(Fptr, "0\n");
    fprintf(Fptr, "\n");

    fprintf(Fptr, "############################\n");
    fprintf(Fptr, "#TAR INFO FORMAT\n");
    fprintf(Fptr, "#\n");
    fprintf(Fptr, "#<TAR FILE NAME>:<LIST OF FILES>\n");
    fprintf(Fptr, "\n");
    fprintf(Fptr, "TarInfo\n");
    fprintf(Fptr, "\n");

    fprintf(Fptr, "############################\n");
    fprintf(Fptr, "#BIN GROUP FORMAT\n");
    fprintf(Fptr, "#\n");
    fprintf(Fptr, "#<NUMBER OF FILES>\n");
    fprintf(Fptr, "#<LIST OF FILES>\n");
    fprintf(Fptr, "#\n");
    fprintf(Fptr, "BinGroup\n");
    fprintf(Fptr, "0\n");
    fprintf(Fptr, "\n");

    fprintf(Fptr, "############################\n");
    fprintf(Fptr, "#BIN DIR FORMAT\n");
    fprintf(Fptr, "#\n");
    fprintf(Fptr, "#<Number of SubDirectories>\n");
    fprintf(Fptr, "#<SubDirectory>:<List of Files>\n");
    fprintf(Fptr, "\n");
    fprintf(Fptr, "BinDir\n");
    fprintf(Fptr, "0\n");
    fprintf(Fptr, "\n");

    fprintf(Fptr, "############################\n");
    fprintf(Fptr, "#BIN COMMON FORMAT\n");
    fprintf(Fptr, "#\n");
    fprintf(Fptr, "#<Number of Files>\n");
    fprintf(Fptr, "#<List of Files>\n");
    fprintf(Fptr, "\n");
    fprintf(Fptr, "BinCommon\n");
    fprintf(Fptr, "0\n");
    fprintf(Fptr, "\n");

    fprintf(Fptr, "############################\n");
    fprintf(Fptr, "#LIB GROUP FORMAT\n");
    fprintf(Fptr, "#\n");
    fprintf(Fptr, "#<Number of Files>\n");
    fprintf(Fptr, "#<List of Files>\n");
    fprintf(Fptr, "\n");
    fprintf(Fptr, "LibGroup\n");
    fprintf(Fptr, "0\n");
    fprintf(Fptr, "\n");

    fprintf(Fptr, "############################\n");
    fprintf(Fptr, "#LIB DIR FORMAT\n");
    fprintf(Fptr, "#\n");
    fprintf(Fptr, "#<Number of SubDirectories>\n");
    fprintf(Fptr, "#<SubDirectory>:<List of Files>\n");
    fprintf(Fptr, "\n");
    fprintf(Fptr, "LibDir\n");
    fprintf(Fptr, "0\n");
    fprintf(Fptr, "\n");

    fprintf(Fptr, "############################\n");
    fprintf(Fptr, "#LIB COMMON FORMAT\n");
    fprintf(Fptr, "#\n");
    fprintf(Fptr, "#<Number of Files>\n");
    fprintf(Fptr, "#<List of Files>\n");
    fprintf(Fptr, "\n");
    fprintf(Fptr, "LibCommon\n");
    fprintf(Fptr, "0\n");
    fprintf(Fptr, "\n");

    fprintf(Fptr, "############################\n");
    fprintf(Fptr, "#CONFIG GROUP FORMAT>\n");
    fprintf(Fptr, "#\n");
    fprintf(Fptr, "#<Number of Files>\n");
    fprintf(Fptr, "#<List of Files>\n");
    fprintf(Fptr, "\n");
    fprintf(Fptr, "ConfigGroup\n");
    fprintf(Fptr, "0\n");
    fprintf(Fptr, "\n");

    fprintf(Fptr, "############################\n");
    fprintf(Fptr, "#CONFIG DIR FORMAT\n");
    fprintf(Fptr, "#\n");
    fprintf(Fptr, "#<Number of SubDirectories>\n");
    fprintf(Fptr, "#<SubDirectory>:<List of Files>\n");
    fprintf(Fptr, "\n");
    fprintf(Fptr, "ConfigDir\n");
    fprintf(Fptr, "0\n");
    fprintf(Fptr, "\n");

    fprintf(Fptr, "############################\n");
    fprintf(Fptr, "#CONFIG COMMON FORMAT\n");
    fprintf(Fptr, "#\n");
    fprintf(Fptr, "#<Number of Files>\n");
    fprintf(Fptr, "#<List of Files>\n");
    fprintf(Fptr, "\n");
    fprintf(Fptr, "ConfigCommon\n");
    fprintf(Fptr, "0\n");
    fprintf(Fptr, "\n");

    fprintf(Fptr, "############################\n");
    fprintf(Fptr, "#COMMON FORMAT\n");
    fprintf(Fptr, "#\n");
    fprintf(Fptr, "#<Number of Files>\n");
    fprintf(Fptr, "#<List of Files>\n");
    fprintf(Fptr, "\n");
    fprintf(Fptr, "CommonFile\n");
    fprintf(Fptr, "0\n");
    fprintf(Fptr, "\n");

    fclose(Fptr);
    return INK_ERR_OKAY;
  }

}


int
CIFCEntry::ConfigWriteIFC()
{

  FILE *Fptr;
  char *filebuffer, *in_buffer;
  char *p1;
  char OldCountString[CONFIG_UPGRADE_INT_STRING_SIZE];
  int Count = -1;
  long size, amount_read, addsize;

  if (ConfigWriteCheckIFCEnv() == INK_ERR_READ_FILE) {
    Cli_Error("Set $IFCVERSION, $IFCPATH and $IFCFILENAME First\n");
    return CLI_ERROR;
  }
  if (Cli_NewIFCFile() == INK_ERR_WRITE_FILE) {
    return CLI_ERROR;
  }
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

  // writeback
  Fptr = fopen(FileName, "r+");
  fseek(Fptr, p1 - filebuffer, SEEK_SET);

  switch (CountOn) {
  case 1:

    // coverity[secure_coding]
    sscanf(p1, "%d\n", &Count);
    sprintf(OldCountString, "%d", Count);
    p1 += (strlen(OldCountString) + 1);
    Count++;
    addsize = strlen(Input) + CONFIG_UPGRADE_INT_STRING_SIZE + 2;
    in_buffer = new char[addsize + 1];
    sprintf(in_buffer, "%d\n%s\n", Count, Input);
    in_buffer[addsize] = '\0';
    fwrite(in_buffer, sizeof(char), strlen(in_buffer), Fptr);
    delete[]in_buffer;
    break;

  case 0:

    fwrite(Input, sizeof(char), strlen(Input), Fptr);
    fwrite("\n", sizeof(char), strlen("\n"), Fptr);
    break;

  default:

    Cli_Error("Unexpected Value of CountOn\n");
    fclose(Fptr);
    delete[]filebuffer;
    return CLI_ERROR;
  }

  fwrite(p1, sizeof(char), size - (p1 - filebuffer), Fptr);
  fclose(Fptr);
  delete[]filebuffer;
  return CLI_OK;

}

//config read subcommand
int
CIFCEntry::ConfigReadIFC()
{

  FILE *Fptr;
  char *filebuffer;
  long size, amount_read;
  INKError CLI_CHECK;

  if (ConfigWriteCheckIFCEnv() == INK_ERR_READ_FILE) {
    Cli_Error("Set $IFCVERSION, $IFCPATH and $IFCFILENAME First\n");
    return CLI_ERROR;
  }

  if ((Fptr = fopen(FileName, "r")) == NULL) {
    Cli_Printf("ERROR Opening IFC file for read\n");
    return CLI_ERROR;
  }

  fseek(Fptr, 0, SEEK_END);
  size = ftell(Fptr);
  if (size <= 0) {
    Cli_Error("Error Empty IFC FILE\n", FileName);
    fclose(Fptr);
    return CLI_ERROR;
  }

  filebuffer = new char[size + 1];
  fseek(Fptr, 0, SEEK_SET);
  amount_read = fread((void *) filebuffer, (int) sizeof(char), size, Fptr);

  if (size * (int) sizeof(char) != amount_read) {
    Cli_Error("Error Reading IFC File\n");
    delete[]filebuffer;
    return CLI_ERROR;
  }

  filebuffer[amount_read] = '\0';
  fclose(Fptr);

  printf("%s\n", filebuffer);
  delete[]filebuffer;
  return CLI_OK;
}

void
CIFCEntry::PrintEle()
{
  printf("%s:%d:%s\n", KeyWord, CountOn, Input);
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
Cmd_ConfigWrite(ClientData clientData, Tcl_Interp * interp, int argc, char *argv[])
{
  CIFCEntry *EntryPtr;
  int CLI_RETURN;

  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:write") == CLI_ERROR) {
    return CMD_ERROR;
  }

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable, *infoPtr;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  infoPtr = argtable;
  Cli_Debug("Cmd_ConfigWrite argc %d\n", argc);

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {
    switch (argtable->parsed_args) {
    case CMD_CONFIG_WRITE_IFC_HEAD:
      EntryPtr = new CIFCEntry("IfcHead", 0, "%s\n%s\n%s\n%d", argtable[1].arg_string, argtable[2].arg_string,
                               argtable[3].arg_string, argtable[4].arg_int);
      break;
    case CMD_CONFIG_WRITE_FEATURE:
      EntryPtr = new CIFCEntry("Feature", 0, argtable[0].arg_string);
      break;
    case CMD_CONFIG_WRITE_TAR:
      EntryPtr = new CIFCEntry("Tar", 1, argtable[0].arg_string);
      break;
    case CMD_CONFIG_WRITE_TAR_INFO:
      EntryPtr = new CIFCEntry("TarInfo", 0, "%s:%s", argtable[0].arg_string, argtable[1].arg_string);
      break;
    case CMD_CONFIG_WRITE_TAR_COMMON:
      EntryPtr = new CIFCEntry("CommonTar", 1, argtable[0].arg_string);
      break;
    case CMD_CONFIG_WRITE_BIN_DIR:
      EntryPtr = new CIFCEntry("BinDir", 1, "%s:%s", argtable[0].arg_string, argtable[1].arg_string);
      break;
    case CMD_CONFIG_WRITE_BIN_GROUP:
      EntryPtr = new CIFCEntry("BinGroup", 1, argtable[0].arg_string);
      break;
    case CMD_CONFIG_WRITE_BIN_COMMON:
      EntryPtr = new CIFCEntry("BinCommon", 1, argtable[0].arg_string);
      break;
    case CMD_CONFIG_WRITE_LIB_DIR:
      EntryPtr = new CIFCEntry("LibDir", 1, "%s:%s", argtable[0].arg_string, argtable[1].arg_string);
      break;
    case CMD_CONFIG_WRITE_LIB_GROUP:
      EntryPtr = new CIFCEntry("LibGroup", 1, argtable[0].arg_string);
      break;
    case CMD_CONFIG_WRITE_LIB_COMMON:
      EntryPtr = new CIFCEntry("LibCommon", 1, argtable[0].arg_string);
      break;
    case CMD_CONFIG_WRITE_CONFIG_DIR:
      EntryPtr = new CIFCEntry("ConfigDir", 1, "%s:%s", argtable[0].arg_string, argtable[1].arg_string);
      break;
    case CMD_CONFIG_WRITE_CONFIG_GROUP:
      EntryPtr = new CIFCEntry("ConfigGroup", 1, argtable[0].arg_string);
      break;
    case CMD_CONFIG_WRITE_CONFIG_COMMON:
      EntryPtr = new CIFCEntry("ConfigCommon", 1, argtable[0].arg_string);
      break;
    case CMD_CONFIG_WRITE_COMMON_FILE:
      EntryPtr = new CIFCEntry("CommonFile", 1, argtable[0].arg_string);
    }

    if (EntryPtr == NULL) {
      Cli_Error("Error Allocate Memory\n");
      return CLI_ERROR;
    } else {
      CLI_RETURN = EntryPtr->ConfigWriteIFC();
      delete EntryPtr;
      return CLI_RETURN;
    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigWrite
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
Cmd_ConfigRead(ClientData clientData, Tcl_Interp * interp, int argc, char *argv[])
{

  CIFCEntry *EntryPtr;
  int CLI_RETURN;

  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:read") == CLI_ERROR) {
    return CMD_ERROR;
  }

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable, *infoPtr;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  infoPtr = argtable;
  Cli_Debug("Cmd_ConfigRead argc %d\n", argc);

  if (argtable->parsed_args == CLI_PARSED_ARGV_END) {
    EntryPtr = new CIFCEntry("", 0, "");
    if (EntryPtr == NULL) {
      Cli_Error("Error Allocate Memory\n");
      return CLI_ERROR;
    } else {
      CLI_RETURN = EntryPtr->ConfigReadIFC();
      delete EntryPtr;
      return CLI_RETURN;
    }
  }

  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;

}
