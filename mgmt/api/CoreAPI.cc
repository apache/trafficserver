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
 * Implementation of many of TSMgmtAPI functions, but from local side.
 *
 *
 ***************************************************************************/

#include "ink_platform.h"
#include "Main.h"
#include "MgmtUtils.h"
#include "LocalManager.h"
#include "FileManager.h"
#include "Rollback.h"
#include "WebMgmtUtils.h"
#include "Diags.h"
#include "ink_hash_table.h"
#include "ExpandingArray.h"
//#include "I_AccCrypto.h"

#include "CoreAPI.h"
#include "CoreAPIShared.h"
#include "CfgContextUtils.h"
#include "EventCallback.h"
#include "I_Layout.h"

extern int diags_init;          // from Main.cc

// global variable
CallbackTable *local_event_callbacks;

/*-------------------------------------------------------------------------
 * Init
 *-------------------------------------------------------------------------
 * performs any necesary initializations for the local API client,
 * eg. set up global structures; called by the TSMgmtAPI::TSInit()
 */
TSError
Init(const char * /* socket_path ATS_UNUSED */, TSInitOptionT options)
{
  // socket_path should be null; only applies to remote clients
  if (0 == (options & TS_MGMT_OPT_NO_EVENTS)) {
    local_event_callbacks = create_callback_table("local_callbacks");
    if (!local_event_callbacks)
      return TS_ERR_SYS_CALL;
  } else {
    local_event_callbacks = NULL;
  }

  return TS_ERR_OKAY;
}

/*-------------------------------------------------------------------------
 * Terminate
 *-------------------------------------------------------------------------
 * performs any necesary cleanup of global structures, etc,
 * for the local API client,
 */
TSError
Terminate()
{
  delete_callback_table(local_event_callbacks);

  return TS_ERR_OKAY;
}


/*-------------------------------------------------------------------------
 * Diags
 *-------------------------------------------------------------------------
 * Uses the Traffic Manager diags object to display the diags output.
 */
void
Diags(TSDiagsT mode, const char *fmt, va_list ap)
{
  // Mapping TSDiagsT to Diags.h:DiagsLevel
  // Simple casting would work, but not inflexible
  DiagsLevel level = DL_Undefined;
  switch (mode) {
  case TS_DIAG_DIAG:
    level = DL_Diag;
    break;
  case TS_DIAG_DEBUG:
    level = DL_Debug;
    break;
  case TS_DIAG_STATUS:
    level = DL_Status;
    break;
  case TS_DIAG_NOTE:
    level = DL_Note;
    break;
  case TS_DIAG_WARNING:
    level = DL_Warning;
    break;
  case TS_DIAG_ERROR:
    level = DL_Error;
    break;
  case TS_DIAG_FATAL:
    level = DL_Fatal;
    break;
  case TS_DIAG_ALERT:
    level = DL_Alert;
    break;
  case TS_DIAG_EMERGENCY:
    level = DL_Emergency;
    break;
  default:
    level = DL_Diag;
  }

  if (diags_init) {             // check that diags is initialized
    diags->print_va("TSMgmtAPI", level, NULL, fmt, ap);
    va_end(ap);
  }
}


/***************************************************************************
 * Control Operations
 ***************************************************************************/
/*-------------------------------------------------------------------------
 * ProxyStateGet
 *-------------------------------------------------------------------------
 * return TS_PROXY_OFF if  Traffic Server is off.
 * return TS_PROXY_ON if Traffic Server is on.
 */
TSProxyStateT
ProxyStateGet()
{
  if (!lmgmt->processRunning())
    return TS_PROXY_OFF;
  else
    return TS_PROXY_ON;
}

/*-------------------------------------------------------------------------
 * ProxyStateSet
 *-------------------------------------------------------------------------
 * If state == TS_PROXY_ON, will turn on TS (unless it's already running).
 * If steat == TS_PROXY_OFF, will turn off TS (unless it's already off).
 * tsArgs  - (optional) a string with space delimited options that user
 *            wants to start traffic Server with
 */
TSError
ProxyStateSet(TSProxyStateT state, TSCacheClearT clear)
{
  int i = 0;
  char tsArgs[MAX_BUF_SIZE];
  char *proxy_options;
  bool found;

  memset(tsArgs, 0, MAX_BUF_SIZE);

  switch (state) {
  case TS_PROXY_OFF:
    if (!ProxyShutdown())       // from WebMgmtUtils
      goto Lerror;              // unsuccessful shutdown
    break;
  case TS_PROXY_ON:
    if (lmgmt->processRunning())        // already on
      break;

    // taken from mgmt/Main.cc when check the -tsArgs option
    // Update cmd line overrides/environmental overrides/etc
    switch (clear) {
    case TS_CACHE_CLEAR_ON:   // traffic_server -K
      snprintf(tsArgs, sizeof(tsArgs), "-K -M");
      break;
    case TS_CACHE_CLEAR_HOSTDB:       // traffic_server -k
      snprintf(tsArgs, sizeof(tsArgs), "-k -M");
      break;
    case TS_CACHE_CLEAR_OFF:
      // use default tsargs in records.config
      int rec_err = RecGetRecordString_Xmalloc("proxy.config.proxy_binary_opts", &proxy_options);
      found = (rec_err == REC_ERR_OKAY);
      if (!found)
        goto Lerror;

      snprintf(tsArgs, MAX_BUF_SIZE, "%s", proxy_options);
      ats_free(proxy_options);
      break;
    }

    if (strlen(tsArgs) > 0) {   /* Passed command line args for proxy */
      ats_free(lmgmt->proxy_options);
      lmgmt->proxy_options = ats_strdup(tsArgs);
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

  return TS_ERR_OKAY;

Lerror:
  return TS_ERR_FAIL;          /* failed to set proxy  state */
}

/*-------------------------------------------------------------------------
 * Reconfigure
 *-------------------------------------------------------------------------
 * Rereads configuration files
 */
TSError
Reconfigure()
{
  configFiles->rereadConfig();  // TM rereads
  lmgmt->signalEvent(MGMT_EVENT_PLUGIN_CONFIG_UPDATE, "*");     // TS rereads

  return TS_ERR_OKAY;
}

/*-------------------------------------------------------------------------
 * Restart
 *-------------------------------------------------------------------------
 * Restarts Traffic Manager. Traffic Cop must be running in order to
 * restart Traffic Manager!!
 */
TSError
Restart(bool cluster)
{
  if (cluster) {                // Enqueue an event to restart the proxies across the cluster
    // this will kill TM completely;traffic_cop will restart TM/TS
    lmgmt->ccom->sendClusterMessage(CLUSTER_MSG_SHUTDOWN_MANAGER);
  } else {                      // just bounce local proxy
    lmgmt->mgmtShutdown();
  }

  return TS_ERR_OKAY;
}

/*-------------------------------------------------------------------------
 * HardRestart
 *-------------------------------------------------------------------------
 * Cannot be executed locally since it requires a restart of Traffic Cop.
 * So just return TS_ERR_FAIL. Should only be called by remote API clients.
 */
TSError
HardRestart()
{
  return TS_ERR_FAIL;
}


/*-------------------------------------------------------------------------
 * Bouncer
 *-------------------------------------------------------------------------
 * Bounces traffic_server process(es).
 */
TSError
Bounce(bool cluster)
{
  if (cluster) {
    lmgmt->ccom->sendClusterMessage(CLUSTER_MSG_BOUNCE_PROCESS);
  } else {
    lmgmt->processBounce();
  }

  return TS_ERR_OKAY;
}

/*-------------------------------------------------------------------------
 * StorageDeviceCmdOffline
 *-------------------------------------------------------------------------
 * Disable a storage device.
 * [amc] I don't think this is called but is required because of the way the
 * CoreAPI is linked (it must match the remote CoreAPI signature so compiling
 * this source or CoreAPIRemote.cc yields the same set of symbols).
 */
TSError
StorageDeviceCmdOffline(char const* dev)
{
  lmgmt->signalEvent(MGMT_EVENT_STORAGE_DEVICE_CMD_OFFLINE, dev);
  return TS_ERR_OKAY;
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
TSError
MgmtRecordGet(const char *rec_name, TSRecordEle * rec_ele)
{
  RecDataT rec_type;
  char rec_val[MAX_BUF_SIZE];
  char *str_val;
  MgmtIntCounter counter_val;
  MgmtInt int_val;

  Debug("RecOp", "[MgmtRecordGet] Start\n");

  // initialize the record name
  rec_ele->rec_name = ats_strdup(rec_name);
  memset(rec_val, 0, MAX_BUF_SIZE);

  // get variable type; returns INVALID if invalid rec_name
  rec_type = varType(rec_name);
  switch (rec_type) {
  case RECD_COUNTER:
    rec_ele->rec_type = TS_REC_COUNTER;
    if (!varCounterFromName(rec_name, &(counter_val)))
      return TS_ERR_FAIL;
    rec_ele->counter_val = (TSCounter) counter_val;

    Debug("RecOp", "[MgmtRecordGet] Get Counter Var %s = %" PRId64"\n", rec_ele->rec_name, rec_ele->counter_val);
    break;

  case RECD_INT:
    rec_ele->rec_type = TS_REC_INT;
    if (!varIntFromName(rec_name, &(int_val)))
      return TS_ERR_FAIL;
    rec_ele->int_val = (TSInt) int_val;

    Debug("RecOp", "[MgmtRecordGet] Get Int Var %s = %" PRId64"\n", rec_ele->rec_name, rec_ele->int_val);
    break;

  case RECD_FLOAT:
    rec_ele->rec_type = TS_REC_FLOAT;
    if (!varFloatFromName(rec_name, &(rec_ele->float_val)))
      return TS_ERR_FAIL;

    Debug("RecOp", "[MgmtRecordGet] Get Float Var %s = %f\n", rec_ele->rec_name, rec_ele->float_val);
    break;

  case RECD_STRING:
    if (!varStrFromName(rec_name, rec_val, MAX_BUF_SIZE))
      return TS_ERR_FAIL;


    if (rec_val[0] != '\0') {   // non-NULL string value
      // allocate memory & duplicate string value
      str_val = ats_strdup(rec_val);
    } else {
      str_val = ats_strdup("NULL");
    }

    rec_ele->rec_type = TS_REC_STRING;
    rec_ele->string_val = str_val;
    Debug("RecOp", "[MgmtRecordGet] Get String Var %s = %s\n", rec_ele->rec_name, rec_ele->string_val);
    break;

  default:                     // UNKOWN TYPE
    Debug("RecOp", "[MgmtRecordGet] Get Failed : %d is Unknown Var type %s\n", rec_type, rec_name);
    return TS_ERR_FAIL;
  }

  return TS_ERR_OKAY;
}

// This is not implemented in the Core side of the API because we don't want
// to buffer up all the matching records in memory. We stream the records
// directory onto the management socket in handle_record_match(). This stub
// is just here for link time dependencies.
TSError
MgmtRecordGetMatching(const char * /* regex */, TSList /* rec_vals */)
{
  return TS_ERR_FAIL;
}

/*-------------------------------------------------------------------------
 * reads the RecordsConfig info to determine which type of action is needed
 * when the record rec_name is changed; if the rec_name is invalid,
 * then returns TS_ACTION_UNDEFINED
 */
TSActionNeedT
determine_action_need(const char *rec_name)
{
  RecUpdateT update_t;

  if (REC_ERR_OKAY != RecGetRecordUpdateType(rec_name, &update_t))
    return TS_ACTION_UNDEFINED;

  switch (update_t) {
  case RECU_NULL:                // default:don't know behaviour
    return TS_ACTION_UNDEFINED;

  case RECU_DYNAMIC:             // update dynamically by rereading config files
    return TS_ACTION_RECONFIGURE;

  case RECU_RESTART_TS:          // requires TS restart
    return TS_ACTION_RESTART;

  case RECU_RESTART_TM:          // requires TM/TS restart
    return TS_ACTION_RESTART;

  case RECU_RESTART_TC:          // requires TC/TM/TS restart
    return TS_ACTION_SHUTDOWN;

  default:                     // shouldn't get here actually
    return TS_ACTION_UNDEFINED;
  }

  return TS_ACTION_UNDEFINED;  // ERROR
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
TSError
MgmtRecordSet(const char *rec_name, const char *val, TSActionNeedT * action_need)
{
  Debug("RecOp", "[MgmtRecordSet] Start\n");

  if (!rec_name || !val || !action_need)
    return TS_ERR_PARAMS;

  *action_need = determine_action_need(rec_name);

  if (recordValidityCheck(rec_name, val)) {
    if (varSetFromStr(rec_name, val))
      return TS_ERR_OKAY;
  }

  return TS_ERR_FAIL;
}


/*-------------------------------------------------------------------------
 * MgmtRecordSetInt
 *-------------------------------------------------------------------------
 * Use the record's name to look up the record value and its type.
 * Returns TS_ERR_FAIL if the type is not a valid integer.
 * Converts the integer value to a string and call MgmtRecordSet
 */
TSError
MgmtRecordSetInt(const char *rec_name, MgmtInt int_val, TSActionNeedT * action_need)
{
  if (!rec_name || !action_need)
    return TS_ERR_PARAMS;

  // convert int value to string for validity check
  char str_val[MAX_RECORD_SIZE];

  memset(str_val, 0, MAX_RECORD_SIZE);
  snprintf(str_val, sizeof(str_val), "%" PRId64 "", int_val);

  return MgmtRecordSet(rec_name, str_val, action_need);
}


/*-------------------------------------------------------------------------
 * MgmtRecordSetCounter
 *-------------------------------------------------------------------------
 * converts the counter_val to a string and uses MgmtRecordSet
 */
TSError
MgmtRecordSetCounter(const char *rec_name, MgmtIntCounter counter_val, TSActionNeedT * action_need)
{
  if (!rec_name || !action_need)
    return TS_ERR_PARAMS;

  // convert int value to string for validity check
  char str_val[MAX_RECORD_SIZE];

  memset(str_val, 0, MAX_RECORD_SIZE);
  snprintf(str_val, sizeof(str_val), "%" PRId64 "", counter_val);

  return MgmtRecordSet(rec_name, str_val, action_need);
}


/*-------------------------------------------------------------------------
 * MgmtRecordSetFloat
 *-------------------------------------------------------------------------
 * converts the float value to string (to do record validity check)
 * and calls MgmtRecordSet
 */
TSError
MgmtRecordSetFloat(const char *rec_name, MgmtFloat float_val, TSActionNeedT * action_need)
{
  if (!rec_name || !action_need)
    return TS_ERR_PARAMS;

  // convert float value to string for validity check
  char str_val[MAX_RECORD_SIZE];

  memset(str_val, 0, MAX_RECORD_SIZE);
  snprintf(str_val, sizeof(str_val), "%f", float_val);

  return MgmtRecordSet(rec_name, str_val, action_need);
}


/*-------------------------------------------------------------------------
 * MgmtRecordSetString
 *-------------------------------------------------------------------------
 * The string value is copied so it's okay to free the string later
 */
TSError
MgmtRecordSetString(const char *rec_name, const char *string_val, TSActionNeedT * action_need)
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
TSError
ReadFile(TSFileNameT file, char **text, int *size, int *version)
{
  const char *fname;
  Rollback *file_rb;
  int ret, old_file_len;
  textBuffer *old_file_content;
  char *old_file_lines;
  version_t ver;

  Debug("FileOp", "[get_lines_from_file] START\n");


  fname = filename_to_string(file);
  if (!fname)
    return TS_ERR_READ_FILE;

  ret = configFiles->getRollbackObj(fname, &file_rb);
  if (ret != true) {
    Debug("FileOp", "[get_lines_from_file] Can't get Rollback for file: %s\n", fname);
    return TS_ERR_READ_FILE;
  }
  ver = file_rb->getCurrentVersion();
  file_rb->getVersion(ver, &old_file_content);
  *version = ver;

  // don't need to allocate memory b/c "getVersion" allocates memory
  old_file_lines = old_file_content->bufPtr();
  old_file_len = strlen(old_file_lines);

  *text = ats_strdup(old_file_lines);      //make copy before deleting textBuffer
  *size = old_file_len;

  delete old_file_content;      // delete textBuffer

  return TS_ERR_OKAY;
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
TSError
WriteFile(TSFileNameT file, char *text, int size, int version)
{
  const char *fname;
  Rollback *file_rb;
  textBuffer *file_content;
  int ret;
  version_t ver;


  fname = filename_to_string(file);
  if (!fname)
    return TS_ERR_WRITE_FILE;

  // get rollback object for config file
  mgmt_log(stderr, "[CfgFileIO::WriteFile] %s\n", fname);
  if (!(configFiles->getRollbackObj(fname, &file_rb))) {
    mgmt_log(stderr, "[CfgFileIO::WriteFile] ERROR getting rollback object\n");
    //goto generate_error_msg;
  }

  // if version < 0 then, just use next version in sequence;
  // otherwise check if trying to commit an old version
  if (version >= 0) {
    // check that the current version is equal to or less than the version
    // that wants to be written
    ver = file_rb->getCurrentVersion();
    if (ver != version)         // trying to commit an old version
      return TS_ERR_WRITE_FILE;
  }
  // use rollback object to update file with new content
  file_content = new textBuffer(size + 1);
  ret = file_content->copyFrom(text, size);
  if (ret < 0) {
    delete file_content;
    return TS_ERR_WRITE_FILE;
  }

  if ((file_rb->forceUpdate(file_content, -1)) != OK_ROLLBACK) {
    delete file_content;
    return TS_ERR_WRITE_FILE;
  }

  delete file_content;
  return TS_ERR_OKAY;
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
TSError
EventSignal(char * /* event_name ATS_UNUSED */, va_list /* ap ATS_UNUSED */)
{
  //char *text;
  //int id;

  //id = get_event_id(event_name);
  //text = get_event_text(event_name);
  //lmgmt->alarm_keeper->signalAlarm(id, text, NULL);

  return TS_ERR_OKAY;
}

/*-------------------------------------------------------------------------
 * EventResolve
 *-------------------------------------------------------------------------
 * Resolves the event of the given event_name. If the event is already
 * unresolved, just return TS_ERR_OKAY.

 */
TSError
EventResolve(char *event_name)
{
  alarm_t a;

  if (!event_name)
    return TS_ERR_PARAMS;

  a = get_event_id(event_name);
  lmgmt->alarm_keeper->resolveAlarm(a);

  return TS_ERR_OKAY;
}

/*-------------------------------------------------------------------------
 * ActiveEventGetMlt
 *-------------------------------------------------------------------------
 * returns failure, and an incomplete active_alarms list if any of
 * functions fail for a single event
 * note: returns list of local alarms at that instant of fn call (snapshot)
 */
TSError
ActiveEventGetMlt(LLQ * active_events)
{
  InkHashTable *event_ht;
  InkHashTableEntry *entry;
  InkHashTableIteratorState iterator_state;
  int event_id;
  char *event_name;

  if (!active_events)
    return TS_ERR_PARAMS;

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
        return TS_ERR_FAIL;
    }
  }

  return TS_ERR_OKAY;
}

/*-------------------------------------------------------------------------
 * EventIsActive
 *-------------------------------------------------------------------------
 * Sets *is_current to true if the event named event_name is currently
 * unresolved; otherwise sets *is_current to false.
 */
TSError
EventIsActive(char *event_name, bool * is_current)
{
  alarm_t a;

  if (!event_name || !is_current)
    return TS_ERR_PARAMS;

  a = get_event_id(event_name);
  // consider an invalid event_name an error
  if (a < 0)
    return TS_ERR_PARAMS;
  if (lmgmt->alarm_keeper->isCurrentAlarm(a))
    *is_current = true;         // currently an event
  else
    *is_current = false;

  return TS_ERR_OKAY;
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
TSError
EventSignalCbRegister(char *event_name, TSEventSignalFunc func, void *data)
{
  return cb_table_register(local_event_callbacks, event_name, func, data, NULL);

}

/*-------------------------------------------------------------------------
 * EventSignalCbUnregister
 *-------------------------------------------------------------------------
 * Removes the callback function from the local side CallbackTable
 */
TSError
EventSignalCbUnregister(char *event_name, TSEventSignalFunc func)
{
  return cb_table_unregister(local_event_callbacks, event_name, func);
}

/***************************************************************************
 * Snapshots
 ***************************************************************************/
TSError
SnapshotTake(char *snapshot_name)
{
  char *snapDirFromRecordsConf;
  bool found;
  char snapDir[PATH_NAME_MAX + 1];

  if (!snapshot_name)
    return TS_ERR_PARAMS;

  int rec_err = RecGetRecordString_Xmalloc("proxy.config.snapshot_dir", &snapDirFromRecordsConf);
  found = (rec_err == REC_ERR_OKAY);
  ink_release_assert(found);
  // XXX: Why was that offset to config dir?
  //      Any path should be prefix relative thought
  //
  Layout::relative_to(snapDir, sizeof(snapDir), Layout::get()->sysconfdir, snapDirFromRecordsConf);
  ats_free(snapDirFromRecordsConf);

  SnapResult result = configFiles->takeSnap(snapshot_name, snapDir);
  if (result != SNAP_OK)
    return TS_ERR_FAIL;
  else
    return TS_ERR_OKAY;
}

TSError
SnapshotRestore(char *snapshot_name)
{
  char *snapDirFromRecordsConf;
  bool found;
  char snapDir[PATH_NAME_MAX + 1];

  if (!snapshot_name)
    return TS_ERR_PARAMS;

  int rec_err = RecGetRecordString_Xmalloc("proxy.config.snapshot_dir", &snapDirFromRecordsConf);
  found = (rec_err == REC_ERR_OKAY);
  ink_release_assert(found);
  // XXX: Why was that offset to config dir?
  //      Any path should be prefix relative thought
  //
  Layout::relative_to(snapDir, sizeof(snapDir), Layout::get()->sysconfdir, snapDirFromRecordsConf);
  ats_free(snapDirFromRecordsConf);

  SnapResult result = configFiles->restoreSnap(snapshot_name, snapDir);
  ats_free(snapDirFromRecordsConf);
  if (result != SNAP_OK)
    return TS_ERR_FAIL;
  else
    return TS_ERR_OKAY;
}

TSError
SnapshotRemove(char *snapshot_name)
{
  char *snapDirFromRecordsConf;
  bool found;
  char snapDir[PATH_NAME_MAX + 1];

  if (!snapshot_name)
    return TS_ERR_PARAMS;

  int rec_err = RecGetRecordString_Xmalloc("proxy.config.snapshot_dir", &snapDirFromRecordsConf);
  found = (rec_err == REC_ERR_OKAY);
  ink_release_assert(found);
  // XXX: Why was that offset to config dir?
  //      Any path should be prefix relative thought
  //
  Layout::relative_to(snapDir, sizeof(snapDir), Layout::get()->sysconfdir, snapDirFromRecordsConf);
  ats_free(snapDirFromRecordsConf);

  SnapResult result = configFiles->removeSnap(snapshot_name, snapDir);
  ats_free(snapDirFromRecordsConf);
  if (result != SNAP_OK)
    return TS_ERR_FAIL;
  else
    return TS_ERR_OKAY;
}

/* based on FileManager.cc::displaySnapOption() */
TSError
SnapshotGetMlt(LLQ * snapshots)
{
  ExpandingArray snap_list(25, true);
  SnapResult snap_result;
  int num_snaps;
  char *snap_name;

  snap_result = configFiles->WalkSnaps(&snap_list);
  if (snap_result != SNAP_OK)
    return TS_ERR_FAIL;

  num_snaps = snap_list.getNumEntries();
  for (int i = 0; i < num_snaps; i++) {
    snap_name = (char *) (snap_list[i]);
    if (snap_name)
      enqueue(snapshots, ats_strdup(snap_name));
  }

  return TS_ERR_OKAY;
}

/*-------------------------------------------------------------------------
 * StatsReset
 *-------------------------------------------------------------------------
 * Iterates through the RecordsConfig table, and for all stats
 * (type PROCESS, NODE, CLUSTER), sets them back to their default value
 * If one stat fails to be set correctly, then continues onto next one,
 * but will return TS_ERR_FAIL. Only returns TS_ERR_OKAY if all
 * stats are set back to defaults successfully.
 */
TSError
StatsReset(bool cluster, const char *name)
{
  if (cluster)
    lmgmt->ccom->sendClusterMessage(CLUSTER_MSG_CLEAR_STATS, name);
  else
    lmgmt->clearStats(name);
  return TS_ERR_OKAY;
}

/*-------------------------------------------------------------
 * rmserver.cfg
 *-------------------------------------------------------------*/
