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
 * Filename: NetworkUtilsRemote.cc
 * Purpose: This file contains functions used by remote api client to
 *          marshal requests to TM and unmarshal replies from TM.
 *          Also stores the information about the client's current
 *          socket connection to Traffic Manager
 * Created: 8/9/00
 *
 *
 ***************************************************************************/

#include "ink_config.h"
#include "ink_defs.h"
#include "ink_sock.h"
#include "ink_string.h"
#include "I_Layout.h"
#include "NetworkUtilsRemote.h"
#include "CoreAPI.h"
#include "CoreAPIShared.h"
#include "EventRegistration.h"
#include "MgmtSocket.h"

extern CallbackTable *remote_event_callbacks;

int main_socket_fd = -1;
int event_socket_fd = -1;

// need to store for reconnecting scenario
char *main_socket_path = NULL;  // "<path>/mgmtapisocket"
char *event_socket_path = NULL; // "<path>/eventapisocket"

// From CoreAPIRemote.cc
extern ink_thread ts_test_thread;
extern ink_thread ts_event_thread;
extern TSInitOptionT ts_init_options;


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
  // form by replacing "mgmtapisocket" with "eventapisocket"
  if (path) {
    main_socket_path = Layout::relative_to(path, "mgmtapisocket");
    event_socket_path = Layout::relative_to(path, "eventapisocket");
  } else {
    main_socket_path = NULL;
    event_socket_path = NULL;
  }

  return;
}

/**********************************************************************
 * socket_test
 *
 * purpose: performs socket write to check status of other end of connection
 * input: None
 * output: return   0 if other end of connection closed;
 *         return < 0 if socket write failed due to some other error
 *         return > 0 if socket write successful
 * notes: send the test msg: UNDEFINED_OP 0(=msg_len)
 **********************************************************************/
int
socket_test(int fd)
{
  char msg[6];                  /* 6 = SIZE_OP + SIZE_LEN */
  int16_t op;
  int32_t msg_len = 0;
  int ret, amount_read = 0;

  // write the op
  op = (int16_t) UNDEFINED_OP;
  memcpy(msg, (void *) &op, SIZE_OP_T);

  // write msg-len = 0
  memcpy(msg + SIZE_OP_T, &msg_len, SIZE_LEN);

  while (amount_read < 6) {
    ret = write(fd, msg + amount_read, 6 - amount_read);
    if (ret < 0) {
      if (errno == EAGAIN)
        continue;

      if (errno == EPIPE || errno == ENOTCONN) {        // other socket end is closed
        return 0;
      }

      return -1;
    }
    amount_read += ret;
  }

  return 1;                     // write was successful; connection still open
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
TSError
ts_connect()
{
  struct sockaddr_un client_sock;
  struct sockaddr_un client_event_sock;

  int sockaddr_len;

  // make sure a socket path is set up
  if (!main_socket_path || !event_socket_path)
    goto ERROR;

  // create a socket
  main_socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (main_socket_fd < 0) {
    //fprintf(stderr, "[connect] ERROR: can't open socket\n");
    goto ERROR;                 // ERROR - can't open socket
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
  if (connect(main_socket_fd, (struct sockaddr *) &client_sock, sockaddr_len) < 0) {
    fprintf(stderr, "[connect] ERROR (main_socket_fd %d): %s\n", main_socket_fd, strerror(int(errno)));
    close(main_socket_fd);
    main_socket_fd = -1;
    goto ERROR;                 //connection is down
  }
  // -------- set up the event socket ------------------
  // create a socket
  event_socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (event_socket_fd < 0) {
    //fprintf(stderr, "[connect] ERROR: can't open event socket\n");
    close(main_socket_fd);      // close the other socket too!
    main_socket_fd = -1;
    goto ERROR;                 // ERROR - can't open socket
  }
  // setup Unix domain socket
  memset(&client_event_sock, 0, sizeof(sockaddr_un));
  client_event_sock.sun_family = AF_UNIX;
  ink_strlcpy(client_event_sock.sun_path, event_socket_path, sizeof(client_sock.sun_path));
#if defined(darwin) || defined(freebsd)
  sockaddr_len = sizeof(sockaddr_un);
#else
  sockaddr_len = sizeof(client_event_sock.sun_family) + strlen(client_event_sock.sun_path);
#endif
  // connect call
  if (connect(event_socket_fd, (struct sockaddr *) &client_event_sock, sockaddr_len) < 0) {
    //fprintf(stderr, "[connect] ERROR (event_socket_fd %d): %s\n", event_socket_fd, strerror(int(errno)));
    close(event_socket_fd);
    close(main_socket_fd);
    event_socket_fd = -1;
    main_socket_fd = -1;
    goto ERROR;                 //connection is down
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
TSError
disconnect()
{
  int ret;

  if (main_socket_fd > 0) {
    ret = close(main_socket_fd);
    main_socket_fd = -1;
    if (ret < 0)
      return TS_ERR_FAIL;
  }

  if (event_socket_fd > 0) {
    ret = close(event_socket_fd);
    event_socket_fd = -1;
    if (ret < 0)
      return TS_ERR_FAIL;
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
TSError
reconnect()
{
  TSError err;

  err = disconnect();
  if (err != TS_ERR_OKAY)      // problem disconnecting
    return err;

  // use the socket_path that was called by remote client on first init
  // use connect instead of TSInit() b/c if TM restarted, client-side tables
  // would be recreated; just want to reconnect to same socket_path
  err = ts_connect();
  if (err != TS_ERR_OKAY)      // problem establishing connection
    return err;

  // relaunch a new event thread since socket_fd changed
  if (0 == (ts_init_options & TS_MGMT_OPT_NO_EVENTS)) {
    ts_event_thread = ink_thread_create(event_poll_thread_main, &event_socket_fd, 0, DEFAULT_STACK_SIZE);
    // reregister the callbacks on the TM side for this new client connection
    if (remote_event_callbacks) {
      err = send_register_all_callbacks(event_socket_fd, remote_event_callbacks);
      if (err != TS_ERR_OKAY)      // problem establishing connection
        return err;
    }
  } else {
    ts_event_thread = static_cast<ink_thread>(NULL);
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
TSError
reconnect_loop(int num_attempts)
{
  int numTries = 0;
  TSError err = TS_ERR_FAIL;

  while (numTries < num_attempts) {
    numTries++;
    err = reconnect();
    if (err == TS_ERR_OKAY) {
      //fprintf(stderr, "[reconnect_loop] Successful reconnction; Leave loop\n");
      return TS_ERR_OKAY;      // successful connection
    }
    sleep(1);                   // to make it slower
  }

  //fprintf(stderr, "[reconnect_loop] FAIL TO CONNECT after %d tries\n", num_attempts);
  return err;                   // unsuccessful connection after num_attempts
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
TSError
connect_and_send(const char *msg, int msg_len)
{
  TSError err;
  int total_wrote = 0, ret;

  // connects to TM and does all necessary event updates required
  err = reconnect();
  if (err != TS_ERR_OKAY)
    return err;

  // makes sure the descriptor is writable
  if (socket_write_timeout(main_socket_fd, MAX_TIME_WAIT, 0) <= 0) {
    return TS_ERR_NET_TIMEOUT;
  }
  // connection successfully established; resend msg
  // socket_fd should be new fd
  while (total_wrote < msg_len) {
    ret = write(main_socket_fd, msg + total_wrote, msg_len - total_wrote);

    if (ret == 0) {
      return TS_ERR_NET_EOF;
    }

    if (ret < 0) {
      if (errno == EAGAIN)
        continue;
      else if (errno == EPIPE || errno == ENOTCONN) {
        // clean-up sockets
        close(main_socket_fd);
        close(event_socket_fd);
        main_socket_fd = -1;
        event_socket_fd = -1;

        return TS_ERR_NET_ESTABLISH;   // can't establish connection

      } else
        return TS_ERR_NET_WRITE;       // general socket writing error

    }

    total_wrote += ret;
  }

  return TS_ERR_OKAY;
}

static TSError
socket_read_conn(int fd, uint8_t * buf, size_t needed)
{
  size_t consumed = 0;
  ssize_t ret;

  while (needed > consumed) {
    ret = read(fd, buf, needed - consumed);

    if (ret < 0) {
      if (errno == EAGAIN) {
        continue;
      } else {
        return TS_ERR_NET_READ;
      }
    }

    if (ret == 0) {
      return TS_ERR_NET_EOF;
    }

    buf += ret;
    consumed += ret;
  }

  return TS_ERR_OKAY;
}

/**************************************************************************
 * socket_write_conn
 *
 * purpose: guarantees writing of n bytes; if connection error, tries
 *          reconnecting to TM again (in case TM was restarted)
 * input:   fd to write to, buffer to write from & number of bytes to write
 * output:  TS_ERR_xx
 * note:   EPIPE - this happens if client makes a call after stopping then
 *         starting TM again.
 *         ENOTCONN - this happens if the client tries to make a call after
 *         stopping TM, but before starting it; then restarts TM and makes a
 *          new call
 * In the send_xx_request function, use a special socket writing function
 * which calls connect_and_send() instead of just the basic connect():
 * 1) if the write returns EPIPE error, then call connect_and_send()
 * 2) return the value returned from EPIPE
 *************************************************************************/
static TSError
socket_write_conn(int fd, const char *msg_buf, int bytes)
{
  int ret, byte_wrote = 0;

  // makes sure the descriptor is writable
  if (socket_write_timeout(fd, MAX_TIME_WAIT, 0) <= 0) {
    return TS_ERR_NET_TIMEOUT;
  }
  // write until we fulfill the number
  while (byte_wrote < bytes) {
    ret = write(fd, msg_buf + byte_wrote, bytes - byte_wrote);

    if (ret == 0) {
      return TS_ERR_NET_EOF;
    }

    if (ret < 0) {
      if (errno == EAGAIN)
        continue;

      else if (errno == EPIPE || errno == ENOTCONN) {   // other socket end is closed
        // clean-up of sockets is done in reconnect()
        return connect_and_send(msg_buf, bytes);
      } else
        return TS_ERR_NET_WRITE;
    }
    // we are all good here
    byte_wrote += ret;
  }

  return TS_ERR_OKAY;
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
  while (1) {
    if (main_socket_fd == -1 || socket_test(main_socket_fd) <= 0) {
      // ASSUMES that in between the time the socket_test is made
      // and this reconnect call is made, the main_socket_fd remains
      // the same (eg. no one else called reconnect to TM successfully!!
      // WHAT IF in between this time, the client had issued a request
      // calling socket_write_conn which then calls reconnect(); then
      // reconnect will return an "ALREADY CONNECTED" error when it
      // tries to connect, and on the next loop iteration, the socket_test
      // will actually pass because main_socket_fd is valid!!
      if (reconnect() == TS_ERR_OKAY) {
        //fprintf(stderr, "[socket_test_thread] reconnect succeeds\n");
      }
    }

    sleep(5);
  }

  ink_thread_exit(NULL);
  return NULL;
}

/**********************************************************************
 * MARSHALL REQUESTS
 **********************************************************************/
/**********************************************************************
 * send_request
 *
 * purpose: sends file read request to Traffic Manager
 * input:   fd - file descriptor to use to send to
 *          op - the type of OpType request sending
 * output:  TS_ERR_xx
 * notes:  used by operations which don't need to send any additional
 *         parameters
 * format: <OpType> <msg_len=0>
 **********************************************************************/
TSError
send_request(int fd, OpType op)
{
  int16_t op_t;
  int32_t msg_len;
  char msg_buf[SIZE_OP_T + SIZE_LEN];
  TSError err;

  // fill in op type
  op_t = (int16_t) op;
  memcpy(msg_buf, &op_t, SIZE_OP_T);

  // fill in msg_len == 0
  msg_len = 0;
  memcpy(msg_buf + SIZE_OP_T, &msg_len, SIZE_LEN);

  // send message
  err = socket_write_conn(fd, msg_buf, SIZE_OP_T + SIZE_LEN);
  return err;
}

/**********************************************************************
 * send_request_name (helper fn)
 *
 * purpose: sends generic  request with one string argument name
 * input: fd - file descriptor to use
 *        op - .
 * output: TS_ERR_xx
 * note: format: <OpType> <str_len> <string>
 **********************************************************************/
TSError
send_request_name(int fd, OpType op, const char *name)
{
  char *msg_buf;
  int16_t op_t;
  int32_t msg_len;
  int total_len;
  TSError err;

  if (name == NULL) {           //reg callback for all events when op==EVENT_REG_CALLBACK
    msg_len = 0;
  } else {
    msg_len = (int32_t) strlen(name);
  }

  total_len = SIZE_OP_T + SIZE_LEN + msg_len;
  msg_buf = (char *)ats_malloc(sizeof(char) * total_len);

  // fill in op type
  op_t = (int16_t) op;
  memcpy(msg_buf, (void *) &op_t, SIZE_OP_T);

  // fill in msg_len
  memcpy(msg_buf + SIZE_OP_T, (void *) &msg_len, SIZE_LEN);

  // fill in name (if NOT NULL)
  if (name)
    memcpy(msg_buf + SIZE_OP_T + SIZE_LEN, name, msg_len);


  // send message
  err = socket_write_conn(fd, msg_buf, total_len);
  ats_free(msg_buf);
  return err;
}

/**********************************************************************
 * send_request_name_value (helper fn)
 *
 * purpose: sends generic request with 2 str arguments; a name-value pair
 * input: fd - file descriptor to use
 *        op - Op type
 * output: TS_ERR_xx
 * note: format: <OpType> <name-len> <val-len> <name> <val>
 **********************************************************************/
TSError
send_request_name_value(int fd, OpType op, const char *name, const char *value)
{
  char *msg_buf;
  int msg_pos = 0, total_len;
  int32_t msg_len, name_len, val_size;    // these are written to msg
  int16_t op_t;
  TSError err;

  if (!name || !value)
    return TS_ERR_PARAMS;

  // set the sizes
  name_len = strlen(name);
  val_size = strlen(value);
  msg_len = (SIZE_LEN * 2) + name_len + val_size;
  total_len = SIZE_OP_T + SIZE_LEN + msg_len;
  msg_buf = (char *)ats_malloc(sizeof(char) * (total_len));

  // fill in op type
  op_t = (int16_t) op;
  memcpy(msg_buf + msg_pos, (void *) &op_t, SIZE_OP_T);
  msg_pos += SIZE_OP_T;

  // fill in msg length
  memcpy(msg_buf + msg_pos, (void *) &msg_len, SIZE_LEN);
  msg_pos += SIZE_LEN;

  // fill in record name length
  memcpy(msg_buf + msg_pos, (void *) &name_len, SIZE_LEN);
  msg_pos += SIZE_LEN;

  // fill in record value length
  memcpy(msg_buf + msg_pos, (void *) &val_size, SIZE_LEN);
  msg_pos += SIZE_LEN;

  // fill in record name
  memcpy(msg_buf + msg_pos, name, name_len);
  msg_pos += name_len;

  // fill in record value
  memcpy(msg_buf + msg_pos, value, val_size);

  // send message
  err = socket_write_conn(fd, msg_buf, total_len);
  ats_free(msg_buf);
  return err;
}

/**********************************************************************
 * send_request_bool (helper)
 *
 * purpose: sends a simple op with a boolean flag argument
 * input: fd      - file descriptor to use
 *        flag    - boolean flag
 * output: TS_ERR_xx
 **********************************************************************/
TSError
send_request_bool(int fd, OpType op, bool flag)
{
  char msg_buf[SIZE_OP_T + SIZE_LEN + SIZE_BOOL];
  int16_t flag_t;
  int32_t msg_len;

  // Fill in the operator
  memcpy(msg_buf, (void *) &op, SIZE_OP_T);

  // fill in msg_len = SIZE_BOOL
  msg_len = (int32_t) SIZE_BOOL;
  memcpy(msg_buf + SIZE_OP_T, (void *)&msg_len, SIZE_LEN);

  // Fill in the argument (the boolean flag)
  flag_t = flag ? 1 : 0;
  memcpy(msg_buf + SIZE_OP_T + SIZE_LEN, (void *) &flag_t, SIZE_BOOL);

  // send message
  return socket_write_conn(fd, msg_buf, SIZE_OP_T + SIZE_LEN + SIZE_BOOL);
}

/**********************************************************************
 * send_file_read_request
 *
 * purpose: sends file read request to Traffic Manager
 * input:   fd - file descriptor to use to send to
 *          file - file to read
 * output:  TS_ERR_xx
 * notes:   first must create the message and then send it across network
 *          msg format = <OpType> <msg_len> <TSFileNameT>
 **********************************************************************/
TSError
send_file_read_request(int fd, TSFileNameT file)
{
  char msg_buf[SIZE_OP_T + SIZE_LEN + SIZE_FILE_T];
  int msg_pos = 0;
  int32_t msg_len = (int32_t) SIZE_FILE_T;  //marshalled values
  int16_t op, file_t;

  // fill in op type
  op = (int16_t) FILE_READ;
  memcpy(msg_buf + msg_pos, &op, SIZE_OP_T);
  msg_pos += SIZE_OP_T;

  // fill in msg length
  memcpy(msg_buf + msg_pos, &msg_len, SIZE_LEN);
  msg_pos += SIZE_LEN;

  // fill in file type
  file_t = (int16_t) file;
  memcpy(msg_buf + msg_pos, &file_t, SIZE_FILE_T);

  // send message
  return socket_write_conn(fd, msg_buf, SIZE_OP_T + SIZE_LEN + SIZE_FILE_T);
}

/**********************************************************************
 * send_file_write_request
 *
 * purpose: sends file write request to Traffic Manager
 * input: fd - file descriptor to use
 *        file - file to read
 *        text - new text to write to specified file
 *        size - length of the text
 *        ver  - version of the file to be written
 * output: TS_ERR_xx
 * notes: format - FILE_WRITE <msg_len> <file_type> <file_ver> <file_size> <text>
 **********************************************************************/
TSError
send_file_write_request(int fd, TSFileNameT file, int ver, int size, char *text)
{
  char *msg_buf;
  int msg_pos = 0, total_len;
  int32_t msg_len, f_size;        //marshalled values
  int16_t op, file_t, f_ver;
  TSError err;

  if (!text)
    return TS_ERR_PARAMS;

  msg_len = SIZE_FILE_T + SIZE_VER + SIZE_LEN + size;
  total_len = SIZE_OP_T + SIZE_LEN + msg_len;
  msg_buf = (char *)ats_malloc(sizeof(char) * total_len);

  // fill in op type
  op = (int16_t) FILE_WRITE;
  memcpy(msg_buf + msg_pos, &op, SIZE_OP_T);
  msg_pos += SIZE_OP_T;

  // fill in msg length
  memcpy(msg_buf + msg_pos, &msg_len, SIZE_LEN);
  msg_pos += SIZE_LEN;

  // fill in file type
  file_t = (int16_t) file;
  memcpy(msg_buf + msg_pos, &file_t, SIZE_FILE_T);
  msg_pos += SIZE_FILE_T;

  // fill in file version
  f_ver = (int16_t) ver;
  memcpy(msg_buf + msg_pos, &f_ver, SIZE_VER);
  msg_pos += SIZE_VER;

  // fill in file size
  f_size = (int32_t) size;        //typecase to be safe
  memcpy(msg_buf + msg_pos, &f_size, SIZE_LEN);
  msg_pos += SIZE_LEN;

  // fill in text of file
  memcpy(msg_buf + msg_pos, text, size);

  // send message
  err = socket_write_conn(fd, msg_buf, total_len);
  ats_free(msg_buf);
  return err;
}

static TSError
send_record_get_x_request(OpType optype, int fd, const char *rec_name)
{
  char *msg_buf;
  int msg_pos = 0, total_len;
  int16_t op = (int16_t)optype;
  int32_t msg_len;
  TSError err;

  ink_assert(op == RECORD_GET || op == RECORD_MATCH_GET);

  if (!rec_name) {
    return TS_ERR_PARAMS;
  }

  total_len = SIZE_OP_T + SIZE_LEN + strlen(rec_name);
  msg_buf = (char *)ats_malloc(sizeof(char) * total_len);

  // fill in op type
  memcpy(msg_buf + msg_pos, (void *) &op, SIZE_OP_T);
  msg_pos += SIZE_OP_T;

  // fill in msg length
  msg_len = (int32_t) strlen(rec_name);
  memcpy(msg_buf + msg_pos, (void *) &msg_len, SIZE_LEN);
  msg_pos += SIZE_LEN;

  // fill in record name
  memcpy(msg_buf + msg_pos, rec_name, strlen(rec_name));

  // send message
  err = socket_write_conn(fd, msg_buf, total_len);
  ats_free(msg_buf);
  return err;
}

/**********************************************************************
 * send_record_get_request
 *
 * purpose: sends request to get record value from Traffic Manager
 * input: fd       - file descriptor to use
 *        rec_name - name of record to retrieve value for
 * output: TS_ERR_xx
 * format: RECORD_GET <msg_len> <rec_name>
 **********************************************************************/
TSError
send_record_get_request(int fd, const char *rec_name)
{
  return send_record_get_x_request(RECORD_GET, fd, rec_name);
}

/**********************************************************************
 * send_record_match_request
 *
 * purpose: sends request to get a list of matching record values from Traffic Manager
 * input: fd       - file descriptor to use
 *        rec_name - regex to match against record names
 * output: TS_ERR_xx
 * format: sequence of RECORD_GET <msg_len> <rec_name>
 **********************************************************************/
TSError
send_record_match_request(int fd, const char *rec_regex)
{
  return send_record_get_x_request(RECORD_MATCH_GET, fd, rec_regex);
}

/*------ control functions -------------------------------------------*/
/**********************************************************************
 * send_proxy_state_get_request
 *
 * purpose: sends request to get the proxy state (on/off)
 * input: fd       - file descriptor to use
 * output: TS_ERR_xx
 * note: format: PROXY_STATE_GET 0(=msg_len)
 **********************************************************************/
TSError
send_proxy_state_get_request(int fd)
{
  TSError err;

  err = send_request(fd, PROXY_STATE_GET);
  return err;
}

/**********************************************************************
 * send_proxy_state_set_request
 *
 * purpose: sends request to set the proxy state (on/off)
 * input: fd    - file descriptor to use
 *        state - TS_PROXY_ON, TS_PROXY_OFF
 * output: TS_ERR_xx
 * note: format: PROXY_STATE_SET  <msg_len> <TSProxyStateT> <TSCacheClearT>
 **********************************************************************/
TSError
send_proxy_state_set_request(int fd, TSProxyStateT state, TSCacheClearT clear)
{
  char msg_buf[SIZE_OP_T + SIZE_LEN + SIZE_PROXY_T + SIZE_TS_ARG_T];
  int16_t op, state_t, cache_t;
  int32_t msg_len;

  // fill in op type
  op = (int16_t) PROXY_STATE_SET;
  memcpy(msg_buf, (void *) &op, SIZE_OP_T);

  // fill in msg_len
  msg_len = (int32_t) (SIZE_PROXY_T + SIZE_TS_ARG_T);
  memcpy(msg_buf + SIZE_OP_T, (void *) &msg_len, SIZE_LEN);

  // fill in proxy state
  state_t = (int16_t) state;
  memcpy(msg_buf + SIZE_OP_T + SIZE_LEN, (void *) &state_t, SIZE_PROXY_T);

  // fill in cache clearing option
  cache_t = (int16_t) clear;
  memcpy(msg_buf + SIZE_OP_T + SIZE_LEN + SIZE_PROXY_T, (void *) &cache_t, SIZE_TS_ARG_T);

  // send message
  return socket_write_conn(fd, msg_buf, SIZE_OP_T + SIZE_LEN + SIZE_PROXY_T + SIZE_TS_ARG_T);
}


/*------ events -------------------------------------------------------*/

/**********************************************************************
 * send_register_all_callbacks
 *
 * purpose: determines all events which have at least one callback registered
 *          and sends message to notify TM that this client has a callback
 *          registered for each event
 * input: None
 * output: return TS_ERR_OKAY only if ALL events sent okay
 * notes: could create a function which just sends a list of all the events to
 * reregister; but actually just reuse the function
 * send_request_name(EVENT_REG_CALLBACK) and call it for each event
 * 1) get list of all events with callbacks
 * 2) for each event, call send_request_name
 **********************************************************************/
TSError
send_register_all_callbacks(int fd, CallbackTable * cb_table)
{
  LLQ *events_with_cb;
  TSError err, send_err = TS_ERR_FAIL;
  bool no_errors = true;        // set to false if one send is not okay

  events_with_cb = get_events_with_callbacks(cb_table);
  // need to check that the list has all the events registered
  if (!events_with_cb) {        // all events have registered callback
    err = send_request_name(fd, EVENT_REG_CALLBACK, NULL);
    if (err != TS_ERR_OKAY)
      return err;
  } else {
    char *event_name;
    int event_id;
    int num_events = queue_len(events_with_cb);
    // iterate through the LLQ and send request for each event
    for (int i = 0; i < num_events; i++) {
      event_id = *(int *) dequeue(events_with_cb);
      event_name = (char *) get_event_name(event_id);
      if (event_name) {
        err = send_request_name(fd, EVENT_REG_CALLBACK, event_name);
        ats_free(event_name);      // free memory
        if (err != TS_ERR_OKAY) {
          send_err = err;       // save the type of send error
          no_errors = false;
        }
      }
      // REMEMBER: WON"T GET A REPLY from TM side!
    }
  }

  if (events_with_cb)
    delete_queue(events_with_cb);

  if (no_errors)
    return TS_ERR_OKAY;
  else
    return send_err;
}

/**********************************************************************
 * send_unregister_all_callbacks
 *
 * purpose: determines all events which have no callback registered
 *          and sends message to notify TM that this client has no
 *          callbacks registered for that event
 * input: None
 * output: TS_ERR_OKAY only if all send requests are okay
 * notes: could create a function which just sends a list of all the events to
 * unregister; but actually just reuse the function
 * send_request_name(EVENT_UNREG_CALLBACK) and call it for each event
 **********************************************************************/
TSError
send_unregister_all_callbacks(int fd, CallbackTable * cb_table)
{
  char *event_name;
  int event_id;
  LLQ *events_with_cb;          // list of events with at least one callback
  int reg_callback[NUM_EVENTS];
  TSError err, send_err = TS_ERR_FAIL;
  bool no_errors = true;        // set to false if at least one send fails

  // init array so that all events don't have any callbacks
  for (int i = 0; i < NUM_EVENTS; i++) {
    reg_callback[i] = 0;
  }

  events_with_cb = get_events_with_callbacks(cb_table);
  if (!events_with_cb) {        // all events have a registered callback
    return TS_ERR_OKAY;
  } else {
    int num_events = queue_len(events_with_cb);
    // iterate through the LLQ and mark events that have a callback
    for (int i = 0; i < num_events; i++) {
      event_id = *(int *) dequeue(events_with_cb);
      reg_callback[event_id] = 1;       // mark the event as having a callback
    }
    delete_queue(events_with_cb);
  }

  // send message to TM to mark unregister
  for (int k = 0; k < NUM_EVENTS; k++) {
    if (reg_callback[k] == 0) { // event has no registered callbacks
      event_name = get_event_name(k);
      err = send_request_name(fd, EVENT_UNREG_CALLBACK, event_name);
      ats_free(event_name);
      if (err != TS_ERR_OKAY) {
        send_err = err;         //save the type of the sending error
        no_errors = false;
      }
      // REMEMBER: WON"T GET A REPLY!
      // only the event_poll_thread_main does any reading of the event_socket;
      // so DO NOT parse reply b/c a reply won't be sent
    }
  }

  if (no_errors)
    return TS_ERR_OKAY;
  else
    return send_err;
}

/**********************************************************************
 * send_diags_msg
 *
 * purpose: sends the diag msg across along with they diag msg type
 * input: mode - type of diags msg
 *        msg  - the diags msg
 * output: TS_ERR_xx
 * note: format: <OpType> <msg_len> <TSDiagsT> <diag_msg_len> <diag_msg>
 **********************************************************************/
TSError
send_diags_msg(int fd, TSDiagsT mode, const char *diag_msg)
{
  char *msg_buf;
  int16_t op_t, diag_t;
  int32_t msg_len, diag_msg_len;
  int total_len;
  TSError err;

  if (!diag_msg)
    return TS_ERR_PARAMS;

  diag_msg_len = (int32_t) strlen(diag_msg);
  msg_len = SIZE_DIAGS_T + SIZE_LEN + diag_msg_len;
  total_len = SIZE_OP_T + SIZE_LEN + msg_len;
  msg_buf = (char *)ats_malloc(sizeof(char) * total_len);

  // fill in op type
  op_t = (int16_t) DIAGS;
  memcpy(msg_buf, (void *) &op_t, SIZE_OP_T);

  // fill in entire msg len
  memcpy(msg_buf + SIZE_OP_T, (void *) &msg_len, SIZE_LEN);

  // fill in TSDiagsT
  diag_t = (int16_t) mode;
  memcpy(msg_buf + SIZE_OP_T + SIZE_LEN, (void *) &diag_t, SIZE_DIAGS_T);

  // fill in diags msg_len
  memcpy(msg_buf + SIZE_OP_T + SIZE_LEN + SIZE_DIAGS_T, (void *) &diag_msg_len, SIZE_LEN);

  // fill in diags msg
  memcpy(msg_buf + SIZE_OP_T + SIZE_LEN + SIZE_DIAGS_T + SIZE_LEN, diag_msg, diag_msg_len);

  // send message
  err = socket_write_conn(fd, msg_buf, total_len);
  ats_free(msg_buf);
  return err;
}


/**********************************************************************
 * UNMARSHAL REPLIES
 **********************************************************************/

/* Error handling implementation:
 * All the parsing functions which parse the reply returned from local side
 * also must read the TSERror return value sent from local side; this return
 * value is the same value that will be returned by the parsing function.
 * ALL PARSING FUNCTIONS MUST FIRST CHECK that the retval is TS_ERR_OKAY;
 * if it is not, then DON"T PARSE THE REST OF THE REPLY!!
 */

/* Reading replies:
 * The reading is done in while loop in the parse_xx_reply functions;
 * need to add a timeout so that the function is not left looping and
 * waiting if a msg isn't sent to the socket from local side (eg. TM died)
 */

/**********************************************************************
 * parse_reply
 *
 * purpose: parses a reply from traffic manager. return that error
 * input: fd
 * output: errors on error or fill up class with response &
 *         return TS_ERR_xx
 * notes: only returns an TSError
 **********************************************************************/
TSError
parse_reply(int fd)
{
  TSError ret;
  int16_t ret_val;

  // check to see if anything to read; wait for specified time = 1 sec
  if (socket_read_timeout(fd, MAX_TIME_WAIT, 0) <= 0) { // time expires before ready to read
    return TS_ERR_NET_TIMEOUT;
  }

  ret = socket_read_conn(fd, (uint8_t *)&ret_val, SIZE_ERR_T);
  if (ret != TS_ERR_OKAY) {
    return ret;
  }

  return (TSError) ret_val;
}

/**********************************************************************
 * parse_reply_list
 *
 * purpose: parses a TM reply to a request to get a list of string tokens
 * input: fd - socket to read
 *        list - will contain delimited string list of tokens
 * output: TS_ERR_xx
 * notes:
 * format: <TSError> <string_list_len> <delimited_string_list>
 **********************************************************************/
TSError
parse_reply_list(int fd, char **list)
{
  int16_t ret_val;
  int32_t list_size;
  TSError err_t;

  if (!list) {
    return TS_ERR_PARAMS;
  }

  // check to see if anything to read; wait for specified time
  if (socket_read_timeout(fd, MAX_TIME_WAIT, 0) <= 0) {
    return TS_ERR_NET_TIMEOUT;
  }

  // get the return value (TSError type)
  err_t = socket_read_conn(fd, (uint8_t *)&ret_val, SIZE_ERR_T);
  if (err_t != TS_ERR_OKAY) {
    return err_t;
  }

  // if !TS_ERR_OKAY, stop reading rest of msg
  err_t = (TSError) ret_val;
  if (err_t != TS_ERR_OKAY) {
    return err_t;
  }

  // now get size of string event list
  err_t = socket_read_conn(fd, (uint8_t *)&list_size, SIZE_LEN);
  if (err_t != TS_ERR_OKAY) {
    return err_t;
  }

  // get the delimited event list string
  *list = (char *)ats_malloc(sizeof(char) * (list_size + 1));
  err_t = socket_read_conn(fd, (uint8_t *)(*list), list_size);
  if (err_t != TS_ERR_OKAY) {
    ats_free(*list);
    *list = NULL;
    return err_t;
  }

  // add end of string to end of the record value
  ((char *) (*list))[list_size] = '\0';
  return err_t;
}


/**********************************************************************
 * parse_file_read_reply
 *
 * purpose: parses a file read reply from traffic manager.
 * input: fd
 *        ver -
 *        size - size of text
 *        text -
 * output: errors on error or fill up class with response &
 *         return TS_ERR_xx
 * notes: reply format = <TSError> <file_version> <file_size> <text>
 **********************************************************************/
TSError
parse_file_read_reply(int fd, int *ver, int *size, char **text)
{
  int32_t f_size;
  int16_t ret_val, f_ver;
  TSError err_t;

  if (!ver || !size || !text)
    return TS_ERR_PARAMS;

  // check to see if anything to read; wait for specified time
  if (socket_read_timeout(fd, MAX_TIME_WAIT, 0) <= 0) { // time expires before ready to read
    return TS_ERR_NET_TIMEOUT;
  }

  // get the error return value
  err_t = socket_read_conn(fd, (uint8_t *)&ret_val, SIZE_ERR_T);
  if (err_t != TS_ERR_OKAY) {
    return err_t;
  }

  // if !TS_ERR_OKAY, stop reading rest of msg
  err_t = (TSError) ret_val;
  if (err_t != TS_ERR_OKAY) {
    return err_t;
  }

  // now get file version
  err_t = socket_read_conn(fd, (uint8_t *)&f_ver, SIZE_VER);
  if (err_t != TS_ERR_OKAY) {
    return err_t;
  }

  *ver = (int) f_ver;

  // now get file size
  err_t = socket_read_conn(fd, (uint8_t *)&f_size, SIZE_LEN);
  if (err_t != TS_ERR_OKAY) {
    return err_t;
  }

  *size = (int) f_size;

  // check size before reading text
  if ((*size) <= 0) {
    *text = ats_strndup("", 1);                 // set to empty string
    return TS_ERR_OKAY;
  }

  // now we got the size, we can read everything into our msg * then parse it
  *text = (char *)ats_malloc(sizeof(char) * (f_size + 1));
  err_t = socket_read_conn(fd, (uint8_t *)(*text), f_size);
  if (err_t != TS_ERR_OKAY) {
    ats_free(*text);
    *text = NULL;
    return err_t;
  }

  (*text)[f_size] = '\0';     // end the string
  return TS_ERR_OKAY;
}

/**********************************************************************
 * parse_record_get_reply
 *
 * purpose: parses a record_get reply from traffic manager.
 * input: fd
 *        retval   -
 *        rec_type - the type of the record
 *        rec_value - the value of the record in string format
 * output: errors on error or fill up class with response &
 *         return SUCC
 * notes: reply format = <TSError> <val_size> <name_size> <rec_type> <record_value> <record_name>
 * Zero-length values and names are supported. If the size field is 0, the corresponding
 * value field is not transmitted.
 * It's the responsibility of the calling function to conver the rec_value
 * based on the rec_type!!
 **********************************************************************/
TSError
parse_record_get_reply(int fd, TSRecordT * rec_type, void **rec_val, char **rec_name)
{
  int16_t ret_val, rec_t;
  int32_t val_size, name_size;
  TSError err_t;

  if (!rec_type || !rec_val) {
    return TS_ERR_PARAMS;
  }

  *rec_name = NULL;
  *rec_val = NULL;

  // check to see if anything to read; wait for specified time
  if (socket_read_timeout(fd, MAX_TIME_WAIT, 0) <= 0) { //time expired before ready to read
    return TS_ERR_NET_TIMEOUT;
  }

  // get the return value (TSError type)
  err_t = socket_read_conn(fd, (uint8_t *)&ret_val, SIZE_ERR_T);
  if (err_t != TS_ERR_OKAY) {
    goto fail;
  }

  // if !TS_ERR_OKAY, stop reading rest of msg
  err_t = (TSError) ret_val;
  if (err_t != TS_ERR_OKAY) {
    goto fail;
  }

  // now get size of record_value
  err_t = socket_read_conn(fd, (uint8_t *)&val_size, SIZE_LEN);
  if (err_t != TS_ERR_OKAY) {
    goto fail;
  }

  // now get size of record name
  err_t = socket_read_conn(fd, (uint8_t *)&name_size, SIZE_LEN);
  if (err_t != TS_ERR_OKAY) {
    goto fail;
  }

  // get the record type
  err_t = socket_read_conn(fd, (uint8_t *)&rec_t, SIZE_REC_T);
  if (err_t != TS_ERR_OKAY) {
    goto fail;
  }

  *rec_type = (TSRecordT) rec_t;

  // get record value (if there is one)
  if (val_size) {
    if (*rec_type == TS_REC_STRING) {
      *rec_val = ats_malloc(sizeof(char) * (val_size + 1));
    } else {
      *rec_val = ats_malloc(sizeof(char) * (val_size));
    }

    err_t = socket_read_conn(fd, (uint8_t *)(*rec_val), val_size);
    if (err_t != TS_ERR_OKAY) {
      goto fail;
    }

    // add end of string to end of the record value
    if (*rec_type == TS_REC_STRING) {
      ((char *) (*rec_val))[val_size] = '\0';
    }
  }

  // get the record name (if there is one)
  if (name_size) {
    *rec_name = (char *)ats_malloc(sizeof(char) * (name_size + 1));
    err_t = socket_read_conn(fd, (uint8_t *)(*rec_name), name_size);
    if (err_t != TS_ERR_OKAY) {
      goto fail;
    }

    (*rec_name)[name_size] = '\0';
  }

  return TS_ERR_OKAY;

fail:
  ats_free(*rec_val);
  ats_free(*rec_name);
  *rec_val = NULL;
  *rec_name = NULL;
  return err_t;
}

/**********************************************************************
 * parse_record_set_reply
 *
 * purpose: parses a record_set reply from traffic manager.
 * input: fd
 *        action_need - will contain the type of action needed from the set
 * output: TS_ERR_xx
 * notes: reply format = <TSError> <val_size> <rec_type> <record_value>
 * It's the responsibility of the calling function to conver the rec_value
 * based on the rec_type!!
 **********************************************************************/
TSError
parse_record_set_reply(int fd, TSActionNeedT * action_need)
{
  int16_t ret_val, action_t;
  TSError err_t;

  if (!action_need)
    return TS_ERR_PARAMS;

  // check to see if anything to read; wait for specified time
  if (socket_read_timeout(fd, MAX_TIME_WAIT, 0) <= 0) {
    return TS_ERR_NET_TIMEOUT;
  }

  // get the return value (TSError type)
  err_t = socket_read_conn(fd, (uint8_t *)&ret_val, SIZE_ERR_T);
  if (err_t != TS_ERR_OKAY) {
    return err_t;
  }

  // if !TS_ERR_OKAY, stop reading rest of msg
  err_t = (TSError) ret_val;
  if (err_t != TS_ERR_OKAY) {
    return err_t;
  }

  // now get the action needed
  err_t = socket_read_conn(fd, (uint8_t *)&action_t, SIZE_ACTION_T);
  if (err_t != TS_ERR_OKAY) {
    return err_t;
  }

  *action_need = (TSActionNeedT) action_t;
  return err_t;
}


/**********************************************************************
 * parse_proxy_state_get_reply
 *
 * purpose: parses a TM reply to a PROXY_STATE_GET request
 * input: fd
 *        state - will contain the state of the proxy
 * output: TS_ERR_xx
 * notes: function is DIFFERENT becuase it has NO TSError at head of msg
 * format: <TSProxyStateT>
 **********************************************************************/
TSError
parse_proxy_state_get_reply(int fd, TSProxyStateT * state)
{
  TSError err_t;
  int16_t state_t;

  if (!state)
    return TS_ERR_PARAMS;

  // check to see if anything to read; wait for specified time
  if (socket_read_timeout(fd, MAX_TIME_WAIT, 0) <= 0) { // time expires before ready to read
    return TS_ERR_NET_TIMEOUT;
  }

  // now get proxy state
  err_t = socket_read_conn(fd, (uint8_t *)&state_t, SIZE_PROXY_T);
  if (err_t != TS_ERR_OKAY) {
    return err_t;
  }

  *state = (TSProxyStateT) state_t;
  return TS_ERR_OKAY;
}

/*------------- events ---------------------------------------------*/

/**********************************************************************
 * parse_event_active_reply
 *
 * purpose: parses a TM reply to a request to get status of an event
 * input: fd - socket to read
 *        is_active - set to true if event is active; false otherwise
 * output: TS_ERR_xx
 * notes:
 * format: reply format = <TSError> <bool>
 **********************************************************************/
TSError
parse_event_active_reply(int fd, bool * is_active)
{
  int16_t ret_val, active;
  TSError err_t;

  if (!is_active)
    return TS_ERR_PARAMS;

  // check to see if anything to read; wait for specified time
  if (socket_read_timeout(fd, MAX_TIME_WAIT, 0) <= 0) {
    return TS_ERR_NET_TIMEOUT;
  }

  // get the return value (TSError type)
  err_t = socket_read_conn(fd, (uint8_t *)&ret_val, SIZE_ERR_T);
  if (err_t != TS_ERR_OKAY) {
    return err_t;
  }

  // if !TS_ERR_OKAY, stop reading rest of msg
  err_t = (TSError) ret_val;
  if (err_t != TS_ERR_OKAY) {
    return err_t;
  }

  // now get the boolean
  err_t = socket_read_conn(fd, (uint8_t *)&active, SIZE_BOOL);
  if (err_t != TS_ERR_OKAY) {
    return err_t;
  }

  *is_active = (bool) active;
  return err_t;
}

/**********************************************************************
 * parse_event_notification
 *
 * purpose: parses the event notification message from TM when an event
 *          is signalled; stores the event info in the TSEvent
 * input: fd - socket to read
 *        event - where the event info from msg is stored
 * output:TS_ERR_OKAY, TS_ERR_NET_READ, TS_ERR_NET_EOF, TS_ERR_PARAMS
 * notes:
 * format: <OpType> <event_name_len> <event_name> <desc_len> <desc>
 **********************************************************************/
TSError
parse_event_notification(int fd, TSEvent * event)
{
  OpType msg_type;
  int16_t type_op;
  int32_t msg_len;
  char *event_name = NULL, *desc = NULL;
  TSError err_t;

  if (!event)
    return TS_ERR_PARAMS;

  // read the operation type; should be EVENT_NOTIFY
  err_t = socket_read_conn(fd, (uint8_t *)&type_op, SIZE_OP_T);
  if (err_t != TS_ERR_OKAY) {
    return err_t;
  }

  // got the message type; the msg_type should be EVENT_NOTIFY
  msg_type = (OpType) type_op;
  if (msg_type != EVENT_NOTIFY) {
    return TS_ERR_FAIL;
  }

  // read in event name length
  err_t = socket_read_conn(fd, (uint8_t *)&msg_len, SIZE_LEN);
  if (err_t != TS_ERR_OKAY) {
    return err_t;
  }

  // read the event name
  event_name = (char *)ats_malloc(sizeof(char) * (msg_len + 1));
  err_t = socket_read_conn(fd, (uint8_t *)event_name, msg_len);
  if (err_t != TS_ERR_OKAY) {
    goto fail;
  }

  event_name[msg_len] = '\0';   // end the string

  // read in event description length
  err_t = socket_read_conn(fd, (uint8_t *)&msg_len, SIZE_LEN);
  if (err_t != TS_ERR_OKAY) {
    goto fail;
  }

  // read the event description
  desc = (char *)ats_malloc(sizeof(char) * (msg_len + 1));
  err_t = socket_read_conn(fd, (uint8_t *)desc, msg_len);
  if (err_t != TS_ERR_OKAY) {
    goto fail;
  }

  desc[msg_len] = '\0';         // end the string

  // fill in event info
  event->name = event_name;
  event->id = (int) get_event_id(event_name);
  event->description = desc;

  return TS_ERR_OKAY;

fail:
  ats_free(event_name);
  ats_free(desc);
  return err_t;
}
