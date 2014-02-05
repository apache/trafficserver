/** @file

  This file contains various utility functions which call the TSMgmtAPI.

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
#include "I_Layout.h"
#include <stdlib.h>
#include <unistd.h>
#include "CliMgmtUtils.h"
#include "CliDisplay.h"
#include "definitions.h"
#include "ConfigCmd.h"


void Cli_DisplayMgmtAPI_Error(TSError status);

// Get a records.config variable by name
TSError
Cli_RecordGet(const char *rec_name, TSRecordEle * rec_val)
{
  TSError status;
  if ((status = TSRecordGet((char *) rec_name, rec_val))) {
    Cli_Debug(ERR_RECORD_GET, rec_name);
    Cli_DisplayMgmtAPI_Error(status);
  }
  return status;
}

// Get an integer type records.config variable
TSError
Cli_RecordGetInt(const char *rec_name, TSInt * int_val)
{
  TSError status;
  if ((status = TSRecordGetInt(rec_name, int_val))) {
    Cli_Debug(ERR_RECORD_GET_INT, rec_name);
    Cli_DisplayMgmtAPI_Error(status);
  }
  return status;
}

// Get an counter type records.config variable
TSError
Cli_RecordGetCounter(const char *rec_name, TSCounter * ctr_val)
{
  TSError status;
  if ((status = TSRecordGetCounter(rec_name, ctr_val))) {
    Cli_Debug(ERR_RECORD_GET_COUNTER, rec_name);
    Cli_DisplayMgmtAPI_Error(status);
  }
  return status;
}

// Get a float type records.config variable
TSError
Cli_RecordGetFloat(const char *rec_name, TSFloat * float_val)
{
  TSError status;
  if ((status = TSRecordGetFloat(rec_name, float_val))) {
    Cli_Debug(ERR_RECORD_GET_FLOAT, rec_name);
    Cli_DisplayMgmtAPI_Error(status);
  }
  return status;
}

// Get a string type records.config variable
TSError
Cli_RecordGetString(const char *rec_name, char **string_val)
{
  TSError status;
  if ((status = TSRecordGetString(rec_name, string_val))) {
    Cli_Debug(ERR_RECORD_GET_STRING, rec_name);
    Cli_DisplayMgmtAPI_Error(status);
  }
  return status;
}

// Use a string to set a records.config variable
TSError
Cli_RecordSet(const char *rec_name, const char *rec_value, TSActionNeedT * action_need)
{
  TSError status;
  if ((status = TSRecordSet((char *) rec_name, (TSString) rec_value, action_need))) {
    Cli_Debug(ERR_RECORD_SET, rec_name, rec_value);
    Cli_DisplayMgmtAPI_Error(status);
  }
  return status;
}

// Set an integer type records.config variable
TSError
Cli_RecordSetInt(const char *rec_name, TSInt int_val, TSActionNeedT * action_need)
{
  TSError status;
  if ((status = TSRecordSetInt(rec_name, int_val, action_need))) {
    Cli_Debug(ERR_RECORD_SET_INT, rec_name, int_val);
    Cli_DisplayMgmtAPI_Error(status);
  }
  return status;
}

// Set a float type records.config variable
TSError
Cli_RecordSetFloat(const char *rec_name, TSFloat float_val, TSActionNeedT * action_need)
{
  TSError status;
  if ((status = TSRecordSetFloat(rec_name, float_val, action_need))) {
    Cli_Debug(ERR_RECORD_SET_FLOAT, rec_name, float_val);
    Cli_DisplayMgmtAPI_Error(status);
  }
  return status;
}


// Set a string type records.config variable
TSError
Cli_RecordSetString(const char *rec_name, TSString str_val, TSActionNeedT * action_need)
{
  TSError status;
  if ((status = TSRecordSetString(rec_name, str_val, action_need))) {
    Cli_Debug(ERR_RECORD_SET_STRING, rec_name, str_val);
    Cli_DisplayMgmtAPI_Error(status);
  }
  return status;
}

void
Cli_DisplayMgmtAPI_Error(TSError status)
{
  switch (status) {
  case TS_ERR_OKAY:           // do nothing
    break;
  case TS_ERR_READ_FILE:
    Cli_Printf("\nERROR: Unable to read config file.\n\n");
    break;
  case TS_ERR_WRITE_FILE:
    Cli_Printf("\nERROR: Unable to write config file.\n\n");
    break;
  case TS_ERR_PARSE_CONFIG_RULE:
    Cli_Printf("\nERROR: Unable to parse config file.\n\n");
    break;
  case TS_ERR_INVALID_CONFIG_RULE:
    Cli_Printf("\nERROR: Invalid Configuration Rule in config file.\n\n");
    break;
  case TS_ERR_NET_ESTABLISH:
    Cli_Printf("\nERROR: Unable to establish connection to traffic_manager.\n"
               "       Ability to make configuration changes depends on traffic_manager.\n");
    break;
  case TS_ERR_NET_READ:
    Cli_Printf("\nERROR: Unable to read data from traffic_manager.\n"
               "       Ability to monitor the system changes depends on traffic_manager.\n");
    break;
  case TS_ERR_NET_WRITE:
    Cli_Printf("\nERROR: Unable to write configuration data to traffic_manager.\n"
               "       Ability to make configuration changes depends on traffic_manager.\n");
    break;
  case TS_ERR_NET_EOF:
    Cli_Printf("\nERROR: Unexpected EOF while communicating with traffic_manager.\n"
               "       Ability to make configuration changes depends on traffic_manager.\n");
    break;
  case TS_ERR_NET_TIMEOUT:
    Cli_Printf("\nERROR: Timed-out while communicating with traffic_manager.\n"
               "       Ability to make configuration changes depends on traffic_manager.\n");
    break;
  case TS_ERR_SYS_CALL:
    Cli_Printf("\nERROR: Internal System Call failed.\n\n");
    break;
  case TS_ERR_PARAMS:
    Cli_Printf("\nERROR: Invalid parameters passed to a function.\n\n");
    break;
  case TS_ERR_FAIL:
    Cli_Printf("\nERROR: Invalid parameter specified.\n" "       Check parameters for correct syntax and type.\n\n");
    break;
  default:
    Cli_Printf("\nERROR: Undocumented Error. Status = %d.\n\n", status);
    break;
  }
}

// Retrieve and display contents of a rules file
TSError
Cli_DisplayRules(TSFileNameT fname)
{
  TSError status;
  char *text;
  int size = 0, version = 0;

  if ((status = TSConfigFileRead(fname, &text, &size, &version))) {
    Cli_Debug(ERR_CONFIG_FILE_READ, fname);
    Cli_DisplayMgmtAPI_Error(status);
  } else {
    if (size) {
      // Fix TSqa12220: use printf directly since Cli_Printf may
      // not allocate enough buffer space to display the file contents
      puts(text);
      ats_free(text);
    } else {
      Cli_Printf("no rules\n");
    }
  }

  return status;
}

// Retrieve and use config file from remote URL
TSError
Cli_SetConfigFileFromUrl(TSFileNameT file, const char *url)
{
  char *buf;
  int size = 0;
  int version = -1;
  TSError status;

  Cli_Debug("Cli_SetConfigFileFromUrl: file %d url %s\n", file, url);

  // read config file from Url
  if ((status = TSReadFromUrl((char *) url, NULL, NULL, &buf, &size))) {
    Cli_Debug(ERR_READ_FROM_URL, url);
    Cli_DisplayMgmtAPI_Error(status);
    return status;
  }

  Cli_Debug("Cli_SetConfigFileFromUrl: size %d version %d\n", size, version);

  Cli_Debug("Cli_SetConfigFileFromUrl: buf\n%s\n", buf);

  // write config file
  if ((status = TSConfigFileWrite(file, buf, size, version))) {
    Cli_Debug(ERR_CONFIG_FILE_WRITE, file);
    Cli_DisplayMgmtAPI_Error(status);
    if (size)
      ats_free(buf);
    return status;
  }

  if (size)
    ats_free(buf);

  Cli_Printf("Successfully updated config file.\n");

  return status;
}

// enable recent configuration changes by performing the action specified
// by the action_need value
TSError
Cli_ConfigEnactChanges(TSActionNeedT action_need)
{
  TSError status;

  Cli_Debug("Cli_ConfigEnactChanges: action_need %d\n", action_need);

  switch (action_need) {
  case TS_ACTION_SHUTDOWN:
    Cli_Debug("Cli_ConfigEnactChanges: TS_ACTION_SHUTDOWN\n");
    Cli_Printf("\nHard Restart required.\n"
               "  Change will take effect after next Hard Restart.\n"
               "  Use the \"config:hard-restart\" command to restart now.\n\n");
    break;

  case TS_ACTION_RESTART:
    Cli_Debug("Cli_ConfigEnactChanges: TS_ACTION_RESTART\n");
    Cli_Printf("\nRestart required.\n"
               "  Change will take effect after next Restart.\n"
               "  Use the \"config:restart\" command to restart now.\n\n");
    break;

  case TS_ACTION_DYNAMIC:
    Cli_Debug("Cli_ConfigEnactChanges: TS_ACTION_DYNAMIC\n");
    // no additional action required
    break;

  case TS_ACTION_RECONFIGURE:
    Cli_Debug("Cli_ConfigEnactChanges: TS_ACTION_RECONFIGURE\n");
    status = TSActionDo(TS_ACTION_RECONFIGURE);
    if (status) {
      Cli_Error("\nERROR %d: Failed to reread configuration files.\n\n", status);
      return TS_ERR_FAIL;
    }
    break;

  default:
    Cli_Debug("  Status Message #%d\n", action_need);
    Cli_Error("\nYou may need to use the \"config:hard-restart\" command\n" "to enable this configuration change.\n\n");
    return TS_ERR_OKAY;
  }

  return TS_ERR_OKAY;
}

// evaluate "stringval" and return 1 if "on", otherwise 0
int
Cli_EvalOnOffString(char *stringval)
{
  if (strcmp(stringval, "on") == 0) {
    return 1;
  }
  if (strcmp(stringval, "off") == 0) {
    return 0;
  }

  return -1;
}

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
int
Cli_RecordOnOff_Action(int action, const char *record, const char *on_off)
{
  TSActionNeedT action_need;
  TSError status;
  TSInt int_val;

  switch (action) {
  case RECORD_SET:
    if (on_off) {
      if (!strcasecmp(on_off, "on")) {
        int_val = 1;
      } else if (!strcasecmp(on_off, "off")) {
        int_val = 0;
      } else {
        Cli_Error("Expected \"on\" or \"off\" but got %s\n", on_off);
        return CLI_ERROR;
      }
    } else {
      Cli_Error("Expected <on | off> but got nothing.\n");
      return CLI_ERROR;
    }
    status = Cli_RecordSetInt(record, int_val, &action_need);
    if (status != TS_ERR_OKAY) {
      return status;
    }
    return (Cli_ConfigEnactChanges(action_need));

  case RECORD_GET:
    int_val = -1;
    status = Cli_RecordGetInt(record, &int_val);
    Cli_PrintEnable("", int_val);
    return CLI_OK;
  }
  return CLI_ERROR;
}

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
int
Cli_RecordInt_Action(int action, const char *record, int value)
{
  switch (action) {
  case RECORD_SET:
    {
      TSActionNeedT action_need = TS_ACTION_UNDEFINED;
      TSError status = Cli_RecordSetInt(record, value, &action_need);

      if (status) {
        return status;
      }
      return (Cli_ConfigEnactChanges(action_need));
    }
  case RECORD_GET:
    {
      TSInt value_in = -1;
      TSError status = Cli_RecordGetInt(record, &value_in);

      if (status) {
        return status;
      }
      Cli_Printf("%d\n", value_in);
      return CLI_OK;
    }
  }
  return CLI_ERROR;
}

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
int
Cli_RecordHostname_Action(int action, char *record, char *hostname)
{
  TSError status;
  TSActionNeedT action_need = TS_ACTION_UNDEFINED;
  TSString str_val = NULL;

  switch (action) {
  case RECORD_SET:
    if (IsValidFQHostname(hostname) == CLI_OK) {
      status = Cli_RecordSetString(record, (TSString) hostname, &action_need);

      if (status) {
        return status;
      }
      return (Cli_ConfigEnactChanges(action_need));
    }
    Cli_Error("ERROR: %s is an invalid name.\n", hostname);
    return CLI_ERROR;

  case RECORD_GET:
    status = Cli_RecordGetString(record, &str_val);
    if (status) {
      return status;
    }
    if (str_val)
      Cli_Printf("%s\n", str_val);
    else
      Cli_Printf("none\n");
    return CLI_OK;
  }
  return CLI_ERROR;
}

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
int
Cli_RecordString_Action(int action, const char *record, char *string_val)
{
  TSError status;
  TSActionNeedT action_need = TS_ACTION_UNDEFINED;
  TSString str_val = NULL;

  switch (action) {
  case RECORD_SET:
    status = Cli_RecordSetString(record, (TSString) string_val, &action_need);

    if (status) {
      return status;
    }
    return (Cli_ConfigEnactChanges(action_need));

  case RECORD_GET:
    status = Cli_RecordGetString(record, &str_val);
    if (status) {
      return status;
    }
    if (str_val)
      Cli_Printf("%s\n", str_val);
    else
      Cli_Printf("none\n");
    return CLI_OK;
  }
  return CLI_ERROR;
}

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
int
Cli_ConfigFileURL_Action(TSFileNameT file, const char *filename, const char *url)
{
  TSError status;
  // Retrieve  file from url

  if (url == NULL) {
    Cli_Printf("%s File Rules\n", filename);
    Cli_Printf("----------------------------\n");
    status = Cli_DisplayRules(file);
    return status;
  }
  Cli_Printf("Retrieve and Install %s file from url %s\n", filename, url);

  status = Cli_SetConfigFileFromUrl(file, url);

  return (status);
}

int
cliCheckIfEnabled(const char *command)
{
  if (enable_restricted_commands == false) {
    Cli_Error("\n%s is a restricted command only accessible from enable mode\n\n", command);
    return CLI_ERROR;
  }
  return CLI_OK;
}

int
GetTSDirectory(char *ts_path, size_t ts_path_len)
{

  ink_strlcpy(ts_path, Layout::get()->bindir, ts_path_len);
  if (access(ts_path, R_OK) == -1) {
    Cli_Error("unable to access() '%s': %d, %s\n",
              ts_path, errno, strerror(errno));
    Cli_Error(" Please set correct path in env variable TS_ROOT \n");
    return -1;
  }

  return 0;
}

int
StopTrafficServer()
{
  char ts_path[PATH_NAME_MAX + 1];
  char stop_ts[1024];

  if (GetTSDirectory(ts_path,sizeof(ts_path))) {
    return CLI_ERROR;
  }
  snprintf(stop_ts, sizeof(stop_ts), "%s/stop_traffic_server", ts_path);
  if (system(stop_ts) == -1)
    return CLI_ERROR;

  return 0;
}

int
StartTrafficServer()
{
  char ts_path[PATH_NAME_MAX + 1];
  char start_ts[1024];

  if (GetTSDirectory(ts_path,sizeof(ts_path))) {
    return CLI_ERROR;
  }
  // root user should start_traffic_shell as inktomi user
  if (getuid() == 0) {
    snprintf(start_ts, sizeof(start_ts), "/bin/su - inktomi -c \"%s/start_traffic_server\"", ts_path);
  } else {
    snprintf(start_ts, sizeof(start_ts), "%s/start_traffic_server", ts_path);
  }
  if (system(start_ts) == -1)
    return CLI_ERROR;

  return 0;
}

int
Cli_CheckPluginStatus(TSString plugin)
{

  int match = 0;
  TSCfgContext ctx;
  TSCfgIterState ctx_state;
  TSPluginEle *ele;

  ctx = TSCfgContextCreate(TS_FNAME_PLUGIN);
  if (TSCfgContextGet(ctx) != TS_ERR_OKAY) {
    printf("ERROR READING FILE\n");
  }
  ele = (TSPluginEle *) TSCfgContextGetFirst(ctx, &ctx_state);

  while (ele) {
    if (!strcasecmp(plugin, ele->name)) {
      match = 1;
      break;
    }
    ele = (TSPluginEle *) TSCfgContextGetNext(ctx, &ctx_state);
  }

  if (match) {
    return CLI_OK;
  } else {
    return CLI_ERROR;
  }

}
