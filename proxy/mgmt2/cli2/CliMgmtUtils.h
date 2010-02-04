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
 * Filename: CliMgmtUtils.h
 * Purpose: This file declares various utility functions which
 *          call the INKMgmtAPI.
 *
 * 
 ****************************************************************/

#include "../api2/include/INKMgmtAPI.h"

#define PATH_NAME_MAX         511 // instead of PATH_MAX which is inconsistent
                                  // on various OSs (linux-4096,osx/bsd-1024,
                                  //                 windows-260,etc)

// TODO: consolidate location of these defaults
#define DEFAULT_ROOT_DIRECTORY            PREFIX
#define DEFAULT_LOCAL_STATE_DIRECTORY     "./var/trafficserver"
#define DEFAULT_SYSTEM_CONFIG_DIRECTORY   "./etc/trafficserver"
#define DEFAULT_LOG_DIRECTORY             "./var/log/trafficserver"
#define DEFAULT_TS_DIRECTORY_FILE         PREFIX "/etc/traffic_server"

#define RECORD_GET 0
#define RECORD_SET 1

// Get a records.config variable by name
INKError Cli_RecordGet(const char *rec_name, INKRecordEle * rec_val);

// Get an integer type records.config variable
INKError Cli_RecordGetInt(char *rec_name, INKInt * int_val);

// Get an counter type records.config variable
INKError Cli_RecordGetCounter(char *rec_name, INKCounter * ctr_val);

// Get a float type records.config variable
INKError Cli_RecordGetFloat(char *rec_name, INKFloat * float_val);

// Get a string type records.config variable
INKError Cli_RecordGetString(char *rec_name, INKString * string_val);

// Use a string to set a records.config variable
INKError Cli_RecordSet(const char *rec_name, const char *rec_value, INKActionNeedT * action_need);

// Set an integer type records.config variable
INKError Cli_RecordSetInt(char *rec_name, INKInt int_val, INKActionNeedT * action_need);

//Set a float type records.config variable
INKError Cli_RecordSetFloat(char *rec_name, INKFloat float_val, INKActionNeedT * action_need);

// Set a string type records.config variable  
INKError Cli_RecordSetString(char *rec_name, INKString str_val, INKActionNeedT * action_need);

// Retrieve and display contents of a rules file
INKError Cli_DisplayRules(INKFileNameT fname);

// Retrieve and use config file from remote URL
INKError Cli_SetConfigFileFromUrl(INKFileNameT file, const char *url);

// enable recent configuration changes by performing the action specified
// by the action_need value
INKError Cli_ConfigEnactChanges(INKActionNeedT action_need);

// evaluate "stringval" and return 1 if "on", otherwise 0
int Cli_EvalOnOffString(char *stringval);

////////////////////////////////////////////////////////////////
// Cli_Record functions are used by various config commands
// to get/display and set variables in records.config
//

////////////////////////////////////////////////////////////////
// Cli_RecordOnOff_Action
//
// used for records.config INT variables when 1 = on, 0 = off
//
// action = RECORD_GET retrieve and display the variable
//          RECORD_SET set the variable
//
// record = variable in records.config
//
// on_off = "on" mean 1, "off" mean 0
//
int Cli_RecordOnOff_Action(int action, char *record, char *on_off);

////////////////////////////////////////////////////////////////
// Cli_RecordInt_Action
//
// used for records.config INT variables
//
// action = RECORD_GET retrieve and display the variable
//          RECORD_SET set the variable
//
// record = variable in records.config
//
// value = the integer value used by RECORD_SET
//
int Cli_RecordInt_Action(int action, char *record, int value);

////////////////////////////////////////////////////////////////
// Cli_RecordHostname_Action
//
// used for records.config STRING variables
// performs checking to see if string is a valid fully qualified hostname
//
// action = RECORD_GET retrieve and display the variable
//          RECORD_SET set the variable
//
// record = variable in records.config
//
// hostname = string to set
//
int Cli_RecordHostname_Action(int action, char *record, char *hostname);

////////////////////////////////////////////////////////////////
// Cli_RecordString_Action
//
// used for records.config STRING variables
//
// action = RECORD_GET retrieve and display the variable
//          RECORD_SET set the variable
//
// record = variable in records.config
//
// string_val = string to set
//
int Cli_RecordString_Action(int action, char *record, char *string_val);

////////////////////////////////////////////////////////////////
// Cli_ConfigFileURL_Action
//
// used for config files other than records.config
//
// file = integer which specifies config file
//
// filename = config file name to display
//
// url = if non-NULL, update the file using contents of URL
//
int Cli_ConfigFileURL_Action(INKFileNameT file, char *filename, const char *url);

extern int enable_restricted_commands;

int cliCheckIfEnabled(char *command);

int GetTSDirectory(char *ts_path, size_t ts_path_len);

int StopTrafficServer();
int StartTrafficServer();
int Cli_CheckPluginStatus(INKString plugin);
