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
 * Filename: EventControlMain.cc
 * Purpose: Handles all event requests from the user.
 * Created: 01/08/01
 * Created by: lant
 *
 ***************************************************************************/

#include "tscore/ink_platform.h"
#include "tscore/ink_sock.h"
#include "LocalManager.h"
#include "MgmtSocket.h"
#include "MgmtMarshall.h"
#include "MgmtUtils.h"
#include "EventControlMain.h"
#include "CoreAPI.h"
#include "NetworkUtilsLocal.h"
#include "NetworkMessage.h"

// variables that are very important
ink_mutex mgmt_events_lock;
LLQ *mgmt_events;
std::unordered_map<int, EventClientT *> accepted_clients; // list of all accepted client connections

static TSMgmtError handle_event_message(EventClientT *client, void *req, size_t reqlen);

/*********************************************************************
 * new_event_client
 *
 * purpose: creates a new EventClientT and return pointer to it
 * input: None
 * output: EventClientT
 * note: None
 *********************************************************************/
EventClientT *
new_event_client()
{
  EventClientT *ele = static_cast<EventClientT *>(ats_malloc(sizeof(EventClientT)));

  // now set the alarms registered section
  for (bool &i : ele->events_registered) {
    i = false;
  }

  ele->adr = static_cast<struct sockaddr *>(ats_malloc(sizeof(struct sockaddr)));
  return ele;
}

/*********************************************************************
 * delete_event_client
 *
 * purpose: frees memory allocated for an EventClientT
 * input: EventClientT
 * output: None
 * note: None
 *********************************************************************/
void
delete_event_client(EventClientT *client)
{
  if (client) {
    ats_free(client->adr);
    ats_free(client);
  }
  return;
}

/*********************************************************************
 * remove_event_client
 *
 * purpose: removes the EventClientT from the specified hashtable; includes
 *          removing the binding and freeing the ClientT
 * input: client - the ClientT to remove
 * output:
 *********************************************************************/
void
remove_event_client(EventClientT *client, std::unordered_map<int, EventClientT *> &table)
{
  // close client socket
  close_socket(client->fd);

  // remove client binding from hash table
  table.erase(client->fd);

  // free ClientT
  delete_event_client(client);

  return;
}

/*********************************************************************
 * init_mgmt_events
 *
 * purpose: initializes the mgmt_events queue which is intended to hold
 *          TM events.
 * input:
 * output: TS_ERR_xx
 * note: None
 *********************************************************************/
TSMgmtError
init_mgmt_events()
{
  ink_mutex_init(&mgmt_events_lock);

  // initialize queue
  mgmt_events = create_queue();
  if (!mgmt_events) {
    ink_mutex_destroy(&mgmt_events_lock);
    return TS_ERR_SYS_CALL;
  }

  return TS_ERR_OKAY;
}

/*********************************************************************
 * delete_mgmt_events
 *
 * purpose: frees the mgmt_events queue.
 * input:
 * output: None
 * note: None
 *********************************************************************/
void
delete_mgmt_events()
{
  // obtain lock
  ink_mutex_acquire(&mgmt_events_lock);

  // delete the queue associated with the queue of events
  delete_event_queue(mgmt_events);

  // release it
  ink_mutex_release(&mgmt_events_lock);

  // kill lock
  ink_mutex_destroy(&mgmt_events_lock);

  delete_queue(mgmt_events);

  return;
}

/*********************************************************************
 * delete_event_queue
 *
 * purpose: frees queue where the elements are of type TSMgmtEvent* 's
 * input: LLQ * q - a queue with entries of TSMgmtEvent*'s
 * output: None
 * note: None
 *********************************************************************/
void
delete_event_queue(LLQ *q)
{
  if (!q) {
    return;
  }

  while (!queue_is_empty(q)) {
    TSMgmtEvent *ele = static_cast<TSMgmtEvent *>(dequeue(q));
    ats_free(ele);
  }

  delete_queue(q);
  return;
}

/*********************************************************************
 * apiEventCallback
 *
 * purpose: callback function registered with alarm processor so that
 *          each time alarm is signalled, can enqueue it in the mgmt_events
 *          queue
 * input:
 * output: None
 * note: None
 *********************************************************************/
void
apiEventCallback(alarm_t newAlarm, const char * /* ip ATS_UNUSED */, const char *desc)
{
  // create an TSMgmtEvent
  // addEvent(new_alarm, ip, desc) // adds event to mgmt_events
  TSMgmtEvent *newEvent;

  newEvent       = TSEventCreate();
  newEvent->id   = newAlarm;
  newEvent->name = get_event_name(newEvent->id);
  // newEvent->ip   = ats_strdup(ip);
  if (desc) {
    newEvent->description = ats_strdup(desc);
  } else {
    newEvent->description = ats_strdup("None");
  }

  // add it to the mgmt_events list
  ink_mutex_acquire(&mgmt_events_lock);
  enqueue(mgmt_events, newEvent);
  ink_mutex_release(&mgmt_events_lock);

  return;
}

/*********************************************************************
 * event_callback_main
 *
 * This function is run as a thread in WebIntrMain.cc that listens on a
 * specified socket. It loops until Traffic Manager dies.
 * In the loop, it just listens on a socket, ready to accept any connections,
 * until receives a request from the remote API client. Parse the request
 * to determine which CoreAPI call to make.
 *********************************************************************/
void *
event_callback_main(void *arg)
{
  int ret;
  int *socket_fd;
  int con_socket_fd; // main socket for listening to new connections

  socket_fd     = static_cast<int *>(arg);
  con_socket_fd = *socket_fd; // the socket for event callbacks

  Debug("event", "[event_callback_main] listen on socket = %d", con_socket_fd);

  // initialize queue for holding mgmt events
  if ((ret = init_mgmt_events()) != TS_ERR_OKAY) {
    return nullptr;
  }
  // register callback with alarms processor
  lmgmt->alarm_keeper->registerCallback(apiEventCallback);

  // now we can start listening, accepting connections and servicing requests
  int new_con_fd; // new connection fd when socket accepts connection

  fd_set selectFDs;           // for select call
  EventClientT *client_entry; // an entry of fd to alarms mapping
  struct timeval timeout;

  while (true) {
    // LINUX fix: to prevent hard-spin reset timeout on each loop
    timeout.tv_sec  = 1;
    timeout.tv_usec = 0;

    FD_ZERO(&selectFDs);

    if (con_socket_fd >= 0) {
      FD_SET(con_socket_fd, &selectFDs);
      Debug("event", "[event_callback_main] add fd %d to select set", con_socket_fd);
    }
    // see if there are more fd to set
    for (auto &&it : accepted_clients) {
      client_entry = it.second;
      if (client_entry->fd >= 0) { // add fd to select set
        FD_SET(client_entry->fd, &selectFDs);
      }
    }

    // select call - timeout is set so we can check events at regular intervals
    int fds_ready = mgmt_select(FD_SETSIZE, &selectFDs, (fd_set *)nullptr, (fd_set *)nullptr, &timeout);

    // check return
    if (fds_ready > 0) {
      // we got connections or requests!

      // first check for connections!
      if (con_socket_fd >= 0 && FD_ISSET(con_socket_fd, &selectFDs)) {
        fds_ready--;

        // create a new instance of the fd to alarms registered mapping
        EventClientT *new_client_con = new_event_client();

        if (!new_client_con) {
          // Debug ("TS_Control_Main", "can't create new EventClientT for new connection");
        } else {
          // accept connection
          socklen_t addr_len = (sizeof(struct sockaddr));
          new_con_fd         = mgmt_accept(con_socket_fd, new_client_con->adr, &addr_len);
          new_client_con->fd = new_con_fd;
          accepted_clients.emplace(new_client_con->fd, new_client_con);
          Debug("event", "[event_callback_main] Accept new connection: fd=%d", new_con_fd);
        }
      } // end if (new_con_fd >= 0 && FD_ISSET(new_con_fd, &selectFDs))

      // some other file descriptor; for each one, service request
      if (fds_ready > 0) { // RECEIVED A REQUEST from remote API client
        // see if there are more fd to set - iterate through all entries in hash table
        for (auto it = accepted_clients.begin(); it != accepted_clients.end();) {
          client_entry = it->second;
          ++it; // prevent the breaking of remove_event_client
          // got information check
          if (client_entry->fd && FD_ISSET(client_entry->fd, &selectFDs)) {
            // SERVICE REQUEST - read the op and message into a buffer
            // clear the fields first
            void *req;
            size_t reqlen;

            ret = preprocess_msg(client_entry->fd, &req, &reqlen);
            if (ret == TS_ERR_NET_READ || ret == TS_ERR_NET_EOF) { // preprocess_msg FAILED!
              Debug("event", "[event_callback_main] preprocess_msg FAILED; skip!");
              remove_event_client(client_entry, accepted_clients);
              continue;
            }

            ret = handle_event_message(client_entry, req, reqlen);
            ats_free(req);

            if (ret == TS_ERR_NET_WRITE || ret == TS_ERR_NET_EOF) {
              Debug("event", "[event_callback_main] ERROR: handle_control_message");
              remove_event_client(client_entry, accepted_clients);
              continue;
            }

          } // end if(client_entry->fd && FD_ISSET(client_entry->fd, &selectFDs))
        }   // end for (auto it = accepted_clients.begin(); it != accepted_clients.end();)
      }     // end if (fds_ready > 0)

    } // end if (fds_ready > 0)

    // ------------ service loop is done, check for events now -------------
    // for each event in the mgmt_events list, uses the event id to check the
    // events_registered queue for each client connection to see if that client
    // has a callback registered for that event_id

    TSMgmtEvent *event;

    if (!mgmt_events || queue_is_empty(mgmt_events)) { // no events to process
      // fprintf(stderr, "[event_callback_main] NO EVENTS TO PROCESS\n");
      Debug("event", "[event_callback_main] NO EVENTS TO PROCESS");
      continue;
    }
    // iterate through each event in mgmt_events
    while (!queue_is_empty(mgmt_events)) {
      ink_mutex_acquire(&mgmt_events_lock);                     // acquire lock
      event = static_cast<TSMgmtEvent *>(dequeue(mgmt_events)); // get what we want
      ink_mutex_release(&mgmt_events_lock);                     // release lock

      if (!event) {
        continue;
      }

      // fprintf(stderr, "[event_callback_main] have an EVENT TO PROCESS\n");

      // iterate through all entries in hash table, if any
      for (auto &&it : accepted_clients) {
        client_entry = it.second;
        if (client_entry->events_registered[event->id]) {
          OpType optype           = OpType::EVENT_NOTIFY;
          MgmtMarshallString name = event->name;
          MgmtMarshallString desc = event->description;

          ret = send_mgmt_request(client_entry->fd, OpType::EVENT_NOTIFY, &optype, &name, &desc);
          if (ret != TS_ERR_OKAY) {
            Debug("event", "sending even notification to fd [%d] failed.", client_entry->fd);
          }
        }
        // get next client connection, if any
      } // end while(con_entry)

      // now we can delete the event
      // fprintf(stderr, "[event_callback_main] DELETE EVENT\n");
      TSEventDestroy(event);
    } // end while (!queue_is_empty)

  } // end while (1)

  // delete tables
  delete_mgmt_events();

  // iterate through hash table; close client socket connections and remove entry
  for (auto &&it : accepted_clients) {
    client_entry = it.second;
    if (client_entry->fd >= 0) {
      close_socket(client_entry->fd);
    }
    accepted_clients.erase(client_entry->fd); // remove binding
    delete_event_client(client_entry);        // free ClientT
  }
  // all entries should be removed and freed already
  accepted_clients.clear();

  ink_thread_exit(nullptr);
  return nullptr;
}

/*-------------------------------------------------------------------------
                             HANDLER FUNCTIONS
 --------------------------------------------------------------------------*/

/**************************************************************************
 * handle_event_reg_callback
 *
 * purpose: handles request to register a callback for a specific event (or all events)
 * input: client - the client currently reading the msg from
 *        req    - the event_name
 * output: TS_ERR_xx
 * note: the req should be the event name; does not send a reply to client
 *************************************************************************/
static TSMgmtError
handle_event_reg_callback(EventClientT *client, void *req, size_t reqlen)
{
  MgmtMarshallInt optype;
  MgmtMarshallString name = nullptr;
  TSMgmtError ret;

  ret = recv_mgmt_request(req, reqlen, OpType::EVENT_REG_CALLBACK, &optype, &name);
  if (ret != TS_ERR_OKAY) {
    goto done;
  }

  // mark the specified alarm as "wanting to be notified" in the client's alarm_registered list
  if (strlen(name) == 0) { // mark all alarms
    for (bool &i : client->events_registered) {
      i = true;
    }
  } else {
    int id = get_event_id(name);
    if (id < 0) {
      ret = TS_ERR_FAIL;
      goto done;
    }

    client->events_registered[id] = true;
  }

  ret = TS_ERR_OKAY;

done:
  ats_free(name);
  return ret;
}

/**************************************************************************
 * handle_event_unreg_callback
 *
 * purpose: handles request to unregister a callback for a specific event (or all events)
 * input: client - the client currently reading the msg from
 *        req    - the event_name
 * output: TS_ERR_xx
 * note: the req should be the event name; does not send reply to client
 *************************************************************************/
static TSMgmtError
handle_event_unreg_callback(EventClientT *client, void *req, size_t reqlen)
{
  MgmtMarshallInt optype;
  MgmtMarshallString name = nullptr;
  TSMgmtError ret;

  ret = recv_mgmt_request(req, reqlen, OpType::EVENT_UNREG_CALLBACK, &optype, &name);
  if (ret != TS_ERR_OKAY) {
    goto done;
  }

  // mark the specified alarm as "wanting to be notified" in the client's alarm_registered list
  if (strlen(name) == 0) { // mark all alarms
    for (bool &i : client->events_registered) {
      i = false;
    }
  } else {
    int id = get_event_id(name);
    if (id < 0) {
      ret = TS_ERR_FAIL;
      goto done;
    }

    client->events_registered[id] = false;
  }

  ret = TS_ERR_OKAY;

done:
  ats_free(name);
  return ret;
}

using event_message_handler = TSMgmtError (*)(EventClientT *, void *, size_t);

static const event_message_handler handlers[] = {
  nullptr,                     // RECORD_SET
  nullptr,                     // RECORD_GET
  nullptr,                     // PROXY_STATE_GET
  nullptr,                     // PROXY_STATE_SET
  nullptr,                     // RECONFIGURE
  nullptr,                     // RESTART
  nullptr,                     // BOUNCE
  nullptr,                     // EVENT_RESOLVE
  nullptr,                     // EVENT_GET_MLT
  nullptr,                     // EVENT_ACTIVE
  handle_event_reg_callback,   // EVENT_REG_CALLBACK
  handle_event_unreg_callback, // EVENT_UNREG_CALLBACK
  nullptr,                     // EVENT_NOTIFY
  nullptr,                     // DIAGS
  nullptr,                     // STATS_RESET_NODE
  nullptr,                     // STORAGE_DEVICE_CMD_OFFLINE
  nullptr,                     // RECORD_MATCH_GET
  nullptr,                     // LIFECYCLE_MESSAGE
  nullptr,                     // HOST_STATUS_UP
  nullptr,                     // HOST_STATUS_DOWN
};

static TSMgmtError
handle_event_message(EventClientT *client, void *req, size_t reqlen)
{
  OpType optype = extract_mgmt_request_optype(req, reqlen);

  if (static_cast<unsigned>(optype) >= countof(handlers)) {
    goto fail;
  }

  if (handlers[static_cast<unsigned>(optype)] == nullptr) {
    goto fail;
  }

  if (mgmt_has_peereid()) {
    uid_t euid = -1;
    gid_t egid = -1;

    // For now, all event messages require privilege. This is compatible with earlier
    // versions of Traffic Server that always required privilege.
    if (mgmt_get_peereid(client->fd, &euid, &egid) == -1 || (euid != 0 && euid != geteuid())) {
      return TS_ERR_PERMISSION_DENIED;
    }
  }

  return handlers[static_cast<unsigned>(optype)](client, req, reqlen);

fail:
  mgmt_elog(0, "%s: missing handler for type %d event message\n", __func__, static_cast<int>(optype));
  return TS_ERR_PARAMS;
}
