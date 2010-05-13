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

#include "test-protocol.h"

static INKAction actionp;

/*
 * Cleanup continuation data and destroy the continuation
 */
static void
clean_and_exit(INKCont contp)
{
  INKDebug(DEBUG_TAG, "Entered clean_and_exit");
  LOG_SET_FUNCTION_NAME("clean_and_exit");

  ConnData *conn_data = (ConnData *) INKContDataGet(contp);

  if (conn_data->pending_action && !INKActionDone(conn_data->pending_action)) {
    INKActionCancel(conn_data->pending_action);
  }

  if (conn_data->client_vconn) {
    INKVConnClose(conn_data->client_vconn);
  }

  if (conn_data->client_request) {
    INKfree(conn_data->client_request);
  }

  if (conn_data->client_response) {
    INKfree(conn_data->client_response);
  }

  if (conn_data->pattern) {
    INKfree(conn_data->pattern);
  }

  if (conn_data->client_request_buffer) {
    if (conn_data->client_request_buffer_reader) {
      INKIOBufferReaderFree(conn_data->client_request_buffer_reader);
    }
    INKIOBufferDestroy(conn_data->client_request_buffer);
  }

  if (conn_data->client_response_buffer) {
    if (conn_data->client_response_buffer_reader) {
      INKIOBufferReaderFree(conn_data->client_response_buffer_reader);
    }
    INKIOBufferDestroy(conn_data->client_response_buffer);
  }

  INKfree(conn_data);
  INKContDestroy(contp);
}

/*
 * Get the remote ip and port of the net vconnection
 */
static void
get_remote_ip(INKVConn client_vconn)
{
  INKDebug(DEBUG_TAG, "Entered get_remote_ip");
  LOG_SET_FUNCTION_NAME("get_remote_ip");

  unsigned int ip;
  int port;

  /* get the remote ip */
  if (INKNetVConnRemoteIPGet(client_vconn, &ip) == INK_SUCCESS) {
    if (INKTextLogObjectWrite(log, "The remote IP of the net vconnection is %d \n", ip) != INK_SUCCESS) {
      LOG_ERROR("INKTextLogObjectWrite");
    }
  } else {
    LOG_ERROR("INKNetVConnRemoteIPGet");
  }

  /* negative test for INKNetVConnRemoteIPGet */
#ifdef DEBUG
  if (INKNetVConnRemoteIPGet(NULL, &ip) != INK_ERROR) {
    LOG_ERROR_NEG("INKNetVConnRemoteIPGet(NULL,...)");
  }
#endif

  /* get the remote port */
  if (INKNetVConnRemotePortGet(client_vconn, &port) == INK_SUCCESS) {
    if (INKTextLogObjectWrite(log, "The remote port of the net vconnection is %d \n", port) != INK_SUCCESS) {
      LOG_ERROR("INKTextLogObjectWrite");
    }
  } else {
    LOG_ERROR("INKNetVConnRemotePortGet");
  }

  /* negative test for INKNetVConnRemotePortGet */
#ifdef DEBUG
  if (INKNetVConnRemotePortGet(NULL, &port) != INK_ERROR) {
    LOG_ERROR_NEG("INKNetVConnRemotePortGet(NULL,...)");
  }
#endif

  /* flush the log */
  if (INKTextLogObjectFlush(log) != INK_SUCCESS) {
    LOG_ERROR("INKTextLogObjectFlush");
  }
}


static int
parse_request(ConnData * conn_data)
{
  INKDebug(DEBUG_TAG, "Entered parse_request");
  INKDebug(DEBUG_TAG, "client request: \n%s", conn_data->client_request);
  LOG_SET_FUNCTION_NAME("parse_request");

  char *temp = strtok(conn_data->client_request, " ");
  if (temp != NULL)
    strcpy(conn_data->pattern, temp);
  else
    return -1;

  temp = strtok(NULL, " ");
  if (temp != NULL)
    conn_data->number = atoi(temp);
  else
    return -1;

  return 0;
}


/*
 * Log the client request to the text log object
 */
static void
log_request(ConnData * conn_data)
{
  INKDebug(DEBUG_TAG, "Entered log_request");
  LOG_SET_FUNCTION_NAME("log_request");

  /* write to the log object */
  if (INKTextLogObjectWrite(log, "The client requests a pattern of %s repeated in %d of times\n",
                            conn_data->pattern, conn_data->number) != INK_SUCCESS) {
    LOG_ERROR("INKTextLogObjectWrite");
  }

  /* negative test for INKTextLogObjectWrite */
#ifdef DEBUG
  if (INKTextLogObjectWrite(NULL, "negative test") != INK_ERROR) {
    LOG_ERROR_NEG("INKTextLogObjectWrite(NULL,...)");
  }
#endif

  /* flush the log object */
  if (INKTextLogObjectFlush(log) != INK_SUCCESS) {
    LOG_ERROR("INKTextLogObjectFlush");
  }
#ifdef DEBUG
  if (INKTextLogObjectFlush(NULL) != INK_ERROR) {
    LOG_ERROR_NEG("INKTextLogObjectFlush(NULL)");
  }
#endif

}

/*
 * Generate client response
 */
static void
generate_response(ConnData * conn_data)
{
  INKDebug(DEBUG_TAG, "Entered generate_response");
  LOG_SET_FUNCTION_NAME("generate_response");

  int response_length, i;

  /* repeat the pattern in number of times and save it to conn_data->client_response */
  response_length = conn_data->number * strlen(conn_data->pattern) + 1;
  conn_data->client_response = (char *) INKmalloc(response_length * sizeof(char));
  conn_data->client_response[0] = '\0';

  for (i = 0; i < conn_data->number; i++) {
    strcat(conn_data->client_response, conn_data->pattern);
  }

  INKDebug(DEBUG_TAG, "client response is:\n%s\n", conn_data->client_response);
}

/*
 * callback function for INKVConnWrite
 */
int
send_response_handler(INKCont contp, INKEvent event, void *data)
{
  INKDebug(DEBUG_TAG, "Entered send_response_handler");
  LOG_SET_FUNCTION_NAME("send_response_handler");

  ConnData *conn_data;

  conn_data = (ConnData *) INKContDataGet(contp);

  switch (event) {
  case INK_EVENT_VCONN_WRITE_READY:
    if (INKVIOReenable(conn_data->client_write_vio) != INK_SUCCESS) {
      LOG_ERROR("INKVIOReenable");
      clean_and_exit(contp);
      return -1;
    }
    break;
  case INK_EVENT_VCONN_WRITE_COMPLETE:
    clean_and_exit(contp);
    break;
  default:
    clean_and_exit(contp);
    return -1;
    break;
  }

  return 0;
}

/*
 * Send response to teh client
 */
static void
send_response(ConnData * conn_data, INKCont contp)
{
  INKDebug(DEBUG_TAG, "Entered send_response");
  LOG_SET_FUNCTION_NAME("send_response");

  int copied_length;

  /* negative test for INKIOBufferSizedCreate */
#ifdef DEBUG
  if (INKIOBufferSizedCreate(-1) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKIOBufferSizedCreate(-1)");
  }
#endif

  /* create the response IOBuffer */
  conn_data->client_response_buffer = INKIOBufferSizedCreate(INK_IOBUFFER_SIZE_INDEX_1K);

  if (conn_data->client_response_buffer == INK_ERROR_PTR || conn_data->client_response_buffer == NULL) {
    LOG_ERROR("INKIOBufferSizedCreate");
    clean_and_exit(contp);
    return;
  }

  /* get the IOBuffer reader */
  if ((conn_data->client_response_buffer_reader =
       INKIOBufferReaderAlloc(conn_data->client_response_buffer)) == INK_ERROR_PTR) {
    LOG_ERROR("INKIOBufferReaderAlloc");
    clean_and_exit(contp);
    return;
  }

  /* negative test for INKIOBufferWrite */
#ifdef DEBUG
  if (INKIOBufferWrite(NULL, conn_data->client_response, strlen(conn_data->client_response)) != INK_ERROR) {
    LOG_ERROR_NEG("INKIOBufferWrite(NULL,...)");
  }
  if (INKIOBufferWrite(conn_data->client_response_buffer, NULL, strlen(conn_data->client_response)) != INK_ERROR) {
    LOG_ERROR_NEG("INKIOBufferWrite(conn_data->client_response_buffer,NULL,...)");
  }
#endif

  /* copy the response to the IOBuffer */
  copied_length =
    INKIOBufferWrite(conn_data->client_response_buffer, conn_data->client_response, strlen(conn_data->client_response));

  if (copied_length == INK_ERROR) {
    LOG_ERROR("INKIOBufferWrite");
    clean_and_exit(contp);
    return;
  }

  /* send the response to the client */
  conn_data->current_handler = &send_response_handler;
  if ((conn_data->client_write_vio = INKVConnWrite(conn_data->client_vconn, contp,
                                                   conn_data->client_response_buffer_reader,
                                                   copied_length)) == INK_ERROR_PTR) {
    LOG_ERROR("INKVConnWrite");
    clean_and_exit(contp);
    return;
  }
}

/*
 * callback function for INKVConnRead
 */
int
read_request_handler(INKCont contp, INKEvent event, void *data)
{
  INKDebug(DEBUG_TAG, "Entered read_request_handler");
  LOG_SET_FUNCTION_NAME("read_request_handler");

  ConnData *conn_data;
  int read_avail, output_len, block_avail;
  INKIOBufferBlock block;
  char *buf = NULL;
  const char *block_start;

  conn_data = (ConnData *) INKContDataGet(contp);

  switch (event) {
  case INK_EVENT_VCONN_READ_READY:

    if ((read_avail = INKIOBufferReaderAvail(conn_data->client_request_buffer_reader)) == INK_ERROR) {
      LOG_ERROR("INKIOBufferReaderAvail");
      clean_and_exit(contp);
      return -1;
    }

    INKDebug(DEBUG_TAG, "read_avail = %d \n", read_avail);

    if (read_avail < WATER_MARK) {
      LOG_ERROR("INKIOBufferWaterMarkSet");
    }

    if (read_avail > 0) {
      /* copy the partly read client request from IOBuffer to conn_data->client_request */
      buf = (char *) INKmalloc(read_avail + 1);

      INKIOBufferReaderCopy(conn_data->client_request_buffer_reader, buf, read_avail);
      if (INKIOBufferReaderConsume(conn_data->client_request_buffer_reader, read_avail) != INK_SUCCESS) {
        LOG_ERROR("INKIOBufferReaderConsume");
        clean_and_exit(contp);
        return -1;
      }

      buf[read_avail] = '\0';

      strcat(conn_data->client_request, buf);

      INKfree(buf);

      /* check if the end of the client request has been reached */
      if (strstr(conn_data->client_request, "\r\n\r\n") != NULL) {

        if (parse_request(conn_data) == -1) {
          clean_and_exit(contp);
          return -1;
        }
        log_request(conn_data);
        generate_response(conn_data);
        send_response(conn_data, contp);
        return 0;
      }
    }

    /* continue reading data from the client */
    if (INKVIOReenable(conn_data->client_read_vio) == INK_ERROR) {
      LOG_ERROR("INKVIOReenable");
      clean_and_exit(contp);
      return -1;
    }
    break;

  default:
    clean_and_exit(contp);
    return -1;
  }
  return 0;
}

int
start_handler(INKCont contp, INKEvent event, void *data)
{
  INKDebug(DEBUG_TAG, "Entered start_handler");
  LOG_SET_FUNCTION_NAME("start_handler");

  ConnData *conn_data;
  int watermark;

  conn_data = (ConnData *) INKContDataGet(contp);

  /* create client request IOBuffer and buffer reader */
  if ((conn_data->client_request_buffer = INKIOBufferCreate()) == INK_ERROR_PTR) {
    LOG_ERROR("INKIOBufferCreate");
    clean_and_exit(contp);
    return -1;
  }
  if ((conn_data->client_request_buffer_reader =
       INKIOBufferReaderAlloc(conn_data->client_request_buffer)) == INK_ERROR_PTR) {
    LOG_ERROR("INKIOBufferReaderAlloc");
    clean_and_exit(contp);
    return -1;
  }

  /* negative test cases for INKIOBufferWaterMarkSet */
#ifdef DEBUG
  if (INKIOBufferWaterMarkSet(NULL, WATER_MARK) != INK_ERROR) {
    LOG_ERROR_NEG("INKIOBufferWaterMarkSet(NULL,...)");
  }
  if (INKIOBufferWaterMarkSet(conn_data->client_request_buffer, -1) != INK_ERROR) {
    LOG_ERROR_NEG("INKIOBufferWaterMarkSet(conn_data->client_request_buffer,-1)");
  }
#endif
  /* negative test cases for INKIOBufferWaterMarkGet */
#ifdef DEBUG
  if (INKIOBufferWaterMarkGet(NULL, &watermark) != INK_ERROR) {
    LOG_ERROR_NEG("INKIOBufferWaterMarkGet(NULL,...)");
  }
#endif

  /* set the watermark of the client request iobuffer */
  if (INKIOBufferWaterMarkSet(conn_data->client_request_buffer, WATER_MARK) != INK_SUCCESS) {
    LOG_ERROR("INKIOBufferWaterMarkSet");
  }
  if (INKIOBufferWaterMarkGet(conn_data->client_request_buffer, &watermark) != INK_SUCCESS) {
    LOG_ERROR("INKIOBufferWaterMarkGet");
  } else if (watermark != WATER_MARK) {
    LOG_ERROR("INKIOBufferWaterMarkSet");
  }

  conn_data->current_handler = &read_request_handler;

  /* start reading request from the client */
  if ((conn_data->client_read_vio = INKVConnRead(conn_data->client_vconn, (INKCont) contp,
                                                 conn_data->client_request_buffer, INT_MAX)) == INK_ERROR_PTR) {
    LOG_ERROR("INKVConnRead");
    clean_and_exit(contp);
    return -1;
  }
  return 0;
}

static int
main_handler(INKCont contp, INKEvent event, void *data)
{
  LOG_SET_FUNCTION_NAME("main_handler");

  ConnData *conn_data;

  conn_data = (ConnData *) INKContDataGet(contp);
  ConnHandler current_handler = conn_data->current_handler;
  return (*current_handler) (contp, event, data);
}

/*
 * Create the state machine that handles the connection between the client and proxy
 */
static INKCont
conn_sm_create(INKMutex conn_mutex, INKVConn client_vconn)
{
  LOG_SET_FUNCTION_NAME("conn_sm_create");

  INKCont contp;
  ConnData *conn_data;

  conn_data = (ConnData *) INKmalloc(sizeof(ConnData));

  conn_data->mutex = conn_mutex;
  conn_data->pending_action = NULL;
  conn_data->client_vconn = client_vconn;
  conn_data->client_request = (char *) INKmalloc((MAX_REQUEST_LENGTH + 1) * sizeof(char));
  conn_data->client_request[0] = '\0';
  conn_data->client_response = NULL;
  conn_data->pattern = (char *) INKmalloc((MAX_PATTERN_LENGTH + 1) * sizeof(char));
  conn_data->number = 0;
  conn_data->client_read_vio = NULL;
  conn_data->client_write_vio = NULL;
  conn_data->client_request_buffer = NULL;
  conn_data->client_response_buffer = NULL;
  conn_data->client_request_buffer_reader = NULL;
  conn_data->client_response_buffer_reader = NULL;

  conn_data->current_handler = &start_handler;

  if ((contp = INKContCreate(main_handler, conn_data->mutex)) == INK_ERROR_PTR) {
    LOG_ERROR("INKContCreate");
  }
  if (INKContDataSet(contp, conn_data) != INK_SUCCESS) {
    return (void *) INK_ERROR_PTR;
  }
  return contp;
}

/*
 * callback function for INKNetAccept
 */
static int
accept_handler(INKCont contp, INKEvent event, void *edata)
{
  LOG_SET_FUNCTION_NAME("accept_handler");

  INKMutex conn_mutex;
  INKCont conn_sm;
  INKVConn client_vconn;

  client_vconn = (INKVConn) edata;

  switch (event) {
  case INK_EVENT_NET_ACCEPT:

    INKDebug(DEBUG_TAG, "accepted the client request");

    /* get the remote(client) IP and port of the net vconnection */
    get_remote_ip(client_vconn);

    if ((conn_mutex = INKMutexCreate()) == INK_ERROR_PTR) {
      LOG_ERROR_AND_RETURN("INKMutexCreate");
    }

    /* create the state machine that handles the connection */
    conn_sm = (INKCont) conn_sm_create(conn_mutex, client_vconn);
    if (conn_sm == INK_ERROR_PTR) {
      LOG_ERROR_AND_RETURN("conn_sm_create");
    }

    INKDebug(DEBUG_TAG, "connection state machine created");

    /* call the state machine */
    if (INKMutexLock(conn_mutex) != INK_SUCCESS) {
      LOG_ERROR_AND_RETURN("INKMutexLock");
    }
    INKContCall(conn_sm, INK_EVENT_NONE, NULL);
    if (INKMutexUnlock(conn_mutex) != INK_SUCCESS) {
      LOG_ERROR_AND_RETURN("INKMutexUnlock");
    }
    break;

  default:
    /* Something wrong with the network, if there are any
       pending NetAccept, cancel them. */
    if (actionp && !INKActionDone(actionp)) {
      INKActionCancel(actionp);
    }

    INKContDestroy(contp);
    break;
  }

  return 0;

}

/*
 * Create the text log object and set its parameters
 */
static int
create_log()
{
  LOG_SET_FUNCTION_NAME("create_log");
  INKDebug(DEBUG_TAG, "Entered create_log");

  /* negative test for INKTextLogObjectCreate */
#ifdef DEBUG
  /* log name is NULL */
  if (INKTextLogObjectCreate(NULL, INK_LOG_MODE_ADD_TIMESTAMP, &log) != INK_ERROR) {
    LOG_ERROR_NEG("INKTextLogObjectCreate(NULL,...)");
  }
  /* sub-directory doesn't exist */
  if (INKTextLogObjectCreate("aaa/bbb", INK_LOG_MODE_ADD_TIMESTAMP, &log) != INK_ERROR) {
    LOG_ERROR_NEG("INKTextLogObjectCreate(aaa/bbb,...)");
  }
  /* undefined mode value */
  if (INKTextLogObjectCreate("ccc", -1, &log) != INK_ERROR) {
    LOG_ERROR_NEG("INKTextLogObjectCreate(ccc,-1,...)");
  }
#endif

  /* create a text log object and set its parameters */
  if (INKTextLogObjectCreate("test-protocol", INK_LOG_MODE_ADD_TIMESTAMP, &log) != INK_SUCCESS) {
    LOG_ERROR_AND_RETURN("INKTextLogObjectCreate");
  }

  if (INKTextLogObjectHeaderSet(log, "Text log for test-protocol plugin") != INK_SUCCESS) {
    LOG_ERROR_AND_RETURN("INKTextLogObjectHeaderSet");
  }

  if (INKTextLogObjectRollingEnabledSet(log, 1) != INK_SUCCESS) {
    LOG_ERROR_AND_RETURN("INKTextLogObjectRollingEnabledSet");
  }

  if (INKTextLogObjectRollingIntervalSecSet(log, 3600) != INK_SUCCESS) {
    LOG_ERROR_AND_RETURN("INKTextLogObjectRollingIntervalSecSet");
  }

  if (INKTextLogObjectRollingOffsetHrSet(log, 0) != INK_SUCCESS) {
    LOG_ERROR_AND_RETURN("INKTextLogObjectRollingOffsetHrSet");
  }

  /* negative test for INKTextLogObject*Set functions */
#ifdef DEBUG
  if (INKTextLogObjectHeaderSet(NULL, "Text log for test-protocol plugin") != INK_ERROR) {
    LOG_ERROR_NEG("INKTextLogObjectHeaderSet(NULL,)");
  }

  if (INKTextLogObjectRollingEnabledSet(NULL, 1) != INK_ERROR) {
    LOG_ERROR_NEG("INKTextLogObjectRollingEnabledSet(NULL,)");
  }

  if (INKTextLogObjectRollingIntervalSecSet(NULL, 3600) != INK_ERROR) {
    LOG_ERROR_NEG("INKTextLogObjectRollingIntervalSecSet(NULL,)");
  }

  if (INKTextLogObjectRollingOffsetHrSet(NULL, 0) != INK_ERROR) {
    LOG_ERROR_NEG("INKTextLogObjectRollingOffsetHrSet(NULL,)");
  }
#endif

  return 0;
}

void
INKPluginInit(int argc, const char *argv[])
{
  LOG_SET_FUNCTION_NAME("INKPluginInit");

  int accept_port;
  INKCont contp;

  /* default value of accept port */
  accept_port = 7493;

  if (argc != 2) {
    INKDebug(DEBUG_TAG, "Usage: protocol.so accept_port\n");
  } else {
    accept_port = atoi(argv[1]);
  }

  if ((contp = INKContCreate(accept_handler, INKMutexCreate())) == INK_ERROR_PTR) {
    LOG_ERROR("INKContCreate");

    if (INKTextLogObjectDestroy(log) != INK_SUCCESS) {
      LOG_ERROR("INKTextLogObjectDestroy");
    }

    exit(-1);
  }

  /* create the text log object */
  if (create_log() == -1) {
    exit(-1);
  }

  /* negative test for INKNetAccept */
#ifdef DEBUG
  if (INKNetAccept(NULL, accept_port) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKNetAccept(NULL,...)");
  }
#endif

  if ((actionp = INKNetAccept(contp, accept_port)) == INK_ERROR_PTR) {
    LOG_ERROR("INKNetAccept");
  }
  // TODO any other place to call INKTextLogObjectDestroy()?
}
