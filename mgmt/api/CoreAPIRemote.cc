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
 * Filename: CoreAPIRemote.cc
 * Purpose: Implementation of CoreAPI.h interface but from remote client
 *          perspective, so must also add networking calls. Basically, any
 *          TSMgmtAPI calls which are "special" for remote clients
 *          need to be implemented here.
 * Note: For remote implementation of this interface, most functions will:
 *  1) marshal: create the message to send across network
 *  2) connect and send request
 *  3) unmarshal: parse the reply (checking for TSError)
 *
 * Created: lant
 *
 ***************************************************************************/

#include "ink_config.h"
#include "ink_defs.h"
#include <strings.h>
#include "ink_string.h"
#include "I_Layout.h"
#include "ParseRules.h"
#include "CoreAPI.h"
#include "CoreAPIShared.h"
#include "CfgContextUtils.h"
#include "NetworkUtilsRemote.h"
#include "NetworkUtilsDefs.h"
#include "EventRegistration.h"
#include "EventCallback.h"

// extern variables
extern CallbackTable *remote_event_callbacks;   // from EventRegistration
extern int main_socket_fd;      // from NetworkUtils
extern int event_socket_fd;

// forward declarations
TSError send_and_parse_basic(OpType op);
TSError send_and_parse_list(OpType op, LLQ * list);
TSError send_and_parse_name(OpType op, char *name);
TSError mgmt_record_set(const char *rec_name, const char *rec_val, TSActionNeedT * action_need);
bool start_binary(const char *abs_bin_path);

// global variables
// need to store the thread id associated with socket_test_thread
// in case we want to  explicitly stop/cancel the testing thread
ink_thread ts_test_thread;
ink_thread ts_event_thread;
TSInitOptionT ts_init_options;

/***************************************************************************
 * Helper Functions
 ***************************************************************************/
/*-------------------------------------------------------------------------
 * send_and_parse_basic (helper function)
 *-------------------------------------------------------------------------
 * helper function used by operations which only require sending a simple
 * operation type and parsing a simple error return value
 */
TSError
send_and_parse_basic(OpType op)
{
  TSError err;

  err = send_request(main_socket_fd, op);
  if (err != TS_ERR_OKAY)
    return err;                 // networking error

  err = parse_reply(main_socket_fd);

  return err;
}

/*-------------------------------------------------------------------------
 * send_and_parse_list (helper function)
 *-------------------------------------------------------------------------
 * helper function used by operations which only require sending a simple
 * operation type and parsing a string delimited list
 * (delimited with REMOTE_DELIM_STR) and storing the tokens in the list
 * parameter
 */
TSError
send_and_parse_list(OpType op, LLQ * list)
{
  TSError ret;
  char *list_str;
  const char *tok;
  Tokenizer tokens(REMOTE_DELIM_STR);
  tok_iter_state i_state;

  if (!list)
    return TS_ERR_PARAMS;

  // create and send request
  ret = send_request(main_socket_fd, op);
  if (ret != TS_ERR_OKAY)
    return ret;

  // parse the reply = delimited list of ids of active event names
  ret = parse_reply_list(main_socket_fd, &list_str);
  if (ret != TS_ERR_OKAY)
    return ret;

  // tokenize the list_str and put into LLQ; use Tokenizer
  if (!list_str)
    return TS_ERR_FAIL;

  tokens.Initialize(list_str, COPY_TOKS);
  tok = tokens.iterFirst(&i_state);
  while (tok != NULL) {
    enqueue(list, ats_strdup(tok));        // add token to LLQ
    tok = tokens.iterNext(&i_state);
  }

  ats_free(list_str);
  return TS_ERR_OKAY;
}

/*-------------------------------------------------------------------------
 * send_and_parse_name (helper function)
 *-------------------------------------------------------------------------
 * helper function used by operations which only require sending a simple
 * operation type with one string name argument and then parsing a simple
 * TSError reply
 * NOTE: name can be a NULL parameter!
 */
TSError
send_and_parse_name(OpType op, char *name)
{
  TSError ret;

  // create and send request
  ret = send_request_name(main_socket_fd, op, name);
  if (ret != TS_ERR_OKAY)
    return ret;

  // parse the reply
  ret = parse_reply(main_socket_fd);

  return ret;
}


/*-------------------------------------------------------------------------
 * mgmt_record_set (helper function)
 *-------------------------------------------------------------------------
 * Helper function for all Set functions:
 * NOTE: regardless of the type of the record being set,
 * it is converted to a string. Then on the local side, the
 * CoreAPI::MgmtRecordSet function will do the appropriate type
 * converstion from the string to the record's type (eg. MgmtInt, MgmtString..)
 * Hence, on the local side, don't have to worry about typecasting a
 * void*. Just read out the string from socket and pass it MgmtRecordSet.
 */
TSError
mgmt_record_set(const char *rec_name, const char *rec_val, TSActionNeedT * action_need)
{
  TSError err;

  if (!rec_name || !rec_val || !action_need)
    return TS_ERR_PARAMS;

  // create and send request
  err = send_request_name_value(main_socket_fd, RECORD_SET, rec_name, rec_val);
  if (err != TS_ERR_OKAY)
    return err;

  // parse the reply to get TSError response and TSActionNeedT
  err = parse_record_set_reply(main_socket_fd, action_need);

  return err;
}


/*-------------------------------------------------------------------------
 * start_binary
 *-------------------------------------------------------------------------
 * helper function which calls the executable specified by abs_bin_path;
 * used by HardRestart to call the stop/start_traffic_server scripts
 * Output: returns false if fail, true if successful
 */
bool
start_binary(const char *abs_bin_path)
{
  TSDiags(TS_DIAG_NOTE, "[start_binary] abs_bin_path = %s", abs_bin_path);
  // before doing anything, check for existence of binary and its execute
  // permissions
  if (access(abs_bin_path, F_OK) < 0) {
    // ERROR: can't find binary
    TSDiags(TS_DIAG_ERROR, "Cannot find executable %s", abs_bin_path);
    return false;
  }
  // binary exists, check permissions
  else if (access(abs_bin_path, R_OK | X_OK) < 0) {
    // ERROR: doesn't have proper permissions
    TSDiags(TS_DIAG_ERROR, "Cannot execute %s", abs_bin_path);
    return false;
  }

  if (system(abs_bin_path) == -1) {
    TSDiags(TS_DIAG_ERROR, "Cannot system(%s)", abs_bin_path);
    return false;
  }

  return true;
}


/***************************************************************************
 * SetUp Operations
 ***************************************************************************/
TSError
Init(const char *socket_path, TSInitOptionT options)
{
  TSError err = TS_ERR_OKAY;

  ts_init_options = options;

  // XXX This should use RecConfigReadRuntimeDir(), but that's not linked into the management
  // libraries. The caller has to pass down the right socket path :(
  if (!socket_path) {
    Layout::create();
    socket_path = Layout::get()->runtimedir;
  }

  // store socket_path
  set_socket_paths(socket_path);

  // need to ignore SIGPIPE signal; in the case that TM is restarted
  signal(SIGPIPE, SIG_IGN);

  // EVENT setup - initialize callback queue
  if (0 == (ts_init_options & TS_MGMT_OPT_NO_EVENTS)) {
    remote_event_callbacks = create_callback_table("remote_callbacks");
    if (!remote_event_callbacks)
      return TS_ERR_SYS_CALL;
  } else {
    remote_event_callbacks = NULL;
  }

  // try to connect to traffic manager
  // do this last so that everything else on client side is set up even if
  // connection fails; this might happen if client is set up and running
  // before TM
  err = ts_connect();
  if (err != TS_ERR_OKAY)
    goto END;

  // if connected, create event thread that listens for events from TM
  if (0 == (ts_init_options & TS_MGMT_OPT_NO_EVENTS)) {
    ts_event_thread = ink_thread_create(event_poll_thread_main, &event_socket_fd, 0, DEFAULT_STACK_SIZE);
  } else {
    ts_event_thread = static_cast<ink_thread>(NULL);
  }

END:

  // create thread that periodically checks the socket connection
  // with TM alive - reconnects if not alive
  if (0 == (ts_init_options & TS_MGMT_OPT_NO_SOCK_TESTS)) {
    ts_test_thread = ink_thread_create(socket_test_thread, NULL, 0, DEFAULT_STACK_SIZE);
  } else {
    ts_test_thread = static_cast<ink_thread>(NULL);
  }

  return err;

}

// does clean up for remote API client; destroy structures and disconnects
TSError
Terminate()
{
  TSError err;

  if (remote_event_callbacks)
    delete_callback_table(remote_event_callbacks);

  // be sure to do this before reset socket_fd's
  err = disconnect();
  if (err != TS_ERR_OKAY)
    return err;

  // cancel the listening socket thread
  // it's important to call this before setting paths to NULL because the
  // socket_test_thread actually will try to reconnect() and this funntion
  // will seg fault if the socket paths are NULL while it is connecting;
  // the thread will be cancelled at a cancellation point in the
  // socket_test_thread, eg. sleep
  if (ts_test_thread)
    ink_thread_cancel(ts_test_thread);
  if (ts_event_thread)
    ink_thread_cancel(ts_event_thread);

  // Before clear, we should confirm these
  // two threads have finished. Or the clear
  // operation may lead them crash.
  if (ts_test_thread)
    ink_thread_join(ts_test_thread);
  if (ts_event_thread)
    ink_thread_join(ts_event_thread);

  // Clear operation
  ts_test_thread = static_cast<ink_thread>(NULL);
  ts_event_thread = static_cast<ink_thread>(NULL);
  set_socket_paths(NULL);       // clear the socket_path

  return TS_ERR_OKAY;
}

// ONLY have very basic diag functionality for remote cliets.
// When a remote client tries to use diags (wants to output runtime
// diagnostics, the diagnostics will be outputted to the machine
// the remote client is logged into (the one TM is running on)
void
Diags(TSDiagsT mode, const char *fmt, va_list ap)
{
  char diag_msg[MAX_BUF_SIZE];

  // format the diag message now so it can be sent
  // vsnprintf does not compile on DEC
  vsnprintf(diag_msg, MAX_BUF_SIZE - 1, fmt, ap);
  TSError ret = send_diags_msg(main_socket_fd, mode, diag_msg);
  if (ret != TS_ERR_OKAY) {
    //fprintf(stderr, "[Diags] error sending diags message\n");
  }
}

/***************************************************************************
 * Control Operations
 ***************************************************************************/
TSProxyStateT
ProxyStateGet()
{
  TSError ret;
  TSProxyStateT state;

  ret = send_request(main_socket_fd, PROXY_STATE_GET);
  if (ret != TS_ERR_OKAY)      // Networking error
    return TS_PROXY_UNDEFINED;

  ret = parse_proxy_state_get_reply(main_socket_fd, &state);
  if (ret != TS_ERR_OKAY)      // Newtorking error
    return TS_PROXY_UNDEFINED;

  return state;
}

TSError
ProxyStateSet(TSProxyStateT state, TSCacheClearT clear)
{
  TSError ret;

  ret = send_proxy_state_set_request(main_socket_fd, state, clear);
  if (ret != TS_ERR_OKAY)
    return ret;                 // networking error

  ret = parse_reply(main_socket_fd);

  return ret;
}

TSError
Reconfigure()
{
  return send_and_parse_basic(RECONFIGURE);
}

/*-------------------------------------------------------------------------
 * Restart
 *-------------------------------------------------------------------------
 * if restart of TM is successful, need to reconnect to TM;
 * it's possible that the SUCCESS msg is received before the
 * restarting of TM is totally complete(?) b/c the core Restart call
 * only signals the event putting it in a msg queue;
 * so keep trying to reconnect until successful or for MAX_CONN_TRIES
 */
TSError
Restart(bool cluster)
{
  TSError ret;

  ret = send_request_bool(main_socket_fd, RESTART, cluster);
  if (ret != TS_ERR_OKAY)
    return ret;                 // networking error

  ret = parse_reply(main_socket_fd);

  if (ret == TS_ERR_OKAY) {
    ret = reconnect_loop(MAX_CONN_TRIES);
  }

  return ret;
}


/*-------------------------------------------------------------------------
 * HardRestart
 *-------------------------------------------------------------------------
 * Restarts Traffic Cop by using the stop_traffic_server, start_traffic_server script
 */
TSError
HardRestart()
{
  char start_path[1024];
  char stop_path[1024];

  if (!Layout::get() || !Layout::get()->bindir)
    return TS_ERR_FAIL;
  // determine the path of where start and stop TS scripts stored
  TSDiags(TS_DIAG_NOTE, "Root Directory: %s", Layout::get()->bindir);

  Layout::relative_to(start_path, sizeof(start_path), Layout::get()->bindir, "start_traffic_server");
  Layout::relative_to(stop_path, sizeof(stop_path), Layout::get()->bindir, "stop_traffic_server");

  TSDiags(TS_DIAG_NOTE, "[HardRestart] start_path = %s", start_path);
  TSDiags(TS_DIAG_NOTE, "[HardRestart] stop_path = %s", stop_path);

  if (!start_binary(stop_path)) // call stop_traffic_server script
    return TS_ERR_FAIL;

  if (!start_binary(start_path))        // call start_traffic_server script
    return TS_ERR_FAIL;

  return TS_ERR_OKAY;
}

/*-------------------------------------------------------------------------
 * Bounce
 *-------------------------------------------------------------------------
 * Restart the traffic_server process(es) only.
 */
TSError
Bounce(bool cluster)
{
  TSError ret;

  ret = send_request_bool(main_socket_fd, BOUNCE, cluster);
  if (ret != TS_ERR_OKAY)
    return ret;                 // networking error

  return parse_reply(main_socket_fd);
}


/*-------------------------------------------------------------------------
 * StorageDeviceCmdOffline
 *-------------------------------------------------------------------------
 * Disable a storage device.
 */
TSError
StorageDeviceCmdOffline(char const* dev)
{
  TSError ret;
  ret = send_request_name(main_socket_fd, STORAGE_DEVICE_CMD_OFFLINE, dev);
  return TS_ERR_OKAY != ret ? ret : parse_reply(main_socket_fd);
}

/***************************************************************************
 * Record Operations
 ***************************************************************************/
static TSError
mgmt_record_get_reply(TSRecordEle * rec_ele)
{
  TSError ret;
  void *val;
  char *name;

  ink_zero(*rec_ele);
  rec_ele->rec_type = TS_REC_UNDEFINED;

  // parse the reply to get record value and type
  ret = parse_record_get_reply(main_socket_fd, &(rec_ele->rec_type), &val, &name);
  if (ret != TS_ERR_OKAY) {
    return ret;
  }

  // convert the record value to appropriate type
  if (val) {
    switch (rec_ele->rec_type) {
    case TS_REC_INT:
      rec_ele->int_val = *(TSInt *) val;
      break;
    case TS_REC_COUNTER:
      rec_ele->counter_val = *(TSCounter *) val;
      break;
    case TS_REC_FLOAT:
      rec_ele->float_val = *(TSFloat *) val;
      break;
    case TS_REC_STRING:
      rec_ele->string_val = ats_strdup((char *) val);
      break;
    default:
      ; // nothing ... shut up compiler!
    }
  }

  if (name) {
    rec_ele->rec_name = name;
  }

  ats_free(val);
  return TS_ERR_OKAY;
}

// note that the record value is being sent as chunk of memory, regardless of
// record type; it's not being converted to a string!!
TSError
MgmtRecordGet(const char *rec_name, TSRecordEle * rec_ele)
{
  TSError ret;

  if (!rec_name || !rec_ele) {
    return TS_ERR_PARAMS;
  }

  // create and send request
  ret = send_record_get_request(main_socket_fd, rec_name);
  if (ret != TS_ERR_OKAY) {
    return ret;
  }

  return mgmt_record_get_reply(rec_ele);
}

TSError
MgmtRecordGetMatching(const char * regex, TSList rec_vals)
{
  TSError       ret;
  TSRecordEle * rec_ele;

  ret = send_record_match_request(main_socket_fd, regex);
  if (ret != TS_ERR_OKAY) {
    return ret;
  }

  for (;;) {
    rec_ele = TSRecordEleCreate();

    // parse the reply to get record value and type
    ret = mgmt_record_get_reply(rec_ele);
    if (ret != TS_ERR_OKAY) {
      goto fail;
    }

    // A NULL record ends the list.
    if (rec_ele->rec_type == TS_REC_UNDEFINED) {
      break;
    }

    enqueue((LLQ *) rec_vals, rec_ele);
  }

  return TS_ERR_OKAY;

fail:

  TSRecordEleDestroy(rec_ele);
  for (rec_ele = (TSRecordEle *) dequeue((LLQ *) rec_vals); rec_ele; rec_ele = (TSRecordEle *) dequeue((LLQ *) rec_vals)) {
      TSRecordEleDestroy(rec_ele);
  }

  return ret;
}

TSError
MgmtRecordSet(const char *rec_name, const char *val, TSActionNeedT * action_need)
{
  TSError ret;

  if (!rec_name || !val || !action_need)
    return TS_ERR_PARAMS;

  ret = mgmt_record_set(rec_name, val, action_need);
  return ret;
}

// first convert the MgmtInt into a string
// NOTE: use long long, not just long, MgmtInt = int64_t
TSError
MgmtRecordSetInt(const char *rec_name, MgmtInt int_val, TSActionNeedT * action_need)
{
  char str_val[MAX_RECORD_SIZE];
  TSError ret;

  if (!rec_name || !action_need)
    return TS_ERR_PARAMS;

  bzero(str_val, MAX_RECORD_SIZE);
  snprintf(str_val, sizeof(str_val), "%" PRId64 "", int_val);
  ret = mgmt_record_set(rec_name, str_val, action_need);

  return ret;
}

// first convert the MgmtIntCounter into a string
TSError
MgmtRecordSetCounter(const char *rec_name, MgmtIntCounter counter_val, TSActionNeedT * action_need)
{
  char str_val[MAX_RECORD_SIZE];
  TSError ret;

  if (!rec_name || !action_need)
    return TS_ERR_PARAMS;

  bzero(str_val, MAX_RECORD_SIZE);
  snprintf(str_val, sizeof(str_val), "%" PRId64 "", counter_val);
  ret = mgmt_record_set(rec_name, str_val, action_need);

  return ret;
}

// first convert the MgmtFloat into string
TSError
MgmtRecordSetFloat(const char *rec_name, MgmtFloat float_val, TSActionNeedT * action_need)
{
  char str_val[MAX_RECORD_SIZE];
  TSError ret;

  bzero(str_val, MAX_RECORD_SIZE);
  if (snprintf(str_val, sizeof(str_val), "%f", float_val) < 0)
    return TS_ERR_SYS_CALL;
  ret = mgmt_record_set(rec_name, str_val, action_need);

  return ret;
}


TSError
MgmtRecordSetString(const char *rec_name, const char *string_val, TSActionNeedT * action_need)
{
  TSError ret;

  if (!rec_name || !action_need)
    return TS_ERR_PARAMS;

  ret = mgmt_record_set(rec_name, string_val, action_need);
  return ret;
}


/***************************************************************************
 * File Operations
 ***************************************************************************/
/*-------------------------------------------------------------------------
 * ReadFile
 *-------------------------------------------------------------------------
 * Purpose: returns copy of the most recent version of the file
 * Input:   file - the config file to read
 *          text - a buffer is allocated on the text char* pointer
 *          size - the size of the buffer is returned
 * Output:
 *
 * Marshals a read file request that can be sent over the unix domain socket.
 * Connects to the socket and sends request over. Parses the response from
 * Traffic Manager.
 */
TSError
ReadFile(TSFileNameT file, char **text, int *size, int *version)
{
  TSError ret;

  // marshal data into message request to be sent over socket
  // create connection and send request
  ret = send_file_read_request(main_socket_fd, file);
  if (ret != TS_ERR_OKAY)
    return ret;

  // read response from socket and unmarshal the response
  ret = parse_file_read_reply(main_socket_fd, version, size, text);

  return ret;
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
 *
 * Marshals a write file request that can be sent over the unix domain socket.
 * Connects to the socket and sends request over. Parses the response from
 * Traffic Manager.
 */
TSError
WriteFile(TSFileNameT file, char *text, int size, int version)
{
  TSError ret;

  ret = send_file_write_request(main_socket_fd, file, version, size, text);
  if (ret != TS_ERR_OKAY)
    return ret;

  ret = parse_reply(main_socket_fd);

  return ret;
}


/***************************************************************************
 * Events
 ***************************************************************************/
/*-------------------------------------------------------------------------
 * EventSignal
 *-------------------------------------------------------------------------
 * LAN - need to implement
 */
TSError
EventSignal(char */* event_name ATS_UNUSED */, va_list /* ap ATS_UNUSED */)
{
  return TS_ERR_FAIL;
}

/*-------------------------------------------------------------------------
 * EventResolve
 *-------------------------------------------------------------------------
 * purpose: Resolves the event of the specified name
 * note:    when sending the message request, actually sends the event name,
 *          not the event id
 */
TSError
EventResolve(char *event_name)
{
  if (!event_name)
    return TS_ERR_PARAMS;

  return (send_and_parse_name(EVENT_RESOLVE, event_name));
}

/*-------------------------------------------------------------------------
 * ActiveEventGetMlt
 *-------------------------------------------------------------------------
 * purpose: Retrieves a list of active(unresolved) events
 * note:    list of event names returned in network msg which must be tokenized
 */
TSError
ActiveEventGetMlt(LLQ * active_events)
{
  if (!active_events)
    return TS_ERR_PARAMS;

  return (send_and_parse_list(EVENT_GET_MLT, active_events));
}

/*-------------------------------------------------------------------------
 * EventIsActive
 *-------------------------------------------------------------------------
 * determines if the event_name is active; sets result in is_current
 */
TSError
EventIsActive(char *event_name, bool * is_current)
{
  TSError ret;

  if (!event_name || !is_current)
    return TS_ERR_PARAMS;

  // create and send request
  ret = send_request_name(main_socket_fd, EVENT_ACTIVE, event_name);
  if (ret != TS_ERR_OKAY)
    return ret;

  // parse the reply
  ret = parse_event_active_reply(main_socket_fd, is_current);
  if (ret != TS_ERR_OKAY)
    return ret;

  return ret;
}

/*-------------------------------------------------------------------------
 * EventSignalCbRegister
 *-------------------------------------------------------------------------
 * Adds the callback function in appropriate places in the remote side
 * callback table.
 * If this is the first callback to be registered for a certain event type,
 * then sends a callback registration notification to TM so that TM will know
 * which events have remote callbacks registered on it.
 */
TSError
EventSignalCbRegister(char *event_name, TSEventSignalFunc func, void *data)
{
  bool first_time = 0;
  TSError err;

  if (func == NULL)
    return TS_ERR_PARAMS;
  if (!remote_event_callbacks)
    return TS_ERR_FAIL;

  err = cb_table_register(remote_event_callbacks, event_name, func, data, &first_time);
  if (err != TS_ERR_OKAY)
    return err;

  // if we need to notify traffic manager of the event then send msg
  if (first_time) {
    err = send_request_name(event_socket_fd, EVENT_REG_CALLBACK, event_name);
    if (err != TS_ERR_OKAY)
      return err;
  }

  return TS_ERR_OKAY;
}

/*-------------------------------------------------------------------------
 * EventSignalCbUnregister
 *-------------------------------------------------------------------------
 * Removes the callback function from the remote side callback table.
 * After removing the callback function, needs to check which events now
 * no longer have any callbacks registered at all; sends an unregister callback
 * notification to TM so that TM knows that that event doesn't have any
 * remote callbacks registered for it
 * Input: event_name - the event to unregister the callback from; if NULL,
 *                     unregisters the specified func from all events
 *        func       - the callback function to unregister; if NULL, then
 *                     unregisters all callback functions for the event_name
 *                     specified
 */
TSError
EventSignalCbUnregister(char *event_name, TSEventSignalFunc func)
{
  TSError err;

  if (!remote_event_callbacks)
    return TS_ERR_FAIL;

  // remove the callback function from the table
  err = cb_table_unregister(remote_event_callbacks, event_name, func);
  if (err != TS_ERR_OKAY)
    return err;

  // check if we need to notify traffic manager of the event (notify TM
  // only if the event has no callbacks)
  err = send_unregister_all_callbacks(event_socket_fd, remote_event_callbacks);
  if (err != TS_ERR_OKAY)
    return err;

  return TS_ERR_OKAY;
}

/***************************************************************************
 * Snapshots
 ***************************************************************************/
TSError
SnapshotTake(char *snapshot_name)
{
  if (!snapshot_name)
    return TS_ERR_PARAMS;

  return send_and_parse_name(SNAPSHOT_TAKE, snapshot_name);
}

TSError
SnapshotRestore(char *snapshot_name)
{
  if (!snapshot_name)
    return TS_ERR_PARAMS;

  return send_and_parse_name(SNAPSHOT_RESTORE, snapshot_name);
}

TSError
SnapshotRemove(char *snapshot_name)
{
  if (!snapshot_name)
    return TS_ERR_PARAMS;

  return send_and_parse_name(SNAPSHOT_REMOVE, snapshot_name);
}

TSError
SnapshotGetMlt(LLQ * snapshots)
{
  return send_and_parse_list(SNAPSHOT_GET_MLT, snapshots);
}

TSError
StatsReset(bool cluster, const char* name)
{
  TSError ret;

  if (cluster) {
    ret = send_request_name(main_socket_fd, STATS_RESET_CLUSTER, name);
  } else {
    ret = send_request_name(main_socket_fd, STATS_RESET_NODE, name);
  }
  if (ret != TS_ERR_OKAY)
    return ret;                 // networking error

  return parse_reply(main_socket_fd);
}
