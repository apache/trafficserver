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

static TSAction actionp;

/*
 * Cleanup continuation data and destroy the continuation
 */
static void
clean_and_exit(TSCont contp)
{
  LOG_SET_FUNCTION_NAME("clean_and_exit");

  ConnData *conn_data = (ConnData *) TSContDataGet(contp);

  TSDebug(DEBUG_TAG, "Entered clean_and_exit");

  if (conn_data->pending_action && !TSActionDone(conn_data->pending_action)) {
    TSActionCancel(conn_data->pending_action);
  }

  if (conn_data->client_vconn) {
    TSVConnClose(conn_data->client_vconn);
  }

  if (conn_data->client_request) {
    TSfree(conn_data->client_request);
  }

  if (conn_data->client_response) {
    TSfree(conn_data->client_response);
  }

  if (conn_data->pattern) {
    TSfree(conn_data->pattern);
  }

  if (conn_data->client_request_buffer) {
    if (conn_data->client_request_buffer_reader) {
      TSIOBufferReaderFree(conn_data->client_request_buffer_reader);
    }
    TSIOBufferDestroy(conn_data->client_request_buffer);
  }

  if (conn_data->client_response_buffer) {
    if (conn_data->client_response_buffer_reader) {
      TSIOBufferReaderFree(conn_data->client_response_buffer_reader);
    }
    TSIOBufferDestroy(conn_data->client_response_buffer);
  }

  TSfree(conn_data);
  TSContDestroy(contp);
}

/*
 * Get the remote ip and port of the net vconnection
 */
static void
get_remote_ip(TSVConn client_vconn)
{
  LOG_SET_FUNCTION_NAME("get_remote_ip");

  unsigned int ip;
  int port;

  TSDebug(DEBUG_TAG, "Entered get_remote_ip");

  /* get the remote ip */
  if (TSNetVConnRemoteIPGet(client_vconn, &ip) == TS_SUCCESS) {
    if (TSTextLogObjectWrite(plugin_log, "Netvconn remote ip: %d", ip) != TS_SUCCESS) {
      LOG_ERROR("TSTextLogObjectWrite");
    }
  } else {
    LOG_ERROR("TSNetVConnRemoteIPGet");
  }

  /* negative test for TSNetVConnRemoteIPGet */
#ifdef DEBUG
  if (TSNetVConnRemoteIPGet(NULL, &ip) != TS_ERROR) {
    LOG_ERROR_NEG("TSNetVConnRemoteIPGet(NULL,...)");
  }
#endif

  /* get the remote port */
  if (TSNetVConnRemotePortGet(client_vconn, &port) == TS_SUCCESS) {
    if (TSTextLogObjectWrite(plugin_log, "Netvconn remote port: %d", port) != TS_SUCCESS) {
      LOG_ERROR("TSTextLogObjectWrite");
    }
  } else {
    LOG_ERROR("TSNetVConnRemotePortGet");
  }

  /* negative test for TSNetVConnRemotePortGet */
#ifdef DEBUG
  if (TSNetVConnRemotePortGet(NULL, &port) != TS_ERROR) {
    LOG_ERROR_NEG("TSNetVConnRemotePortGet(NULL,...)");
  }
#endif

  /* flush the log */
  if (TSTextLogObjectFlush(plugin_log) != TS_SUCCESS) {
    LOG_ERROR("TSTextLogObjectFlush");
  }
}


static int
parse_request(ConnData * conn_data)
{
  LOG_SET_FUNCTION_NAME("parse_request");

  char *temp = strtok(conn_data->client_request, " ");

  TSDebug(DEBUG_TAG, "Entered parse_request");
  TSDebug(DEBUG_TAG, "client request: \n%s", conn_data->client_request);

  if ((temp != NULL) && (strlen(temp) <= MAX_PATTERN_LENGTH))
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
  LOG_SET_FUNCTION_NAME("log_request");

  TSDebug(DEBUG_TAG, "Entered log_request");

  /* write to the log object */
  if (TSTextLogObjectWrite(plugin_log, "Client request: %s %d", conn_data->pattern, conn_data->number) != TS_SUCCESS) {
    LOG_ERROR("TSTextLogObjectWrite");
  }

  /* negative test for TSTextLogObjectWrite */
#ifdef DEBUG
  if (TSTextLogObjectWrite(NULL, "negative test") != TS_ERROR) {
    LOG_ERROR_NEG("TSTextLogObjectWrite(NULL,...)");
  }
#endif

  /* flush the log object */
  if (TSTextLogObjectFlush(plugin_log) != TS_SUCCESS) {
    LOG_ERROR("TSTextLogObjectFlush");
  }
#ifdef DEBUG
  if (TSTextLogObjectFlush(NULL) != TS_ERROR) {
    LOG_ERROR_NEG("TSTextLogObjectFlush(NULL)");
  }
#endif

}

/*
 * Generate client response
 */
static void
generate_response(ConnData * conn_data)
{
  LOG_SET_FUNCTION_NAME("generate_response");

  int response_length, i;

  TSDebug(DEBUG_TAG, "Entered generate_response");

  /* repeat the pattern in number of times and save it to conn_data->client_response */
  response_length = conn_data->number * strlen(conn_data->pattern) + 1;
  conn_data->client_response = (char *) TSmalloc(response_length * sizeof(char));
  conn_data->client_response[0] = '\0';

  for (i = 0; i < conn_data->number; i++) {
    strcat(conn_data->client_response, conn_data->pattern);
  }

  TSDebug(DEBUG_TAG, "client response is:\n%s\n", conn_data->client_response);
}

/*
 * callback function for TSVConnWrite
 */
int
send_response_handler(TSCont contp, TSEvent event, void *data)
{
  LOG_SET_FUNCTION_NAME("send_response_handler");

  ConnData *conn_data;

  conn_data = (ConnData *) TSContDataGet(contp);

  TSDebug(DEBUG_TAG, "Entered send_response_handler");

  switch (event) {
  case TS_EVENT_VCONN_WRITE_READY:
    if (TSVIOReenable(conn_data->client_write_vio) != TS_SUCCESS) {
      LOG_ERROR("TSVIOReenable");
      clean_and_exit(contp);
      return -1;
    }
    break;
  case TS_EVENT_VCONN_WRITE_COMPLETE:
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
send_response(ConnData * conn_data, TSCont contp)
{
  LOG_SET_FUNCTION_NAME("send_response");

  int copied_length;

  TSDebug(DEBUG_TAG, "Entered send_response");

  /* negative test for TSIOBufferSizedCreate */
#ifdef DEBUG
  if (TSIOBufferSizedCreate(-1) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSIOBufferSizedCreate(-1)");
  }
#endif

  /* create the response IOBuffer */
  conn_data->client_response_buffer = TSIOBufferSizedCreate(TS_IOBUFFER_SIZE_INDEX_1K);

  if (conn_data->client_response_buffer == TS_ERROR_PTR || conn_data->client_response_buffer == NULL) {
    LOG_ERROR("TSIOBufferSizedCreate");
    clean_and_exit(contp);
    return;
  }

  /* get the IOBuffer reader */
  if ((conn_data->client_response_buffer_reader =
       TSIOBufferReaderAlloc(conn_data->client_response_buffer)) == TS_ERROR_PTR) {
    LOG_ERROR("TSIOBufferReaderAlloc");
    clean_and_exit(contp);
    return;
  }

  /* negative test for TSIOBufferWrite */
#ifdef DEBUG
  if (TSIOBufferWrite(NULL, conn_data->client_response, strlen(conn_data->client_response)) != TS_ERROR) {
    LOG_ERROR_NEG("TSIOBufferWrite(NULL,...)");
  }
  if (TSIOBufferWrite(conn_data->client_response_buffer, NULL, strlen(conn_data->client_response)) != TS_ERROR) {
    LOG_ERROR_NEG("TSIOBufferWrite(conn_data->client_response_buffer,NULL,...)");
  }
#endif

  /* copy the response to the IOBuffer */
  copied_length =
    TSIOBufferWrite(conn_data->client_response_buffer, conn_data->client_response, strlen(conn_data->client_response));

  if (copied_length == TS_ERROR) {
    LOG_ERROR("TSIOBufferWrite");
    clean_and_exit(contp);
    return;
  }

  /* send the response to the client */
  conn_data->current_handler = &send_response_handler;
  if ((conn_data->client_write_vio = TSVConnWrite(conn_data->client_vconn, contp,
                                                   conn_data->client_response_buffer_reader,
                                                   copied_length)) == TS_ERROR_PTR) {
    LOG_ERROR("TSVConnWrite");
    clean_and_exit(contp);
    return;
  }
}

/*
 * callback function for TSVConnRead
 */
int
read_request_handler(TSCont contp, TSEvent event, void *data)
{
  LOG_SET_FUNCTION_NAME("read_request_handler");

  ConnData *conn_data;
  int read_avail, output_len, block_avail;
  TSIOBufferBlock block;
  char *buf = NULL;
  const char *block_start;

  conn_data = (ConnData *) TSContDataGet(contp);

  TSDebug(DEBUG_TAG, "Entered read_request_handler");

  switch (event) {
  case TS_EVENT_VCONN_READ_READY:

    if ((read_avail = TSIOBufferReaderAvail(conn_data->client_request_buffer_reader)) == TS_ERROR) {
      LOG_ERROR("TSIOBufferReaderAvail");
      clean_and_exit(contp);
      return -1;
    }

    TSDebug(DEBUG_TAG, "read_avail = %d \n", read_avail);

    if (read_avail > 0) {
      /* copy the partly read client request from IOBuffer to conn_data->client_request */
      buf = (char *) TSmalloc(read_avail + 1);

      output_len = 0;
      while (read_avail > 0) {
        if ((block = TSIOBufferReaderStart(conn_data->client_request_buffer_reader)) == TS_ERROR_PTR) {
          LOG_ERROR("TSIOBufferReaderStart");
          clean_and_exit(contp);
          return -1;
        }
        if ((block_start =
             (char *) TSIOBufferBlockReadStart(block, conn_data->client_request_buffer_reader,
                                                &block_avail)) == TS_ERROR_PTR) {
          LOG_ERROR("TSIOBufferBlockReadStart");
          clean_and_exit(contp);
          return -1;
        }

        if (block_avail == 0) {
          break;
        }

        memcpy(buf + output_len, block_start, block_avail);
        output_len += block_avail;
        if (TSIOBufferReaderConsume(conn_data->client_request_buffer_reader, block_avail) != TS_SUCCESS) {
          LOG_ERROR("TSIOBufferReaderConsume");
          clean_and_exit(contp);
          return -1;
        }
        read_avail -= block_avail;
      }
      buf[output_len] = '\0';

      if ((strlen(conn_data->client_request) + strlen(buf)) > MAX_REQUEST_LENGTH) {
        TSDebug(PLUGIN_NAME, "Client request length exceeds the limit");
        clean_and_exit(contp);
        return -1;
      }

      strcat(conn_data->client_request, buf);

      TSfree(buf);

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
    if (TSVIOReenable(conn_data->client_read_vio) == TS_ERROR) {
      LOG_ERROR("TSVIOReenable");
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
start_handler(TSCont contp, TSEvent event, void *data)
{
  LOG_SET_FUNCTION_NAME("start_handler");

  ConnData *conn_data;
  int watermark;

  conn_data = (ConnData *) TSContDataGet(contp);

  TSDebug(DEBUG_TAG, "Entered start_handler");

  /* create client request IOBuffer and buffer reader */
  if ((conn_data->client_request_buffer = TSIOBufferCreate()) == TS_ERROR_PTR) {
    LOG_ERROR("TSIOBufferCreate");
    clean_and_exit(contp);
    return -1;
  }
  if ((conn_data->client_request_buffer_reader =
       TSIOBufferReaderAlloc(conn_data->client_request_buffer)) == TS_ERROR_PTR) {
    LOG_ERROR("TSIOBufferReaderAlloc");
    clean_and_exit(contp);
    return -1;
  }

  /* negative test cases for TSIOBufferWaterMarkSet */
#ifdef DEBUG
  if (TSIOBufferWaterMarkSet(NULL, WATER_MARK) != TS_ERROR) {
    LOG_ERROR_NEG("TSIOBufferWaterMarkSet(NULL,...)");
  }
  if (TSIOBufferWaterMarkSet(conn_data->client_request_buffer, -1) != TS_ERROR) {
    LOG_ERROR_NEG("TSIOBufferWaterMarkSet(conn_data->client_request_buffer,-1)");
  }
#endif
  /* negative test cases for TSIOBufferWaterMarkGet */
#ifdef DEBUG
  if (TSIOBufferWaterMarkGet(NULL, &watermark) != TS_ERROR) {
    LOG_ERROR_NEG("TSIOBufferWaterMarkGet(NULL,...)");
  }
#endif

  /* set the watermark of the client request iobuffer */
  if (TSIOBufferWaterMarkSet(conn_data->client_request_buffer, WATER_MARK) != TS_SUCCESS) {
    LOG_ERROR("TSIOBufferWaterMarkSet");
  }
  if (TSIOBufferWaterMarkGet(conn_data->client_request_buffer, &watermark) != TS_SUCCESS) {
    LOG_ERROR("TSIOBufferWaterMarkGet");
  } else if (watermark != WATER_MARK) {
    LOG_ERROR("TSIOBufferWaterMarkSet");
  }

  conn_data->current_handler = &read_request_handler;

  /* start reading request from the client */
  if ((conn_data->client_read_vio = TSVConnRead(conn_data->client_vconn, (TSCont) contp,
                                                 conn_data->client_request_buffer, INT_MAX)) == TS_ERROR_PTR) {
    LOG_ERROR("TSVConnRead");
    clean_and_exit(contp);
    return -1;
  }
  return 0;
}

static int
main_handler(TSCont contp, TSEvent event, void *data)
{
  LOG_SET_FUNCTION_NAME("main_handler");

  ConnData *conn_data = (ConnData *) TSContDataGet(contp);
  ConnHandler current_handler = conn_data->current_handler;
  return (*current_handler) (contp, event, data);
}

/*
 * Create the state machine that handles the connection between the client and proxy
 */
static TSCont
conn_sm_create(TSMutex conn_mutex, TSVConn client_vconn)
{
  LOG_SET_FUNCTION_NAME("conn_sm_create");

  TSCont contp;
  ConnData *conn_data;

  conn_data = (ConnData *) TSmalloc(sizeof(ConnData));

  conn_data->mutex = conn_mutex;
  conn_data->pending_action = NULL;
  conn_data->client_vconn = client_vconn;
  conn_data->client_request = (char *) TSmalloc((MAX_REQUEST_LENGTH + 1) * sizeof(char));
  conn_data->client_request[0] = '\0';
  conn_data->client_response = NULL;
  conn_data->pattern = (char *) TSmalloc((MAX_PATTERN_LENGTH + 1) * sizeof(char));
  conn_data->number = 0;
  conn_data->client_read_vio = NULL;
  conn_data->client_write_vio = NULL;
  conn_data->client_request_buffer = NULL;
  conn_data->client_response_buffer = NULL;
  conn_data->client_request_buffer_reader = NULL;
  conn_data->client_response_buffer_reader = NULL;

  conn_data->current_handler = &start_handler;

  if ((contp = TSContCreate(main_handler, conn_data->mutex)) == TS_ERROR_PTR) {
    LOG_ERROR("TSContCreate");
  }
  if (TSContDataSet(contp, conn_data) != TS_SUCCESS) {
    return (void *) TS_ERROR_PTR;
  }
  return contp;
}

/*
 * callback function for TSNetAccept
 */
static int
accept_handler(TSCont contp, TSEvent event, void *edata)
{
  LOG_SET_FUNCTION_NAME("accept_handler");

  TSMutex conn_mutex;
  TSCont conn_sm;
  TSVConn client_vconn;

  client_vconn = (TSVConn) edata;

  switch (event) {
  case TS_EVENT_NET_ACCEPT:

    TSDebug(DEBUG_TAG, "accepted the client request");

    /* get the remote(client) IP and port of the net vconnection */
    get_remote_ip(client_vconn);

    if ((conn_mutex = TSMutexCreate()) == TS_ERROR_PTR) {
      LOG_ERROR_AND_RETURN("TSMutexCreate");
    }

    /* create the state machine that handles the connection */
    conn_sm = (TSCont) conn_sm_create(conn_mutex, client_vconn);
    if (conn_sm == TS_ERROR_PTR) {
      LOG_ERROR_AND_RETURN("conn_sm_create");
    }

    TSDebug(DEBUG_TAG, "connection state machine created");

    /* call the state machine */
    if (TSMutexLock(conn_mutex) != TS_SUCCESS) {
      LOG_ERROR_AND_RETURN("TSMutexLock");
    }
    TSContCall(conn_sm, TS_EVENT_NONE, NULL);
    if (TSMutexUnlock(conn_mutex) != TS_SUCCESS) {
      LOG_ERROR_AND_RETURN("TSMutexUnlock");
    }
    break;

  default:
    /* Something wrong with the network, if there are any
       pending NetAccept, cancel them. */
    if (actionp && !TSActionDone(actionp)) {
      TSActionCancel(actionp);
    }

    TSContDestroy(contp);
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

  TSTextLogObject test_log = NULL;

  TSDebug(DEBUG_TAG, "Entered create_log");

  /* negative test for TSTextLogObjectCreate */
#ifdef DEBUG
  /* log name is NULL */
  if (TSTextLogObjectCreate(NULL, TS_LOG_MODE_ADD_TIMESTAMP, &test_log) != TS_ERROR) {
    LOG_ERROR_NEG("TSTextLogObjectCreate(NULL,...)");
  }
  /* sub-directory doesn't exist */
  if (TSTextLogObjectCreate("aaa/bbb", TS_LOG_MODE_ADD_TIMESTAMP, &test_log) != TS_ERROR) {
    LOG_ERROR_NEG("TSTextLogObjectCreate(aaa/bbb,...)");
  }
  /* undefined mode value */
  if (TSTextLogObjectCreate("ccc", -1, &test_log) != TS_ERROR) {
    LOG_ERROR_NEG("TSTextLogObjectCreate(ccc,-1,...)");
  }
#endif

  /* create a text log object and then destroy it, just for testing TSTextLogObjectDestroy */
  if (TSTextLogObjectCreate("test-log", TS_LOG_MODE_DO_NOT_RENAME, &test_log) != TS_SUCCESS) {
    LOG_ERROR_AND_RETURN("TSTextLogObjectCreate");
  }
  if (test_log && TSTextLogObjectDestroy(test_log) != TS_SUCCESS) {
    LOG_ERROR_AND_RETURN("TSTextLogObjectDestroy");
  }

  /* create a text log object and set its parameters */
  if (TSTextLogObjectCreate("test-protocol", TS_LOG_MODE_ADD_TIMESTAMP, &plugin_log) != TS_SUCCESS) {
    LOG_ERROR_AND_RETURN("TSTextLogObjectCreate");
  }

  if (TSTextLogObjectHeaderSet(plugin_log, "Text log for test-protocol plugin") != TS_SUCCESS) {
    LOG_ERROR_AND_RETURN("TSTextLogObjectHeaderSet");
  }

  if (TSTextLogObjectRollingEnabledSet(plugin_log, 1) != TS_SUCCESS) {
    LOG_ERROR_AND_RETURN("TSTextLogObjectRollingEnabledSet");
  }

  if (TSTextLogObjectRollingIntervalSecSet(plugin_log, 1800) != TS_SUCCESS) {
    LOG_ERROR_AND_RETURN("TSTextLogObjectRollingIntervalSecSet");
  }

  if (TSTextLogObjectRollingOffsetHrSet(plugin_log, 0) != TS_SUCCESS) {
    LOG_ERROR_AND_RETURN("TSTextLogObjectRollingOffsetHrSet");
  }

  /* negative test for TSTextLogObject*Set functions */
#ifdef DEBUG
  if (TSTextLogObjectHeaderSet(NULL, "Text log for test-protocol plugin") != TS_ERROR) {
    LOG_ERROR_NEG("TSTextLogObjectHeaderSet(NULL,)");
  }

  if (TSTextLogObjectRollingEnabledSet(NULL, 1) != TS_ERROR) {
    LOG_ERROR_NEG("TSTextLogObjectRollingEnabledSet(NULL,)");
  }

  if (TSTextLogObjectRollingIntervalSecSet(NULL, 3600) != TS_ERROR) {
    LOG_ERROR_NEG("TSTextLogObjectRollingIntervalSecSet(NULL,)");
  }

  if (TSTextLogObjectRollingOffsetHrSet(NULL, 0) != TS_ERROR) {
    LOG_ERROR_NEG("TSTextLogObjectRollingOffsetHrSet(NULL,)");
  }
#endif

  return 0;
}

void
TSPluginInit(int argc, const char *argv[])
{
  LOG_SET_FUNCTION_NAME("TSPluginInit");

  int accept_port;
  TSCont contp;

  /* default value of accept port */
  accept_port = 7493;

  if (argc != 2) {
    TSDebug(DEBUG_TAG, "Usage: protocol.so accept_port\n");
  } else {
    accept_port = atoi(argv[1]);
  }

  if ((contp = TSContCreate(accept_handler, TSMutexCreate())) == TS_ERROR_PTR) {
    LOG_ERROR("TSContCreate");

    exit(-1);
  }

  /* create the text log object */
  if (create_log() == -1) {
    exit(-1);
  }

  /* negative test for TSNetAccept */
#ifdef DEBUG
  if (TSNetAccept(NULL, accept_port) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSNetAccept(NULL,...)");
  }
#endif

  if ((actionp = TSNetAccept(contp, accept_port)) == TS_ERROR_PTR) {
    LOG_ERROR("TSNetAccept");
  }

}
