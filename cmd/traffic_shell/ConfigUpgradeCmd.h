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

/************C****************************************************
 * Filename: ConfigUpgradeCmd.h
 * Purpose: This file contains the CLI's "config:" command definitions.
 ****************************************************************/



#include <tcl.h>
#include <stdlib.h>
#include <createArgument.h>
#include <definitions.h>



#ifndef __CONFIG_UPGRADE_CMD_H__
#define __CONFIG_UPGRADE_CMD_H__


#define CONFIG_UPGRADE_INT_STRING_SIZE 5
#define CONFIG_UPGRADE_STRING_SIZE 256
#define CONFIG_UPGRADE_BUF_SIZE 10240


#define IFC_BEGIN         "Begin"
#define IFC_HEAD          "IfcHead"
#define IFC_FEATURE       "Feature"
#define IFC_TAR           "Tar"
#define IFC_COMMON_TAR    "CommonTar"
#define IFC_TAR_INFO      "TarInfo"
#define IFC_BIN_GROUP     "BinGroup"
#define IFC_BIN_DIR       "BinDir"
#define IFC_BIN_COMMON    "BinCommon"
#define IFC_LIB_GROUP     "LibGroup"
#define IFC_LIB_DIR       "LibDir"
#define IFC_LIB_COMMON    "LibCommon"
#define IFC_CONFIG_GROUP  "ConfigGroup"
#define IFC_CONFIG_DIR    "ConfigDir"
#define IFC_CONFIG_COMMON "ConfigCommon"
#define IFC_COMMON_FILE   "CommonFile"
#define IFC_END           "End"
#define IFC_LIST_BEGIN    "{"
#define IFC_LIST_END      "}"

typedef enum
{
  IFC_KEY_IFCHEAD = 10,
  IFC_KEY_FEATURE,
  IFC_KEY_TAR,
  IFC_KEY_COMMON_TAR,
  IFC_KEY_TAR_INFO,
  IFC_KEY_BIN_GROUP,
  IFC_KEY_BIN_DIR,
  IFC_KEY_BIN_COMMON,
  IFC_KEY_LIB_GROUP,
  IFC_KEY_LIB_DIR,
  IFC_KEY_LIB_COMMON,
  IFC_KEY_CONFIG_GROUP,
  IFC_KEY_CONFIG_DIR,
  IFC_KEY_CONFIG_COMMON,
  IFC_KEY_COMMON_FILE,
  IFC_KEY_END
} IfcKeyWord;

class CIFCWriteEntry
{

  char FileName[CONFIG_UPGRADE_STRING_SIZE];
  char Version[CONFIG_UPGRADE_STRING_SIZE];
  char KeyWord[CONFIG_UPGRADE_STRING_SIZE];
  char Input[CONFIG_UPGRADE_BUF_SIZE];
  int CountOn;

  // write IFC file
  int ConfigWriteIFCEle();

public:

    CIFCWriteEntry();
   ~CIFCWriteEntry();

  // PrintOut the Element of this Entry
  void PrintEle();

  TSError Cli_NewIFCFile();

  static TSError ConfigWriteCheckIFCEnv();

  int ConfigWriteIFCHead(char *ts_version, char *build_date, char *platform, int nodes);

  int ConfigWriteIFCFeature(char *input);

  int ConfigWriteIFCTar(char *input);

  int ConfigWriteIFCCommonTar(char *input);

  int ConfigWriteIFCTarInfo(char *tar, char *filelist);

  int ConfigWriteIFCBinCommon(char *input);

  int ConfigWriteIFCBinGroup(char *input);

  int ConfigWriteIFCBinDir(char *subdir, char *filelist);

  int ConfigWriteIFCLibCommon(char *input);

  int ConfigWriteIFCLibGroup(char *input);

  int ConfigWriteIFCLibDir(char *subdir, char *filelist);

  int ConfigWriteIFCConfigCommon(char *input);

  int ConfigWriteIFCConfigGroup(char *input);

  int ConfigWriteIFCConfigDir(char *subdir, char *filelist);

  int ConfigWriteIFCCommonFile(char *input);

};


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
int Cmd_ConfigWrite(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigWrite
//
// Register "config:write" arguments with the Tcl interpreter.
//
int CmdArgs_ConfigWrite();


class CIFCReadEntry
{

  char FileName[CONFIG_UPGRADE_STRING_SIZE];
  char Version[CONFIG_UPGRADE_STRING_SIZE];
  char KeyWord[CONFIG_UPGRADE_STRING_SIZE];
  char Output[CONFIG_UPGRADE_BUF_SIZE];
  int CountOn;
  int Count;

  // read IFC file
  int ConfigReadIFCEle();

  int ConfigReadPrintIFCEle();

public:

    CIFCReadEntry();
   ~CIFCReadEntry();

  static TSError ConfigReadCheckIFCEnv();

  int ConfigReadIFCHead();

  int ConfigReadIFCFeature();

  int ConfigReadIFCTar();

  int ConfigReadIFCCommonTar();

  int ConfigReadIFCTarInfo();

  int ConfigReadIFCBinCommon();

  int ConfigReadIFCBinGroup();

  int ConfigReadIFCBinDir();

  int ConfigReadIFCLibCommon();

  int ConfigReadIFCLibGroup();

  int ConfigReadIFCLibDir();

  int ConfigReadIFCConfigCommon();

  int ConfigReadIFCConfigGroup();

  int ConfigReadIFCConfigDir();

  int ConfigReadIFCCommonFile();

};



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
int Cmd_ConfigRead(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigRead
//
// Register "config:read" arguments with the Tcl interpreter.
//
int CmdArgs_ConfigRead();

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigSaveUrl
//
// Register "config:save-url" arguments with the Tcl interpreter.
//

int Cmd_ConfigSaveUrl(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[]);

int CmdArgs_ConfigSaveUrl();


#endif /* __CONFIG_UPGRADE_CMD_H__ */
