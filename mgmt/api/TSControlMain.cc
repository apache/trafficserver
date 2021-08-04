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
#include "tscore/ink_platform.h"
#include "tscore/ink_sock.h"
#include "LocalManager.h"
#include "MgmtUtils.h"
#include "MgmtSocket.h"
#include "NetworkMessage.h"
#include "TSControlMain.h"
#include "CoreAPI.h"
#include "CoreAPIShared.h"
#include "NetworkUtilsLocal.h"

#include <unordered_map>

#define TIMEOUT_SECS 1 // the num secs for select timeout

static std::unordered_map<int, ClientT *> accepted_con; // a list of all accepted client connections

static TSMgmtError handle_control_message(int fd, void *msg, size_t msglen);

/*********************************************************************
 * create_client
 *
 * purpose: creates a new ClientT and return pointer to it
 * input: None
 * output: ClientT
 * note: created for each accepted client connection
 *********************************************************************/
static ClientT *
create_client()
{
  ClientT *ele = static_cast<ClientT *>(ats_malloc(sizeof(ClientT)));

  ele->adr = static_cast<struct sockaddr *>(ats_malloc(sizeof(struct sockaddr)));
  return ele;
}

/*********************************************************************
 * delete_client
 *
 * purpose: frees dynamic memory allocated for a ClientT
 * input: client - the ClientT to free
 * output:
 *********************************************************************/
static void
delete_client(ClientT *client)
{
  if (client) {
    ats_free(client->adr);
    ats_free(client);
  }
  return;
}

/*********************************************************************
 * remove_client
 *
 * purpose: removes the ClientT from the specified hashtable; includes
 *          removing the binding and freeing the ClientT
 * input: client - the ClientT to remove
 * output:
 *********************************************************************/
static void
remove_client(ClientT *client, std::unordered_map<int, ClientT *> &table)
{
  // close client socket
  close_socket(client->fd);

  // remove client binding from hash table
  table.erase(client->fd);

  // free ClientT
  delete_client(client);

  return;
}

/*********************************************************************
 * ts_ctrl_main
 *
 * This function is run as a thread in WebIntrMain.cc that listens on a
 * specified socket. It loops until Traffic Manager dies.
 * In the loop, it just listens on a socket, ready to accept any connections,
 * until receives a request from the remote API client. Parse the request
 * to determine which CoreAPI call to make.
 *********************************************************************/
void *
ts_ctrl_main(void *arg)
{
  int ret;
  int *socket_fd;
  int con_socket_fd; // main socket for listening to new connections

  socket_fd     = static_cast<int *>(arg);
  con_socket_fd = *socket_fd;

  // now we can start listening, accepting connections and servicing requests
  int new_con_fd; // new socket fd when accept connection

  fd_set selectFDs;      // for select call
  ClientT *client_entry; // an entry of fd to alarms mapping
  struct timeval timeout;

  // loops until TM dies; waits for and processes requests from clients
  while (true) {
    // LINUX: to prevent hard-spin of CPU,  reset timeout on each loop
    timeout.tv_sec  = TIMEOUT_SECS;
    timeout.tv_usec = 0;

    FD_ZERO(&selectFDs);

    if (con_socket_fd >= 0) {
      FD_SET(con_socket_fd, &selectFDs);
      // Debug("ts_main", "[ts_ctrl_main] add fd %d to select set", con_socket_fd);
    }

    for (auto &&it : accepted_con) {
      client_entry = it.second;
      if (client_entry->fd >= 0) { // add fd to select set
        FD_SET(client_entry->fd, &selectFDs);
        Debug("ts_main", "[ts_ctrl_main] add fd %d to select set", client_entry->fd);
      }
    }

    // select call - timeout is set so we can check events at regular intervals
    int fds_ready = mgmt_select(FD_SETSIZE, &selectFDs, (fd_set *)nullptr, (fd_set *)nullptr, &timeout);

    // check if have any connections or requests
    if (fds_ready > 0) {
      // first check for connections!
      if (con_socket_fd >= 0 && FD_ISSET(con_socket_fd, &selectFDs)) {
        fds_ready--;

        // create a new instance to store client connection info
        ClientT *new_client_con = create_client();
        if (!new_client_con) {
          // return TS_ERR_SYS_CALL; WHAT TO DO? just keep going
          Debug("ts_main", "[ts_ctrl_main] can't allocate new ClientT");
        } else { // accept connection
          socklen_t addr_len = (sizeof(struct sockaddr));
          new_con_fd         = mgmt_accept(con_socket_fd, new_client_con->adr, &addr_len);
          new_client_con->fd = new_con_fd;
          accepted_con.emplace(new_client_con->fd, new_client_con);
          Debug("ts_main", "[ts_ctrl_main] Add new client connection");
        }
      } // end if(new_con_fd >= 0 && FD_ISSET(new_con_fd, &selectFDs))

      // some other file descriptor; for each one, service request
      if (fds_ready > 0) { // RECEIVED A REQUEST from remote API client
        // see if there are more fd to set - iterate through all entries in hash table

        for (auto it = accepted_con.begin(); it != accepted_con.end();) {
          Debug("ts_main", "[ts_ctrl_main] We have a remote client request!");
          client_entry = it->second;
          ++it; // prevent the breaking of remove_client
          // got information; check
          if (client_entry->fd && FD_ISSET(client_entry->fd, &selectFDs)) {
            void *req = nullptr;
            size_t reqlen;

            ret = preprocess_msg(client_entry->fd, &req, &reqlen);
            if (ret == TS_ERR_NET_READ || ret == TS_ERR_NET_EOF) {
              // occurs when remote API client terminates connection
              Debug("ts_main", "[ts_ctrl_main] ERROR: preprocess_msg - remove client %d ", client_entry->fd);
              remove_client(client_entry, accepted_con);
              continue;
            }

            ret = handle_control_message(client_entry->fd, req, reqlen);
            ats_free(req);

            if (ret != TS_ERR_OKAY) {
              Debug("ts_main", "[ts_ctrl_main] ERROR: sending response for message (%d)", ret);
              // XXX this doesn't actually send a error response ...
              remove_client(client_entry, accepted_con);
              continue;
            }

          } // end if(client_entry->fd && FD_ISSET(client_entry->fd, &selectFDs))
        }   // end for (auto it = accepted_con.begin(); it != accepted_con.end();)
      }     // end if (fds_ready > 0)

    } // end if (fds_ready > 0)

  } // end while (1)

  // if we get here something's wrong, just clean up
  Debug("ts_main", "[ts_ctrl_main] CLOSING AND SHUTTING DOWN OPERATIONS");
  close_socket(con_socket_fd);

  for (auto &&it : accepted_con) {
    client_entry = it.second;
    if (client_entry->fd >= 0) {
      close_socket(client_entry->fd); // close socket
    }
    accepted_con.erase(client_entry->fd);
    delete_client(client_entry); // free ClientT
  }
  // all entries should be removed and freed already
  accepted_con.clear();

  ink_thread_exit(nullptr);
  return nullptr;
}

/*-------------------------------------------------------------------------
                             HANDLER FUNCTIONS
 --------------------------------------------------------------------------*/
/* NOTE: all the handle_xx functions basically, take the request, parse it,
 * and send a reply back to the remote client. So even if error occurs,
 * each handler functions MUST SEND A REPLY BACK!!
 */

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
  MgmtMarshallInt err = TS_ERR_OKAY;
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

  return send_mgmt_response(fd, OpType::RECORD_GET, &err, &rclass, &type, &name, &value);
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
  int *fd = static_cast<int *>(edata);
  *fd     = send_record_get_response(*fd, rec);
}

static TSMgmtError
handle_record_get(int fd, void *req, size_t reqlen)
{
  TSMgmtError ret;
  MgmtMarshallInt optype;
  MgmtMarshallString name;

  int fderr = fd; // [in,out] variable for the fd and error

  ret = recv_mgmt_request(req, reqlen, OpType::RECORD_GET, &optype, &name);
  if (ret != TS_ERR_OKAY) {
    return ret;
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
    ret = static_cast<TSMgmtError>(fderr);
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
  record_match_state *match = static_cast<record_match_state *>(edata);

  if (match->err != TS_ERR_OKAY) {
    return;
  }

  match->err = send_record_get_response(match->fd, rec);
}

static TSMgmtError
handle_record_match(int fd, void *req, size_t reqlen)
{
  TSMgmtError ret;
  record_match_state match;
  MgmtMarshallInt optype;
  MgmtMarshallString name;

  ret = recv_mgmt_request(req, reqlen, OpType::RECORD_MATCH_GET, &optype, &name);
  if (ret != TS_ERR_OKAY) {
    return ret;
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
static TSMgmtError
handle_record_set(int fd, void *req, size_t reqlen)
{
  TSMgmtError ret;
  TSActionNeedT action = TS_ACTION_UNDEFINED;
  MgmtMarshallInt optype;
  MgmtMarshallString name  = nullptr;
  MgmtMarshallString value = nullptr;

  ret = recv_mgmt_request(req, reqlen, OpType::RECORD_SET, &optype, &name, &value);
  if (ret != TS_ERR_OKAY) {
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

  MgmtMarshallInt err = ret;
  MgmtMarshallInt act = action;
  return send_mgmt_response(fd, OpType::RECORD_SET, &err, &act);
}

/**************************************************************************
 * handle_proxy_state_get
 *
 * purpose: handles request to get the state of the proxy (TS)
 * output: TS_ERR_xx
 * note: None
 *************************************************************************/
static TSMgmtError
handle_proxy_state_get(int fd, void *req, size_t reqlen)
{
  MgmtMarshallInt optype;
  MgmtMarshallInt err;
  MgmtMarshallInt state = TS_PROXY_UNDEFINED;

  err = recv_mgmt_request(req, reqlen, OpType::PROXY_STATE_GET, &optype);
  if (err == TS_ERR_OKAY) {
    state = ProxyStateGet();
  }

  return send_mgmt_response(fd, OpType::PROXY_STATE_GET, &err, &state);
}

/**************************************************************************
 * handle_proxy_state_set
 *
 * purpose: handles the request to set the state of the proxy (TS)
 * output: TS_ERR_xx
 * note: None
 *************************************************************************/
static TSMgmtError
handle_proxy_state_set(int fd, void *req, size_t reqlen)
{
  MgmtMarshallInt optype;
  MgmtMarshallInt state;
  MgmtMarshallInt clear;

  MgmtMarshallInt err;

  err = recv_mgmt_request(req, reqlen, OpType::PROXY_STATE_SET, &optype, &state, &clear);
  if (err != TS_ERR_OKAY) {
    return send_mgmt_response(fd, OpType::PROXY_STATE_SET, &err);
  }

  err = ProxyStateSet(static_cast<TSProxyStateT>(state), static_cast<TSCacheClearT>(clear));
  return send_mgmt_response(fd, OpType::PROXY_STATE_SET, &err);
}

/**************************************************************************
 * handle_reconfigure
 *
 * purpose: handles request to reread the config files
 * output: TS_ERR_xx
 * note: None
 *************************************************************************/
static TSMgmtError
handle_reconfigure(int fd, void *req, size_t reqlen)
{
  MgmtMarshallInt err;
  MgmtMarshallInt optype;

  err = recv_mgmt_request(req, reqlen, OpType::RECONFIGURE, &optype);
  if (err == TS_ERR_OKAY) {
    err = Reconfigure();
  }

  return send_mgmt_response(fd, OpType::RECONFIGURE, &err);
}

/**************************************************************************
 * handle_restart
 *
 * purpose: handles request to restart TM and TS
 * output: TS_ERR_xx
 * note: None
 *************************************************************************/
static TSMgmtError
handle_restart(int fd, void *req, size_t reqlen)
{
  OpType optype;
  MgmtMarshallInt options;
  MgmtMarshallInt err;

  err = recv_mgmt_request(req, reqlen, OpType::RESTART, &optype, &options);
  if (err == TS_ERR_OKAY) {
    switch (optype) {
    case OpType::BOUNCE:
      err = Bounce(options);
      break;
    case OpType::RESTART:
      err = Restart(options);
      break;
    default:
      err = TS_ERR_PARAMS;
      break;
    }
  }

  return send_mgmt_response(fd, OpType::RESTART, &err);
}

/**************************************************************************
 * handle_stop
 *
 * purpose: handles request to stop TS
 * output: TS_ERR_xx
 * note: None
 *************************************************************************/
static TSMgmtError
handle_stop(int fd, void *req, size_t reqlen)
{
  OpType optype;
  MgmtMarshallInt options;
  MgmtMarshallInt err;

  err = recv_mgmt_request(req, reqlen, OpType::STOP, &optype, &options);
  if (err == TS_ERR_OKAY) {
    err = Stop(options);
  }

  return send_mgmt_response(fd, OpType::STOP, &err);
}

/**************************************************************************
 * handle_drain
 *
 * purpose: handles request to drain TS
 * output: TS_ERR_xx
 * note: None
 *************************************************************************/
static TSMgmtError
handle_drain(int fd, void *req, size_t reqlen)
{
  OpType optype;
  MgmtMarshallInt options;
  MgmtMarshallInt err;

  err = recv_mgmt_request(req, reqlen, OpType::DRAIN, &optype, &options);
  if (err == TS_ERR_OKAY) {
    err = Drain(options);
  }

  return send_mgmt_response(fd, OpType::DRAIN, &err);
}

/**************************************************************************
 * handle_storage_device_cmd_offline
 *
 * purpose: handle storage offline command.
 * output: TS_ERR_xx
 * note: None
 *************************************************************************/
static TSMgmtError
handle_storage_device_cmd_offline(int fd, void *req, size_t reqlen)
{
  MgmtMarshallInt optype;
  MgmtMarshallString name = nullptr;
  MgmtMarshallInt err;

  err = recv_mgmt_request(req, reqlen, OpType::STORAGE_DEVICE_CMD_OFFLINE, &optype, &name);
  if (err == TS_ERR_OKAY) {
    // forward to server
    lmgmt->signalEvent(MGMT_EVENT_STORAGE_DEVICE_CMD_OFFLINE, name);
  }

  return send_mgmt_response(fd, OpType::STORAGE_DEVICE_CMD_OFFLINE, &err);
}

/**************************************************************************
 * handle_event_resolve
 *
 * purpose: handles request to resolve an event
 * output: TS_ERR_xx
 * note: the req should be the event name
 *************************************************************************/
static TSMgmtError
handle_event_resolve(int fd, void *req, size_t reqlen)
{
  MgmtMarshallInt optype;
  MgmtMarshallString name = nullptr;
  MgmtMarshallInt err;

  err = recv_mgmt_request(req, reqlen, OpType::EVENT_RESOLVE, &optype, &name);
  if (err == TS_ERR_OKAY) {
    err = EventResolve(name);
  }

  ats_free(name);
  return send_mgmt_response(fd, OpType::EVENT_RESOLVE, &err);
}

/**************************************************************************
 * handle_event_get_mlt
 *
 * purpose: handles request to get list of active events
 * output: TS_ERR_xx
 * note: the req should be the event name
 *************************************************************************/
static TSMgmtError
handle_event_get_mlt(int fd, void *req, size_t reqlen)
{
  LLQ *event_list = create_queue();
  char buf[MAX_BUF_SIZE];
  char *event_name;
  int buf_pos = 0;

  MgmtMarshallInt optype;
  MgmtMarshallInt err;
  MgmtMarshallString list = nullptr;

  err = recv_mgmt_request(req, reqlen, OpType::EVENT_GET_MLT, &optype);
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
    event_name = static_cast<char *>(dequeue(event_list));
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
  return send_mgmt_response(fd, OpType::EVENT_GET_MLT, &err, &list);
}

/**************************************************************************
 * handle_event_active
 *
 * purpose: handles request to resolve an event
 * output: TS_ERR_xx
 * note: the req should be the event name
 *************************************************************************/
static TSMgmtError
handle_event_active(int fd, void *req, size_t reqlen)
{
  bool active;
  MgmtMarshallInt optype;
  MgmtMarshallString name = nullptr;

  MgmtMarshallInt err;
  MgmtMarshallInt bval = 0;

  err = recv_mgmt_request(req, reqlen, OpType::EVENT_ACTIVE, &optype, &name);
  if (err != TS_ERR_OKAY) {
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
  return send_mgmt_response(fd, OpType::EVENT_ACTIVE, &err, &bval);
}

/**************************************************************************
 * handle_stats_reset
 *
 * purpose: handles request to reset statistics to default values
 * output: TS_ERR_xx
 *************************************************************************/
static TSMgmtError
handle_stats_reset(int fd, void *req, size_t reqlen)
{
  OpType optype;
  MgmtMarshallString name = nullptr;
  MgmtMarshallInt err;

  err = recv_mgmt_request(req, reqlen, OpType::STATS_RESET_NODE, &optype, &name);
  if (err == TS_ERR_OKAY) {
    err = StatsReset(name);
  }

  ats_free(name);
  return send_mgmt_response(fd, optype, &err);
}

/**************************************************************************
 * handle_host_status_up
 *
 * purpose: handles request to reset statistics to default values
 * output: TS_ERR_xx
 *************************************************************************/
static TSMgmtError
handle_host_status_up(int fd, void *req, size_t reqlen)
{
  OpType optype;
  MgmtMarshallString name   = nullptr;
  MgmtMarshallString reason = nullptr;
  MgmtMarshallInt err;
  MgmtMarshallInt down_time;

  err = recv_mgmt_request(req, reqlen, OpType::HOST_STATUS_UP, &optype, &name, &reason, &down_time);
  if (err == TS_ERR_OKAY) {
    lmgmt->signalEvent(MGMT_EVENT_HOST_STATUS_UP, static_cast<char *>(req), reqlen);
  }

  ats_free(name);
  return send_mgmt_response(fd, optype, &err);
}

/**************************************************************************
 * handle_host_status_down
 *
 * purpose: handles request to reset statistics to default values
 * output: TS_ERR_xx
 *************************************************************************/
static TSMgmtError
handle_host_status_down(int fd, void *req, size_t reqlen)
{
  OpType optype;
  MgmtMarshallString name   = nullptr;
  MgmtMarshallString reason = nullptr;
  MgmtMarshallInt err;
  MgmtMarshallInt down_time;

  err = recv_mgmt_request(req, reqlen, OpType::HOST_STATUS_DOWN, &optype, &name, &reason, &down_time);
  if (err == TS_ERR_OKAY) {
    lmgmt->signalEvent(MGMT_EVENT_HOST_STATUS_DOWN, static_cast<char *>(req), reqlen);
  }

  ats_free(name);
  return send_mgmt_response(fd, optype, &err);
}
/**************************************************************************
 * handle_api_ping
 *
 * purpose: handles the API_PING message that is sent by API clients to keep
 *    the management socket alive
 * output: TS_ERR_xx. There is no response message.
 *************************************************************************/
static TSMgmtError
handle_api_ping(int /* fd */, void *req, size_t reqlen)
{
  MgmtMarshallInt optype;
  MgmtMarshallInt stamp;

  return recv_mgmt_request(req, reqlen, OpType::API_PING, &optype, &stamp);
}

static TSMgmtError
handle_server_backtrace(int fd, void *req, size_t reqlen)
{
  MgmtMarshallInt optype;
  MgmtMarshallInt options;
  MgmtMarshallString trace = nullptr;
  MgmtMarshallInt err;

  err = recv_mgmt_request(req, reqlen, OpType::SERVER_BACKTRACE, &optype, &options);
  if (err == TS_ERR_OKAY) {
    err = ServerBacktrace(options, &trace);
  }

  err = send_mgmt_response(fd, OpType::SERVER_BACKTRACE, &err, &trace);
  ats_free(trace);

  return static_cast<TSMgmtError>(err);
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

  record_match_state *match = static_cast<record_match_state *>(edata);

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

  err = send_mgmt_response(match->fd, OpType::RECORD_DESCRIBE_CONFIG, &err, &rec_name, &rec_value, &rec_default, &rec_type,
                           &rec_class, &rec_version, &rec_rsb, &rec_order, &rec_access, &rec_update, &rec_updatetype,
                           &rec_checktype, &rec_source, &rec_checkexpr);

done:
  match->err = err;
}

static TSMgmtError
handle_record_describe(int fd, void *req, size_t reqlen)
{
  TSMgmtError ret;
  record_match_state match;
  MgmtMarshallInt optype;
  MgmtMarshallInt options;
  MgmtMarshallString name;

  ret = recv_mgmt_request(req, reqlen, OpType::RECORD_DESCRIBE_CONFIG, &optype, &name, &options);
  if (ret != TS_ERR_OKAY) {
    return ret;
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
 * purpose: handle lifecycle message to plugins
 * output: TS_ERR_xx
 * note: None
 *************************************************************************/
static TSMgmtError
handle_lifecycle_message(int fd, void *req, size_t reqlen)
{
  MgmtMarshallInt optype;
  MgmtMarshallInt err;
  MgmtMarshallString tag;
  MgmtMarshallData data;

  err = recv_mgmt_request(req, reqlen, OpType::LIFECYCLE_MESSAGE, &optype, &tag, &data);
  if (err == TS_ERR_OKAY) {
    lmgmt->signalEvent(MGMT_EVENT_LIFECYCLE_MESSAGE, static_cast<char *>(req), reqlen);
  }

  return send_mgmt_response(fd, OpType::LIFECYCLE_MESSAGE, &err);
}
/**************************************************************************/

struct control_message_handler {
  unsigned flags;
  TSMgmtError (*handler)(int, void *, size_t);
};

static const control_message_handler handlers[] = {
  /* RECORD_SET                 */ {MGMT_API_PRIVILEGED, handle_record_set},
  /* RECORD_GET                 */ {0, handle_record_get},
  /* PROXY_STATE_GET            */ {0, handle_proxy_state_get},
  /* PROXY_STATE_SET            */ {MGMT_API_PRIVILEGED, handle_proxy_state_set},
  /* RECONFIGURE                */ {MGMT_API_PRIVILEGED, handle_reconfigure},
  /* RESTART                    */ {MGMT_API_PRIVILEGED, handle_restart},
  /* BOUNCE                     */ {MGMT_API_PRIVILEGED, handle_restart},
  /* STOP                       */ {MGMT_API_PRIVILEGED, handle_stop},
  /* DRAIN                      */ {MGMT_API_PRIVILEGED, handle_drain},
  /* EVENT_RESOLVE              */ {MGMT_API_PRIVILEGED, handle_event_resolve},
  /* EVENT_GET_MLT              */ {0, handle_event_get_mlt},
  /* EVENT_ACTIVE               */ {0, handle_event_active},
  /* EVENT_REG_CALLBACK         */ {0, nullptr},
  /* EVENT_UNREG_CALLBACK       */ {0, nullptr},
  /* EVENT_NOTIFY               */ {0, nullptr},
  /* STATS_RESET_NODE           */ {MGMT_API_PRIVILEGED, handle_stats_reset},
  /* STORAGE_DEVICE_CMD_OFFLINE */ {MGMT_API_PRIVILEGED, handle_storage_device_cmd_offline},
  /* RECORD_MATCH_GET           */ {0, handle_record_match},
  /* API_PING                   */ {0, handle_api_ping},
  /* SERVER_BACKTRACE           */ {MGMT_API_PRIVILEGED, handle_server_backtrace},
  /* RECORD_DESCRIBE_CONFIG     */ {0, handle_record_describe},
  /* LIFECYCLE_MESSAGE          */ {MGMT_API_PRIVILEGED, handle_lifecycle_message},
  /* HOST_STATUS_UP             */ {MGMT_API_PRIVILEGED, handle_host_status_up},
  /* HOST_STATUS_DOWN           */ {MGMT_API_PRIVILEGED, handle_host_status_down},
};

// This should use countof(), but we need a constexpr :-/
static_assert((sizeof(handlers) / sizeof(handlers[0])) == static_cast<unsigned>(OpType::UNDEFINED_OP),
              "handlers array is not of correct size");

static TSMgmtError
handle_control_message(int fd, void *req, size_t reqlen)
{
  OpType optype = extract_mgmt_request_optype(req, reqlen);
  TSMgmtError error;

  if (static_cast<unsigned>(optype) >= countof(handlers)) {
    goto fail;
  }

  if (handlers[static_cast<unsigned>(optype)].handler == nullptr) {
    goto fail;
  }

  if (mgmt_has_peereid()) {
    uid_t euid = -1;
    gid_t egid = -1;

    // For privileged calls, ensure we have caller credentials and that the caller is privileged.
    if (handlers[static_cast<unsigned>(optype)].flags & MGMT_API_PRIVILEGED) {
      if (mgmt_get_peereid(fd, &euid, &egid) == -1 || (euid != 0 && euid != geteuid())) {
        Debug("ts_main", "denied privileged API access on fd=%d for uid=%d gid=%d", fd, euid, egid);
        return send_mgmt_error(fd, optype, TS_ERR_PERMISSION_DENIED);
      }
    }
  }

  Debug("ts_main", "handling message type=%d ptr=%p len=%zu on fd=%d", static_cast<int>(optype), req, reqlen, fd);

  error = handlers[static_cast<unsigned>(optype)].handler(fd, req, reqlen);
  if (error != TS_ERR_OKAY) {
    // NOTE: if the error was produced by the handler sending a response, this could attempt to
    // send a response again. However, this would only happen if sending the response failed, so
    // it is safe to fail to send it again here ...
    return send_mgmt_error(fd, optype, error);
  }

  return TS_ERR_OKAY;

fail:
  mgmt_elog(0, "%s: missing handler for type %d control message\n", __func__, static_cast<int>(optype));
  return TS_ERR_PARAMS;
}
