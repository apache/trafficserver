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

/*****************************************************************************
 * CoreAPI.cc
 *
 * Implementation of many of INKMgmtAPI functions, but from local side.
 * 
 * 
 ***************************************************************************/

#include "ink_platform.h"
#include "Main.h"
#include "LocalManager.h"
#include "FileManager.h"
#include "Rollback.h"
#include "RecordsConfig.h"
#include "WebMgmtUtils.h"
#include "Diags.h"
#include "ink_hash_table.h"
#include "ExpandingArray.h"
//#include "I_AccCrypto.h"

#include "CoreAPI.h"
#include "CoreAPIShared.h"
#include "CfgContextUtils.h"
#include "EventCallback.h"
#if defined(OEM)
#include "INKMgmtAPI.h"
#include "CfgContextManager.h"
#endif

extern int diags_init;          // from Main.cc

// global variable
CallbackTable *local_event_callbacks;

/*-------------------------------------------------------------------------
 * determine_action_need (forward declaration)
 *-------------------------------------------------------------------------
 * uses the update type that's stored with the record rec_name to determine
 * which type of INKActionNeedT to return.
 */
INKActionNeedT determine_action_need(char *rec_name);

/*-------------------------------------------------------------------------
 * Init
 *-------------------------------------------------------------------------
 * performs any necesary initializations for the local API client, 
 * eg. set up global structures; called by the INKMgmtAPI::INKInit() 
 */
INKError
Init(char *socket_path)
{
  // socket_path should be null; only applies to remote clients
  local_event_callbacks = create_callback_table("local_callbacks");
  if (!local_event_callbacks)
    return INK_ERR_SYS_CALL;

  return INK_ERR_OKAY;
}

/*-------------------------------------------------------------------------
 * Terminate
 *-------------------------------------------------------------------------
 * performs any necesary cleanup of global structures, etc, 
 * for the local API client, 
 */
INKError
Terminate()
{
  delete_callback_table(local_event_callbacks);

  return INK_ERR_OKAY;
}


/*-------------------------------------------------------------------------
 * Diags
 *------------------------------------------------------------------------- 
 * Uses the Traffic Manager diags object to display the diags output.
 */
void
Diags(INKDiagsT mode, const char *fmt, va_list ap)
{
  // Mapping INKDiagsT to Diags.h:DiagsLevel
  // Simple casting would work, but not inflexible
  DiagsLevel level = DL_Undefined;
  switch (mode) {
  case INK_DIAG_DIAG:
    level = DL_Diag;
    break;
  case INK_DIAG_DEBUG:
    level = DL_Debug;
    break;
  case INK_DIAG_STATUS:
    level = DL_Status;
    break;
  case INK_DIAG_NOTE:
    level = DL_Note;
    break;
  case INK_DIAG_WARNING:
    level = DL_Warning;
    break;
  case INK_DIAG_ERROR:
    level = DL_Error;
    break;
  case INK_DIAG_FATAL:
    level = DL_Fatal;
    break;
  case INK_DIAG_ALERT:
    level = DL_Alert;
    break;
  case INK_DIAG_EMERGENCY:
    level = DL_Emergency;
    break;
  default:
    level = DL_Diag;
  }

  if (diags_init) {             // check that diags is initialized
    diags->print_va("INKMgmtAPI", level, NULL, NULL, fmt, ap);
    va_end(ap);
  }
}


/***************************************************************************
 * Control Operations
 ***************************************************************************/
/*-------------------------------------------------------------------------
 * ProxyStateGet
 *-------------------------------------------------------------------------
 * return INK_PROXY_OFF if  Traffic Server is off.
 * return INK_PROXY_ON if Traffic Server is on. 
 */
INKProxyStateT
ProxyStateGet()
{
  if (!lmgmt->processRunning())
    return INK_PROXY_OFF;
  else
    return INK_PROXY_ON;
}

/*-------------------------------------------------------------------------
 * ProxyStateSet
 *-------------------------------------------------------------------------
 * If state == INK_PROXY_ON, will turn on TS (unless it's already running).
 * If steat == INK_PROXY_OFF, will turn off TS (unless it's already off).
 * tsArgs  - (optional) a string with space delimited options that user 
 *            wants to start traffic Server with
 */
INKError
ProxyStateSet(INKProxyStateT state, INKCacheClearT clear)
{
  int i = 0;
  char tsArgs[MAX_BUF_SIZE];
  char *proxy_options;
  bool found;

  memset(tsArgs, 0, MAX_BUF_SIZE);

  switch (state) {
  case INK_PROXY_OFF:
    if (!ProxyShutdown())       // from WebMgmtUtils 
      goto Lerror;              // unsuccessful shutdown
    break;
  case INK_PROXY_ON:
    if (lmgmt->processRunning())        // already on
      break;

    // taken from mgmt2/Main.cc when check the -tsArgs option
    // Update cmd line overrides/environmental overrides/etc
    switch (clear) {
    case INK_CACHE_CLEAR_ON:   // traffic_server -K
      snprintf(tsArgs, sizeof(tsArgs), "-K -M");
      break;
    case INK_CACHE_CLEAR_HOSTDB:       // traffic_server -k
      snprintf(tsArgs, sizeof(tsArgs), "-k -M");
      break;
    case INK_CACHE_CLEAR_OFF:
      // use default tsargs in records.config
      int rec_err = RecGetRecordString_Xmalloc("proxy.config.proxy_binary_opts", &proxy_options);
      found = (rec_err == REC_ERR_OKAY);
      if (!found)
        goto Lerror;

      ink_snprintf(tsArgs, MAX_BUF_SIZE, "%s", proxy_options);
      xfree(proxy_options);
      break;
    }

    if (strlen(tsArgs) > 0) {   /* Passed command line args for proxy */
      if (lmgmt->proxy_options) {
        xfree(lmgmt->proxy_options);
      }
      lmgmt->proxy_options = xstrdup(tsArgs);
      mgmt_log("[ProxyStateSet] Traffic Server Args: '%s'\n", lmgmt->proxy_options);
    }

    lmgmt->run_proxy = true;
    lmgmt->listenForProxy();
    do {
      mgmt_sleep_sec(1);
    } while (i++ < 20 && (lmgmt->proxy_running == 0));
    if (!lmgmt->processRunning())
      goto Lerror;
    break;
  default:
    goto Lerror;
  }

  return INK_ERR_OKAY;

Lerror:
  return INK_ERR_FAIL;          /* failed to set proxy  state */
}

/*-------------------------------------------------------------------------
 * Reconfigure
 *-------------------------------------------------------------------------
 * Rereads configuration files
 */
INKError
Reconfigure()
{
  configFiles->rereadConfig();  // TM rereads
  lmgmt->signalEvent(MGMT_EVENT_PLUGIN_CONFIG_UPDATE, "*");     // TS rereads

  return INK_ERR_OKAY;
}

/*-------------------------------------------------------------------------
 * Restart
 *-------------------------------------------------------------------------
 * Restarts Traffic Manager. Traffic Cop must be running in order to 
 * restart Traffic Manager!!
 */
INKError
Restart(bool cluster)
{
  if (cluster) {                // Enqueue an event to restart the proxies across the cluster
    // this will kill TM completely;traffic_cop will restart TM/TS 
    lmgmt->ccom->sendClusterMessage(CLUSTER_MSG_SHUTDOWN_MANAGER);
  } else {                      // just bounce local proxy
    lmgmt->mgmtShutdown(0);
  }

  return INK_ERR_OKAY;
}

/*-------------------------------------------------------------------------
 * HardRestart
 *-------------------------------------------------------------------------
 * Cannot be executed locally since it requires a restart of Traffic Cop.
 * So just return INK_ERR_FAIL. Should only be called by remote API clients.
 */
INKError
HardRestart()
{
  return INK_ERR_FAIL;
}


/**************************************************************************
 * RECORD OPERATIONS 	     
 *************************************************************************/
/*-------------------------------------------------------------------------
 * MgmtRecordGet
 *-------------------------------------------------------------------------
 * rec_ele has allocated memory already but with all empty fields. 
 * The record info associated with rec_name will be returned in rec_ele.
 */
INKError
MgmtRecordGet(char *rec_name, INKRecordEle * rec_ele)
{
  RecDataT rec_type;
  char rec_val[MAX_BUF_SIZE];
  char *str_val;
  MgmtIntCounter counter_val;
  MgmtInt int_val;
  MgmtLLong llong_val;

  Debug("RecOp", "[MgmtRecordGet] Start\n");

  // initialize the record name 
  rec_ele->rec_name = xstrdup(rec_name);
  memset(rec_val, 0, MAX_BUF_SIZE);

  // get variable type; returns INVALID if invalid rec_name
  rec_type = varType(rec_name);
  switch (rec_type) {
  case RECD_COUNTER:
    rec_ele->rec_type = INK_REC_COUNTER;
    if (!varCounterFromName(rec_name, &(counter_val)))
      return INK_ERR_FAIL;
    rec_ele->counter_val = (INKCounter) counter_val;

    Debug("RecOp", "[MgmtRecordGet] Get Counter Var %s = %d\n", rec_ele->rec_name, rec_ele->counter_val);
    break;

  case RECD_INT:
    rec_ele->rec_type = INK_REC_INT;
    if (!varIntFromName(rec_name, &(int_val)))
      return INK_ERR_FAIL;
    rec_ele->int_val = (INKInt) int_val;

    Debug("RecOp", "[MgmtRecordGet] Get Int Var %s = %d\n", rec_ele->rec_name, rec_ele->int_val);
    break;

  case RECD_LLONG:
    rec_ele->rec_type = INK_REC_LLONG;
    if (!varLLongFromName(rec_name, &(llong_val)))
      return INK_ERR_FAIL;
    rec_ele->llong_val = (INKLLong) llong_val;

    Debug("RecOp", "[MgmtRecordGet] Get LLong Var %s = %lld\n", rec_ele->rec_name, rec_ele->llong_val);
    break;

  case RECD_FLOAT:
    rec_ele->rec_type = INK_REC_FLOAT;
    if (!varFloatFromName(rec_name, &(rec_ele->float_val)))
      return INK_ERR_FAIL;

    Debug("RecOp", "[MgmtRecordGet] Get Float Var %s = %f\n", rec_ele->rec_name, rec_ele->float_val);
    break;

  case RECD_STRING:
    if (!varStrFromName(rec_name, rec_val, MAX_BUF_SIZE))
      return INK_ERR_FAIL;


    if (rec_val[0] != '\0') {   // non-NULL string value 
      // allocate memory & duplicate string value 
      str_val = xstrdup(rec_val);
    } else {
      str_val = xstrdup("NULL");
    }

    rec_ele->rec_type = INK_REC_STRING;
    rec_ele->string_val = str_val;
    Debug("RecOp", "[MgmtRecordGet] Get String Var %s = %s\n", rec_ele->rec_name, rec_ele->string_val);
    break;

  default:                     // UNKOWN TYPE 
    Debug("RecOp", "[MgmtRecordGet] Get Failed : %d is Unknown Var type %s\n", rec_type, rec_name);
    return INK_ERR_FAIL;
  }

  return INK_ERR_OKAY;
}


/*-------------------------------------------------------------------------
 * determine_action_need (HELPER FN)
 *-------------------------------------------------------------------------
 * reads the RecordsConfig info to determine which type of action is needed 
 * when the record rec_name is changed; if the rec_name is invalid, 
 * then returns INK_ACTION_UNDEFINED
 */
INKActionNeedT
determine_action_need(char *rec_name)
{
  int r;                        // index in RecordsConfig[]
  RecordUpdateType update_t;

  // INKqa09916
  // the hashtable lookup will return 0 if there is no binding for the 
  // rec_name in the hashtable
  if (!RecordsConfigIndex->mgmt_hash_table_lookup((char *) rec_name, (void **) &r))
    return INK_ACTION_UNDEFINED;        // failed to find the rec_name in the records table

  update_t = RecordsConfig[r].update;

  switch (update_t) {
  case RU_NULL:                // default:don't know behaviour
    return INK_ACTION_UNDEFINED;

  case RU_REREAD:              // update dynamically by rereading config files
    return INK_ACTION_RECONFIGURE;

  case RU_RESTART_TS:          // requires TS restart
    return INK_ACTION_RESTART;

  case RU_RESTART_TM:          // requirs TM/TS restart
    return INK_ACTION_RESTART;

  case RU_RESTART_TC:          // requires TC/TM/TS restart
    return INK_ACTION_SHUTDOWN;
  default:                     // shouldn't get here actually
    return INK_ACTION_UNDEFINED;
  }
  return INK_ACTION_UNDEFINED;  // ERROR
}


/*-------------------------------------------------------------------------
 * MgmtRecordSet
 *-------------------------------------------------------------------------
 * Uses bool WebMgmtUtils::varSetFromStr(const char*, const char* )
 * Sets the named local manager variable from the value string
 * passed in.  Does the appropriate type conversion on
 * value string to get it to the type of the local manager
 * variable
 *
 *  returns true if the variable was successfully set 
 *   and false otherwise 
 */
INKError
MgmtRecordSet(char *rec_name, char *val, INKActionNeedT * action_need)
{
  Debug("RecOp", "[MgmtRecordSet] Start\n");

  if (!rec_name || !val || !action_need)
    return INK_ERR_PARAMS;

  *action_need = determine_action_need(rec_name);

  if (recordValidityCheck(rec_name, val)) {
    if (varSetFromStr(rec_name, val))
      return INK_ERR_OKAY;
  }

  return INK_ERR_FAIL;
}


/*-------------------------------------------------------------------------
 * MgmtRecordSetInt
 *-------------------------------------------------------------------------
 * Use the record's name to look up the record value and its type.
 * Returns INK_ERR_FAIL if the type is not a valid integer.
 * Converts the integer value to a string and call MgmtRecordSet
 */
INKError
MgmtRecordSetInt(char *rec_name, MgmtInt int_val, INKActionNeedT * action_need)
{
  if (!rec_name || !action_need)
    return INK_ERR_PARAMS;

  // convert int value to string for validity check
  char str_val[MAX_RECORD_SIZE];

  memset(str_val, 0, MAX_RECORD_SIZE);
  snprintf(str_val, sizeof(str_val), "%lld", int_val);

  return MgmtRecordSet(rec_name, str_val, action_need);

  /* INKqa10300
   *action_need = determine_action_need(rec_name); 
   if (recordValidityCheck(rec_name, str_val)) {
   if (varSetInt(rec_name, int_val))
   return INK_ERR_OKAY;
   }
   return INK_ERR_FAIL;
   */
}


/*-------------------------------------------------------------------------
 * MgmtRecordSetCounter
 *-------------------------------------------------------------------------
 * converts the counter_val to a string and uses MgmtRecordSet
 */
INKError
MgmtRecordSetCounter(char *rec_name, MgmtIntCounter counter_val, INKActionNeedT * action_need)
{
  if (!rec_name || !action_need)
    return INK_ERR_PARAMS;

  // convert int value to string for validity check
  char str_val[MAX_RECORD_SIZE];

  memset(str_val, 0, MAX_RECORD_SIZE);
  snprintf(str_val, sizeof(str_val), "%lld", counter_val);

  return MgmtRecordSet(rec_name, str_val, action_need);

  /* INKqa10300
   *action_need = determine_action_need(rec_name);
   if (recordValidityCheck(rec_name, str_val)) {
   if (varSetCounter(rec_name, counter_val)) 
   return INK_ERR_OKAY;
   }
   return INK_ERR_FAIL;
   */
}


/*-------------------------------------------------------------------------
 * MgmtRecordSetFloat
 *-------------------------------------------------------------------------
 * converts the float value to string (to do record validity check)
 * and calls MgmtRecordSet
 */
INKError
MgmtRecordSetFloat(char *rec_name, MgmtFloat float_val, INKActionNeedT * action_need)
{
  if (!rec_name || !action_need)
    return INK_ERR_PARAMS;

  // convert float value to string for validity check
  char str_val[MAX_RECORD_SIZE];

  memset(str_val, 0, MAX_RECORD_SIZE);
  snprintf(str_val, sizeof(str_val), "%f", float_val);

  return MgmtRecordSet(rec_name, str_val, action_need);

  /* INKqa10300
   *action_need = determine_action_need(rec_name);
   if (recordValidityCheck(rec_name, str_val)) { 
   if (varSetFloat(rec_name, float_val))
   return INK_ERR_OKAY;
   }
   return INK_ERR_FAIL; 
   */
}


/*-------------------------------------------------------------------------
 * MgmtRecordSetString
 *-------------------------------------------------------------------------
 * The string value is copied so it's okay to free the string later
 */
INKError
MgmtRecordSetString(char *rec_name, INKString string_val, INKActionNeedT * action_need)
{
  return MgmtRecordSet(rec_name, string_val, action_need);
}


/**************************************************************************
 * FILE OPERATIONS 	     
 *************************************************************************/

/*-------------------------------------------------------------------------
 * ReadFile (MgmtAPILocal::get_lines_from_file)
 *-------------------------------------------------------------------------
 * Purpose: returns copy of the most recent version of the file
 * Input:   file - the config file to read
 *          text - a buffer is allocated on the text char* pointer
 *          size - the size of the buffer is returned
 *          ver  - the version number of file being read
 * Note: CALLEE must DEALLOCATE text memory returned
 */
INKError
ReadFile(INKFileNameT file, char **text, int *size, int *version)
{
  char *fname;
  Rollback *file_rb;
  int ret, old_file_len;
  textBuffer *old_file_content;
  char *old_file_lines;
  version_t ver;

  Debug("FileOp", "[get_lines_from_file] START\n");

#if defined(OEM)
  if (file == INK_FNAME_RMSERVER) {
    char *fileBuff, *NfileBuff, *ps;
    Tokenizer lineTok("\n");
    tok_iter_state lineTok_state;
    char *line;
    long NfileBuffsize;
    int llength;
    char *new_line;

    if ((ReadRmCfgFile(&fileBuff)) != INK_ERR_OKAY) {
      return INK_ERR_FAIL;
    }
    //      Debug("config", "Filebuff size :%d\n", strlen(fileBuff));
    NfileBuffsize = strlen(fileBuff) * 2;
    NfileBuff = new char[NfileBuffsize];
    memset((void *) NfileBuff, 0, NfileBuffsize);
    lineTok.Initialize(fileBuff);

    line = (char *) lineTok.iterFirst(&lineTok_state);
    ps = NfileBuff;
    while (line) {
      if (strstr(line, RM_ADMIN_PORT)) {
        new_line = RmdeXMLize(line, &llength);
        sprintf(ps, "%s", new_line);
        if (!new_line) {
          xfree(new_line);
        }
        ps += llength;
      } else if (strstr(line, RM_LISTTAG_SCU_ADMIN)) {
        sprintf(ps, "#%s\n", line);
        ps += strlen(line) + 2;
        RmReadCfgList(&lineTok, &lineTok_state, &ps, INK_RM_LISTTAG_SCU_ADMIN);
      } else if (strstr(line, RM_LISTTAG_CNN_REALM)) {
        sprintf(ps, "#%s\n", line);
        ps += strlen(line) + 2;
        RmReadCfgList(&lineTok, &lineTok_state, &ps, INK_RM_LISTTAG_CNN_REALM);
      } else if (strstr(line, RM_LISTTAG_ADMIN_FILE)) {
        sprintf(ps, "#%s\n", line);
        ps += strlen(line) + 2;
        RmReadCfgList(&lineTok, &lineTok_state, &ps, INK_RM_LISTTAG_ADMIN_FILE);
      } else if (strstr(line, RM_LISTTAG_AUTH)) {
        sprintf(ps, "#%s\n", line);
        ps += strlen(line) + 2;
        RmReadCfgList(&lineTok, &lineTok_state, &ps, INK_RM_LISTTAG_AUTH);
      } else if (strstr(line, RM_LISTTAG_PROXY)) {
        sprintf(ps, "#%s\n", line);
        ps += strlen(line) + 2;
        RmReadCfgList(&lineTok, &lineTok_state, &ps, INK_RM_LISTTAG_PROXY);
      } else if (strstr(line, RM_LISTTAG_PNA_RDT)) {
        sprintf(ps, "#%s\n", line);
        ps += strlen(line) + 2;
        RmReadCfgList(&lineTok, &lineTok_state, &ps, INK_RM_LISTTAG_PNA_RDT);
      } else {
        sprintf(ps, "#%s\n", line);
        ps += strlen(line) + 2;
      }
      line = (char *) lineTok.iterNext(&lineTok_state);
    }
    delete[]fileBuff;
    *text = NfileBuff;
    *size = NfileBuffsize;
    *version = 1;
    return INK_ERR_OKAY;
  }
#endif

  fname = filename_to_string(file);
  if (!fname)
    return INK_ERR_READ_FILE;

  ret = configFiles->getRollbackObj(fname, &file_rb);
  if (ret != TRUE) {
    Debug("FileOp", "[get_lines_from_file] Can't get Rollback for file: %s\n", fname);
    xfree(fname);
    return INK_ERR_READ_FILE;
  }
  xfree(fname);
  ver = file_rb->getCurrentVersion();
  file_rb->getVersion(ver, &old_file_content);
  *version = ver;

  // don't need to allocate memory b/c "getVersion" allocates memory
#ifdef _WIN32                   // BZ48741
  convertHtmlToUnix(old_file_content->bufPtr());
#endif
  old_file_lines = old_file_content->bufPtr();
  old_file_len = strlen(old_file_lines);

  *text = xstrdup(old_file_lines);      //make copy before deleting textBuffer
  *size = old_file_len;

  delete old_file_content;      // delete textBuffer

  return INK_ERR_OKAY;
}

/*-------------------------------------------------------------------------
 * WriteFile
 *-------------------------------------------------------------------------
 * Purpose: replaces the current file with the file passed in; 
 *  does forceUpdate for Rollback and FileManager so correct file
 *  versioning is maintained
 * Input: file - the config file to write
 *        text - text buffer to write
 *        size - the size of the buffer to write
 *        version - the current version level; new file will have the 
 *                  version number above this one  
 */
INKError
WriteFile(INKFileNameT file, char *text, int size, int version)
{
  char *fname;
  Rollback *file_rb;
  textBuffer *file_content;
  int ret;
  version_t ver;

#if defined(OEM)
  if (file == INK_FNAME_RMSERVER) {
    char *NfileBuff, *ps;
    Tokenizer lineTok("\n");
    tok_iter_state lineTok_state;
    char *line, *new_line;
    int llength;
    long NfileBuffsize;

    lineTok.Initialize(text);
    line = (char *) lineTok.iterFirst(&lineTok_state);
    NfileBuffsize = strlen(text) * 2;
    NfileBuff = new char[NfileBuffsize];
    memset((void *) NfileBuff, 0, NfileBuffsize);
    ps = NfileBuff;
    while (line) {
      if (*line == '#') {
        sprintf(ps, "%s\n", line + 1);  /* need this \n, since the old \n is used by tokenizer */
        ps += strlen(line);
      } else {
        new_line = RmXMLize(line, &llength);
        sprintf(ps, "%s", new_line);
        if (!new_line) {
          xfree(new_line);
        }
        ps += llength;
      }
      line = (char *) lineTok.iterNext(&lineTok_state);
    }
    if (WriteRmCfgFile(NfileBuff) != INK_ERR_OKAY) {
      delete[]NfileBuff;
      return INK_ERR_WRITE_FILE;
    }
    delete[]NfileBuff;
    return INK_ERR_OKAY;
  }
#endif

  fname = filename_to_string(file);
  if (!fname)
    return INK_ERR_WRITE_FILE;

  // get rollback object for config file
  mgmt_log(stderr, "[CfgFileIO::WriteFile] %s\n", fname);
  if (!(configFiles->getRollbackObj(fname, &file_rb))) {
    mgmt_log(stderr, "[CfgFileIO::WriteFile] ERROR getting rollback object\n");
    //goto generate_error_msg;
  }
  xfree(fname);

  // if version < 0 then, just use next version in sequence; 
  // otherwise check if trying to commit an old version
  if (version >= 0) {
    // check that the current version is equal to or less than the version
    // that wants to be written
    ver = file_rb->getCurrentVersion();
    if (ver != version)         // trying to commit an old version
      return INK_ERR_WRITE_FILE;
  }
  // use rollback object to update file with new content
  file_content = new textBuffer(size + 1);
  ret = file_content->copyFrom(text, size);
  if (ret < 0) {
    delete file_content;
    return INK_ERR_WRITE_FILE;
  }

  if ((file_rb->forceUpdate(file_content, -1)) != OK_ROLLBACK) {
    delete file_content;
    return INK_ERR_WRITE_FILE;
  }

  delete file_content;
  return INK_ERR_OKAY;
}

/**************************************************************************
 * EVENTS	     
 *************************************************************************/
/*-------------------------------------------------------------------------
 * EventSignal
 *-------------------------------------------------------------------------
 * LAN: THIS FUNCTION IS HACKED AND INCOMPLETE!!!!! 
 * with the current alarm processor system, the argument list is NOT
 * used; a set description is associated with each alarm already; 
 * be careful because this alarm description is used to keep track
 * of alarms in the current alarm processor
 */
INKError
EventSignal(char *event_name, va_list ap)
{
  //char *text;
  //int id;

  //id = get_event_id(event_name);
  //text = get_event_text(event_name);
  //lmgmt->alarm_keeper->signalAlarm(id, text, NULL);

  return INK_ERR_OKAY;
}

/*-------------------------------------------------------------------------
 * EventResolve
 *-------------------------------------------------------------------------
 * Resolves the event of the given event_name. If the event is already
 * unresolved, just return INK_ERR_OKAY. 

 */
INKError
EventResolve(char *event_name)
{
  alarm_t a;

  if (!event_name)
    return INK_ERR_PARAMS;

  a = get_event_id(event_name);
  lmgmt->alarm_keeper->resolveAlarm(a);

  return INK_ERR_OKAY;
}

/*-------------------------------------------------------------------------
 * ActiveEventGetMlt
 *-------------------------------------------------------------------------
 * returns failure, and an incomplete active_alarms list if any of 
 * functions fail for a single event
 * note: returns list of local alarms at that instant of fn call (snapshot)
 */
INKError
ActiveEventGetMlt(LLQ * active_events)
{
  InkHashTable *event_ht;
  InkHashTableEntry *entry;
  InkHashTableIteratorState iterator_state;
  int event_id;
  char *event_name;

  if (!active_events)
    return INK_ERR_PARAMS;

  // Alarms stores a hashtable of all active alarms where: 
  // key = alarm_t, 
  // value = alarm_description defined in Alarms.cc alarmText[] array
  event_ht = lmgmt->alarm_keeper->getLocalAlarms();

  // iterate through hash-table and insert event_name's into active_events list
  for (entry = ink_hash_table_iterator_first(event_ht, &iterator_state);
       entry != NULL; entry = ink_hash_table_iterator_next(event_ht, &iterator_state)) {

    char *key = (char *) ink_hash_table_entry_key(event_ht, entry);

    // convert key to int; insert into llQ
    event_id = ink_atoi(key);
    event_name = get_event_name(event_id);
    if (event_name) {
      if (!enqueue(active_events, event_name))  // returns true if successful
        return INK_ERR_FAIL;
    }
  }

  return INK_ERR_OKAY;
}

/*-------------------------------------------------------------------------
 * EventIsActive
 *-------------------------------------------------------------------------
 * Sets *is_current to true if the event named event_name is currently
 * unresolved; otherwise sets *is_current to false.
 */
INKError
EventIsActive(char *event_name, bool * is_current)
{
  alarm_t a;

  if (!event_name || !is_current)
    return INK_ERR_PARAMS;

  a = get_event_id(event_name);
  // consider an invalid event_name an error
  if (a < 0)
    return INK_ERR_PARAMS;
  if (lmgmt->alarm_keeper->isCurrentAlarm(a))
    *is_current = true;         // currently an event
  else
    *is_current = false;

  return INK_ERR_OKAY;
}

/*-------------------------------------------------------------------------
 * EventSignalCbRegister
 *-------------------------------------------------------------------------
 * This differs from the remote side callback registered. Technically, I think
 * we need to redesign the alarm processor before we can handle the callback
 * functionality we want to accomplish. Because currently the alarm processor
 * only allow registering callbacks for general alarms. 
 * Mimic remote side and have a separate structure (eg. hashtable) of
 * event callback functions for each type of event. The functions are also
 * stored in the the hashtable, not in the TM alarm processor model
 */
INKError
EventSignalCbRegister(char *event_name, INKEventSignalFunc func, void *data)
{
  return cb_table_register(local_event_callbacks, event_name, func, data, NULL);

}

/*-------------------------------------------------------------------------
 * EventSignalCbUnregister
 *-------------------------------------------------------------------------
 * Removes the callback function from the local side CallbackTable
 */
INKError
EventSignalCbUnregister(char *event_name, INKEventSignalFunc func)
{
  return cb_table_unregister(local_event_callbacks, event_name, func);
}

/***************************************************************************
 * Snapshots
 ***************************************************************************/
INKError
SnapshotTake(char *snapshot_name)
{
  char *snapDirFromRecordsConf;
  bool found;

  if (!snapshot_name)
    return INK_ERR_PARAMS;

  int rec_err = RecGetRecordString_Xmalloc("proxy.config.snapshot_dir", &snapDirFromRecordsConf);
  found = (rec_err == REC_ERR_OKAY);

  ink_assert(found);

  if (snapDirFromRecordsConf[0] != '/') {
    char *config_dir;
    int rec_err = RecGetRecordString_Xmalloc("proxy.config.config_dir", &config_dir);
    found = (rec_err == REC_ERR_OKAY);
    ink_assert(found);

    char *snap_dir_cpy = strdup(snapDirFromRecordsConf);
    int newLen;

    newLen = strlen(snap_dir_cpy) + strlen(config_dir) + 2;
    xfree(snapDirFromRecordsConf);
    snapDirFromRecordsConf = new char[newLen];
    ink_assert(snapDirFromRecordsConf != NULL);
    snprintf(snapDirFromRecordsConf, newLen, "%s%s%s", config_dir, DIR_SEP, snap_dir_cpy);
    xfree(config_dir);
    xfree(snap_dir_cpy);
  }

  SnapResult result = configFiles->takeSnap(snapshot_name, snapDirFromRecordsConf);
  xfree(snapDirFromRecordsConf);
  if (result != SNAP_OK)
    return INK_ERR_FAIL;
  else
    return INK_ERR_OKAY;
}

INKError
SnapshotRestore(char *snapshot_name)
{
  char *snapDirFromRecordsConf;
  bool found;

  if (!snapshot_name)
    return INK_ERR_PARAMS;

  int rec_err = RecGetRecordString_Xmalloc("proxy.config.snapshot_dir", &snapDirFromRecordsConf);
  found = (rec_err == REC_ERR_OKAY);
  ink_assert(found);

  if (snapDirFromRecordsConf[0] != '/') {
    char *config_dir;
    int rec_err = RecGetRecordString_Xmalloc("proxy.config.config_dir", &config_dir);
    found = (rec_err == REC_ERR_OKAY);
    ink_assert(found);

    char *snap_dir_cpy = strdup(snapDirFromRecordsConf);
    int newLen;

    newLen = strlen(snap_dir_cpy) + strlen(config_dir) + 2;
    xfree(snapDirFromRecordsConf);
    snapDirFromRecordsConf = new char[newLen];
    ink_assert(snapDirFromRecordsConf != NULL);
    snprintf(snapDirFromRecordsConf, newLen, "%s%s%s", config_dir, DIR_SEP, snap_dir_cpy);
    xfree(config_dir);
    xfree(snap_dir_cpy);
  }

  SnapResult result = configFiles->restoreSnap(snapshot_name, snapDirFromRecordsConf);
  xfree(snapDirFromRecordsConf);
  if (result != SNAP_OK)
    return INK_ERR_FAIL;
  else
    return INK_ERR_OKAY;
}

INKError
SnapshotRemove(char *snapshot_name)
{
  char *snapDirFromRecordsConf;
  bool found;
  if (!snapshot_name)
    return INK_ERR_PARAMS;

  int rec_err = RecGetRecordString_Xmalloc("proxy.config.snapshot_dir", &snapDirFromRecordsConf);
  found = (rec_err == REC_ERR_OKAY);
  ink_assert(found);

  if (snapDirFromRecordsConf[0] != '/') {
    char *config_dir;
    rec_err = RecGetRecordString_Xmalloc("proxy.config.config_dir", &config_dir);
    found = (rec_err == REC_ERR_OKAY);
    ink_assert(found);

    char *snap_dir_cpy = strdup(snapDirFromRecordsConf);
    int newLen;

    newLen = strlen(snap_dir_cpy) + strlen(config_dir) + 2;
    xfree(snapDirFromRecordsConf);
    snapDirFromRecordsConf = new char[newLen];
    ink_assert(snapDirFromRecordsConf != NULL);
    snprintf(snapDirFromRecordsConf, newLen, "%s%s%s", config_dir, DIR_SEP, snap_dir_cpy);
    xfree(config_dir);
    xfree(snap_dir_cpy);
  }

  SnapResult result = configFiles->removeSnap(snapshot_name, snapDirFromRecordsConf);
  xfree(snapDirFromRecordsConf);
  if (result != SNAP_OK)
    return INK_ERR_FAIL;
  else
    return INK_ERR_OKAY;
}

/* based on FileManager.cc::displaySnapOption() */
INKError
SnapshotGetMlt(LLQ * snapshots)
{
  ExpandingArray snap_list(25, true);
  SnapResult snap_result;
  int num_snaps;
  char *snap_name;

  snap_result = configFiles->WalkSnaps(&snap_list);
  if (snap_result != SNAP_OK)
    return INK_ERR_FAIL;

  num_snaps = snap_list.getNumEntries();
  for (int i = 0; i < num_snaps; i++) {
    snap_name = (char *) (snap_list[i]);
    if (snap_name)
      enqueue(snapshots, xstrdup(snap_name));
  }

  return INK_ERR_OKAY;
}

/*-------------------------------------------------------------------------
 * StatsReset
 *-------------------------------------------------------------------------
 * Iterates through the RecordsConfig table, and for all stats
 * (type PROCESS, NODE, CLUSTER), sets them back to their default value 
 * If one stat fails to be set correctly, then continues onto next one, 
 * but will return INK_ERR_FAIL. Only returns INK_ERR_OKAY if all
 * stats are set back to defaults succesfully. 
 */
INKError
StatsReset()
{
  bool okay = true;

  // iterate through all records in RecordsConfig
  int i = 0;
  while (RecordsConfig[i].value_type != INVALID) {
    if (RecordsConfig[i].type == PROCESS || RecordsConfig[i].type == NODE || RecordsConfig[i].type == CLUSTER) {
      // stats variable, so restore default using table value
      if (varSetFromStr(RecordsConfig[i].name, RecordsConfig[i].value) == false)
        okay = false;
    }
    i++;
  }

  if (okay)
    return INK_ERR_OKAY;
  else
    return INK_ERR_FAIL;
}

/*-------------------------------------------------------------------------
 * EncryptToFile
 *-------------------------------------------------------------------------
 * Encrypts the password and stores the encrypted password in the
 * location specified by "filepath"  
 */
INKError
EncryptToFile(char *passwd, char *filepath)
{
  //AuthString fileAuthStr(filepath);
  //AuthString passwdAuthStr(passwd);
  /*if (!AccCrypto::encryptToFile(fileAuthStr, passwdAuthStr)) {
    Debug("config", "[EncryptToFile] Failed to encrypt password");
    return INK_ERR_FAIL;
  }*/

  return INK_ERR_OKAY;
}


/* Network conifguration functions */

/*-------------------------------------------------------------
 * rmserver.cfg 
 *-------------------------------------------------------------*/
#if defined(OEM)

INKString
GetRmCfgPath()
{
  INKString path;
#ifndef _WIN32

  char buf[1024];
  FILE *ts_file, *rec_file, *pid_file;
  int i = 0, num_args = 0, found_pid_path = 0;
  char buffer[1024];
  char proxy_restart_cmd[1024];
  char ts_base_dir[1024];
  char rec_config[1024];
  static char *restart_cmd_args[100];
  INKString tmp;
  INKString temp;
  INKString tmp2;
  char *env_path;

  // INST will set ROOT and INST_ROOT properly, try ROOT first
  if ((env_path = getenv("ROOT")) || (env_path = getenv("INST_ROOT"))) {
    strcnpy(ts_base_dir, env_path, 1023);
  } else {
    if ((ts_file = fopen("/etc/traffic_server", "r")) == NULL) {
      strcpy(ts_base_dir, "/home/trafficserver");
    } else {
      fgets(buffer, 1024, ts_file);
      fclose(ts_file);
      while (!isspace(buffer[i])) {
        ts_base_dir[i] = buffer[i];
        i++;
      }
      ts_base_dir[i] = '\0';
    }
  }

  sprintf(rec_config, "%s/conf/yts/records.config", ts_base_dir);

  if ((rec_file = fopen(rec_config, "r")) == NULL) {
    fprintf(stderr, "Error: unable to open %s.\n", rec_config);
    return NULL;
  }

  while (fgets(buffer, 1024, rec_file) != NULL) {
    if (strstr(buffer, "proxy.config.rni.proxy_restart_cmd") != NULL) {
      if ((tmp = strstr(buffer, "STRING ")) != NULL) {
        tmp += strlen("STRING ");
        for (i = 0; tmp[i] != '\n' && tmp[i] != '\0'; i++) {
          proxy_restart_cmd[i] = tmp[i];
        }
        proxy_restart_cmd[i] = '\0';

        tmp = proxy_restart_cmd;
        while ((tmp2 = strtok(tmp, " \t")) != NULL) {
          restart_cmd_args[num_args++] = strdup(tmp2);
          tmp = NULL;
        }
        restart_cmd_args[num_args] = NULL;
      }
    }
  }
  fclose(rec_file);

  path = xstrdup(restart_cmd_args[num_args - 1]);
  //    printf("rmservercfgpath: %s \n",path);
  if (!path) {
    fprintf(stderr, "Error[get_rmserver_path]:rmserver.cfg path not found!\n");
        /***************************************************************
	  strcpy(temp,path);
	  strcpy(rmserver_path,path);
	  
	  tmp = temp;
	  if ((tmp1 = strstr(tmp, "/rmserver.cfg")) != NULL) 
	  tmp[tmp1-tmp] = '\0';
	  if(!tmp) {
	  fprintf(stderr,"Error:rmserver.cfg not found in identified path!\n"); 
	  return INK_ERR_FAIL;
	  ***************************************************************/
    return NULL;
  }

#endif
  return path;
}

/*
DeXMLize the line and get rid of the quotes
*/

INKString
RmdeXMLize(INKString XMLline, int *lengthp)
{
  INKString linecp;
  INKString head;
  INKString tail;
  INKString quote_1;

  linecp = xstrdup(XMLline);
  memset((void *) linecp, 0, strlen(linecp));
  head = strstr(XMLline, "<Var");
  tail = strstr(XMLline, "\"/>");
  quote_1 = strstr(XMLline, "\"");

  if (head && tail && quote_1) {
    memcpy(linecp, (void *) (head + 4), (int) quote_1 - (int) head - 4);
    memcpy(linecp + (int) quote_1 - (int) head - 4, (void *) (quote_1 + 1), (int) tail - (int) quote_1 - 1);
    linecp = strcat(linecp, "\n");
    *lengthp = strlen(linecp);
  } else {
    *lengthp = 0;
  }
  return linecp;
}

INKString
RmXMLize(INKString line, int *lengthp)
{
  INKString XMLline;

  XMLline = new char[strlen(line) + 9];
  memset(XMLline, 0, strlen(line) + 9);
  sprintf(XMLline, "%s%s%s\n", "<Var ", line, "/>");
  *lengthp = strlen(XMLline);
  //Debug ("CoreAPI", "XMLline as %s\n", XMLline);
  return XMLline;
}

INKError
ReadRmCfgFile(char **Buffp)
{

  INKString path;
  long fsize;
  FILE *fp;

  //read the rmserver.cfg file
  path = GetRmCfgPath();
  if (!path) {
    fprintf(stderr, "Error:rmserver.cfg path not found!\n");
    return INK_ERR_FAIL;
  }
  if ((fp = fopen(path, "r")) == NULL) {
    fprintf(stderr, "Error: unable to open %s\n", path);
    return INK_ERR_READ_FILE;
  }
  if (path) {
    xfree(path);
  }
  /* Get the file size to alloc an text buffer */
  if (fseek(fp, 0, SEEK_END) < 0) {
    mgmt_fatal(stderr, "[CoreAPI::ReadFile] Failed seek in conf file: '%s'\n", path);
    return INK_ERR_FAIL;
  } else {
    fsize = ftell(fp);
    rewind(fp);
    *Buffp = new char[fsize + 1];
    memset(*Buffp, 0, fsize + 1);
    if (fread(*Buffp, sizeof(char), fsize, fp) == (size_t) fsize) {
      fclose(fp);
      return INK_ERR_OKAY;
    } else {
      fclose(fp);
      return INK_ERR_READ_FILE;
    }
  }
}

/*
  This function process the list in XML file with Certain interested Tags.
 */
void
RmReadCfgList(Tokenizer * Tp, tok_iter_state * Tstate, char **buffp, INKRmServerListT ListType)
{

  char *line, *new_line;
  bool deXMLed;
  int llength;

  line = (char *) Tp->iterNext(Tstate);
  do {
    deXMLed = false;
    switch (ListType) {
    case INK_RM_LISTTAG_SCU_ADMIN:
    case INK_RM_LISTTAG_CNN_REALM:
    case INK_RM_LISTTAG_ADMIN_FILE:
    case INK_RM_LISTTAG_AUTH:
      /* process the REALM */
      if (strstr(line, RM_REALM)) {
        new_line = RmdeXMLize(line, &llength);
        deXMLed = true;
      }
      break;
    case INK_RM_LISTTAG_PROXY:
      /* prcess the RTSP/PNA/MAXs */
      if (strstr(line, RM_PNA_PORT) ||
          strstr(line, RM_MAX_PROXY_CONN) || strstr(line, RM_MAX_GWBW) || strstr(line, RM_MAX_PXBW)) {
        new_line = RmdeXMLize(line, &llength);
        deXMLed = true;
      }
      break;
    case INK_RM_LISTTAG_PNA_RDT:
      /* process the PNARedirector */
      if (strstr(line, RM_PNA_RDT_PORT) || strstr(line, RM_PNA_RDT_IP)) {
        new_line = RmdeXMLize(line, &llength);
        deXMLed = true;
      }
      break;
    }
    if (!deXMLed) {
      sprintf(*buffp, "#%s\n", line);
      *buffp += (strlen(line) + 2);
    } else {
      sprintf(*buffp, "%s", new_line);
      if (!new_line) {
        xfree(new_line);
      }
      *buffp += llength;
    }
    line = (char *) Tp->iterNext(Tstate);
  } while (!strstr(line, "/List"));

  sprintf(*buffp, "#%s\n", line);
  *buffp += (strlen(line) + 2);
  return;
}

INKError
WriteRmCfgFile(char *text)
{

  INKString path;
  FILE *fp;
  INKError rc;

  //read the rmserver.cfg file
  path = GetRmCfgPath();
  if (!path) {
    fprintf(stderr, "Error:rmserver.cfg path not found!\n");
    return INK_ERR_FAIL;
  }
  if ((fp = fopen(path, "w")) == NULL) {
    fprintf(stderr, "Error: unable to open %s\n", path);
    return INK_ERR_READ_FILE;
  }
  long fsize = strlen(text);
  if (fwrite((void *) text, sizeof(char), fsize, fp) == (size_t) fsize) {
    rc = INK_ERR_OKAY;
  } else {
    rc = INK_ERR_WRITE_FILE;
  }
  fclose(fp);
  return rc;
}

#endif
