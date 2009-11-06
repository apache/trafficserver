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
 *          INKMgmtAPI calls which are "special" for remote clients 
 *          need to be implemented here.
 * Note: For remote implementation of this interface, most functions will:
 *  1) marshal: create the message to send across network
 *  2) connect and send request
 *  3) unmarshal: parse the reply (checking for INKError) 
 *
 * Created: lant 
 * 
 ***************************************************************************/

#include <strings.h>
#include "ink_snprintf.h"
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
INKError send_and_parse_basic(OpType op);
INKError send_and_parse_list(OpType op, LLQ * list);
INKError send_and_parse_name(OpType op, char *name);
INKError mgmt_record_set(char *rec_name, char *rec_val, INKActionNeedT * action_need);
bool start_binary(const char *abs_bin_path);
char *get_root_dir();

// global variables
// need to store the thread id associated with socket_test_thread 
// in case we want to  explicitly stop/cancel the testing thread
ink_thread ink_test_thread;

/***************************************************************************
 * Helper Functions
 ***************************************************************************/
/*-------------------------------------------------------------------------
 * send_and_parse_basic (helper function)
 *-------------------------------------------------------------------------
 * helper function used by operations which only require sending a simple
 * operation type and parsing a simple error return value 
 */
INKError
send_and_parse_basic(OpType op)
{
  INKError err;

  err = send_request(main_socket_fd, op);
  if (err != INK_ERR_OKAY)
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
INKError
send_and_parse_list(OpType op, LLQ * list)
{
  INKError ret;
  char *list_str;
  const char *tok;
  Tokenizer tokens(REMOTE_DELIM_STR);
  tok_iter_state i_state;

  if (!list)
    return INK_ERR_PARAMS;

  // create and send request
  ret = send_request(main_socket_fd, op);
  if (ret != INK_ERR_OKAY)
    return ret;

  // parse the reply = delimited list of ids of active event names 
  ret = parse_reply_list(main_socket_fd, &list_str);
  if (ret != INK_ERR_OKAY)
    return ret;

  // tokenize the list_str and put into LLQ; use Tokenizer
  if (!list_str)
    return INK_ERR_FAIL;

  tokens.Initialize(list_str, COPY_TOKS);
  tok = tokens.iterFirst(&i_state);
  while (tok != NULL) {
    enqueue(list, xstrdup(tok));        // add token to LLQ
    tok = tokens.iterNext(&i_state);
  }

  if (list_str)
    xfree(list_str);

  return INK_ERR_OKAY;
}

/*-------------------------------------------------------------------------
 * send_and_parse_name (helper function)
 *-------------------------------------------------------------------------
 * helper function used by operations which only require sending a simple
 * operation type with one string name argument and then parsing a simple
 * INKError reply 
 * NOTE: name can be a NULL parameter!
 */
INKError
send_and_parse_name(OpType op, char *name)
{
  INKError ret;

  // create and send request
  ret = send_request_name(main_socket_fd, op, name);
  if (ret != INK_ERR_OKAY)
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
INKError
mgmt_record_set(char *rec_name, char *rec_val, INKActionNeedT * action_need)
{
  INKError err;

  if (!rec_name || !rec_val || !action_need)
    return INK_ERR_PARAMS;

  // create and send request
  err = send_request_name_value(main_socket_fd, RECORD_SET, rec_name, rec_val);
  if (err != INK_ERR_OKAY)
    return err;

  // parse the reply to get INKError response and INKActionNeedT
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
  INKDiags(INK_DIAG_NOTE, "[start_binary] abs_bin_path = %s", abs_bin_path);
  // before doing anything, check for existence of binary and its execute 
  // permissions
  if (access(abs_bin_path, F_OK) < 0) {
    // ERROR: can't find binary
    INKDiags(INK_DIAG_ERROR, "Cannot find executable %s", abs_bin_path);
    return false;
  }
  // binary exists, check permissions
  else if (access(abs_bin_path, R_OK | X_OK) < 0) {
    // ERROR: doesn't have proper permissions
    INKDiags(INK_DIAG_ERROR, "Cannot execute %s", abs_bin_path);
    return false;
  }

  if (system(abs_bin_path) == -1) {
    INKDiags(INK_DIAG_ERROR, "Cannot system(%s)", abs_bin_path);
    return false;
  }

  return true;
}

/*-------------------------------------------------------------------------
 * get_root_dir
 *-------------------------------------------------------------------------
 * This function retrieves the root directory path from /etc/traffic_server 
 * file. If there is no /etc/traffic_server file to be found, returns NULL 
 * (copied from TrafficCop.cc). The string returned is NOT ALLOCATED. 
 * Used by HardRestart to determine full path of start/stop_traffic_server scripts.
 */
#ifndef _WIN32
char *
get_root_dir()
{
  FILE *ts_file;
  char buffer[1024];
  int i = 0;
  // Changed this to static, this function is not reentrant ... /leif
  static char root_dir[1024];
  char *env_path;

  bzero(root_dir, 1024);

  if ((env_path = getenv("ROOT")) || (env_path = getenv("INST_ROOT"))) {
    strncpy(root_dir, env_path, 1023);
    return root_dir;
  }

  if ((ts_file = fopen("/etc/traffic_server", "r")) != NULL) {
    NOWARN_UNUSED_RETURN(fgets(buffer, 1024, ts_file));
    fclose(ts_file);
    while (!ParseRules::is_space(buffer[i])) {
      root_dir[i] = buffer[i];
      i++;
    }
    root_dir[i] = '\0';
  } else {
    strncpy(root_dir, "/home/trafficserver", sizeof(root_dir));
  }

  if (root_dir[0] == '\0')
    return NULL;
  else
    return root_dir;
}
#endif

/***************************************************************************
 * SetUp Operations
 ***************************************************************************/

// signal handler for SIGUSR1 - sent when cancelling the socket-test-thread
// Doesn't really need to do anything since it's currently only called when
// terminating the remote client api, so everything should close anyways
void
terminate_signal(int sig)
{
  //fprintf(stderr, "[terminate_signal] received SIGUSR1 signal\n");
  return;
}


INKError
Init(char *socket_path)
{
  INKError err = INK_ERR_OKAY;

  // SOCKET setup
  ink_assert(socket_path);
  if (!socket_path)
    return INK_ERR_PARAMS;

  // store socket_path
  set_socket_paths(socket_path);

  // need to ignore SIGPIPE signal; in the case that TM is restarted
  signal(SIGPIPE, SIG_IGN);
  signal(SIGUSR1, terminate_signal);    // for cancelling socket_test_thread 

  // EVENT setup - initialize callback queue
  remote_event_callbacks = create_callback_table("remote_callbacks");
  if (!remote_event_callbacks)
    return INK_ERR_SYS_CALL;

  // try to connect to traffic manager
  // do this last so that everything else on client side is set up even if 
  // connection fails; this might happen if client is set up and running 
  // before TM
  err = connect();
  if (err != INK_ERR_OKAY)
    goto END;

  // if connected, create event thread that listens for events from TM
  ink_thread_create(event_poll_thread_main, &event_socket_fd);

END:

  // create thread that periodically checks the socket connection 
  // with TM alive - reconnects if not alive
  ink_test_thread = ink_thread_create(socket_test_thread, NULL);
  return err;

}

// does clean up for remote API client; destroy structures and disconnects 
INKError
Terminate()
{
  INKError err;

  delete_callback_table(remote_event_callbacks);

  // be sure to do this before reset socket_fd's
  err = disconnect();
  if (err != INK_ERR_OKAY)
    return err;

  // cancel the listening socket thread
  // it's important to call this before setting paths to NULL because the 
  // socket_test_thread actually will try to reconnect() and this funntion
  // will seg fault if the socket paths are NULL while it is connecting;
  // the thread will be cancelled at a cancellation point in the 
  // socket_test_thread, eg. sleep
  ink_thread_cancel(ink_test_thread);

  set_socket_paths(NULL);       // clear the socket_path

  return INK_ERR_OKAY;
}

// ONLY have very basic diag functionality for remote cliets. 
// When a remote client tries to use diags (wants to output runtime
// diagnostics, the diagnostics will be outputted to the machine
// the remote client is logged into (the one TM is running on)
void
Diags(INKDiagsT mode, const char *fmt, va_list ap)
{
  char diag_msg[MAX_BUF_SIZE];

  // format the diag message now so it can be sent
  // vsnprintf does not compile on DEC
  ink_vsnprintf(diag_msg, MAX_BUF_SIZE - 1, fmt, ap);
  INKError ret = send_diags_msg(main_socket_fd, mode, diag_msg);
  if (ret != INK_ERR_OKAY) {
    //fprintf(stderr, "[Diags] error sending diags message\n");
  }
}

/***************************************************************************
 * Control Operations
 ***************************************************************************/
INKProxyStateT
ProxyStateGet()
{
  INKError ret;
  INKProxyStateT state;

  ret = send_request(main_socket_fd, PROXY_STATE_GET);
  if (ret != INK_ERR_OKAY)      // Networking error
    return INK_PROXY_UNDEFINED;

  ret = parse_proxy_state_get_reply(main_socket_fd, &state);
  if (ret != INK_ERR_OKAY)      // Newtorking error
    return INK_PROXY_UNDEFINED;

  return state;
}

INKError
ProxyStateSet(INKProxyStateT state, INKCacheClearT clear)
{
  INKError ret;

  ret = send_proxy_state_set_request(main_socket_fd, state, clear);
  if (ret != INK_ERR_OKAY)
    return ret;                 // networking error

  ret = parse_reply(main_socket_fd);

  return ret;
}

INKError
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
INKError
Restart(bool cluster)
{
  INKError ret;

  ret = send_restart_request(main_socket_fd, cluster);
  if (ret != INK_ERR_OKAY)
    return ret;                 // networking error

  ret = parse_reply(main_socket_fd);

  if (ret == INK_ERR_OKAY) {
    ret = reconnect_loop(MAX_CONN_TRIES);
  }

  return ret;
}


/*-------------------------------------------------------------------------
 * HardRestart
 *-------------------------------------------------------------------------
 * Restarts Traffic Cop by using the stop_traffic_server, start_traffic_server script
 */
INKError
HardRestart()
{
  char start_path[1024];
  char stop_path[1024];

  // determine the path of where start and stop TS scripts stored
  char *root_dir = get_root_dir();
  INKDiags(INK_DIAG_NOTE, "Root Directory: %s", root_dir);
  if (!root_dir)
    return INK_ERR_FAIL;

  bzero(start_path, 1024);
  bzero(stop_path, 1024);
  char *root_copy = xstrdup(root_dir);
  ink_snprintf(start_path, sizeof(start_path), "%s/bin/start_traffic_server", root_copy);
  ink_snprintf(stop_path, sizeof(stop_path), "%s/bin/stop_traffic_server", root_copy);
  xfree(root_copy);

  INKDiags(INK_DIAG_NOTE, "[HardRestart] start_path = %s", start_path);
  INKDiags(INK_DIAG_NOTE, "[HardRestart] stop_path = %s", stop_path);

  if (!start_binary(stop_path)) // call stop_traffic_server script
    return INK_ERR_FAIL;

  if (!start_binary(start_path))        // call start_traffic_server script
    return INK_ERR_FAIL;


  return INK_ERR_OKAY;
}

/***************************************************************************
 * Record Operations
 ***************************************************************************/
// note that the record value is being sent as chunk of memory, regardless of
// record type; it's not being converted to a string!!
INKError
MgmtRecordGet(char *rec_name, INKRecordEle * rec_ele)
{
  INKError ret;
  void *val;

  if (!rec_name || !rec_ele)
    return INK_ERR_PARAMS;

  rec_ele->rec_name = xstrdup(rec_name);

  // create and send request
  ret = send_record_get_request(main_socket_fd, rec_ele->rec_name);
  if (ret != INK_ERR_OKAY)
    return ret;

  // parse the reply to get record value and type
  ret = parse_record_get_reply(main_socket_fd, &(rec_ele->rec_type), &val);
  if (ret != INK_ERR_OKAY)
    return ret;

  // convert the record value to appropriate type
  switch (rec_ele->rec_type) {
  case INK_REC_INT:
    rec_ele->int_val = *(INKInt *) val;
    break;
  case INK_REC_COUNTER:
    rec_ele->counter_val = *(INKCounter *) val;
    break;
  case INK_REC_FLOAT:
    rec_ele->float_val = *(INKFloat *) val;
    break;
  case INK_REC_STRING:
    rec_ele->string_val = xstrdup((char *) val);
    break;
  default:                     // ERROR - invalid record type
    return INK_ERR_FAIL;
  }

  if (val)
    xfree(val);
  return INK_ERR_OKAY;
}


INKError
MgmtRecordSet(char *rec_name, char *val, INKActionNeedT * action_need)
{
  INKError ret;

  if (!rec_name || !val || !action_need)
    return INK_ERR_PARAMS;

  ret = mgmt_record_set(rec_name, val, action_need);
  return ret;
}

// first convert the MgmtInt into a string
// NOTE: use long long, not just long, MgmtInt = ink64
INKError
MgmtRecordSetInt(char *rec_name, MgmtInt int_val, INKActionNeedT * action_need)
{
  char str_val[MAX_RECORD_SIZE];
  INKError ret;

  if (!rec_name || !action_need)
    return INK_ERR_PARAMS;

  bzero(str_val, MAX_RECORD_SIZE);
  ink_snprintf(str_val, sizeof(str_val), "%lld", int_val);
  ret = mgmt_record_set(rec_name, str_val, action_need);

  return ret;
}

// first convert the MgmtIntCounter into a string
INKError
MgmtRecordSetCounter(char *rec_name, MgmtIntCounter counter_val, INKActionNeedT * action_need)
{
  char str_val[MAX_RECORD_SIZE];
  INKError ret;

  if (!rec_name || !action_need)
    return INK_ERR_PARAMS;

  bzero(str_val, MAX_RECORD_SIZE);
  ink_snprintf(str_val, sizeof(str_val), "%lld", counter_val);
  ret = mgmt_record_set(rec_name, str_val, action_need);

  return ret;
}

// first convert the MgmtFloat into string
INKError
MgmtRecordSetFloat(char *rec_name, MgmtFloat float_val, INKActionNeedT * action_need)
{
  char str_val[MAX_RECORD_SIZE];
  INKError ret;

  bzero(str_val, MAX_RECORD_SIZE);
  if (ink_snprintf(str_val, sizeof(str_val), "%f", float_val) < 0)
    return INK_ERR_SYS_CALL;
  ret = mgmt_record_set(rec_name, str_val, action_need);

  return ret;
}


INKError
MgmtRecordSetString(char *rec_name, MgmtString string_val, INKActionNeedT * action_need)
{
  INKError ret;

  if (!rec_name || !action_need)
    return INK_ERR_PARAMS;

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
INKError
ReadFile(INKFileNameT file, char **text, int *size, int *version)
{
  INKError ret;

  // marshal data into message request to be sent over socket
  // create connection and send request
  ret = send_file_read_request(main_socket_fd, file);
  if (ret != INK_ERR_OKAY)
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
INKError
WriteFile(INKFileNameT file, char *text, int size, int version)
{
  INKError ret;

  ret = send_file_write_request(main_socket_fd, file, version, size, text);
  if (ret != INK_ERR_OKAY)
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
INKError
EventSignal(char *event_name, va_list ap)
{
  return INK_ERR_FAIL;
}

/*-------------------------------------------------------------------------
 * EventResolve
 *-------------------------------------------------------------------------
 * purpose: Resolves the event of the specified name
 * note:    when sending the message request, actually sends the event name,
 *          not the event id
 */
INKError
EventResolve(char *event_name)
{
  if (!event_name)
    return INK_ERR_PARAMS;

  return (send_and_parse_name(EVENT_RESOLVE, event_name));
}

/*-------------------------------------------------------------------------
 * ActiveEventGetMlt
 *-------------------------------------------------------------------------
 * purpose: Retrieves a list of active(unresolved) events
 * note:    list of event names returned in network msg which must be tokenized
 */
INKError
ActiveEventGetMlt(LLQ * active_events)
{
  if (!active_events)
    return INK_ERR_PARAMS;

  return (send_and_parse_list(EVENT_GET_MLT, active_events));
}

/*-------------------------------------------------------------------------
 * EventIsActive
 *-------------------------------------------------------------------------
 * determines if the event_name is active; sets result in is_current
 */
INKError
EventIsActive(char *event_name, bool * is_current)
{
  INKError ret;

  if (!event_name || !is_current)
    return INK_ERR_PARAMS;

  // create and send request
  ret = send_request_name(main_socket_fd, EVENT_ACTIVE, event_name);
  if (ret != INK_ERR_OKAY)
    return ret;

  // parse the reply
  ret = parse_event_active_reply(main_socket_fd, is_current);
  if (ret != INK_ERR_OKAY)
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
INKError
EventSignalCbRegister(char *event_name, INKEventSignalFunc func, void *data)
{
  bool first_time = 0;
  INKError err;

  if (func == NULL)
    return INK_ERR_PARAMS;

  err = cb_table_register(remote_event_callbacks, event_name, func, data, &first_time);
  if (err != INK_ERR_OKAY)
    return err;

  // if we need to notify traffic manager of the event then send msg
  if (first_time) {
    err = send_request_name(event_socket_fd, EVENT_REG_CALLBACK, event_name);
    if (err != INK_ERR_OKAY)
      return err;
  }

  return INK_ERR_OKAY;
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
INKError
EventSignalCbUnregister(char *event_name, INKEventSignalFunc func)
{
  INKError err;

  // remove the callback function from the table
  err = cb_table_unregister(remote_event_callbacks, event_name, func);
  if (err != INK_ERR_OKAY)
    return err;

  // check if we need to notify traffic manager of the event (notify TM
  // only if the event has no callbacks)
  err = send_unregister_all_callbacks(event_socket_fd, remote_event_callbacks);
  if (err != INK_ERR_OKAY)
    return err;

  return INK_ERR_OKAY;
}

/***************************************************************************
 * Snapshots
 ***************************************************************************/
INKError
SnapshotTake(char *snapshot_name)
{
  if (!snapshot_name)
    return INK_ERR_PARAMS;

  return send_and_parse_name(SNAPSHOT_TAKE, snapshot_name);
}

INKError
SnapshotRestore(char *snapshot_name)
{
  if (!snapshot_name)
    return INK_ERR_PARAMS;

  return send_and_parse_name(SNAPSHOT_RESTORE, snapshot_name);
}

INKError
SnapshotRemove(char *snapshot_name)
{
  if (!snapshot_name)
    return INK_ERR_PARAMS;

  return send_and_parse_name(SNAPSHOT_REMOVE, snapshot_name);
}

INKError
SnapshotGetMlt(LLQ * snapshots)
{
  return send_and_parse_list(SNAPSHOT_GET_MLT, snapshots);
}

INKError
StatsReset()
{
  return send_and_parse_basic(STATS_RESET);
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
  INKError err;

  if (!passwd || !filepath)
    return INK_ERR_PARAMS;

  err = send_request_name_value(main_socket_fd, ENCRYPT_TO_FILE, passwd, filepath);
  if (err != INK_ERR_OKAY)
    return err;

  err = parse_reply(main_socket_fd);

  return err;
}
