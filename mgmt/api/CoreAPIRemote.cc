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
 *  3) unmarshal: parse the reply (checking for TSMgmtError)
 *
 * Created: lant
 *
 ***************************************************************************/

#include "ts/ink_config.h"
#include "ts/ink_defs.h"
#include <strings.h>
#include "ts/ink_string.h"
#include "ts/I_Layout.h"
#include "ts/ParseRules.h"
#include "CoreAPI.h"
#include "CoreAPIShared.h"
#include "CfgContextUtils.h"
#include "NetworkUtilsRemote.h"
#include "EventCallback.h"
#include "MgmtMarshall.h"

// forward declarations
static TSMgmtError send_and_parse_list(OpType op, LLQ *list);
static TSMgmtError mgmt_record_set(const char *rec_name, const char *rec_val, TSActionNeedT *action_need);

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
 * send_and_parse_list (helper function)
 *-------------------------------------------------------------------------
 * helper function used by operations which only require sending a simple
 * operation type and parsing a string delimited list
 * (delimited with REMOTE_DELIM_STR) and storing the tokens in the list
 * parameter
 */
static TSMgmtError
send_and_parse_list(OpType op, LLQ *list)
{
  TSMgmtError ret;
  const char *tok;
  Tokenizer tokens(REMOTE_DELIM_STR);
  tok_iter_state i_state;

  OpType optype = op;
  MgmtMarshallInt err;
  MgmtMarshallData reply    = {NULL, 0};
  MgmtMarshallString strval = NULL;

  if (!list) {
    return TS_ERR_PARAMS;
  }

  // create and send request
  ret = MGMTAPI_SEND_MESSAGE(main_socket_fd, op, &optype);
  if (ret != TS_ERR_OKAY) {
    goto done;
  }

  ret = recv_mgmt_message(main_socket_fd, reply);
  if (ret != TS_ERR_OKAY) {
    goto done;
  }

  ret = recv_mgmt_response(reply.ptr, reply.len, op, &err, &strval);
  if (ret != TS_ERR_OKAY) {
    goto done;
  }

  if (err != TS_ERR_OKAY) {
    ret = (TSMgmtError)err;
    goto done;
  }

  // tokenize the strval and put into LLQ; use Tokenizer
  tokens.Initialize(strval, COPY_TOKS);
  tok = tokens.iterFirst(&i_state);
  while (tok != NULL) {
    enqueue(list, ats_strdup(tok)); // add token to LLQ
    tok = tokens.iterNext(&i_state);
  }

  ret = TS_ERR_OKAY;

done:
  ats_free(reply.ptr);
  ats_free(strval);
  return ret;
}

/*-------------------------------------------------------------------------
 * mgmt_record_set (helper function)
 *-------------------------------------------------------------------------
 * Helper function for all Set functions:
 * NOTE: regardless of the type of the record being set,
 * it is converted to a string. Then on the local side, the
 * CoreAPI::MgmtRecordSet function will do the appropriate type
 * conversion from the string to the record's type (eg. MgmtInt, MgmtString..)
 * Hence, on the local side, don't have to worry about typecasting a
 * void*. Just read out the string from socket and pass it MgmtRecordSet.
 */
static TSMgmtError
mgmt_record_set(const char *rec_name, const char *rec_val, TSActionNeedT *action_need)
{
  TSMgmtError ret;

  OpType optype            = OpType::RECORD_SET;
  MgmtMarshallString name  = const_cast<MgmtMarshallString>(rec_name);
  MgmtMarshallString value = const_cast<MgmtMarshallString>(rec_val);

  MgmtMarshallData reply = {NULL, 0};
  MgmtMarshallInt err;
  MgmtMarshallInt action = TS_ACTION_UNDEFINED;

  if (!rec_name || !rec_val || !action_need) {
    return TS_ERR_PARAMS;
  }

  *action_need = TS_ACTION_UNDEFINED;

  // create and send request
  ret = MGMTAPI_SEND_MESSAGE(main_socket_fd, OpType::RECORD_SET, &optype, &name, &value);
  if (ret != TS_ERR_OKAY) {
    return ret;
  }

  ret = recv_mgmt_message(main_socket_fd, reply);
  if (ret != TS_ERR_OKAY) {
    return ret;
  }

  ret = recv_mgmt_response(reply.ptr, reply.len, OpType::RECORD_SET, &err, &action);
  ats_free(reply.ptr);

  if (ret != TS_ERR_OKAY) {
    return ret;
  }

  if (err != TS_ERR_OKAY) {
    return (TSMgmtError)err;
  }

  *action_need = (TSActionNeedT)action;
  return TS_ERR_OKAY;
}

/***************************************************************************
 * SetUp Operations
 ***************************************************************************/
TSMgmtError
Init(const char *socket_path, TSInitOptionT options)
{
  TSMgmtError err = TS_ERR_OKAY;

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
    ts_event_thread = ink_thread_create(event_poll_thread_main, &event_socket_fd);
  } else {
    ts_event_thread = static_cast<ink_thread>(NULL);
  }

END:

  // create thread that periodically checks the socket connection
  // with TM alive - reconnects if not alive
  if (0 == (ts_init_options & TS_MGMT_OPT_NO_SOCK_TESTS)) {
    ts_test_thread = ink_thread_create(socket_test_thread, NULL);
  } else {
    ts_test_thread = static_cast<ink_thread>(NULL);
  }

  return err;
}

// does clean up for remote API client; destroy structures and disconnects
TSMgmtError
Terminate()
{
  TSMgmtError err;

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
  ts_test_thread  = static_cast<ink_thread>(NULL);
  ts_event_thread = static_cast<ink_thread>(NULL);
  set_socket_paths(NULL); // clear the socket_path

  return TS_ERR_OKAY;
}

// ONLY have very basic diag functionality for remote cliets.
// When a remote client tries to use diags (wants to output runtime
// diagnostics, the diagnostics will be outputted to the machine
// the remote client is logged into (the one TM is running on)
void
DiagnosticMessage(TSDiagsT mode, const char *fmt, va_list ap)
{
  char diag_msg[MAX_BUF_SIZE];

  OpType optype          = OpType::DIAGS;
  MgmtMarshallInt level  = mode;
  MgmtMarshallString msg = diag_msg;

  // format the diag message now so it can be sent
  // vsnprintf does not compile on DEC
  vsnprintf(diag_msg, MAX_BUF_SIZE - 1, fmt, ap);
  (void)MGMTAPI_SEND_MESSAGE(main_socket_fd, OpType::DIAGS, &optype, &level, &msg);
}

/***************************************************************************
 * Control Operations
 ***************************************************************************/
TSProxyStateT
ProxyStateGet()
{
  TSMgmtError ret;
  OpType optype          = OpType::PROXY_STATE_GET;
  MgmtMarshallData reply = {NULL, 0};
  MgmtMarshallInt err;
  MgmtMarshallInt state;

  ret = MGMTAPI_SEND_MESSAGE(main_socket_fd, OpType::PROXY_STATE_GET, &optype);
  if (ret != TS_ERR_OKAY) {
    return TS_PROXY_UNDEFINED;
  }

  ret = recv_mgmt_message(main_socket_fd, reply);
  if (ret != TS_ERR_OKAY) {
    return TS_PROXY_UNDEFINED;
  }

  ret = recv_mgmt_response(reply.ptr, reply.len, OpType::PROXY_STATE_GET, &err, &state);
  ats_free(reply.ptr);

  if (ret != TS_ERR_OKAY || err != TS_ERR_OKAY) {
    return TS_PROXY_UNDEFINED;
  }

  return (TSProxyStateT)state;
}

TSMgmtError
ProxyStateSet(TSProxyStateT state, TSCacheClearT clear)
{
  TSMgmtError ret;
  OpType optype          = OpType::PROXY_STATE_SET;
  MgmtMarshallInt pstate = state;
  MgmtMarshallInt pclear = clear;

  ret = MGMTAPI_SEND_MESSAGE(main_socket_fd, OpType::PROXY_STATE_SET, &optype, &pstate, &pclear);
  return (ret == TS_ERR_OKAY) ? parse_generic_response(OpType::PROXY_STATE_SET, main_socket_fd) : ret;
}

TSMgmtError
ServerBacktrace(unsigned options, char **trace)
{
  ink_release_assert(trace != NULL);
  TSMgmtError ret;
  MgmtMarshallInt err;
  OpType optype             = OpType::SERVER_BACKTRACE;
  MgmtMarshallInt flags     = options;
  MgmtMarshallData reply    = {NULL, 0};
  MgmtMarshallString strval = NULL;

  ret = MGMTAPI_SEND_MESSAGE(main_socket_fd, OpType::SERVER_BACKTRACE, &optype, &flags);
  if (ret != TS_ERR_OKAY) {
    goto fail;
  }

  ret = recv_mgmt_message(main_socket_fd, reply);
  if (ret != TS_ERR_OKAY) {
    goto fail;
  }

  ret = recv_mgmt_response(reply.ptr, reply.len, OpType::SERVER_BACKTRACE, &err, &strval);
  if (ret != TS_ERR_OKAY) {
    goto fail;
  }

  if (err != TS_ERR_OKAY) {
    ret = (TSMgmtError)err;
    goto fail;
  }

  ats_free(reply.ptr);
  *trace = strval;
  return TS_ERR_OKAY;

fail:
  ats_free(reply.ptr);
  ats_free(strval);
  return ret;
}

TSMgmtError
Reconfigure()
{
  TSMgmtError ret;
  OpType optype = OpType::RECONFIGURE;

  ret = MGMTAPI_SEND_MESSAGE(main_socket_fd, OpType::RECONFIGURE, &optype);
  return (ret == TS_ERR_OKAY) ? parse_generic_response(OpType::RECONFIGURE, main_socket_fd) : ret;
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
TSMgmtError
Restart(unsigned options)
{
  TSMgmtError ret;
  OpType optype        = OpType::RESTART;
  MgmtMarshallInt oval = options;

  ret = MGMTAPI_SEND_MESSAGE(main_socket_fd, OpType::RESTART, &optype, &oval);
  if (ret != TS_ERR_OKAY) {
    return ret;
  }

  ret = parse_generic_response(OpType::RESTART, main_socket_fd);
  if (ret == TS_ERR_OKAY) {
    ret = reconnect_loop(MAX_CONN_TRIES);
  }

  return ret;
}

/*-------------------------------------------------------------------------
 * Bounce
 *-------------------------------------------------------------------------
 * Restart the traffic_server process(es) only.
 */
TSMgmtError
Bounce(unsigned options)
{
  TSMgmtError ret;
  OpType optype        = OpType::BOUNCE;
  MgmtMarshallInt oval = options;

  ret = MGMTAPI_SEND_MESSAGE(main_socket_fd, OpType::BOUNCE, &optype, &oval);

  return (ret == TS_ERR_OKAY) ? parse_generic_response(OpType::BOUNCE, main_socket_fd) : ret;
}

/*-------------------------------------------------------------------------
 * StorageDeviceCmdOffline
 *-------------------------------------------------------------------------
 * Disable a storage device.
 */
TSMgmtError
StorageDeviceCmdOffline(char const *dev)
{
  TSMgmtError ret;
  OpType optype           = OpType::STORAGE_DEVICE_CMD_OFFLINE;
  MgmtMarshallString name = const_cast<MgmtMarshallString>(dev);

  ret = MGMTAPI_SEND_MESSAGE(main_socket_fd, OpType::STORAGE_DEVICE_CMD_OFFLINE, &optype, &name);
  return (ret == TS_ERR_OKAY) ? parse_generic_response(OpType::STORAGE_DEVICE_CMD_OFFLINE, main_socket_fd) : ret;
}

/***************************************************************************
 * Record Operations
 ***************************************************************************/

static void
mgmt_record_convert_value(TSRecordT rec_type, const MgmtMarshallData &data, TSRecordValueT &value)
{
  // convert the record value to appropriate type
  if (data.ptr) {
    switch (rec_type) {
    case TS_REC_INT:
      ink_assert(data.len == sizeof(TSInt));
      value.int_val = *(TSInt *)data.ptr;
      break;
    case TS_REC_COUNTER:
      ink_assert(data.len == sizeof(TSCounter));
      value.counter_val = *(TSCounter *)data.ptr;
      break;
    case TS_REC_FLOAT:
      ink_assert(data.len == sizeof(TSFloat));
      value.float_val = *(TSFloat *)data.ptr;
      break;
    case TS_REC_STRING:
      ink_assert(data.len == strlen((char *)data.ptr) + 1);
      value.string_val = ats_strdup((char *)data.ptr);
      break;
    default:; // nothing ... shut up compiler!
    }
  }
}

static TSMgmtError
mgmt_record_get_reply(OpType op, TSRecordEle *rec_ele)
{
  TSMgmtError ret;

  MgmtMarshallData reply = {NULL, 0};
  MgmtMarshallInt err;
  MgmtMarshallInt rclass;
  MgmtMarshallInt type;
  MgmtMarshallString name = NULL;
  MgmtMarshallData value  = {NULL, 0};

  ink_zero(*rec_ele);
  rec_ele->rec_type = TS_REC_UNDEFINED;

  // Receive the next record reply.
  ret = recv_mgmt_message(main_socket_fd, reply);
  if (ret != TS_ERR_OKAY) {
    return ret;
  }

  ret = recv_mgmt_response(reply.ptr, reply.len, op, &err, &rclass, &type, &name, &value);
  ats_free(reply.ptr);
  if (ret != TS_ERR_OKAY) {
    goto done;
  }

  if (err != TS_ERR_OKAY) {
    ret = (TSMgmtError)err;
    goto done;
  }

  rec_ele->rec_class = (TSInt)rclass;
  rec_ele->rec_type  = (TSRecordT)type;
  rec_ele->rec_name  = ats_strdup(name);
  mgmt_record_convert_value(rec_ele->rec_type, value, rec_ele->valueT);

done:
  ats_free(name);
  ats_free(value.ptr);
  return ret;
}

static TSMgmtError
mgmt_record_describe_reply(TSConfigRecordDescription *val)
{
  TSMgmtError ret;
  MgmtMarshallData reply = {NULL, 0};

  ret = recv_mgmt_message(main_socket_fd, reply);
  if (ret != TS_ERR_OKAY) {
    return ret;
  }

  MgmtMarshallInt err;
  MgmtMarshallString name = NULL;
  MgmtMarshallString expr = NULL;
  MgmtMarshallData value  = {NULL, 0};
  MgmtMarshallData deflt  = {NULL, 0};

  MgmtMarshallInt rtype;
  MgmtMarshallInt rclass;
  MgmtMarshallInt version;
  MgmtMarshallInt rsb;
  MgmtMarshallInt order;
  MgmtMarshallInt access;
  MgmtMarshallInt update;
  MgmtMarshallInt updatetype;
  MgmtMarshallInt checktype;
  MgmtMarshallInt source;

  ret = recv_mgmt_response(reply.ptr, reply.len, OpType::RECORD_DESCRIBE_CONFIG, &err, &name, &value, &deflt, &rtype, &rclass,
                           &version, &rsb, &order, &access, &update, &updatetype, &checktype, &source, &expr);

  ats_free(reply.ptr);

  if (ret != TS_ERR_OKAY) {
    goto done;
  }

  if (err != TS_ERR_OKAY) {
    ret = (TSMgmtError)err;
    goto done;
  }

  // Everything is cool, populate the description ...
  val->rec_name       = ats_strdup(name);
  val->rec_checkexpr  = ats_strdup(expr);
  val->rec_type       = (TSRecordT)rtype;
  val->rec_class      = rclass;
  val->rec_version    = version;
  val->rec_rsb        = rsb;
  val->rec_order      = order;
  val->rec_access     = access;
  val->rec_updatetype = updatetype;
  val->rec_checktype  = checktype;
  val->rec_source     = source;

  mgmt_record_convert_value(val->rec_type, value, val->rec_value);
  mgmt_record_convert_value(val->rec_type, deflt, val->rec_default);

done:
  ats_free(name);
  ats_free(expr);
  ats_free(value.ptr);
  ats_free(deflt.ptr);
  return ret;
}

// note that the record value is being sent as chunk of memory, regardless of
// record type; it's not being converted to a string!!
TSMgmtError
MgmtRecordGet(const char *rec_name, TSRecordEle *rec_ele)
{
  TSMgmtError ret;
  OpType optype             = OpType::RECORD_GET;
  MgmtMarshallString record = const_cast<MgmtMarshallString>(rec_name);

  if (!rec_name || !rec_ele) {
    return TS_ERR_PARAMS;
  }

  // create and send request
  ret = MGMTAPI_SEND_MESSAGE(main_socket_fd, OpType::RECORD_GET, &optype, &record);
  return (ret == TS_ERR_OKAY) ? mgmt_record_get_reply(OpType::RECORD_GET, rec_ele) : ret;
}

TSMgmtError
MgmtConfigRecordDescribeMatching(const char *rec_name, unsigned options, TSList rec_vals)
{
  TSMgmtError ret;
  OpType optype             = OpType::RECORD_DESCRIBE_CONFIG;
  MgmtMarshallInt flags     = options | RECORD_DESCRIBE_FLAGS_MATCH;
  MgmtMarshallString record = const_cast<MgmtMarshallString>(rec_name);

  // create and send request
  ret = MGMTAPI_SEND_MESSAGE(main_socket_fd, OpType::RECORD_DESCRIBE_CONFIG, &optype, &record, &flags);
  if (ret != TS_ERR_OKAY) {
    return ret;
  }

  for (;;) {
    TSConfigRecordDescription *val;

    val = TSConfigRecordDescriptionCreate();

    // parse the reply to get record value and type
    ret = mgmt_record_describe_reply(val);
    if (ret != TS_ERR_OKAY) {
      TSConfigRecordDescriptionDestroy(val);
      goto fail;
    }

    // A NULL record ends the list.
    if (val->rec_type == TS_REC_UNDEFINED) {
      TSConfigRecordDescriptionDestroy(val);
      break;
    }

    enqueue((LLQ *)rec_vals, val);
  }

  return TS_ERR_OKAY;

fail:
  while (!queue_is_empty((LLQ *)rec_vals)) {
    TSConfigRecordDescription *val = (TSConfigRecordDescription *)dequeue((LLQ *)rec_vals);
    TSConfigRecordDescriptionDestroy(val);
  }

  return ret;
}

TSMgmtError
MgmtConfigRecordDescribe(const char *rec_name, unsigned options, TSConfigRecordDescription *val)
{
  TSMgmtError ret;
  OpType optype             = OpType::RECORD_DESCRIBE_CONFIG;
  MgmtMarshallInt flags     = options & ~RECORD_DESCRIBE_FLAGS_MATCH;
  MgmtMarshallString record = const_cast<MgmtMarshallString>(rec_name);

  // create and send request
  ret = MGMTAPI_SEND_MESSAGE(main_socket_fd, OpType::RECORD_DESCRIBE_CONFIG, &optype, &record, &flags);
  if (ret != TS_ERR_OKAY) {
    return ret;
  }

  return mgmt_record_describe_reply(val);
}

TSMgmtError
MgmtRecordGetMatching(const char *regex, TSList rec_vals)
{
  TSMgmtError ret;
  TSRecordEle *rec_ele;

  OpType optype             = OpType::RECORD_MATCH_GET;
  MgmtMarshallString record = const_cast<MgmtMarshallString>(regex);

  if (!regex || !rec_vals) {
    return TS_ERR_PARAMS;
  }

  ret = MGMTAPI_SEND_MESSAGE(main_socket_fd, OpType::RECORD_MATCH_GET, &optype, &record);
  if (ret != TS_ERR_OKAY) {
    return ret;
  }

  for (;;) {
    rec_ele = TSRecordEleCreate();

    // parse the reply to get record value and type
    ret = mgmt_record_get_reply(OpType::RECORD_MATCH_GET, rec_ele);
    if (ret != TS_ERR_OKAY) {
      TSRecordEleDestroy(rec_ele);
      goto fail;
    }

    // A NULL record ends the list.
    if (rec_ele->rec_type == TS_REC_UNDEFINED) {
      TSRecordEleDestroy(rec_ele);
      break;
    }

    enqueue((LLQ *)rec_vals, rec_ele);
  }

  return TS_ERR_OKAY;

fail:
  while (!queue_is_empty((LLQ *)rec_vals)) {
    rec_ele = (TSRecordEle *)dequeue((LLQ *)rec_vals);
    TSRecordEleDestroy(rec_ele);
  }

  return ret;
}

TSMgmtError
MgmtRecordSet(const char *rec_name, const char *val, TSActionNeedT *action_need)
{
  TSMgmtError ret;

  if (!rec_name || !val || !action_need)
    return TS_ERR_PARAMS;

  ret = mgmt_record_set(rec_name, val, action_need);
  return ret;
}

// first convert the MgmtInt into a string
// NOTE: use long long, not just long, MgmtInt = int64_t
TSMgmtError
MgmtRecordSetInt(const char *rec_name, MgmtInt int_val, TSActionNeedT *action_need)
{
  char str_val[MAX_RECORD_SIZE];
  TSMgmtError ret;

  if (!rec_name || !action_need)
    return TS_ERR_PARAMS;

  bzero(str_val, MAX_RECORD_SIZE);
  snprintf(str_val, sizeof(str_val), "%" PRId64 "", int_val);
  ret = mgmt_record_set(rec_name, str_val, action_need);

  return ret;
}

// first convert the MgmtIntCounter into a string
TSMgmtError
MgmtRecordSetCounter(const char *rec_name, MgmtIntCounter counter_val, TSActionNeedT *action_need)
{
  char str_val[MAX_RECORD_SIZE];
  TSMgmtError ret;

  if (!rec_name || !action_need)
    return TS_ERR_PARAMS;

  bzero(str_val, MAX_RECORD_SIZE);
  snprintf(str_val, sizeof(str_val), "%" PRId64 "", counter_val);
  ret = mgmt_record_set(rec_name, str_val, action_need);

  return ret;
}

// first convert the MgmtFloat into string
TSMgmtError
MgmtRecordSetFloat(const char *rec_name, MgmtFloat float_val, TSActionNeedT *action_need)
{
  char str_val[MAX_RECORD_SIZE];
  TSMgmtError ret;

  bzero(str_val, MAX_RECORD_SIZE);
  if (snprintf(str_val, sizeof(str_val), "%f", float_val) < 0)
    return TS_ERR_SYS_CALL;
  ret = mgmt_record_set(rec_name, str_val, action_need);

  return ret;
}

TSMgmtError
MgmtRecordSetString(const char *rec_name, const char *string_val, TSActionNeedT *action_need)
{
  TSMgmtError ret;

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
TSMgmtError
ReadFile(TSFileNameT file, char **text, int *size, int *version)
{
  TSMgmtError ret;
  OpType optype       = OpType::FILE_READ;
  MgmtMarshallInt fid = file;

  MgmtMarshallData reply = {NULL, 0};
  MgmtMarshallInt err;
  MgmtMarshallInt vers;
  MgmtMarshallData data = {NULL, 0};

  *text = NULL;
  *size = *version = 0;

  ret = MGMTAPI_SEND_MESSAGE(main_socket_fd, OpType::FILE_READ, &optype, &fid);
  if (ret != TS_ERR_OKAY) {
    return ret;
  }

  ret = recv_mgmt_message(main_socket_fd, reply);
  if (ret != TS_ERR_OKAY) {
    return ret;
  }

  ret = recv_mgmt_response(reply.ptr, reply.len, OpType::FILE_READ, &err, &vers, &data);
  ats_free(reply.ptr);

  if (ret != TS_ERR_OKAY) {
    return ret;
  }

  if (err != TS_ERR_OKAY) {
    return (TSMgmtError)err;
  }

  *version = vers;
  *text    = (char *)data.ptr;
  *size    = (int)data.len;
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
 *
 * Marshals a write file request that can be sent over the unix domain socket.
 * Connects to the socket and sends request over. Parses the response from
 * Traffic Manager.
 */
TSMgmtError
WriteFile(TSFileNameT file, const char *text, int size, int version)
{
  TSMgmtError ret;

  OpType optype         = OpType::FILE_WRITE;
  MgmtMarshallInt fid   = file;
  MgmtMarshallInt vers  = version;
  MgmtMarshallData data = {(void *)text, (size_t)size};

  ret = MGMTAPI_SEND_MESSAGE(main_socket_fd, OpType::FILE_WRITE, &optype, &fid, &vers, &data);
  return (ret == TS_ERR_OKAY) ? parse_generic_response(OpType::FILE_WRITE, main_socket_fd) : ret;
}

/***************************************************************************
 * Events
 ***************************************************************************/
/*-------------------------------------------------------------------------
 * EventSignal
 *-------------------------------------------------------------------------
 * LAN - need to implement
 */
TSMgmtError
EventSignal(const char * /* event_name ATS_UNUSED */, va_list /* ap ATS_UNUSED */)
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
TSMgmtError
EventResolve(const char *event_name)
{
  TSMgmtError ret;
  OpType optype           = OpType::EVENT_RESOLVE;
  MgmtMarshallString name = const_cast<MgmtMarshallString>(event_name);

  if (!event_name)
    return TS_ERR_PARAMS;

  ret = MGMTAPI_SEND_MESSAGE(main_socket_fd, OpType::EVENT_RESOLVE, &optype, &name);
  return (ret == TS_ERR_OKAY) ? parse_generic_response(OpType::EVENT_RESOLVE, main_socket_fd) : ret;
}

/*-------------------------------------------------------------------------
 * ActiveEventGetMlt
 *-------------------------------------------------------------------------
 * purpose: Retrieves a list of active(unresolved) events
 * note:    list of event names returned in network msg which must be tokenized
 */
TSMgmtError
ActiveEventGetMlt(LLQ *active_events)
{
  if (!active_events)
    return TS_ERR_PARAMS;

  return (send_and_parse_list(OpType::EVENT_GET_MLT, active_events));
}

/*-------------------------------------------------------------------------
 * EventIsActive
 *-------------------------------------------------------------------------
 * determines if the event_name is active; sets result in is_current
 */
TSMgmtError
EventIsActive(const char *event_name, bool *is_current)
{
  TSMgmtError ret;
  OpType optype           = OpType::EVENT_ACTIVE;
  MgmtMarshallString name = const_cast<MgmtMarshallString>(event_name);

  MgmtMarshallData reply = {NULL, 0};
  MgmtMarshallInt err;
  MgmtMarshallInt bval;

  if (!event_name || !is_current)
    return TS_ERR_PARAMS;

  // create and send request
  ret = MGMTAPI_SEND_MESSAGE(main_socket_fd, OpType::EVENT_ACTIVE, &optype, &name);
  if (ret != TS_ERR_OKAY) {
    return ret;
  }

  ret = recv_mgmt_message(main_socket_fd, reply);
  if (ret != TS_ERR_OKAY) {
    return ret;
  }

  ret = recv_mgmt_response(reply.ptr, reply.len, OpType::EVENT_ACTIVE, &err, &bval);
  ats_free(reply.ptr);

  if (ret != TS_ERR_OKAY) {
    return ret;
  }

  *is_current = (bval != 0);
  return (TSMgmtError)err;
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
TSMgmtError
EventSignalCbRegister(const char *event_name, TSEventSignalFunc func, void *data)
{
  bool first_time = false;
  TSMgmtError ret;

  if (func == NULL)
    return TS_ERR_PARAMS;
  if (!remote_event_callbacks)
    return TS_ERR_FAIL;

  ret = cb_table_register(remote_event_callbacks, event_name, func, data, &first_time);
  if (ret != TS_ERR_OKAY)
    return ret;

  // if we need to notify traffic manager of the event then send msg
  if (first_time) {
    OpType optype           = OpType::EVENT_REG_CALLBACK;
    MgmtMarshallString name = const_cast<MgmtMarshallString>(event_name);

    ret = MGMTAPI_SEND_MESSAGE(main_socket_fd, OpType::EVENT_REG_CALLBACK, &optype, &name);
    if (ret != TS_ERR_OKAY) {
      return ret;
    }
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
TSMgmtError
EventSignalCbUnregister(const char *event_name, TSEventSignalFunc func)
{
  TSMgmtError err;

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
static TSMgmtError
snapshot_message(OpType op, const char *snapshot_name)
{
  TSMgmtError ret;
  OpType optype           = op;
  MgmtMarshallString name = const_cast<MgmtMarshallString>(snapshot_name);

  if (!snapshot_name)
    return TS_ERR_PARAMS;

  ret = MGMTAPI_SEND_MESSAGE(main_socket_fd, op, &optype, &name);
  return (ret == TS_ERR_OKAY) ? parse_generic_response(op, main_socket_fd) : ret;
}

TSMgmtError
SnapshotTake(const char *snapshot_name)
{
  return snapshot_message(OpType::SNAPSHOT_TAKE, snapshot_name);
}

TSMgmtError
SnapshotRestore(const char *snapshot_name)
{
  return snapshot_message(OpType::SNAPSHOT_RESTORE, snapshot_name);
}

TSMgmtError
SnapshotRemove(const char *snapshot_name)
{
  return snapshot_message(OpType::SNAPSHOT_REMOVE, snapshot_name);
}

TSMgmtError
SnapshotGetMlt(LLQ *snapshots)
{
  if (!snapshots)
    return TS_ERR_PARAMS;

  return send_and_parse_list(OpType::SNAPSHOT_GET_MLT, snapshots);
}

TSMgmtError
StatsReset(bool cluster, const char *stat_name)
{
  TSMgmtError ret;
  OpType op               = cluster ? OpType::STATS_RESET_CLUSTER : OpType::STATS_RESET_NODE;
  OpType optype           = op;
  MgmtMarshallString name = const_cast<MgmtMarshallString>(stat_name);

  ret = MGMTAPI_SEND_MESSAGE(main_socket_fd, op, &optype, &name);
  return (ret == TS_ERR_OKAY) ? parse_generic_response(op, main_socket_fd) : ret;
}
