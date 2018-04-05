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
 * Filename: TSControlMain.cc
 * Purpose: The main section for traffic server that handles all the requests
 *          from the user.
 * Created: 01/08/01
 * Created by: Stephanie Song
 *
 ***************************************************************************/

#include "mgmtapi.h"
#include "ts/ink_platform.h"
#include "ts/ink_sock.h"
#include "LocalManager.h"
#include "MgmtUtils.h"
#include "rpc/utils/MgmtSocket.h"
#include "TSControlMain.h"
#include "CoreAPI.h"
#include "CoreAPIShared.h"
#include "NetworkUtilsLocal.h"
#include "rpc/ServerControl.h"

#define TIMEOUT_SECS 1 // the num secs for select timeout

/*-------------------------------------------------------------------------
                             HANDLER FUNCTIONS
 --------------------------------------------------------------------------*/
/* NOTE: all the handle_xx functions basically, take the request, parse it,
 * and send a reply back to the remote client. So even if error occurs,
 * each handler functions MUST SEND A REPLY BACK!!
 */

/**************************************************************************
 * check_privileges
 *
 * purpose: checks if the caller has privileges to execute the local function
 *
 * input: fd - file descriptor of remote client
 *        priv - whether or not privilege is required
 * output: TS_ERR_OKAY or TS_ERR_PERMISSION_DENIED
 *************************************************************************/
static TSMgmtError
check_privileges(int fd, unsigned priv)
{
  if (mgmt_has_peereid()) {
    uid_t euid = -1;
    gid_t egid = -1;

    // For privileged calls, ensure we have caller credentials and that the caller is privileged.
    if (priv == MGMT_API_PRIVILEGED) {
      if (mgmt_get_peereid(fd, &euid, &egid) == -1 || (euid != 0 && euid != geteuid())) {
        Debug("ts_main", "denied privileged API access on fd=%d for uid=%d gid=%d", fd, euid, egid);
        return TS_ERR_PERMISSION_DENIED;
      }
    }
  }
  return TS_ERR_OKAY;
}

static TSMgmtError
marshall_rec_data(RecDataT rec_type, const RecData &rec_data, MgmtMarshallData &data)
{
  switch (rec_type) {
  case RECD_INT:
    data.ptr = const_cast<RecInt *>(&rec_data.rec_int);
    data.len = sizeof(TSInt);
    break;
  case RECD_COUNTER:
    data.ptr = const_cast<RecCounter *>(&rec_data.rec_counter);
    data.len = sizeof(TSCounter);
    break;
  case RECD_FLOAT:
    data.ptr = const_cast<RecFloat *>(&rec_data.rec_float);
    data.len = sizeof(TSFloat);
    break;
  case RECD_STRING:
    // Make sure to send the NULL in the string value response.
    if (rec_data.rec_string) {
      data.ptr = rec_data.rec_string;
      data.len = strlen(rec_data.rec_string) + 1;
    } else {
      data.ptr = (void *)"NULL";
      data.len = countof("NULL");
    }
    break;
  default: // invalid record type
    return TS_ERR_FAIL;
  }

  return TS_ERR_OKAY;
}

static TSMgmtError
send_record_get_response(int fd, const RecRecord *rec)
{
  MgmtMarshallInt type;
  MgmtMarshallInt rclass;
  MgmtMarshallString name;
  MgmtMarshallData value = {nullptr, 0};

  if (rec) {
    type   = rec->data_type;
    rclass = rec->rec_type;
    name   = const_cast<MgmtMarshallString>(rec->name);
  } else {
    type   = RECD_NULL;
    rclass = RECT_NULL;
    name   = nullptr;
  }

  switch (type) {
  case RECD_INT:
    type      = TS_REC_INT;
    value.ptr = (void *)&rec->data.rec_int;
    value.len = sizeof(RecInt);
    break;
  case RECD_COUNTER:
    type      = TS_REC_COUNTER;
    value.ptr = (void *)&rec->data.rec_counter;
    value.len = sizeof(RecCounter);
    break;
  case RECD_FLOAT:
    type      = TS_REC_FLOAT;
    value.ptr = (void *)&rec->data.rec_float;
    value.len = sizeof(RecFloat);
    break;
  case RECD_STRING:
    // For NULL string parameters, send the literal "NULL" to match the behavior of MgmtRecordGet(). Make sure to send
    // the trailing NULL.
    type = TS_REC_STRING;
    if (rec->data.rec_string) {
      value.ptr = rec->data.rec_string;
      value.len = strlen(rec->data.rec_string) + 1;
    } else {
      value.ptr = const_cast<char *>("NULL");
      value.len = countof("NULL");
    }
    break;
  default:
    type = TS_REC_UNDEFINED;
    break; // skip it
  }

  return mgmt_server->respond(fd, RECORD_GET, &rclass, &type, &name, &value);
}

/**************************************************************************
 * handle_record_get
 *
 * purpose: handles requests to retrieve values of certain variables
 *          in TM. (see local/TSCtrlFunc.cc)
 * input: socket information
 *        req - the msg sent (should = record name to get)
 * output: SUCC or ERR
 * note:
 *************************************************************************/
static void
send_record_get(const RecRecord *rec, void *edata)
{
  int *fd = (int *)edata;
  *fd     = send_record_get_response(*fd, rec);
}

TSMgmtError
handle_record_get(int fd, void *req, size_t reqlen)
{
  TSMgmtError ret = TS_ERR_OKAY;
  MgmtMarshallString name;

  int fderr = fd; // [in,out] variable for the fd and error

  if (mgmt_message_parse(req, reqlen, &name) == -1) {
    return TS_ERR_PARAMS;
  }

  if (strlen(name) == 0) {
    ret = TS_ERR_PARAMS;
    goto done;
  }

  fderr = fd;
  if (RecLookupRecord(name, send_record_get, &fderr) != REC_ERR_OKAY) {
    ret = TS_ERR_PARAMS;
    goto done;
  }

  // If the lookup succeeded, the final error is in "fderr".
  if (ret == TS_ERR_OKAY) {
    ret = (TSMgmtError)fderr;
  }

done:
  ats_free(name);
  return ret;
}

struct record_match_state {
  TSMgmtError err;
  int fd;
};

static void
send_record_match(const RecRecord *rec, void *edata)
{
  record_match_state *match = (record_match_state *)edata;

  if (match->err != TS_ERR_OKAY) {
    return;
  }

  match->err = send_record_get_response(match->fd, rec);
}

TSMgmtError
handle_record_match(int fd, void *req, size_t reqlen)
{
  record_match_state match;
  MgmtMarshallString name;

  if (mgmt_message_parse(req, reqlen, &name) == -1) {
    return TS_ERR_PARAMS;
  }

  if (strlen(name) == 0) {
    ats_free(name);
    return TS_ERR_FAIL;
  }

  match.err = TS_ERR_OKAY;
  match.fd  = fd;

  if (RecLookupMatchingRecords(RECT_ALL, name, send_record_match, &match) != REC_ERR_OKAY) {
    ats_free(name);
    return TS_ERR_FAIL;
  }

  ats_free(name);

  // If successful, send a list terminator.
  if (match.err == TS_ERR_OKAY) {
    return send_record_get_response(fd, nullptr);
  }

  return match.err;
}

/**************************************************************************
 * handle_record_set
 *
 * purpose: handles a set request sent by the client
 * output: SUCC or ERR
 * note: request format = <record name>DELIMITER<record_value>
 *************************************************************************/
TSMgmtError
handle_record_set(int fd, void *req, size_t reqlen)
{
  if (check_privileges(fd, MGMT_API_PRIVILEGED) == TS_ERR_PERMISSION_DENIED) {
    return TS_ERR_PERMISSION_DENIED;
  }

  TSMgmtError ret;
  TSActionNeedT action     = TS_ACTION_UNDEFINED;
  MgmtMarshallString name  = nullptr;
  MgmtMarshallString value = nullptr;

  if (mgmt_message_parse(req, reqlen, &name, &value) == -1) {
    ret = TS_ERR_FAIL;
    goto fail;
  }

  if (strlen(name) == 0) {
    ret = TS_ERR_PARAMS;
    goto fail;
  }

  // call CoreAPI call on Traffic Manager side
  ret = MgmtRecordSet(name, value, &action);

fail:
  ats_free(name);
  ats_free(value);

  MgmtMarshallInt act = action;
  if (mgmt_server->respond(fd, RECORD_SET, &act) != TS_ERR_OKAY) {
    ret = TS_ERR_FAIL;
  }
  return ret;
}

/**************************************************************************
 * handle_proxy_state_get
 *
 * purpose: handles request to get the state of the proxy (TS)
 * output: TS_ERR_xx
 * note: None
 *************************************************************************/
TSMgmtError
handle_proxy_state_get(int fd, void *req, size_t reqlen)
{
  TSMgmtError err;
  MgmtMarshallInt state = TS_PROXY_UNDEFINED;

  err = (reqlen == 0) ? TS_ERR_OKAY : TS_ERR_PARAMS;
  if (err == TS_ERR_OKAY) {
    state = ProxyStateGet();
  }

  mgmt_server->respond(fd, PROXY_STATE_GET, &state);
  return err;
}

/**************************************************************************
 * handle_proxy_state_set
 *
 * purpose: handles the request to set the state of the proxy (TS)
 * output: TS_ERR_xx
 * note: None
 *************************************************************************/
TSMgmtError
handle_proxy_state_set(int fd, void *req, size_t reqlen)
{
  if (check_privileges(fd, MGMT_API_PRIVILEGED) == TS_ERR_PERMISSION_DENIED) {
    return TS_ERR_PERMISSION_DENIED;
  }

  MgmtMarshallInt state;
  MgmtMarshallInt clear;

  if (mgmt_message_parse(req, reqlen, &state, &clear) == -1) {
    return TS_ERR_PARAMS;
  }

  return ProxyStateSet((TSProxyStateT)state, (TSCacheClearT)clear);
}

/**************************************************************************
 * handle_reconfigure
 *
 * purpose: handles request to reread the config files
 * output: TS_ERR_xx
 * note: None
 *************************************************************************/
TSMgmtError
handle_reconfigure(int fd, void *req, size_t reqlen)
{
  if (check_privileges(fd, MGMT_API_PRIVILEGED) == TS_ERR_PERMISSION_DENIED) {
    return TS_ERR_PERMISSION_DENIED;
  }

  TSMgmtError err;

  err = (reqlen == 0) ? TS_ERR_OKAY : TS_ERR_PARAMS; // expect empty message
  if (err == TS_ERR_OKAY) {
    err = Reconfigure();
  }

  return err;
}

/**************************************************************************
 * handle_restart
 *
 * purpose: handles request to restart TM and TS
 * output: TS_ERR_xx
 * note: None
 *************************************************************************/
TSMgmtError
handle_restart(int fd, void *req, size_t reqlen)
{
  if (check_privileges(fd, MGMT_API_PRIVILEGED) == TS_ERR_PERMISSION_DENIED) {
    return TS_ERR_PERMISSION_DENIED;
  }

  MgmtMarshallInt options;
  MgmtMarshallInt optype;
  TSMgmtError err;

  if (mgmt_message_parse(req, reqlen, &optype, &options) == -1) {
    return TS_ERR_PARAMS;
  }

  switch (optype) {
  case BOUNCE:
    err = Bounce(options);
    break;
  case RESTART:
    err = Restart(options);
    break;
  default:
    err = TS_ERR_PARAMS;
    break;
  }

  return err;
}

/**************************************************************************
 * handle_stop
 *
 * purpose: handles request to stop TS
 * output: TS_ERR_xx
 * note: None
 *************************************************************************/
TSMgmtError
handle_stop(int fd, void *req, size_t reqlen)
{
  if (check_privileges(fd, MGMT_API_PRIVILEGED) == TS_ERR_PERMISSION_DENIED) {
    return TS_ERR_PERMISSION_DENIED;
  }

  MgmtMarshallInt options;

  if (mgmt_message_parse(req, reqlen, &options) == -1) {
    return TS_ERR_PARAMS;
  }

  return Stop(options);
}

/**************************************************************************
 * handle_drain
 *
 * purpose: handles request to drain TS
 * output: TS_ERR_xx
 * note: None
 *************************************************************************/
TSMgmtError
handle_drain(int fd, void *req, size_t reqlen)
{
  if (check_privileges(fd, MGMT_API_PRIVILEGED) == TS_ERR_PERMISSION_DENIED) {
    return TS_ERR_PERMISSION_DENIED;
  }

  MgmtMarshallInt options;

  if (mgmt_message_parse(req, reqlen, &options) == -1) {
    return TS_ERR_PARAMS;
  }

  return Drain(options);
}

/**************************************************************************
 * handle_storage_device_cmd_offline
 *
 * purpose: handle storage offline command.
 * output: TS_ERR_xx
 * note: None
 *************************************************************************/
TSMgmtError
handle_storage_device_cmd_offline(int fd, void *req, size_t reqlen)
{
  if (check_privileges(fd, MGMT_API_PRIVILEGED) == TS_ERR_PERMISSION_DENIED) {
    return TS_ERR_PERMISSION_DENIED;
  }

  MgmtMarshallString name = nullptr;

  if (mgmt_message_parse(req, reqlen, &name) == -1) {
    return TS_ERR_PARAMS;
  }

  // forward to server
  lmgmt->signalEvent(MGMT_EVENT_STORAGE_DEVICE_CMD_OFFLINE, name);
  return TS_ERR_OKAY;
}

/**************************************************************************
 * handle_event_resolve
 *
 * purpose: handles request to resolve an event
 * output: TS_ERR_xx
 * note: the req should be the event name
 *************************************************************************/
TSMgmtError
handle_event_resolve(int fd, void *req, size_t reqlen)
{
  if (check_privileges(fd, MGMT_API_PRIVILEGED) == TS_ERR_PERMISSION_DENIED) {
    return TS_ERR_PERMISSION_DENIED;
  }

  MgmtMarshallString name = nullptr;
  TSMgmtError err;

  if (mgmt_message_parse(req, reqlen, &name) == -1) {
    return TS_ERR_PARAMS;
  }

  err = EventResolve(name);
  ats_free(name);
  return err;
}

/**************************************************************************
 * handle_event_get_mlt
 *
 * purpose: handles request to get list of active events
 * output: TS_ERR_xx
 * note: the req should be the event name
 *************************************************************************/
TSMgmtError
handle_event_get_mlt(int fd, void *req, size_t reqlen)
{
  LLQ *event_list = create_queue();
  char buf[MAX_BUF_SIZE];
  char *event_name;
  int buf_pos = 0;

  MgmtMarshallInt err;
  MgmtMarshallString list = nullptr;

  err = (reqlen == 0) ? TS_ERR_OKAY : TS_ERR_PARAMS;
  if (err != TS_ERR_OKAY) {
    goto done;
  }

  // call CoreAPI call on Traffic Manager side; req == event_name
  err = ActiveEventGetMlt(event_list);
  if (err != TS_ERR_OKAY) {
    goto done;
  }

  // iterate through list and put into a delimited string list
  memset(buf, 0, MAX_BUF_SIZE);
  while (!queue_is_empty(event_list)) {
    event_name = (char *)dequeue(event_list);
    if (event_name) {
      snprintf(buf + buf_pos, (MAX_BUF_SIZE - buf_pos), "%s%c", event_name, REMOTE_DELIM);
      buf_pos += (strlen(event_name) + 1);
      ats_free(event_name); // free the llq entry
    }
  }
  buf[buf_pos] = '\0'; // end the string

  // Point the send list to the filled buffer.
  list = buf;

done:
  delete_queue(event_list);
  return mgmt_server->respond(fd, EVENT_GET_MLT, &list);
}

/**************************************************************************
 * handle_event_active
 *
 * purpose: handles request to resolve an event
 * output: TS_ERR_xx
 * note: the req should be the event name
 *************************************************************************/
TSMgmtError
handle_event_active(int fd, void *req, size_t reqlen)
{
  bool active;
  MgmtMarshallString name = nullptr;

  MgmtMarshallInt err;
  MgmtMarshallInt bval = 0;

  if (mgmt_message_parse(req, reqlen, &name) == -1) {
    goto done;
  }

  if (strlen(name) == 0) {
    err = TS_ERR_PARAMS;
    goto done;
  }

  err = EventIsActive(name, &active);
  if (err == TS_ERR_OKAY) {
    bval = active ? 1 : 0;
  }

done:
  ats_free(name);
  return mgmt_server->respond(fd, EVENT_ACTIVE, &bval);
}

/**************************************************************************
 * handle_stats_reset
 *
 * purpose: handles request to reset statistics to default values
 * output: TS_ERR_xx
 *************************************************************************/
TSMgmtError
handle_stats_reset(int fd, void *req, size_t reqlen)
{
  if (check_privileges(fd, MGMT_API_PRIVILEGED) == TS_ERR_PERMISSION_DENIED) {
    return TS_ERR_PERMISSION_DENIED;
  }

  MgmtMarshallString name = nullptr;
  TSMgmtError err;

  if (mgmt_message_parse(req, reqlen, &name) == -1) {
    return TS_ERR_PARAMS;
  }

  err = StatsReset(name);
  ats_free(name);
  return err;
}

/**************************************************************************
 * handle_host_status_up
 *
 * purpose: handles request to reset statistics to default values
 * output: TS_ERR_xx
 *************************************************************************/
TSMgmtError
handle_host_status_up(int fd, void *req, size_t reqlen)
{
  if (check_privileges(fd, MGMT_API_PRIVILEGED) == TS_ERR_PERMISSION_DENIED) {
    return TS_ERR_PERMISSION_DENIED;
  }

  MgmtMarshallString name = nullptr;
  TSMgmtError err;

  if (mgmt_message_parse(req, reqlen, &name) == -1) {
    return TS_ERR_PARAMS;
  }

  err = HostStatusSetUp(name);
  ats_free(name);
  return err;
}

/**************************************************************************
 * handle_host_status_down
 *
 * purpose: handles request to reset statistics to default values
 * output: TS_ERR_xx
 *************************************************************************/
TSMgmtError
handle_host_status_down(int fd, void *req, size_t reqlen)
{
  if (check_privileges(fd, MGMT_API_PRIVILEGED) == TS_ERR_PERMISSION_DENIED) {
    return TS_ERR_PERMISSION_DENIED;
  }

  MgmtMarshallString name = nullptr;
  TSMgmtError err;

  if (mgmt_message_parse(req, reqlen, &name) == -1) {
    return TS_ERR_PARAMS;
  }

  err = HostStatusSetDown(name);
  ats_free(name);
  return err;
}
/**************************************************************************
 * handle_api_ping
 *
 * purpose: handles the API_PING messaghat is sent by API clients to keep
 *    the management socket alive
 * output: TS_ERR_xx. There is no response message.
 *************************************************************************/
TSMgmtError
handle_api_ping(int /* fd */, void *req, size_t reqlen)
{
  MgmtMarshallInt stamp;

  if (mgmt_message_parse(req, reqlen, &stamp) == -1) {
    return TS_ERR_PARAMS;
  }
  return TS_ERR_OKAY;
}

TSMgmtError
handle_server_backtrace(int fd, void *req, size_t reqlen)
{
  if (check_privileges(fd, MGMT_API_PRIVILEGED) == TS_ERR_PERMISSION_DENIED) {
    return TS_ERR_PERMISSION_DENIED;
  }

  MgmtMarshallInt options;
  MgmtMarshallString trace = nullptr;
  MgmtMarshallInt err;

  if (mgmt_message_parse(req, reqlen, &options) == -1) {
    return TS_ERR_PARAMS;
  }

  err = ServerBacktrace(options, &trace);

  err = mgmt_server->respond(fd, SERVER_BACKTRACE, &trace);
  ats_free(trace);

  return (TSMgmtError)err;
}

static void
send_record_describe(const RecRecord *rec, void *edata)
{
  MgmtMarshallString rec_name      = nullptr;
  MgmtMarshallData rec_value       = {nullptr, 0};
  MgmtMarshallData rec_default     = {nullptr, 0};
  MgmtMarshallInt rec_type         = TS_REC_UNDEFINED;
  MgmtMarshallInt rec_class        = RECT_NULL;
  MgmtMarshallInt rec_version      = 0;
  MgmtMarshallInt rec_rsb          = 0;
  MgmtMarshallInt rec_order        = 0;
  MgmtMarshallInt rec_access       = RECA_NULL;
  MgmtMarshallInt rec_update       = RECU_NULL;
  MgmtMarshallInt rec_updatetype   = 0;
  MgmtMarshallInt rec_checktype    = RECC_NULL;
  MgmtMarshallInt rec_source       = REC_SOURCE_NULL;
  MgmtMarshallString rec_checkexpr = nullptr;

  TSMgmtError err = TS_ERR_OKAY;

  record_match_state *match = (record_match_state *)edata;

  if (match->err != TS_ERR_OKAY) {
    return;
  }

  if (rec) {
    // We only describe config variables (for now).
    if (!REC_TYPE_IS_CONFIG(rec->rec_type)) {
      match->err = TS_ERR_PARAMS;
      return;
    }

    rec_name       = const_cast<char *>(rec->name);
    rec_type       = rec->data_type;
    rec_class      = rec->rec_type;
    rec_version    = rec->version;
    rec_rsb        = rec->rsb_id;
    rec_order      = rec->order;
    rec_access     = rec->config_meta.access_type;
    rec_update     = rec->config_meta.update_required;
    rec_updatetype = rec->config_meta.update_type;
    rec_checktype  = rec->config_meta.check_type;
    rec_source     = rec->config_meta.source;
    rec_checkexpr  = rec->config_meta.check_expr;

    switch (rec_type) {
    case RECD_INT:
      rec_type = TS_REC_INT;
      break;
    case RECD_FLOAT:
      rec_type = TS_REC_FLOAT;
      break;
    case RECD_STRING:
      rec_type = TS_REC_STRING;
      break;
    case RECD_COUNTER:
      rec_type = TS_REC_COUNTER;
      break;
    default:
      rec_type = TS_REC_UNDEFINED;
    }

    err = marshall_rec_data(rec->data_type, rec->data, rec_value);
    if (err != TS_ERR_OKAY) {
      goto done;
    }

    err = marshall_rec_data(rec->data_type, rec->data_default, rec_default);
    if (err != TS_ERR_OKAY) {
      goto done;
    }
  }

  err = mgmt_server->respond(match->fd, RECORD_DESCRIBE_CONFIG, &rec_name, &rec_value, &rec_default, &rec_type, &rec_class,
                             &rec_version, &rec_rsb, &rec_order, &rec_access, &rec_update, &rec_updatetype, &rec_checktype,
                             &rec_source, &rec_checkexpr);

done:
  match->err = err;
}

TSMgmtError
handle_record_describe(int fd, void *req, size_t reqlen)
{
  TSMgmtError ret = TS_ERR_OKAY;
  record_match_state match;
  MgmtMarshallInt options;
  MgmtMarshallString name;

  if (mgmt_message_parse(req, reqlen, &name, &options) == -1) {
    return TS_ERR_PARAMS;
  }

  if (strlen(name) == 0) {
    ret = TS_ERR_PARAMS;
    goto done;
  }

  match.err = TS_ERR_OKAY;
  match.fd  = fd;

  if (options & RECORD_DESCRIBE_FLAGS_MATCH) {
    if (RecLookupMatchingRecords(RECT_CONFIG | RECT_LOCAL, name, send_record_describe, &match) != REC_ERR_OKAY) {
      ret = TS_ERR_PARAMS;
      goto done;
    }

    // If successful, send a list terminator.
    if (match.err == TS_ERR_OKAY) {
      send_record_describe(nullptr, &match);
    }

  } else {
    if (RecLookupRecord(name, send_record_describe, &match) != REC_ERR_OKAY) {
      ret = TS_ERR_PARAMS;
      goto done;
    }
  }

  if (ret == TS_ERR_OKAY) {
    ret = match.err;
  }

done:
  ats_free(name);
  return ret;
}

/**************************************************************************
 * handle_lifecycle_message
 *
 * purpose: handle lifecyle message to plugins
 * output: TS_ERR_xx
 * note: None
 *************************************************************************/
TSMgmtError
handle_lifecycle_message(int fd, void *req, size_t reqlen)
{
  if (check_privileges(fd, MGMT_API_PRIVILEGED) == TS_ERR_PERMISSION_DENIED) {
    return TS_ERR_PERMISSION_DENIED;
  }

  MgmtMarshallString tag;
  MgmtMarshallData data;

  if (mgmt_message_parse(req, reqlen, &tag, &data) == -1) {
    return TS_ERR_PARAMS;
  }

  lmgmt->signalEvent(MGMT_EVENT_LIFECYCLE_MESSAGE, static_cast<char *>(req), reqlen);
  return TS_ERR_OKAY;
}