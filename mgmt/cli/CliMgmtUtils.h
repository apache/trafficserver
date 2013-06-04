/** @file

  This file declares various utility functions which call the TSMgmtAPI.

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

#include "mgmtapi.h"

#define RECORD_GET 0
#define RECORD_SET 1

// Get a records.config variable by name
TSError Cli_RecordGet(const char *rec_name, TSRecordEle * rec_val);

// Get an integer type records.config variable
TSError Cli_RecordGetInt(const char *rec_name, TSInt * int_val);

// Get an counter type records.config variable
TSError Cli_RecordGetCounter(const char *rec_name, TSCounter * ctr_val);

// Get a float type records.config variable
TSError Cli_RecordGetFloat(const char *rec_name, TSFloat * float_val);

// Get a string type records.config variable
TSError Cli_RecordGetString(const char *rec_name, char **string_val);

// Use a string to set a records.config variable
TSError Cli_RecordSet(const char *rec_name, const char *rec_value, TSActionNeedT * action_need);

// Set an integer type records.config variable
TSError Cli_RecordSetInt(const char *rec_name, TSInt int_val, TSActionNeedT * action_need);

//Set a float type records.config variable
TSError Cli_RecordSetFloat(const char *rec_name, TSFloat float_val, TSActionNeedT * action_need);

// Set a string type records.config variable
TSError Cli_RecordSetString(const char *rec_name, TSString str_val, TSActionNeedT * action_need);

// Retrieve and display contents of a rules file
TSError Cli_DisplayRules(TSFileNameT fname);

// Retrieve and use config file from remote URL
TSError Cli_SetConfigFileFromUrl(TSFileNameT file, const char *url);

// enable recent configuration changes by performing the action specified
// by the action_need value
TSError Cli_ConfigEnactChanges(TSActionNeedT action_need);

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
int Cli_RecordOnOff_Action(int action, const char *record, const char *on_off);

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
int Cli_RecordInt_Action(int action, const char *record, int value);

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
int Cli_RecordHostname_Action(int action, const char *record, char *hostname);

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
int Cli_RecordString_Action(int action, const char *record, char *string_val);

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
int Cli_ConfigFileURL_Action(TSFileNameT file, const char *filename, const char *url);

extern bool enable_restricted_commands;

int cliCheckIfEnabled(const char *command);

int GetTSDirectory(char *ts_path, size_t ts_path_len);

int StopTrafficServer();
int StartTrafficServer();
int Cli_CheckPluginStatus(TSString plugin);
