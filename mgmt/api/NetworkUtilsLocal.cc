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

/***************************************************************************
 * NetworkUtilsLocal.cc
 *
 * contains implementation of local networking utility functions, such as
 * unmarshalling requests from a remote client and marshalling replies
 *
 *
 ***************************************************************************/

#include "ink_platform.h"
#include "ink_sock.h"
#include "Diags.h"
#include "MgmtUtils.h"
#include "CoreAPIShared.h"
#include "NetworkUtilsLocal.h"

#ifndef MAX_BUF_SIZE
#define MAX_BUF_SIZE 4096
#endif

/**************************************************************************
 * socket_flush
 *
 * flushes the socket by reading the entire message out of the socket
 * and then gets rid of the msg
 **************************************************************************/
TSError
socket_flush(struct SocketInfo sock_info)
{
  int ret, byte_read = 0;
  char buf[MAX_BUF_SIZE];

  // check to see if anything to read; wait only for specified time
  if (socket_read_timeout(sock_info.fd, MAX_TIME_WAIT, 0) <= 0) {
    return TS_ERR_NET_TIMEOUT;
  }
  // read entire message
  while (byte_read < MAX_BUF_SIZE) {

    ret = socket_read(sock_info, buf + byte_read, MAX_BUF_SIZE - byte_read);

    if (ret < 0) {
      if (errno == EAGAIN)
        continue;

      Debug("ts_main", "[socket_read_n] socket read for version byte failed.\n");
      mgmt_elog(0, "[socket_flush] (TS_ERR_NET_READ) %s\n", strerror(errno));
      return TS_ERR_NET_READ;
    }

    if (ret == 0) {
      Debug("ts_main", "[socket_read_n] returned 0 on reading: %s.\n", strerror(errno));
      return TS_ERR_NET_EOF;
    }
    // we are all good here
    byte_read += ret;
  }

  mgmt_elog(0, "[socket_flush] uh oh! didn't finish flushing socket!\n");
  return TS_ERR_FAIL;
}

/**************************************************************************
 * socket_read_n
 *
 * purpose: guarantees reading of n bytes or return error.
 * input:   socket info struct, buffer to read into and number of bytes to read
 * output:  number of bytes read
 * note:    socket_read is implemented in WebUtils.cc
 *************************************************************************/
TSError
socket_read_n(struct SocketInfo sock_info, char *buf, int bytes)
{
  int ret, byte_read = 0;

  // check to see if anything to read; wait for specified time
  if (socket_read_timeout(sock_info.fd, MAX_TIME_WAIT, 0) <= 0) {
    return TS_ERR_NET_TIMEOUT;
  }
  // read until we fulfill the number
  while (byte_read < bytes) {
    ret = socket_read(sock_info, buf + byte_read, bytes - byte_read);

    // error!
    if (ret < 0) {
      if (errno == EAGAIN)
        continue;

      Debug("ts_main", "[socket_read_n] socket read for version byte failed.\n");
      mgmt_elog(0, "[socket_read_n] (TS_ERR_NET_READ) %s\n", strerror(errno));
      return TS_ERR_NET_READ;
    }

    if (ret == 0) {
      Debug("ts_main", "[socket_read_n] returned 0 on reading: %s.\n", strerror(errno));
      return TS_ERR_NET_EOF;
    }
    // we are all good here
    byte_read += ret;
  }

  return TS_ERR_OKAY;
}

/**************************************************************************
 * socket_write_n
 *
 * purpose: guarantees writing of n bytes or return error
 * input:   socket info struct, buffer to write from & number of bytes to write
 * output:  TS_ERR_xx (depends on num bytes written)
 * note:    socket_read is implemented in WebUtils.cc
 *************************************************************************/
TSError
socket_write_n(struct SocketInfo sock_info, const char *buf, int bytes)
{
  int ret, byte_wrote = 0;

  // makes sure the socket descriptor is writable
  if (socket_write_timeout(sock_info.fd, MAX_TIME_WAIT, 0) <= 0) {
    return TS_ERR_NET_TIMEOUT;
  }
  // read until we fulfill the number
  while (byte_wrote < bytes) {
    ret = socket_write(sock_info, buf + byte_wrote, bytes - byte_wrote);

    if (ret < 0) {
      Debug("ts_main", "[socket_write_n] return error %s \n", strerror(errno));
      mgmt_elog(0, "[socket_write_n] %s\n", strerror(errno));
      if (errno == EAGAIN)
        continue;

      return TS_ERR_NET_WRITE;
    }

    if (ret == 0) {
      mgmt_elog(0, "[socket_write_n] %s\n", strerror(errno));
      return TS_ERR_NET_EOF;
    }
    // we are all good here
    byte_wrote += ret;
  }

  return TS_ERR_OKAY;
}


/**********************************************************************
 * preprocess_msg
 *
 * purpose: reads in all the message; parses the message into header info
 *          (OpType + msg_len) and the request portion (used by the handle_xx fns)
 * input: sock_info - socket msg is read from
 *        op_t      - the operation type specified in the msg
 *        msg       - the data from the network message (no OpType or msg_len)
 * output: TS_ERR_xx ( if TS_ERR_OKAY, then parameters set successfully)
 * notes: Since preprocess_msg already removes the OpType and msg_len, this part o
 *        the message is not dealt with by the other parsing functions
 **********************************************************************/
TSError
preprocess_msg(struct SocketInfo sock_info, OpType * op_t, char **req)
{
  TSError ret;
  int req_len;
  int16_t op;

  // read operation type
  ret = socket_read_n(sock_info, (char *) &op, SIZE_OP_T);
  if (ret != TS_ERR_OKAY) {
    Debug("ts_main", "[preprocess_msg] ERROR %d reading op type\n", ret);
    goto Lerror;
  }

  Debug("ts_main", "[preprocess_msg] operation = %d", op);
  *op_t = (OpType) op;          // convert to proper format

  // check if invalid op type
  if ((int) op > UNDEFINED_OP) {
    mgmt_elog(0, "[preprocess_msg] ERROR: %d is invalid op type\n", op);

    // need to flush the invalid message from the socket
    if ((ret = socket_flush(sock_info)) != TS_ERR_NET_EOF)
      mgmt_log("[preprocess_msg] unsuccessful socket flushing\n");
    else
      mgmt_log("[preprocess_msg] successfully flushed the socket\n");

    goto Lerror;
  }
  // now read the request msg size
  ret = socket_read_n(sock_info, (char *) &req_len, SIZE_LEN);
  if (ret != TS_ERR_OKAY) {
    mgmt_elog(0, "[preprocess_msg] ERROR %d reading msg size\n", ret);
    Debug("ts_main", "[preprocess_msg] ERROR %d reading msg size\n", ret);
    goto Lerror;
  }

  Debug("ts_main", "[preprocess_msg] length = %d\n", req_len);

  // use req msg length to fetch the rest of the message
  // first check that there is a "rest of the msg", some msgs just
  // have the op specified
  if (req_len == 0) {
    *req = NULL;
    Debug("ts_main", "[preprocess_msg] request message = NULL\n");
  } else {
    *req = (char *)ats_malloc(sizeof(char) * (req_len + 1));
    ret = socket_read_n(sock_info, *req, req_len);
    if (ret != TS_ERR_OKAY) {
      ats_free(*req);
      goto Lerror;
    }
    // add end of string to end of msg
    (*req)[req_len] = '\0';
    Debug("ts_main", "[preprocess_msg] request message = %s\n", *req);
  }

  return TS_ERR_OKAY;

Lerror:
  return ret;
}


/**********************************************************************
 * Unmarshal Requests
 **********************************************************************/

/**********************************************************************
 * parse_file_read_request
 *
 * purpose: parses a file read request from a remote API client
 * input: req - data that needs to be parsed
 *        file - the file type sent in the request
 * output: TS_ERR_xx
 * notes: request format = <TSFileNameT>
 **********************************************************************/
TSError
parse_file_read_request(char *req, TSFileNameT * file)
{
  int16_t file_t;

  if (!req || !file)
    return TS_ERR_PARAMS;

  // get file type - copy first 2 bytes of request
  memcpy(&file_t, req, SIZE_FILE_T);
  *file = (TSFileNameT) file_t;

  return TS_ERR_OKAY;
}

/**********************************************************************
 * parse_file_write_request
 *
 * purpose: parses a file write request from a remote API client
 * input: socket info
 *        file - the file type to write that was sent in the request
 *        text - the text that needs to be written
 *        size - length of the text
 *        ver  - version of the file that is to be written
 * output: TS_ERR_xx
 * notes: request format = <TSFileNameT> <version> <size> <text>
 **********************************************************************/
TSError
parse_file_write_request(char *req, TSFileNameT * file, int *ver, int *size, char **text)
{
  int16_t file_t, f_ver;
  int32_t f_size;

  // check input is non-NULL
  if (!req || !file || !ver || !size || !text)
    return TS_ERR_PARAMS;

  // get file type - copy first 2 bytes of request
  memcpy(&file_t, req, SIZE_FILE_T);
  *file = (TSFileNameT) file_t;

  // get file version - copy next 2 bytes
  memcpy(&f_ver, req + SIZE_FILE_T, SIZE_VER);
  *ver = (int) f_ver;

  // get file size - copy next 4 bytes
  memcpy(&f_size, req + SIZE_FILE_T + SIZE_VER, SIZE_LEN);
  *size = (int) f_size;

  // get file text
  *text = (char *)ats_malloc(sizeof(char) * (f_size + 1));
  memcpy(*text, req + SIZE_FILE_T + SIZE_VER + SIZE_LEN, f_size);
  (*text)[f_size] = '\0';       // end buffer

  return TS_ERR_OKAY;
}

/**********************************************************************
 * parse_request_name_value
 *
 * purpose: parses a request w/ 2 args from a remote API client
 * input: req - request info from requestor
 *        name - first arg
 *        val  - second arg
 * output: TS_ERR_xx
 * notes: format= <name_len> <val_len> <name> <val>
 **********************************************************************/
TSError
parse_request_name_value(char *req, char **name_1, char **val_1)
{
  int32_t name_len, val_len;
  char *name, *val;

  if (!req || !name_1 || !val_1)
    return TS_ERR_PARAMS;

  // get record name length
  memcpy(&name_len, req, SIZE_LEN);

  // get record value length
  memcpy(&val_len, req + SIZE_LEN, SIZE_LEN);

  // get record name
  name = (char *)ats_malloc(sizeof(char) * (name_len + 1));
  memcpy(name, req + SIZE_LEN + SIZE_LEN, name_len);
  name[name_len] = '\0';        // end string
  *name_1 = name;

  // get record value - can be a MgmtInt, MgmtCounter ...
  val = (char *)ats_malloc(sizeof(char) * (val_len + 1));
  memcpy(val, req + SIZE_LEN + SIZE_LEN + name_len, val_len);
  val[val_len] = '\0';          // end string
  *val_1 = val;

  return TS_ERR_OKAY;
}


/**********************************************************************
 * parse_diags_request
 *
 * purpose: parses a diags request
 * input: diag_msg - the diag msg to be outputted
 *        mode     - indicates what type of diag message
 * output: TS_ERR_xx
 * notes: request format = <TSDiagsT> <diag_msg_len> <diag_msg>
 **********************************************************************/
TSError
parse_diags_request(char *req, TSDiagsT * mode, char **diag_msg)
{
  int16_t diag_t;
  int32_t msg_len;

  // check input is non-NULL
  if (!req || !mode || !diag_msg)
    return TS_ERR_PARAMS;

  // get diags type - copy first 2 bytes of request
  memcpy(&diag_t, req, SIZE_DIAGS_T);
  *mode = (TSDiagsT) diag_t;

  // get msg size - copy next 4 bytes
  memcpy(&msg_len, req + SIZE_DIAGS_T, SIZE_LEN);

  // get msg
  *diag_msg = (char *)ats_malloc(sizeof(char) * (msg_len + 1));
  memcpy(*diag_msg, req + SIZE_DIAGS_T + SIZE_LEN, msg_len);
  (*diag_msg)[msg_len] = '\0';  // end buffer

  return TS_ERR_OKAY;
}

/**********************************************************************
 * parse_proxy_state_request
 *
 * purpose: parses a request to set the proxy state
 * input: diag_msg - the diag msg to be outputted
 *        mode     - indicates what type of diag message
 * output: TS_ERR_xx
 * notes: request format = <TSProxyStateT> <TSCacheClearT>
 **********************************************************************/
TSError
parse_proxy_state_request(char *req, TSProxyStateT * state, TSCacheClearT * clear)
{
  int16_t state_t, cache_t;

  // check input is non-NULL
  if (!req || !state || !clear)
    return TS_ERR_PARAMS;

  // get proxy on/off
  memcpy(&state_t, req, SIZE_PROXY_T);
  *state = (TSProxyStateT) state_t;

  // get cahce-clearing type
  memcpy(&cache_t, req + SIZE_PROXY_T, SIZE_TS_ARG_T);
  *clear = (TSCacheClearT) cache_t;

  return TS_ERR_OKAY;
}

/**********************************************************************
 * Marshal Replies
 **********************************************************************/
/* NOTE: if the send function "return"s before writing to the socket
  then that means that an error occurred, and so the calling function
  must send_reply with the error that occurred. */

/**********************************************************************
 * send_reply
 *
 * purpose: sends a simple TS_ERR_* reply to the request made
 * input: return value - could be extended to support more complex
 *        error codes but for now use only TS_ERR_FAIL, TS_ERR_OKAY
 *        int fd - socket fd to use.
 * output: TS_ERR_*
 * notes: this function does not need to go through the internal structure
 *        so no cleaning up is done.
 **********************************************************************/
TSError
send_reply(struct SocketInfo sock_info, TSError retval)
{
  TSError ret;
  char msg[SIZE_ERR_T];
  int16_t ret_val;

  // write the return value
  ret_val = (int16_t) retval;
  memcpy(msg, (void *) &ret_val, SIZE_ERR_T);

  // now push it to the socket
  ret = socket_write_n(sock_info, msg, SIZE_ERR_T);

  return ret;
}

/**********************************************************************
 * send_reply_list
 *
 * purpose: sends the reply in response to a request to get list of string
 *          tokens (delimited by REMOTE_DELIM_STR)
 * input: sock_info -
 *        retval - TSError return type for the CoreAPI call
 *        list - string delimited list of string tokens
 * output: TS_ERR_*
 * notes:
 * format: <TSError> <string_list_len> <delimited_string_list>
 **********************************************************************/
TSError
send_reply_list(struct SocketInfo sock_info, TSError retval, char *list)
{
  TSError ret;
  int msg_pos = 0, total_len;
  char *msg;
  int16_t ret_val;
  int32_t list_size;              // to be safe, typecast

  if (!list) {
    return TS_ERR_PARAMS;
  }

  total_len = SIZE_ERR_T + SIZE_LEN + strlen(list);
  msg = (char *)ats_malloc(sizeof(char) * total_len);

  // write the return value
  ret_val = (int16_t) retval;
  memcpy(msg, (void *) &ret_val, SIZE_ERR_T);
  msg_pos += SIZE_ERR_T;

  // write the length of the string list
  list_size = (int32_t) strlen(list);
  memcpy(msg + msg_pos, (void *) &list_size, SIZE_LEN);
  msg_pos += SIZE_LEN;

  // write the event string list
  memcpy(msg + msg_pos, list, list_size);

  // now push it to the socket
  ret = socket_write_n(sock_info, msg, total_len);
  ats_free(msg);

  return ret;
}


/**********************************************************************
 * send_record_get_reply
 *
 * purpose: sends reply to the record_get request made
 * input: retval   - result of the record get request
 *        int fd   - socket fd to use.
 *        val      - the value of the record requested
 *        val_size - num bytes the value occupies
 *        rec_type - the type of the record value requested
 * output: TS_ERR_*
 * notes: this function does not need to go through the internal structure
 *        so no cleaning up is done.
 *        format = <TSError> <rec_val_len> <rec_type> <rec_val>
 **********************************************************************/
TSError
send_record_get_reply(struct SocketInfo sock_info, TSError retval, void *val, int val_size, TSRecordT rec_type)
{
  TSError ret;
  int msg_pos = 0, total_len;
  char *msg;
  int16_t record_t, ret_val;
  int32_t v_size;                 // to be safe, typecast

  if (!val) {
    return TS_ERR_PARAMS;
  }

  total_len = SIZE_ERR_T + SIZE_LEN + SIZE_REC_T + val_size;
  msg = (char *)ats_malloc(sizeof(char) * total_len);

  // write the return value
  ret_val = (int16_t) retval;
  memcpy(msg, (void *) &ret_val, SIZE_ERR_T);
  msg_pos += SIZE_ERR_T;

  // write the size of the record value
  v_size = (int32_t) val_size;
  memcpy(msg + msg_pos, (void *) &v_size, SIZE_LEN);
  msg_pos += SIZE_LEN;

  // write the record type
  record_t = (int16_t) rec_type;
  memcpy(msg + msg_pos, (void *) &record_t, SIZE_REC_T);
  msg_pos += SIZE_REC_T;

  // write the record value
  memcpy(msg + msg_pos, val, val_size);

  // now push it to the socket
  ret = socket_write_n(sock_info, msg, total_len);
  ats_free(msg);

  return ret;
}

/**********************************************************************
 * send_record_set_reply
 *
 * purpose: sends reply to the record_set request made
 * input:
 * output: TS_ERR_*
 * notes: this function does not need to go through the internal structure
 *        so no cleaning up is done.
 *        format =
 **********************************************************************/
TSError
send_record_set_reply(struct SocketInfo sock_info, TSError retval, TSActionNeedT action_need)
{
  TSError ret;
  int total_len;
  char *msg;
  int16_t action_t, ret_val;

  total_len = SIZE_ERR_T + SIZE_ACTION_T;
  msg = (char *)ats_malloc(sizeof(char) * total_len);

  // write the return value
  ret_val = (int16_t) retval;
  memcpy(msg, (void *) &ret_val, SIZE_ERR_T);

  // write the action needed
  action_t = (int16_t) action_need;
  memcpy(msg + SIZE_ERR_T, (void *) &action_t, SIZE_ACTION_T);

  // now push it to the socket
  ret = socket_write_n(sock_info, msg, total_len);
  ats_free(msg);

  return ret;
}


/**********************************************************************
 * send_file_read_reply
 *
 * purpose: sends the reply in response to a file read request
 * input: return value - could be extended to support more complex
 *        error codes but for now use only TS_ERR_FAIL, TS_ERR_OKAY
 *        int fd - socket fd to use.
 * output: TS_ERR_*
 * notes: this function does not need to go through the internal structure
 *        so no cleaning up is done.
 *        reply format = <TSError> <file_ver> <file_size> <file_text>
 **********************************************************************/
TSError
send_file_read_reply(struct SocketInfo sock_info, TSError retval, int ver, int size, char *text)
{
  TSError ret;
  int msg_pos = 0, msg_len;
  char *msg;
  int16_t ret_val, f_ver;
  int32_t f_size;                 // to be safe

  if (!text)
    return TS_ERR_PARAMS;

  // allocate space for buffer
  msg_len = SIZE_ERR_T + SIZE_VER + SIZE_LEN + size;
  msg = (char *)ats_malloc(sizeof(char) * msg_len);

  // write the return value
  ret_val = (int16_t) retval;
  memcpy(msg, (void *) &ret_val, SIZE_ERR_T);
  msg_pos += SIZE_ERR_T;

  // write file version
  f_ver = (int16_t) ver;
  memcpy(msg + msg_pos, (void *) &f_ver, SIZE_VER);
  msg_pos += SIZE_VER;

  // write file size
  f_size = (int32_t) size;
  memcpy(msg + msg_pos, (void *) &f_size, SIZE_LEN);
  msg_pos += SIZE_LEN;

  // write the file text
  memcpy(msg + msg_pos, text, size);

  // now push it to the socket
  ret = socket_write_n(sock_info, msg, msg_len);
  ats_free(msg);

  return ret;
}


/**********************************************************************
 * send_proxy_state_get_reply
 *
 * purpose: sends the reply in response to a request to get state of proxy
 * input:
 *        int fd - socket fd to use.
 * output: TS_ERR_*
 * notes: this function DOES NOT HAVE IT"S OWN TSError TO SEND!!!!
 *        reply format = <TSProxyStateT>
 **********************************************************************/
TSError
send_proxy_state_get_reply(struct SocketInfo sock_info, TSProxyStateT state)
{
  TSError ret;
  char msg[SIZE_PROXY_T];
  int16_t state_t;

  // write the state
  state_t = (int16_t) state;
  memcpy(msg, (void *) &state_t, SIZE_PROXY_T);

  // now push it to the socket
  ret = socket_write_n(sock_info, msg, SIZE_PROXY_T);

  return ret;
}


/**********************************************************************
 * send_event_active_reply
 *
 * purpose: sends the reply in response to a request check if event is active
 * input: sock_info -
 *        retval - TSError return type for the EventIsActive core call
 *        active - is the requested event active or not?
 * output: TS_ERR_*
 * notes:
 * format: <TSError> <bool>
 **********************************************************************/
TSError
send_event_active_reply(struct SocketInfo sock_info, TSError retval, bool active)
{
  TSError ret;
  int total_len;
  char *msg;
  int16_t is_active, ret_val;

  total_len = SIZE_ERR_T + SIZE_BOOL;
  msg = (char *)ats_malloc(sizeof(char) * total_len);

  // write the return value
  ret_val = (int16_t) retval;
  memcpy(msg, (void *) &ret_val, SIZE_ERR_T);

  // write the boolean active state
  is_active = (int16_t) active;
  memcpy(msg + SIZE_ERR_T, (void *) &is_active, SIZE_BOOL);

  // now push it to the socket
  ret = socket_write_n(sock_info, msg, total_len);
  ats_free(msg);

  return ret;
}

/**********************************************************************
 * send_event_notification
 *
 * purpose: sends to the client a msg indicating that a certain event
 *          has occurred (this msg will be received by the event_poll_thread)
 * input: fd - file descriptor to use for writing
 *        event - the event that was signalled on TM side
 * output: TS_ERR_xx
 * note: format: <OpType> <event_name_len> <event_name> <desc_len> <desc>
 **********************************************************************/
TSError
send_event_notification(struct SocketInfo sock_info, TSEvent * event)
{
  TSError ret;
  int total_len, name_len, desc_len;
  char *msg;
  int16_t op_t;
  int32_t len;

  if (!event || !event->name || !event->description)
    return TS_ERR_PARAMS;

  name_len = strlen(event->name);
  desc_len = strlen(event->description);
  total_len = SIZE_OP_T + (SIZE_LEN * 2) + name_len + desc_len;
  msg = (char *)ats_malloc(sizeof(char) * total_len);

  // write the operation
  op_t = (int16_t) EVENT_NOTIFY;
  memcpy(msg, (void *) &op_t, SIZE_OP_T);

  // write the size of the event name
  len = (int32_t) name_len;
  memcpy(msg + SIZE_OP_T, (void *) &len, SIZE_LEN);

  // write the event name
  memcpy(msg + SIZE_OP_T + SIZE_LEN, event->name, name_len);

  // write size of description
  len = (int32_t) desc_len;
  memcpy(msg + SIZE_OP_T + SIZE_LEN + name_len, (void *) &len, SIZE_LEN);

  // write the description
  memcpy(msg + SIZE_OP_T + SIZE_LEN + name_len + SIZE_LEN, event->description, desc_len);

  // now push it to the socket
  ret = socket_write_n(sock_info, msg, total_len);
  ats_free(msg);

  return ret;
}
