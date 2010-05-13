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

#include "inktomi++.h"
#include "LocalManager.h"
#include "Main.h"
#include "MgmtSocket.h"
#include "TSControlMain.h"
#include "CoreAPI.h"
#include "CoreAPIShared.h"
#include "NetworkUtilsLocal.h"
#include "NetworkUtilsDefs.h"

#define TIMEOUT_SECS 1;         // the num secs for select timeout

extern int diags_init;          // from Main.cc

InkHashTable *accepted_con;     // a list of all accepted client connections

/*********************************************************************
 * create_client
 *
 * purpose: creates a new ClientT and return pointer to it
 * input: None
 * output: ClientT
 * note: created for each accepted client connection
 *********************************************************************/
ClientT *
create_client()
{
  ClientT *ele;

  ele = (ClientT *) xmalloc(sizeof(ClientT));
  if (!ele)
    return NULL;

  ele->adr = (struct sockaddr *) xmalloc(sizeof(struct sockaddr));
  if (!ele->adr)
    return NULL;

  return ele;
}

/*********************************************************************
 * delete_client
 *
 * purpose: frees dynamic memory allocated for a ClientT
 * input: client - the ClientT to free
 * output:
 *********************************************************************/
void
delete_client(ClientT * client)
{
  if (client) {
    if (client->adr)
      xfree(client->adr);
    xfree(client);
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
void
remove_client(ClientT * client, InkHashTable * table)
{
  // close client socket
  ink_close_socket(client->sock_info.fd);       // close client socket

  // remove client binding from hash table
  ink_hash_table_delete(table, (char *) &client->sock_info.fd);

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
  OpType op_t;                  // operation type for a request; NetworkUtilsDefs.h
  int *socket_fd;
  int con_socket_fd;            // main socket for listening to new connections
  char *req = NULL;             // the request msg sent over from client (not include header)

  socket_fd = (int *) arg;
  con_socket_fd = *socket_fd;

  // initialize queue for accepted con
  accepted_con = ink_hash_table_create(InkHashTableKeyType_Word);
  if (!accepted_con) {
    return NULL;
  }
  // now we can start listening, accepting connections and servicing requests
  int new_con_fd;               // new socket fd when accept connection

  fd_set selectFDs;             // for select call
  InkHashTableEntry *con_entry; // used to obtain client connection info
  ClientT *client_entry;        // an entry of fd to alarms mapping
  InkHashTableIteratorState con_state;  // used to iterate through hash table
  int fds_ready;                // stores return value for select
  struct timeval timeout;
  int addr_len = (sizeof(struct sockaddr));

  // loops until TM dies; waits for and processes requests from clients
  while (1) {
    // LINUX: to prevent hard-spin of CPU,  reset timeout on each loop
    timeout.tv_sec = TIMEOUT_SECS;
    timeout.tv_usec = 0;

    FD_ZERO(&selectFDs);

    if (con_socket_fd >= 0) {
      FD_SET(con_socket_fd, &selectFDs);
      //Debug("ts_main", "[ts_ctrl_main] add fd %d to select set\n", con_socket_fd);
    }
    // see if there are more fd to set
    con_entry = ink_hash_table_iterator_first(accepted_con, &con_state);

    // iterate through all entries in hash table
    while (con_entry) {
      client_entry = (ClientT *) ink_hash_table_entry_value(accepted_con, con_entry);
      if (client_entry->sock_info.fd >= 0) {    // add fd to select set
        FD_SET(client_entry->sock_info.fd, &selectFDs);
        Debug("ts_main", "[ts_ctrl_main] add fd %d to select set\n", client_entry->sock_info.fd);
      }
      con_entry = ink_hash_table_iterator_next(accepted_con, &con_state);
    }

    // select call - timeout is set so we can check events at regular intervals
    fds_ready = mgmt_select(FD_SETSIZE, &selectFDs, (fd_set *) NULL, (fd_set *) NULL, &timeout);

    // check if have any connections or requests
    if (fds_ready > 0) {
      // first check for connections!
      if (con_socket_fd >= 0 && FD_ISSET(con_socket_fd, &selectFDs)) {
        fds_ready--;

        // create a new instance to store client connection info
        ClientT *new_client_con = create_client();
        if (!new_client_con) {
          // return INK_ERR_SYS_CALL; WHAT TO DO? just keep going
          Debug("ts_main", "[ts_ctrl_main] can't allocate new ClientT\n");
        } else {                // accept connection
          new_con_fd = mgmt_accept(con_socket_fd, new_client_con->adr, &addr_len);
          new_client_con->sock_info.fd = new_con_fd;
          new_client_con->sock_info.SSLcon = NULL;
          ink_hash_table_insert(accepted_con, (char *) &new_client_con->sock_info.fd, new_client_con);
          Debug("ts_main", "[ts_ctrl_main] Add new client connection \n");
          //mgmt_log(stderr, "[ts_ctrl_main] Accept new connection: fd=%d\n", new_con_fd);
        }
      }                         // end if(new_con_fd >= 0 && FD_ISSET(new_con_fd, &selectFDs))

      // some other file descriptor; for each one, service request
      if (fds_ready > 0) {      // RECEIVED A REQUEST from remote API client
        // see if there are more fd to set - iterate through all entries in hash table
        con_entry = ink_hash_table_iterator_first(accepted_con, &con_state);
        while (con_entry) {
          Debug("ts_main", "[ts_ctrl_main] We have a remote client request!\n");
          client_entry = (ClientT *) ink_hash_table_entry_value(accepted_con, con_entry);
          // got information; check
          if (client_entry->sock_info.fd && FD_ISSET(client_entry->sock_info.fd, &selectFDs)) {
            // SERVICE REQUEST - read the op and message into a buffer
            // clear the fields first
            op_t = UNDEFINED_OP;
            ret = preprocess_msg(client_entry->sock_info, (OpType *) & op_t, &req);
            if (ret == INK_ERR_NET_READ || ret == INK_ERR_NET_EOF) {
              // occurs when remote API client terminates connection
              Debug("ts_main", "[ts_ctrl_main] ERROR: preprocess_msg - remove client %d \n",
                    client_entry->sock_info.fd);
              mgmt_log("[ts_ctrl_main] preprocess_msg - remove client %d\n", client_entry->sock_info.fd);
              remove_client(client_entry, accepted_con);
              // get next client connection (if any)
              con_entry = ink_hash_table_iterator_next(accepted_con, &con_state);
              continue;
            }
            // determine which handler function to call based on operation
            switch (op_t) {

            case RECORD_GET:
              ret = handle_record_get(client_entry->sock_info, req);
              if (req)
                xfree(req);     // free memory for req
              if (ret == INK_ERR_NET_WRITE || ret == INK_ERR_NET_EOF) {
                Debug("ts_main", "[ts_ctrl_main] ERROR:handle_record_get\n");
                remove_client(client_entry, accepted_con);
                con_entry = ink_hash_table_iterator_next(accepted_con, &con_state);
                continue;
              }
              break;

            case RECORD_SET:
              ret = handle_record_set(client_entry->sock_info, req);
              if (req)
                xfree(req);
              if (ret == INK_ERR_NET_WRITE || ret == INK_ERR_NET_EOF) {
                Debug("ts_main", "[ts_ctrl_main] ERROR:handle_record_set\n");
                remove_client(client_entry, accepted_con);
                con_entry = ink_hash_table_iterator_next(accepted_con, &con_state);
                continue;
              }

              break;

            case FILE_READ:
              ret = handle_file_read(client_entry->sock_info, req);
              if (req)
                xfree(req);
              if (ret == INK_ERR_NET_WRITE || ret == INK_ERR_NET_EOF) {
                Debug("ts_main", "[ts_ctrl_main] ERROR:handle_file_read\n");
                remove_client(client_entry, accepted_con);
                con_entry = ink_hash_table_iterator_next(accepted_con, &con_state);
                continue;
              }

              break;

            case FILE_WRITE:
              ret = handle_file_write(client_entry->sock_info, req);
              if (req)
                xfree(req);
              if (ret == INK_ERR_NET_WRITE || ret == INK_ERR_NET_EOF) {
                Debug("ts_main", "[ts_ctrl_main] ERROR:handle_file_write\n");
                remove_client(client_entry, accepted_con);
                con_entry = ink_hash_table_iterator_next(accepted_con, &con_state);
                continue;
              }

              break;

            case PROXY_STATE_GET:
              ret = handle_proxy_state_get(client_entry->sock_info);
              if (req)
                xfree(req);     // free the request allocated by preprocess_msg
              if (ret == INK_ERR_NET_WRITE || ret == INK_ERR_NET_EOF) {
                Debug("ts_main", "[ts_ctrl_main] ERROR:handle_proxy_state_get\n");
                remove_client(client_entry, accepted_con);
                con_entry = ink_hash_table_iterator_next(accepted_con, &con_state);
                continue;
              }

              break;

            case PROXY_STATE_SET:
              ret = handle_proxy_state_set(client_entry->sock_info, req);
              if (req)
                xfree(req);     // free the request allocated by preprocess_msg
              if (ret == INK_ERR_NET_WRITE || ret == INK_ERR_NET_EOF) {
                Debug("ts_main", "[ts_ctrl_main] ERROR:handle_proxy_state_set\n");
                remove_client(client_entry, accepted_con);
                con_entry = ink_hash_table_iterator_next(accepted_con, &con_state);
                continue;
              }

              break;

            case RECONFIGURE:
              ret = handle_reconfigure(client_entry->sock_info);
              if (req)
                xfree(req);     // free the request allocated by preprocess_msg
              if (ret == INK_ERR_NET_WRITE || ret == INK_ERR_NET_EOF) {
                Debug("ts_main", "[ts_ctrl_main] ERROR:handle_reconfigure\n");
                remove_client(client_entry, accepted_con);
                con_entry = ink_hash_table_iterator_next(accepted_con, &con_state);
                continue;
              }

              break;

            case RESTART:
              ret = handle_restart(client_entry->sock_info, req);
              if (req)
                xfree(req);     // free the request allocated by preprocess_msg
              if (ret == INK_ERR_NET_WRITE || ret == INK_ERR_NET_EOF) {
                Debug("ts_main", "[ts_ctrl_main] ERROR:handle_restart\n");
                remove_client(client_entry, accepted_con);
                con_entry = ink_hash_table_iterator_next(accepted_con, &con_state);
                continue;
              }
              break;

            case EVENT_RESOLVE:
              ret = handle_event_resolve(client_entry->sock_info, req);
              if (req)
                xfree(req);     // free the request allocated by preprocess_msg
              if (ret == INK_ERR_NET_WRITE || ret == INK_ERR_NET_EOF) {
                Debug("ts_main", "[ts_ctrl_main] ERROR:handle_event_resolve\n");
                remove_client(client_entry, accepted_con);
                con_entry = ink_hash_table_iterator_next(accepted_con, &con_state);
                continue;
              }
              break;

            case EVENT_GET_MLT:
              ret = handle_event_get_mlt(client_entry->sock_info);
              if (req)
                xfree(req);     // free the request allocated by preprocess_msg
              if (ret == INK_ERR_NET_WRITE || ret == INK_ERR_NET_EOF) {
                Debug("ts_main", "[ts_ctrl_main] ERROR:event_get_mlt\n");
                remove_client(client_entry, accepted_con);
                con_entry = ink_hash_table_iterator_next(accepted_con, &con_state);
                continue;
              }
              break;

            case EVENT_ACTIVE:
              ret = handle_event_active(client_entry->sock_info, req);
              if (req)
                xfree(req);     // free the request allocated by preprocess_msg
              if (ret == INK_ERR_NET_WRITE || ret == INK_ERR_NET_EOF) {
                Debug("ts_main", "[ts_ctrl_main] ERROR:event_active\n");
                remove_client(client_entry, accepted_con);
                con_entry = ink_hash_table_iterator_next(accepted_con, &con_state);
                continue;
              }
              break;

            case SNAPSHOT_TAKE:
            case SNAPSHOT_RESTORE:
            case SNAPSHOT_REMOVE:
              ret = handle_snapshot(client_entry->sock_info, req, op_t);
              if (req)
                xfree(req);
              if (ret == INK_ERR_NET_WRITE || ret == INK_ERR_NET_EOF) {
                Debug("ts_main", "[ts_ctrl_main] ERROR:handle_snapshot\n");
                remove_client(client_entry, accepted_con);
                con_entry = ink_hash_table_iterator_next(accepted_con, &con_state);
                continue;
              }
              break;

            case SNAPSHOT_GET_MLT:
              ret = handle_snapshot_get_mlt(client_entry->sock_info);
              if (req)
                xfree(req);
              if (ret == INK_ERR_NET_WRITE || ret == INK_ERR_NET_EOF) {
                Debug("ts_main", "[ts_ctrl_main] ERROR:snapshot_get_mlt\n");
                remove_client(client_entry, accepted_con);
                con_entry = ink_hash_table_iterator_next(accepted_con, &con_state);
                continue;
              }
              break;

            case DIAGS:
              if (req) {
                handle_diags(client_entry->sock_info, req);
                xfree(req);
              }
              break;

            case STATS_RESET:
              ret = handle_stats_reset(client_entry->sock_info, req);
              if (req)
                xfree(req);
              if (ret == INK_ERR_NET_WRITE || ret == INK_ERR_NET_EOF) {
                Debug("ts_main", "[ts_ctrl_main] ERROR: stats_reset\n");
                remove_client(client_entry, accepted_con);
                con_entry = ink_hash_table_iterator_next(accepted_con, &con_state);
                continue;
              }
              break;

            case ENCRYPT_TO_FILE:
              ret = handle_encrypt_to_file(client_entry->sock_info, req);
              if (req)
                xfree(req);
              if (ret == INK_ERR_NET_WRITE || ret == INK_ERR_NET_EOF) {
                Debug("ts_main", "[ts_ctrl_main] ERROR: encrypt_to_file");
                remove_client(client_entry, accepted_con);
                con_entry = ink_hash_table_iterator_next(accepted_con, &con_state);
                continue;
              }
              break;

            case UNDEFINED_OP:
            default:
              break;

            }                   // end switch (op_t)
          }                     // end if(client_entry->sock_info.fd && FD_ISSET(client_entry->sock_info.fd, &selectFDs))

          con_entry = ink_hash_table_iterator_next(accepted_con, &con_state);
        }                       // end while (con_entry)
      }                         // end if (fds_ready > 0)

    }                           // end if (fds_ready > 0)

  }                             // end while (1)

  // if we get here something's wrong, just clean up
  Debug("ts_main", "[ts_ctrl_main] CLOSING AND SHUTTING DOWN OPERATIONS\n");
  ink_close_socket(con_socket_fd);

  // iterate through hash table; close client socket connections and remove entry
  con_entry = ink_hash_table_iterator_first(accepted_con, &con_state);
  while (con_entry) {
    client_entry = (ClientT *) ink_hash_table_entry_value(accepted_con, con_entry);
    if (client_entry->sock_info.fd >= 0) {
      ink_close_socket(client_entry->sock_info.fd);     // close socket
    }
    ink_hash_table_delete(accepted_con, (char *) &client_entry->sock_info.fd);  // remove binding
    delete_client(client_entry);        // free ClientT
    con_entry = ink_hash_table_iterator_next(accepted_con, &con_state);
  }
  // all entries should be removed and freed already
  ink_hash_table_destroy(accepted_con);

  ink_thread_exit(NULL);
  return NULL;
}

/*-------------------------------------------------------------------------
                             HANDLER FUNCTIONS
 --------------------------------------------------------------------------*/
/* NOTE: all the handle_xx functions basically, take the request, parse it,
 * and send a reply back to the remote client. So even if error occurs,
 * each handle functions MUST SEND A REPLY BACK!! If an error occurs during
 * parsing the request, or while doing the API call, then must send reply back
 * with only the error return value in the msg!!! It's important that if
 * an error does occur, the "send_reply" function is used; otherwise the socket
 * will get written with too much extraneous stuff; the remote side will
 * only read the INKError type since that's all it expects to be in the message
 * (for an INKError != INK_ERR_OKAY).
 */

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
INKError
handle_record_get(struct SocketInfo sock_info, char *req)
{
  INKError ret;
  INKRecordEle *ele;

  // parse msg - don't really need since the request itself is the record name
  if (!req) {
    ret = send_reply(sock_info, INK_ERR_FAIL);
    return ret;
  }
  // call CoreAPI call on Traffic Manager side
  ele = INKRecordEleCreate();
  ret = MgmtRecordGet(req, ele);
  if (ret != INK_ERR_OKAY) {
    ret = send_reply(sock_info, ret);
    INKRecordEleDestroy(ele);
    return ret;
  }
  // create and send reply back to client
  switch (ele->rec_type) {
  case INK_REC_INT:
    ret = send_record_get_reply(sock_info, ret, &(ele->int_val), sizeof(INKInt), ele->rec_type);
    break;
  case INK_REC_COUNTER:
    ret = send_record_get_reply(sock_info, ret, &(ele->counter_val), sizeof(INKCounter), ele->rec_type);
    break;
  case INK_REC_FLOAT:
    ret = send_record_get_reply(sock_info, ret, &(ele->float_val), sizeof(INKFloat), ele->rec_type);
    break;
  case INK_REC_STRING:
    ret = send_record_get_reply(sock_info, ret, ele->string_val, strlen(ele->string_val), ele->rec_type);
    break;
  default:                     // invalid record type
    ret = send_reply(sock_info, INK_ERR_FAIL);
    INKRecordEleDestroy(ele);
    return ret;
  }

  if (ret != INK_ERR_OKAY) {    // error sending reply
    ret = send_reply(sock_info, ret);
  }

  INKRecordEleDestroy(ele);     // free any memory allocated by CoreAPI call

  return ret;
}


/**************************************************************************
 * handle_record_set
 *
 * purpose: handles a set request sent by the client
 * input: sock_info
 * output: SUCC or ERR
 * note: request format = <record name>DELIMITER<record_value>
 *************************************************************************/
INKError
handle_record_set(struct SocketInfo sock_info, char *req)
{
  char *name, *val;
  INKError ret;
  INKActionNeedT action = INK_ACTION_UNDEFINED;

  if (!req) {
    ret = send_reply(sock_info, INK_ERR_PARAMS);
    return ret;
  }
  // parse request msg
  ret = parse_request_name_value(req, &name, &val);
  if (ret != INK_ERR_OKAY) {
    ret = send_reply(sock_info, ret);
    if (name)
      xfree(name);
    return ret;
  }
  // call CoreAPI call on Traffic Manager side
  ret = MgmtRecordSet(name, val, &action);
  if (name)
    xfree(name);
  if (val)
    xfree(val);

  if (ret != INK_ERR_OKAY) {
    ret = send_reply(sock_info, ret);
    return ret;
  }
  // create and send reply back to client
  ret = send_record_set_reply(sock_info, ret, action);

  return ret;
}

/**************************************************************************
 * handle_file_read
 *
 * purpose: handles request to read a file
 * input: struct SocketInfo sock_info - the socket to use to talk to client
 * output: SUCC or ERR
 * note: None
 *************************************************************************/
INKError
handle_file_read(struct SocketInfo sock_info, char *req)
{
  INKError ret;
  int size, version;
  INKFileNameT file;
  char *text;

  if (!req) {
    ret = send_reply(sock_info, INK_ERR_PARAMS);
    return ret;
  }
  // first parse the message to retrieve needed data
  ret = parse_file_read_request(req, &file);
  if (ret != INK_ERR_OKAY) {
    ret = send_reply(sock_info, ret);
    return ret;
  }
  // make CoreAPI call on Traffic Manager side
  ret = ReadFile(file, &text, &size, &version);
  if (ret != INK_ERR_OKAY) {
    ret = send_reply(sock_info, ret);
    return ret;
  }
  // marshal the file info message that can be returned to client
  ret = send_file_read_reply(sock_info, ret, version, size, text);
  if (ret != INK_ERR_OKAY) {
    ret = send_reply(sock_info, ret);
  }

  if (text)
    xfree(text);                // free memory allocated by ReadFile

  return ret;
}

/**************************************************************************
 * handle_file_write
 *
 * purpose: handles request to write a file
 * input: struct SocketInfo sock_info - the socket to use to talk to client
 * output: SUCC or ERR
 * note: None
 *************************************************************************/
INKError
handle_file_write(struct SocketInfo sock_info, char *req)
{
  INKError ret;
  int size, version;
  INKFileNameT file;
  char *text;

  if (!req) {
    ret = send_reply(sock_info, INK_ERR_PARAMS);
    return ret;
  }
  // first parse the message
  ret = parse_file_write_request(req, &file, &version, &size, &text);
  if (ret != INK_ERR_OKAY) {
    ret = send_reply(sock_info, ret);
    return ret;
  }
  // make CoreAPI call on Traffic Manager side
  ret = WriteFile(file, text, size, version);
  ret = send_reply(sock_info, ret);

  if (text)
    xfree(text);                // free memory allocated by parsing fn.

  return ret;
}


/**************************************************************************
 * handle_proxy_state_get
 *
 * purpose: handles request to get the state of the proxy (TS)
 * input: struct SocketInfo sock_info - the socket to use to talk to client
 * output: INK_ERR_xx
 * note: None
 *************************************************************************/
INKError
handle_proxy_state_get(struct SocketInfo sock_info)
{
  INKProxyStateT state;
  INKError ret;

  // make coreAPI call on local side
  state = ProxyStateGet();

  // send reply back
  ret = send_proxy_state_get_reply(sock_info, state);

  return ret;                   //shouldn't get here
}

/**************************************************************************
 * handle_proxy_state_set
 *
 * purpose: handles the request to set the state of the proxy (TS)
 * input: struct SocketInfo sock_info - the socket to use to talk to client
 *        req - indicates which state to set it to (on/off?) and specifies
 *        what to set the ts options too (optional)
 * output: INK_ERR_xx
 * note: None
 *************************************************************************/
INKError
handle_proxy_state_set(struct SocketInfo sock_info, char *req)
{
  INKProxyStateT state;
  INKCacheClearT clear;
  INKError ret;

  if (!req) {
    ret = INK_ERR_FAIL;
    goto END;
  }
  // the req should specify the state and any cache clearing options
  ret = parse_proxy_state_request(req, &state, &clear);
  if (ret != INK_ERR_OKAY) {
    goto END;
  }

  ret = ProxyStateSet(state, clear);

END:
  ret = send_reply(sock_info, ret);
  return ret;
}

/**************************************************************************
 * handle_reconfigure
 *
 * purpose: handles request to reread the config files
 * input: struct SocketInfo sock_info - the socket to use to talk to client
 * output: INK_ERR_xx
 * note: None
 *************************************************************************/
INKError
handle_reconfigure(struct SocketInfo sock_info)
{
  INKError ret;

  // make local side coreAPI call
  ret = Reconfigure();

  ret = send_reply(sock_info, ret);
  return ret;
}

/**************************************************************************
 * handle_restart
 *
 * purpose: handles request to restart TM and TS
 * input: struct SocketInfo sock_info - the socket to use to talk to client
 *        req - indicates if restart should be cluster wide or not
 * output: INK_ERR_xx
 * note: None
 *************************************************************************/
INKError
handle_restart(struct SocketInfo sock_info, char *req)
{
  ink16 cluster;
  INKError ret;

  if (!req) {
    ret = send_reply(sock_info, INK_ERR_PARAMS);
    return ret;                 // shouldn't get here
  }
  // the req should be a boolean value - typecase it
  memcpy(&cluster, req, SIZE_BOOL);

  if (cluster == 0)             // cluster is true
    ret = Restart(true);
  else                          // cluster is false
    ret = Restart(false);

  ret = send_reply(sock_info, ret);
  return ret;
}

/**************************************************************************
 * handle_event_resolve
 *
 * purpose: handles request to resolve an event
 * input: struct SocketInfo sock_info - the socket to use to talk to client
 * output: INK_ERR_xx
 * note: the req should be the event name
 *************************************************************************/
INKError
handle_event_resolve(struct SocketInfo sock_info, char *req)
{
  INKError ret;

  // parse msg - don't really need since the request itself is the record name
  if (!req) {
    ret = send_reply(sock_info, INK_ERR_PARAMS);
    return ret;                 // shouldn't get here
  }
  // call CoreAPI call on Traffic Manager side; req == event_name
  ret = EventResolve(req);
  ret = send_reply(sock_info, ret);

  return ret;
}

/**************************************************************************
 * handle_event_get_mlt
 *
 * purpose: handles request to get list of active events
 * input: struct SocketInfo sock_info - the socket to use to talk to client
 * output: INK_ERR_xx
 * note: the req should be the event name
 *************************************************************************/
INKError
handle_event_get_mlt(struct SocketInfo sock_info)
{
  INKError ret;
  LLQ *event_list;
  char buf[MAX_BUF_SIZE];
  char *event_name;
  int buf_pos = 0;

  event_list = create_queue();

  // call CoreAPI call on Traffic Manager side; req == event_name
  ret = ActiveEventGetMlt(event_list);
  if (ret != INK_ERR_OKAY) {
    ret = send_reply(sock_info, ret);
    delete_queue(event_list);
    return ret;
  }
  // iterate through list and put into a delimited string list
  memset(buf, 0, MAX_BUF_SIZE);
  while (!queue_is_empty(event_list)) {
    event_name = (char *) dequeue(event_list);
    if (event_name) {
      snprintf(buf + buf_pos, (MAX_BUF_SIZE - buf_pos), "%s%c", event_name, REMOTE_DELIM);
      buf_pos += (strlen(event_name) + 1);
      xfree(event_name);        //free the llq entry
    }
  }
  buf[buf_pos] = '\0';          //end the string

  ret = send_reply_list(sock_info, ret, buf);

  delete_queue(event_list);
  return ret;
}

/**************************************************************************
 * handle_event_active
 *
 * purpose: handles request to resolve an event
 * input: struct SocketInfo sock_info - the socket to use to talk to client
 * output: INK_ERR_xx
 * note: the req should be the event name
 *************************************************************************/
INKError
handle_event_active(struct SocketInfo sock_info, char *req)
{
  INKError ret;
  bool active;

  // parse msg - don't really need since the request itself is the record name
  if (!req) {
    ret = send_reply(sock_info, INK_ERR_PARAMS);
    return ret;                 // shouldn't get here
  }
  // call CoreAPI call on Traffic Manager side; req == event_name
  ret = EventIsActive(req, &active);
  if (ret != INK_ERR_OKAY) {
    ret = send_reply(sock_info, ret);
    return ret;                 //shouldn't get here
  }

  ret = send_event_active_reply(sock_info, ret, active);

  return ret;
}


/**************************************************************************
 * handle_snapshot
 *
 * purpose: handles request to take/remove/restore a snapshot
 * input: struct SocketInfo sock_info - the socket to use to talk to client
 *        req - the snapshot name
 *        op  - SNAPSHOT_TAKE, SNAPSHOT_REMOVE, or SNAPSHOT_RESTORE
 * output: INK_ERR_xx
 *************************************************************************/
INKError
handle_snapshot(struct SocketInfo sock_info, char *req, OpType op)
{
  INKError ret;

  if (!req) {
    ret = send_reply(sock_info, INK_ERR_PARAMS);
    return ret;
  }
  // call CoreAPI call on Traffic Manager side; req == snap_name
  switch (op) {
  case SNAPSHOT_TAKE:
    ret = SnapshotTake(req);
    break;
  case SNAPSHOT_RESTORE:
    ret = SnapshotRestore(req);
    break;
  case SNAPSHOT_REMOVE:
    ret = SnapshotRemove(req);
    break;
  default:
    ret = INK_ERR_FAIL;
    break;
  }

  ret = send_reply(sock_info, ret);
  return ret;
}

/**************************************************************************
 * handle_snapshot_get_mlt
 *
 * purpose: handles request to get list of snapshots
 * input: struct SocketInfo sock_info - the socket to use to talk to client
 * output: INK_ERR_xx
 * note: the req should be the event name
 *************************************************************************/
INKError
handle_snapshot_get_mlt(struct SocketInfo sock_info)
{
  INKError ret;
  LLQ *snap_list;
  char buf[MAX_BUF_SIZE];
  char *snap_name;
  int buf_pos = 0;

  snap_list = create_queue();

  // call CoreAPI call on Traffic Manager side; req == event_name
  ret = SnapshotGetMlt(snap_list);
  if (ret != INK_ERR_OKAY) {
    ret = send_reply(sock_info, ret);
    delete_queue(snap_list);
    return ret;
  }
  // iterate through list and put into a delimited string list
  memset(buf, 0, MAX_BUF_SIZE);
  while (!queue_is_empty(snap_list)) {
    snap_name = (char *) dequeue(snap_list);
    if (snap_name) {
      snprintf(buf + buf_pos, (MAX_BUF_SIZE - buf_pos), "%s%c", snap_name, REMOTE_DELIM);
      buf_pos += (strlen(snap_name) + 1);
      xfree(snap_name);         //free the llq entry
    }
  }
  buf[buf_pos] = '\0';          //end the string

  ret = send_reply_list(sock_info, ret, buf);

  delete_queue(snap_list);
  return ret;
}


/**************************************************************************
 * handle_diags
 *
 * purpose: handles diags request
 * input: struct SocketInfo sock_info - the socket to use to talk to client
 *        req - the diag message (already formatted with arguments)
 * output: INK_ERR_xx
 *************************************************************************/
void
handle_diags(struct SocketInfo sock_info, char *req)
{
  INKError ret;
  INKDiagsT mode;
  char *diag_msg = NULL;
  DiagsLevel level;

  if (!req)
    goto Lerror;

  ret = parse_diags_request(req, &mode, &diag_msg);
  if (ret != INK_ERR_OKAY)
    goto Lerror;


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
    level = DL_Diag;            //default value should be Diag not UNDEFINED
  }

  if (diags_init) {
    diags->print("INKMgmtAPI", level, NULL, NULL, diag_msg);
    if (diag_msg)
      xfree(diag_msg);
    return;
  }

Lerror:
  if (diag_msg)
    xfree(diag_msg);
  return;
}

/**************************************************************************
 * handle_stats_reset
 *
 * purpose: handles request to reset statistics to default values
 * input: struct SocketInfo sock_info - the socket to use to talk to client
 *        req - should be NULL
 * output: INK_ERR_xx
 *************************************************************************/
INKError
handle_stats_reset(struct SocketInfo sock_info, char *req)
{
  INKError ret;

  // call CoreAPI call on Traffic Manager side
  ret = StatsReset();
  ret = send_reply(sock_info, ret);

  return ret;

}

/**************************************************************************
 * handle_encrypt_to_file
 *
 * purpose: handles request to encrypt password to file
 * input: struct SocketInfo sock_info - the socket to use to talk to client
 *        req - should be NULL
 * output: INK_ERR_xx
 *************************************************************************/
INKError
handle_encrypt_to_file(struct SocketInfo sock_info, char *req)
{
  char *pwd, *filepath;
  INKError ret;

  if (!req) {
    ret = send_reply(sock_info, INK_ERR_PARAMS);
    return ret;
  }
  // parse request msg
  ret = parse_request_name_value(req, &pwd, &filepath);
  if (ret != INK_ERR_OKAY) {
    ret = send_reply(sock_info, ret);
    if (pwd)
      xfree(pwd);
    if (filepath)
      xfree(filepath);
    return ret;
  }

  ret = EncryptToFile(pwd, filepath);
  ret = send_reply(sock_info, ret);
  if (pwd)
    xfree(pwd);
  if (filepath)
    xfree(filepath);
  return ret;
}
