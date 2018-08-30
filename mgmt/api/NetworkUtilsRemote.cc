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

#include "tscore/ink_config.h"
#include "tscore/ink_defs.h"
#include "tscore/ink_sock.h"
#include "tscore/ink_string.h"
#include "tscore/ink_memory.h"
#include "tscore/I_Layout.h"
#include "NetworkUtilsRemote.h"
#include "CoreAPI.h"
#include "CoreAPIShared.h"
#include "MgmtSocket.h"
#include "MgmtMarshall.h"

CallbackTable *remote_event_callbacks;

int main_socket_fd  = -1;
int event_socket_fd = -1;

// need to store for reconnecting scenario
char *main_socket_path  = nullptr; // "<path>/mgmtapi.sock"
char *event_socket_path = nullptr; // "<path>/eventapi.sock"

static void *event_callback_thread(void *arg);

/**********************************************************************
 * Socket Helper Functions
 **********************************************************************/
void
set_socket_paths(const char *path)
{
  // free previously set paths if needed
  ats_free(main_socket_path);
  ats_free(event_socket_path);

  // construct paths based on user input
  // form by replacing "mgmtapi.sock" with "eventapi.sock"
  if (path) {
    main_socket_path  = ats_stringdup(Layout::relative_to(path, MGMTAPI_MGMT_SOCKET_NAME));
    event_socket_path = ats_stringdup(Layout::relative_to(path, MGMTAPI_EVENT_SOCKET_NAME));
  } else {
    main_socket_path  = nullptr;
    event_socket_path = nullptr;
  }

  return;
}

/**********************************************************************
 * socket_test
 *
 * purpose: performs socket write to check status of other end of connection
 * input: None
 * output: return false if socket write failed due to some other error
 *         return true if socket write successful
 * notes: send the API_PING test msg
 **********************************************************************/
static bool
socket_test(int fd)
{
  OpType optype       = OpType::API_PING;
  MgmtMarshallInt now = time(nullptr);

  if (MGMTAPI_SEND_MESSAGE(fd, OpType::API_PING, &optype, &now) == TS_ERR_OKAY) {
    return true; // write was successful; connection still open
  }

  return false;
}

/***************************************************************************
 * connect
 *
 * purpose: connects to the port on traffic server that listens to mgmt
 *          requests & issues out responses and alerts
 * 1) create and set the client socket_fd; connect to TM
 * 2) create and set the client's event_socket_fd; connect to TM
 * output: TS_ERR_OKAY          - if both sockets sucessfully connect to TM
 *         TS_ERR_NET_ESTABLISH - at least one unsuccessful connection
 * notes: If connection breaks it is responsibility of client to reconnect
 *        otherwise traffic server will assume mgmt stopped request and
 *        goes back to just sitting and listening for connection.
 ***************************************************************************/
TSMgmtError
ts_connect()
{
  struct sockaddr_un client_sock;
  struct sockaddr_un client_event_sock;

  int sockaddr_len;

  // make sure a socket path is set up
  if (!main_socket_path || !event_socket_path) {
    goto ERROR;
  }
  // make sure the length of main_socket_path do not exceed the sizeof(sun_path)
  if (strlen(main_socket_path) > sizeof(client_sock.sun_path) - 1) {
    goto ERROR;
  }
  // make sure the length of event_socket_path do not exceed the sizeof(sun_path)
  if (strlen(event_socket_path) > sizeof(client_event_sock.sun_path) - 1) {
    goto ERROR;
  }
  // create a socket
  main_socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (main_socket_fd < 0) {
    goto ERROR; // ERROR - can't open socket
  }
  // setup Unix domain socket
  memset(&client_sock, 0, sizeof(sockaddr_un));
  client_sock.sun_family = AF_UNIX;
  ink_strlcpy(client_sock.sun_path, main_socket_path, sizeof(client_sock.sun_path));
#if defined(darwin) || defined(freebsd)
  sockaddr_len = sizeof(sockaddr_un);
#else
  sockaddr_len = sizeof(client_sock.sun_family) + strlen(client_sock.sun_path);
#endif
  // connect call
  if (connect(main_socket_fd, (struct sockaddr *)&client_sock, sockaddr_len) < 0) {
    close(main_socket_fd);
    main_socket_fd = -1;
    goto ERROR; // connection is down
  }
  // -------- set up the event socket ------------------
  // create a socket
  event_socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (event_socket_fd < 0) {
    close(main_socket_fd); // close the other socket too!
    main_socket_fd = -1;
    goto ERROR; // ERROR - can't open socket
  }
  // setup Unix domain socket
  memset(&client_event_sock, 0, sizeof(sockaddr_un));
  client_event_sock.sun_family = AF_UNIX;
  ink_strlcpy(client_event_sock.sun_path, event_socket_path, sizeof(client_event_sock.sun_path));
#if defined(darwin) || defined(freebsd)
  sockaddr_len = sizeof(sockaddr_un);
#else
  sockaddr_len = sizeof(client_event_sock.sun_family) + strlen(client_event_sock.sun_path);
#endif
  // connect call
  if (connect(event_socket_fd, (struct sockaddr *)&client_event_sock, sockaddr_len) < 0) {
    close(event_socket_fd);
    close(main_socket_fd);
    event_socket_fd = -1;
    main_socket_fd  = -1;
    goto ERROR; // connection is down
  }

  return TS_ERR_OKAY;

ERROR:
  return TS_ERR_NET_ESTABLISH;
}

/***************************************************************************
 * disconnect
 *
 * purpose: disconnect from traffic server; closes sockets and resets their values
 * input: None
 * output: TS_ERR_FAIL, TS_ERR_OKAY
 * notes: doesn't do clean up - all cleanup should be done before here
 ***************************************************************************/
TSMgmtError
disconnect()
{
  int ret;

  if (main_socket_fd > 0) {
    ret            = close(main_socket_fd);
    main_socket_fd = -1;
    if (ret < 0) {
      return TS_ERR_FAIL;
    }
  }

  if (event_socket_fd > 0) {
    ret             = close(event_socket_fd);
    event_socket_fd = -1;
    if (ret < 0) {
      return TS_ERR_FAIL;
    }
  }

  return TS_ERR_OKAY;
}

/***************************************************************************
 * reconnect
 *
 * purpose: reconnects to TM (eg. when TM restarts); does all the necesarry
 *          set up for reconnection
 * input: None
 * output: TS_ERR_FAIL, TS_ERR_OKAY
 * notes: necessarry events for a new client-TM connection:
 * 1) get new socket_fd using old socket_path by calling connect()
 * 2) relaunch event_poll_thread_main with new socket_fd
 * 3) re-notify TM of all the client's registered callbacks by send msg
 ***************************************************************************/
TSMgmtError
reconnect()
{
  TSMgmtError err;

  err = disconnect();
  if (err != TS_ERR_OKAY) { // problem disconnecting
    return err;
  }

  // use the socket_path that was called by remote client on first init
  // use connect instead of TSInit() b/c if TM restarted, client-side tables
  // would be recreated; just want to reconnect to same socket_path
  err = ts_connect();
  if (err != TS_ERR_OKAY) { // problem establishing connection
    return err;
  }

  // relaunch a new event thread since socket_fd changed
  if (0 == (ts_init_options & TS_MGMT_OPT_NO_EVENTS)) {
    ink_thread_create(&ts_event_thread, event_poll_thread_main, &event_socket_fd, 0, 0, nullptr);
    // reregister the callbacks on the TM side for this new client connection
    if (remote_event_callbacks) {
      err = send_register_all_callbacks(event_socket_fd, remote_event_callbacks);
      if (err != TS_ERR_OKAY) { // problem establishing connection
        return err;
      }
    }
  } else {
    ts_event_thread = ink_thread_null();
  }

  return TS_ERR_OKAY;
}

/***************************************************************************
 * reconnect_loop
 *
 * purpose: attempts to reconnect to TM (eg. when TM restarts) for the
 *          specified number of times
 * input:  num_attempts - number of reconnection attempts to try before quit
 * output: TS_ERR_OKAY - if successfully reconnected within num_attempts
 *         TS_ERR_xx - the reason the reconnection failed
 * notes:
 ***************************************************************************/
TSMgmtError
reconnect_loop(int num_attempts)
{
  int numTries    = 0;
  TSMgmtError err = TS_ERR_FAIL;

  while (numTries < num_attempts) {
    numTries++;
    err = reconnect();
    if (err == TS_ERR_OKAY) {
      return TS_ERR_OKAY; // successful connection
    }
    sleep(1); // to make it slower
  }

  return err; // unsuccessful connection after num_attempts
}

/*************************************************************************
 * connect_and_send
 *
 * purpose:
 * When sending a request, it's possible that the user had restarted
 * Traffic Manager. This means that the connection between TM and
 * the remote client has been broken, so the client needs to re-"connect"
 * to Traffic Manager. So, after "writing" to the socket in each
 * "send_xx_request" function, need to check if the TM socket has
 * been closed or not; the "write" function's errno will indicate if
 * the other end of the socket has been closed or not. If it is closed,
 * then need to try to re"connect", then resend the message request if
 * the "connect" was successful.
 * 1) try connect()
 * 2) if connect() success, then resend the request.
 * output: TS_ERR_NET_xx - connection problem or TS_ERR_OKAY
 * notes:
 * This function is basically called by the special "socket_write_conn" fn
 * which will call this fn if it tries to write to the socket and discovers
 * the local end of the socket is closed
 * Warning: system also sends a SIGPIPE error when try to write to socket
 * which is not open; which will by default terminate the process;
 * client needs to "ignore" the SIGPIPE signal
 **************************************************************************/
static TSMgmtError
main_socket_reconnect()
{
  TSMgmtError err;

  // connects to TM and does all necessary event updates required
  err = reconnect();
  if (err != TS_ERR_OKAY) {
    return err;
  }

  // makes sure the descriptor is writable
  if (mgmt_write_timeout(main_socket_fd, MAX_TIME_WAIT, 0) <= 0) {
    return TS_ERR_NET_TIMEOUT;
  }

  return TS_ERR_OKAY;
}

static TSMgmtError
socket_write_conn(int fd, const void *msg_buf, size_t bytes)
{
  size_t byte_wrote = 0;

  // makes sure the descriptor is writable
  if (mgmt_write_timeout(fd, MAX_TIME_WAIT, 0) <= 0) {
    return TS_ERR_NET_TIMEOUT;
  }

  // write until we fulfill the number
  while (byte_wrote < bytes) {
    ssize_t ret = write(fd, (const char *)msg_buf + byte_wrote, bytes - byte_wrote);

    if (ret == 0) {
      return TS_ERR_NET_EOF;
    }

    if (ret < 0) {
      if (mgmt_transient_error()) {
        continue;
      } else {
        return TS_ERR_NET_WRITE;
      }
    }

    // we are all good here
    byte_wrote += ret;
  }

  return TS_ERR_OKAY;
}

TSMgmtError
mgmtapi_sender::send(void *msg, size_t msglen) const
{
  const unsigned tries = 5;
  TSMgmtError err;

  for (unsigned i = 0; i < tries; ++i) {
    err = socket_write_conn(this->fd, msg, msglen);
    if (err == TS_ERR_OKAY) {
      return err;
    }

    // clean-up sockets
    close(main_socket_fd);
    close(event_socket_fd);
    main_socket_fd  = -1;
    event_socket_fd = -1;

    err = main_socket_reconnect();
    if (err != TS_ERR_OKAY) {
      return err;
    }
  }

  return TS_ERR_NET_ESTABLISH; // can't establish connection
}

/**********************************************************************
 * socket_test_thread
 *
 * purpose: continually polls to check if local end of socket connection
 *          is still open; this thread is created when the client calls
 *          Init() to initialize the API; and will not
 *          die until the client process dies
 * input: none
 * output: if other end is closed, it reconnects to TM
 * notes: uses the current main_socket_fd because the main_socket_fd could be
 *        in flux; basically it is possible that the client will reconnect
 *        from some other call, thus making the main_socket_fd actually
 *        valid when socket_test is called
 * reason: decided to create this "watcher" thread for the socket
 *         connection because if TM is restarted or the client process
 *         is started before the TM process, then the client will not
 *         be able to receive any event notifications until a "request"
 *         is issued. In order to prevent losing an event notifications
 *         that are called in between the time TM is restarted and
 *         client issues a first request, we just run this thread which
 *         will try to reconnect to TM if it is not already connected
 **********************************************************************/
void *
socket_test_thread(void *)
{
  // loop until client process dies
  while (true) {
    if (main_socket_fd == -1 || !socket_test(main_socket_fd)) {
      // ASSUMES that in between the time the socket_test is made
      // and this reconnect call is made, the main_socket_fd remains
      // the same (eg. no one else called reconnect to TM successfully!!
      // WHAT IF in between this time, the client had issued a request
      // calling socket_write_conn which then calls reconnect(); then
      // reconnect will return an "ALREADY CONNECTED" error when it
      // tries to connect, and on the next loop iteration, the socket_test
      // will actually pass because main_socket_fd is valid!!
      reconnect();
    }

    sleep(5);
  }

  ink_thread_exit(nullptr);
  return nullptr;
}

/**********************************************************************
 * MARSHALL REQUESTS
 **********************************************************************/

/*------ events -------------------------------------------------------*/

/**********************************************************************
 * send_register_all_callbacks
 *
 * purpose: determines all events which have at least one callback registered
 *          and sends message to notify TM that this client has a callback
 *          registered for each event
 * input: None
 * output: return TS_ERR_OKAY only if ALL events sent okay
 * 1) get list of all events with callbacks
 * 2) for each event, send a EVENT_REG_CALLBACK message
 **********************************************************************/
TSMgmtError
send_register_all_callbacks(int fd, CallbackTable *cb_table)
{
  LLQ *events_with_cb;
  TSMgmtError err, send_err = TS_ERR_FAIL;
  bool no_errors = true; // set to false if one send is not okay

  events_with_cb = get_events_with_callbacks(cb_table);
  // need to check that the list has all the events registered
  if (!events_with_cb) { // all events have registered callback
    OpType optype                 = OpType::EVENT_REG_CALLBACK;
    MgmtMarshallString event_name = nullptr;

    err = MGMTAPI_SEND_MESSAGE(fd, OpType::EVENT_REG_CALLBACK, &optype, &event_name);
    if (err != TS_ERR_OKAY) {
      return err;
    }
  } else {
    int num_events = queue_len(events_with_cb);
    // iterate through the LLQ and send request for each event
    for (int i = 0; i < num_events; i++) {
      OpType optype                 = OpType::EVENT_REG_CALLBACK;
      MgmtMarshallInt event_id      = *(int *)dequeue(events_with_cb);
      MgmtMarshallString event_name = (char *)get_event_name(event_id);

      if (event_name) {
        err = MGMTAPI_SEND_MESSAGE(fd, OpType::EVENT_REG_CALLBACK, &optype, &event_name);
        ats_free(event_name); // free memory
        if (err != TS_ERR_OKAY) {
          send_err  = err; // save the type of send error
          no_errors = false;
        }
      }
      // REMEMBER: WON"T GET A REPLY from TM side!
    }
  }

  if (events_with_cb) {
    delete_queue(events_with_cb);
  }

  if (no_errors) {
    return TS_ERR_OKAY;
  } else {
    return send_err;
  }
}

/**********************************************************************
 * send_unregister_all_callbacks
 *
 * purpose: determines all events which have no callback registered
 *          and sends message to notify TM that this client has no
 *          callbacks registered for that event
 * input: None
 * output: TS_ERR_OKAY only if all send requests are okay
 **********************************************************************/
TSMgmtError
send_unregister_all_callbacks(int fd, CallbackTable *cb_table)
{
  int event_id;
  LLQ *events_with_cb; // list of events with at least one callback
  int reg_callback[NUM_EVENTS];
  TSMgmtError err, send_err = TS_ERR_FAIL;
  bool no_errors = true; // set to false if at least one send fails

  // init array so that all events don't have any callbacks
  for (int &i : reg_callback) {
    i = 0;
  }

  events_with_cb = get_events_with_callbacks(cb_table);
  if (!events_with_cb) { // all events have a registered callback
    return TS_ERR_OKAY;
  } else {
    int num_events = queue_len(events_with_cb);
    // iterate through the LLQ and mark events that have a callback
    for (int i = 0; i < num_events; i++) {
      event_id               = *(int *)dequeue(events_with_cb);
      reg_callback[event_id] = 1; // mark the event as having a callback
    }
    delete_queue(events_with_cb);
  }

  // send message to TM to mark unregister
  for (int k = 0; k < NUM_EVENTS; k++) {
    if (reg_callback[k] == 0) { // event has no registered callbacks
      OpType optype                 = OpType::EVENT_UNREG_CALLBACK;
      MgmtMarshallString event_name = get_event_name(k);

      err = MGMTAPI_SEND_MESSAGE(fd, OpType::EVENT_UNREG_CALLBACK, &optype, &event_name);
      ats_free(event_name);
      if (err != TS_ERR_OKAY) {
        send_err  = err; // save the type of the sending error
        no_errors = false;
      }
      // REMEMBER: WON"T GET A REPLY!
      // only the event_poll_thread_main does any reading of the event_socket;
      // so DO NOT parse reply b/c a reply won't be sent
    }
  }

  if (no_errors) {
    return TS_ERR_OKAY;
  } else {
    return send_err;
  }
}

/**********************************************************************
 * UNMARSHAL REPLIES
 **********************************************************************/

TSMgmtError
parse_generic_response(OpType optype, int fd)
{
  TSMgmtError err;
  MgmtMarshallInt ival;
  MgmtMarshallData data = {nullptr, 0};

  err = recv_mgmt_message(fd, data);
  if (err != TS_ERR_OKAY) {
    return err;
  }

  err = recv_mgmt_response(data.ptr, data.len, optype, &ival);
  ats_free(data.ptr);

  if (err != TS_ERR_OKAY) {
    return err;
  }

  return (TSMgmtError)ival;
}

/**********************************************************************
 * event_poll_thread_main
 *
 * purpose: thread listens on the client's event socket connection;
 *          only reads from the event_socket connection and
 *          processes EVENT_NOTIFY messages; each time client
 *          makes new event-socket connection to TM, must launch
 *          a new event_poll_thread_main thread
 * input:   arg - contains the socket_fd to listen on
 * output:  NULL - if error
 * notes:   each time the client's socket connection to TM is reset
 *          a new thread will be launched as old one dies; there are
 *          only two places where a new thread is created:
 *          1) when client first connects (TSInit call)
 *          2) client reconnects() due to a TM restart
 * Uses blocking socket; so blocks until receives an event notification.
 * Shouldn't need to use select since only waiting for a notification
 * message from event_callback_main thread!
 **********************************************************************/
void *
event_poll_thread_main(void *arg)
{
  int sock_fd;

  sock_fd = *((int *)arg); // should be same as event_socket_fd

  // the sock_fd is going to be the one we listen for events on
  while (true) {
    TSMgmtError ret;
    TSMgmtEvent *event = nullptr;

    MgmtMarshallData reply = {nullptr, 0};
    OpType optype;
    MgmtMarshallString name = nullptr;
    MgmtMarshallString desc = nullptr;

    // possible sock_fd is invalid if TM restarts and client reconnects
    if (sock_fd < 0) {
      break;
    }

    // Just wait until we get an event or error. The 0 return from select(2)
    // means we timed out ...
    if (mgmt_read_timeout(main_socket_fd, MAX_TIME_WAIT, 0) == 0) {
      continue;
    }

    ret = recv_mgmt_message(main_socket_fd, reply);
    if (ret != TS_ERR_OKAY) {
      break;
    }

    ret = recv_mgmt_request(reply.ptr, reply.len, OpType::EVENT_NOTIFY, &optype, &name, &desc);
    ats_free(reply.ptr);

    if (ret != TS_ERR_OKAY) {
      ats_free(name);
      ats_free(desc);
      break;
    }

    ink_assert(optype == OpType::EVENT_NOTIFY);

    // The new event takes ownership of the message strings.
    event              = TSEventCreate();
    event->name        = name;
    event->id          = get_event_id(name);
    event->description = desc;

    // got event notice; spawn new thread to handle the event's callback functions
    ink_thread_create(nullptr, event_callback_thread, (void *)event, 0, 0, nullptr);
  }

  ink_thread_exit(nullptr);
  return nullptr;
}

/**********************************************************************
 * event_callback_thread
 *
 * purpose: Given an event, determines and calls the registered cb functions
 *          in the CallbackTable for remote events
 * input: arg - should be an TSMgmtEvent with the event info sent from TM msg
 * output: returns when done calling all the callbacks
 * notes: None
 **********************************************************************/
static void *
event_callback_thread(void *arg)
{
  TSMgmtEvent *event_notice;
  EventCallbackT *event_cb;
  int index;

  event_notice = (TSMgmtEvent *)arg;
  index        = (int)event_notice->id;
  LLQ *func_q; // list of callback functions need to call

  func_q = create_queue();
  if (!func_q) {
    TSEventDestroy(event_notice);
    return nullptr;
  }

  // obtain lock
  ink_mutex_acquire(&remote_event_callbacks->event_callback_lock);

  TSEventSignalFunc cb;

  // check if we have functions to call
  if (remote_event_callbacks->event_callback_l[index] && (!queue_is_empty(remote_event_callbacks->event_callback_l[index]))) {
    int queue_depth = queue_len(remote_event_callbacks->event_callback_l[index]);

    for (int i = 0; i < queue_depth; i++) {
      event_cb = (EventCallbackT *)dequeue(remote_event_callbacks->event_callback_l[index]);
      cb       = event_cb->func;
      enqueue(remote_event_callbacks->event_callback_l[index], event_cb);
      enqueue(func_q, (void *)cb); // add callback function only to list
    }
  }
  // release lock
  ink_mutex_release(&remote_event_callbacks->event_callback_lock);

  // execute the callback function
  while (!queue_is_empty(func_q)) {
    cb = (TSEventSignalFunc)dequeue(func_q);
    (*cb)(event_notice->name, event_notice->description, event_notice->priority, nullptr);
  }

  // clean up event notice
  TSEventDestroy(event_notice);
  delete_queue(func_q);

  // all done!
  ink_thread_exit(nullptr);
  return nullptr;
}
