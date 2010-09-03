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

#include "TxnSM.h"

extern INKTextLogObject protocol_plugin_log;

/* Fix me: currently, tunnelling server_response from OS to both cache and
   client doesn't work for client_vc. So write data first to cache and then
   write cached data to client. */

/* static functions */
int main_handler(INKCont contp, INKEvent event, void *data);

/* functions for clients */
int state_start(INKCont contp, INKEvent event, void *data);
int state_interface_with_client(INKCont contp, INKEvent event, INKVIO vio);
int state_read_request_from_client(INKCont contp, INKEvent event, INKVIO vio);
int state_send_response_to_client(INKCont contp, INKEvent event, INKVIO vio);

/* functions for cache operation */
int state_handle_cache_lookup(INKCont contp, INKEvent event, INKVConn vc);
int state_handle_cache_read_response(INKCont contp, INKEvent event, INKVIO vio);
int state_handle_cache_prepare_for_write(INKCont contp, INKEvent event, INKVConn vc);
int state_write_to_cache(INKCont contp, INKEvent event, INKVIO vio);

/* functions for servers */
int state_build_and_send_request(INKCont contp, INKEvent event, void *data);
int state_dns_lookup(INKCont contp, INKEvent event, INKHostLookupResult host_info);
int state_connect_to_server(INKCont contp, INKEvent event, INKVConn vc);
int state_interface_with_server(INKCont contp, INKEvent event, INKVIO vio);
int state_send_request_to_server(INKCont contp, INKEvent event, INKVIO vio);
int state_read_response_from_server(INKCont contp, INKEvent event, INKVIO vio);

/* misc functions */
int state_done(INKCont contp, INKEvent event, INKVIO vio);

int send_response_to_client(INKCont contp);
int prepare_to_die(INKCont contp);

char *get_info_from_buffer(INKIOBufferReader the_reader);
int is_request_end(char *buf);
int parse_request(char *request, char *server_name, char *file_name);
INKCacheKey CacheKeyCreate(char *file_name);

/* Continuation handler is a function pointer, this function
   is to assign the continuation handler to a specific function. */
int
main_handler(INKCont contp, INKEvent event, void *data)
{
  TxnSM *txn_sm = (TxnSM *) INKContDataGet(contp);
  TxnSMHandler q_current_handler = txn_sm->q_current_handler;

  INKDebug("protocol", "main_handler (contp %X event %d)", contp, event);

  /* handle common cases errors */
  if (event == INK_EVENT_ERROR) {
    return prepare_to_die(contp);
  }

  if (q_current_handler != &state_interface_with_server) {
    if (event == INK_EVENT_VCONN_EOS) {
      return prepare_to_die(contp);
    }
  }

  INKDebug("protocol", "current_handler (%p)", q_current_handler);

  return (*q_current_handler) (contp, event, data);
}

/* Create the Txn data structure and the continuation for the Txn. */
INKCont
TxnSMCreate(INKMutex pmutex, INKVConn client_vc, int server_port)
{
  INKCont contp;
  TxnSM *txn_sm;

  txn_sm = (TxnSM *) INKmalloc(sizeof(TxnSM));

  txn_sm->q_mutex = pmutex;
  txn_sm->q_pending_action = NULL;

  /* Txn will use this server port to connect to the origin server. */
  txn_sm->q_server_port = server_port;
  /* The client_vc is returned by INKNetAccept, refer to Protocol.c. */
  txn_sm->q_client_vc = client_vc;
  /* The server_vc will be created if Txn connects to the origin server. */
  txn_sm->q_server_vc = NULL;

  txn_sm->q_client_read_vio = NULL;
  txn_sm->q_client_write_vio = NULL;
  txn_sm->q_client_request_buffer = NULL;
  txn_sm->q_client_response_buffer = NULL;
  txn_sm->q_client_request_buffer_reader = NULL;
  txn_sm->q_client_response_buffer_reader = NULL;

  txn_sm->q_server_read_vio = NULL;
  txn_sm->q_server_write_vio = NULL;
  txn_sm->q_server_request_buffer = NULL;
  txn_sm->q_server_response_buffer = NULL;
  txn_sm->q_server_request_buffer_reader = NULL;

  /* Char buffers to store client request and server response. */
  txn_sm->q_client_request = (char *) INKmalloc(sizeof(char) * (MAX_REQUEST_LENGTH + 1));
  memset(txn_sm->q_client_request, '\0', (sizeof(char) * (MAX_REQUEST_LENGTH + 1)));
  txn_sm->q_server_response = NULL;
  txn_sm->q_server_response_length = 0;
  txn_sm->q_block_bytes_read = 0;
  txn_sm->q_cache_vc = NULL;
  txn_sm->q_cache_response_length = 0;
  txn_sm->q_cache_read_buffer = NULL;
  txn_sm->q_cache_read_buffer_reader = NULL;

  txn_sm->q_server_name = (char *) INKmalloc(sizeof(char) * (MAX_SERVER_NAME_LENGTH + 1));
  txn_sm->q_file_name = (char *) INKmalloc(sizeof(char) * (MAX_FILE_NAME_LENGTH + 1));

  txn_sm->q_key = NULL;
  txn_sm->q_magic = TXN_SM_ALIVE;

  /* Set the current handler to be state_start. */
  set_handler(txn_sm->q_current_handler, &state_start);

  contp = INKContCreate(main_handler, txn_sm->q_mutex);
  INKContDataSet(contp, txn_sm);
  return contp;
}

/* This function starts to read incoming client request data from client_vc */
int
state_start(INKCont contp, INKEvent event, void *data)
{
  TxnSM *txn_sm = (TxnSM *) INKContDataGet(contp);

  if (!txn_sm->q_client_vc) {
    return prepare_to_die(contp);
  }

  txn_sm->q_client_request_buffer = INKIOBufferCreate();
  txn_sm->q_client_request_buffer_reader = INKIOBufferReaderAlloc(txn_sm->q_client_request_buffer);

  if (!txn_sm->q_client_request_buffer || !txn_sm->q_client_request_buffer_reader) {
    return prepare_to_die(contp);
  }

  /* Now the IOBuffer and IOBufferReader is ready, the data from
     client_vc can be read into the IOBuffer. Since we don't know
     the size of the client request, set the expecting size to be
     INT_MAX, so that we will always get INK_EVENT_VCONN_READ_READY
     event, but never INK_EVENT_VCONN_READ_COMPLETE event. */
  set_handler(txn_sm->q_current_handler, &state_interface_with_client);
  txn_sm->q_client_read_vio = INKVConnRead(txn_sm->q_client_vc, (INKCont) contp,
                                           txn_sm->q_client_request_buffer, INT_MAX);

  return INK_SUCCESS;
}

/* This function is to call proper functions according to the
   VIO argument. If it's read_vio, which means reading request from
   client_vc, call state_read_request_from_client. If it's write_vio,
   which means sending response to client_vc, call
   state_send_response_to_client. If the event is INK_EVENT_VCONN_EOS,
   which means the client closed socket and thus implies the client
   drop all jobs between TxnSM and the client, so go to die. */
int
state_interface_with_client(INKCont contp, INKEvent event, INKVIO vio)
{
  TxnSM *txn_sm = (TxnSM *) INKContDataGet(contp);

  INKDebug("protocol", "enter state_interface_with_client");

  txn_sm->q_pending_action = NULL;

  if (vio == txn_sm->q_client_read_vio)
    return state_read_request_from_client(contp, event, vio);

  /* vio == txn_sm->q_client_write_vio */
  return state_send_response_to_client(contp, event, vio);
}

/* Data is read from client_vc, if all data for the request is in,
   parse it and do cache lookup. */
int
state_read_request_from_client(INKCont contp, INKEvent event, INKVIO vio)
{
  int bytes_read, parse_result;
  char *temp_buf;

  TxnSM *txn_sm = (TxnSM *) INKContDataGet(contp);

  INKDebug("protocol", "enter state_read_request_from_client");

  switch (event) {
  case INK_EVENT_VCONN_READ_READY:
    bytes_read = INKIOBufferReaderAvail(txn_sm->q_client_request_buffer_reader);

    if (bytes_read > 0) {
      temp_buf = (char *) get_info_from_buffer(txn_sm->q_client_request_buffer_reader);
      strcat(txn_sm->q_client_request, temp_buf);
      INKfree(temp_buf);

      /* Check if the request is fully read, if so, do cache lookup. */
      if (strstr(txn_sm->q_client_request, "\r\n\r\n") != NULL) {
        temp_buf = (char *) INKmalloc(sizeof(char) * (strlen(txn_sm->q_client_request) + 1));
        memcpy(temp_buf, txn_sm->q_client_request, strlen(txn_sm->q_client_request));
        temp_buf[strlen(txn_sm->q_client_request)] = '\0';
        parse_result = parse_request(temp_buf, txn_sm->q_server_name, txn_sm->q_file_name);
        INKfree(temp_buf);

        if (parse_result != 1)
          return prepare_to_die(contp);

        /* Start to do cache lookup */
        INKDebug("protocol", "Key material: file name is %d, %s*****", txn_sm->q_file_name, txn_sm->q_file_name);
        txn_sm->q_key = (INKCacheKey) CacheKeyCreate(txn_sm->q_file_name);

        set_handler(txn_sm->q_current_handler, &state_handle_cache_lookup);
        txn_sm->q_pending_action = INKCacheRead(contp, txn_sm->q_key);

        return INK_SUCCESS;
      }
    }

    /* The request is not fully read, reenable the read_vio. */
    INKVIOReenable(txn_sm->q_client_read_vio);
    break;

  default:                     /* Shouldn't get here, prepare to die. */
    return prepare_to_die(contp);

  }
  return INK_SUCCESS;
}

/* This function handle the cache lookup result. If MISS, try to
   open cache write_vc for writing. Otherwise, use the vc returned
   by the cache to read the data from the cache. */
int
state_handle_cache_lookup(INKCont contp, INKEvent event, INKVConn vc)
{
  TxnSM *txn_sm = (TxnSM *) INKContDataGet(contp);
  int response_size;
  int ret_val;

  INKDebug("protocol", "enter state_handle_cache_lookup");

  switch (event) {
  case INK_EVENT_CACHE_OPEN_READ:
    INKDebug("protocol", "cache hit!!!");
    /* Cache hit. */

    /* Write log */
    ret_val = INKTextLogObjectWrite(protocol_plugin_log, "%s %s %d \n", txn_sm->q_file_name, txn_sm->q_server_name, 1);
    if (ret_val != INK_SUCCESS)
      INKError("fail to write into log");

    txn_sm->q_cache_vc = vc;
    txn_sm->q_pending_action = NULL;

    /* Get the size of the cached doc. */
    INKVConnCacheObjectSizeGet(txn_sm->q_cache_vc, &response_size);

    /* Allocate IOBuffer to store data from the cache. */
    txn_sm->q_client_response_buffer = INKIOBufferCreate();
    txn_sm->q_client_response_buffer_reader = INKIOBufferReaderAlloc(txn_sm->q_client_response_buffer);
    txn_sm->q_cache_read_buffer = INKIOBufferCreate();
    txn_sm->q_cache_read_buffer_reader = INKIOBufferReaderAlloc(txn_sm->q_cache_read_buffer);

    if (!txn_sm->q_client_response_buffer ||
        !txn_sm->q_client_response_buffer_reader ||
        !txn_sm->q_cache_read_buffer || !txn_sm->q_cache_read_buffer_reader) {
      return prepare_to_die(contp);
    }

    /* Read doc from the cache. */
    set_handler(txn_sm->q_current_handler, &state_handle_cache_read_response);
    txn_sm->q_cache_read_vio = INKVConnRead(txn_sm->q_cache_vc, contp, txn_sm->q_cache_read_buffer, response_size);

    break;

  case INK_EVENT_CACHE_OPEN_READ_FAILED:
    /* Cache miss or error, open cache write_vc. */
    INKDebug("protocol", "cache miss or error!!!");
    /* Write log */
    ret_val = INKTextLogObjectWrite(protocol_plugin_log, "%s %s %d \n", txn_sm->q_file_name, txn_sm->q_server_name, 0);

    if (ret_val != INK_SUCCESS)
      INKError("fail to write into log");

    set_handler(txn_sm->q_current_handler, &state_handle_cache_prepare_for_write);
    txn_sm->q_pending_action = INKCacheWrite(contp, txn_sm->q_key);
    break;

  default:
    /* unknown event, abort transaction */
    return prepare_to_die(contp);
  }

  return INK_SUCCESS;
}

static void
load_buffer_cache_data(TxnSM * txn_sm)
{
  /* transfer the data from the cache buffer (which must
     fully be consumed on a VCONN_READY event, to the
     server response buffer */
  int rdr_avail = INKIOBufferReaderAvail(txn_sm->q_cache_read_buffer_reader);

  INKDebug("protocol", "entering buffer_cache_data");
  INKDebug("protocol", "loading %d bytes to buffer reader", rdr_avail);

  INKAssert(rdr_avail > 0);

  INKIOBufferCopy(txn_sm->q_client_response_buffer,     /* (cache response buffer) */
                  txn_sm->q_cache_read_buffer_reader,   /* (transient buffer)      */
                  rdr_avail, 0);

  INKIOBufferReaderConsume(txn_sm->q_cache_read_buffer_reader, rdr_avail);
}

/* If the document is fully read out of the cache, close the
   cache read_vc, send the document to the client. Otherwise,
   reenable the read_vio to read more data out. If some error
   occurs, close the read_vc, open write_vc for writing the doc
   into the cache.*/
int
state_handle_cache_read_response(INKCont contp, INKEvent event, INKVIO vio)
{
  TxnSM *txn_sm = (TxnSM *) INKContDataGet(contp);

  INKDebug("protocol", "enter state_handle_cache_read_response");

  txn_sm->q_pending_action = NULL;

  switch (event) {
  case INK_EVENT_VCONN_READ_COMPLETE:
    load_buffer_cache_data(txn_sm);
    INKVConnClose(txn_sm->q_cache_vc);
    txn_sm->q_cache_vc = NULL;
    txn_sm->q_cache_read_vio = NULL;
    txn_sm->q_cache_write_vio = NULL;
    INKIOBufferReaderFree(txn_sm->q_cache_read_buffer_reader);
    INKIOBufferDestroy(txn_sm->q_cache_read_buffer);
    txn_sm->q_cache_read_buffer_reader = NULL;
    txn_sm->q_cache_read_buffer = NULL;
    return send_response_to_client(contp);

  case INK_EVENT_VCONN_READ_READY:
    load_buffer_cache_data(txn_sm);

    INKVIOReenable(txn_sm->q_cache_read_vio);
    break;

  default:
    /* Error */
    if (txn_sm->q_cache_vc) {
      INKVConnClose(txn_sm->q_cache_vc);
      txn_sm->q_cache_vc = NULL;
      txn_sm->q_cache_read_vio = NULL;
      txn_sm->q_cache_write_vio = NULL;
    }

    /* Open the write_vc, after getting doc from the origin server,
       write the doc into the cache. */
    set_handler(txn_sm->q_current_handler, &state_handle_cache_prepare_for_write);
    INKAssert(txn_sm->q_pending_action == NULL);
    txn_sm->q_pending_action = INKCacheWrite(contp, txn_sm->q_key);
    break;

  }
  return INK_SUCCESS;
}

/* The cache processor call us back with the vc to use for writing
   data into the cache.
   In case of error, abort txn. */
int
state_handle_cache_prepare_for_write(INKCont contp, INKEvent event, INKVConn vc)
{
  TxnSM *txn_sm = (TxnSM *) INKContDataGet(contp);

  INKDebug("protocol", "enter state_handle_cache_prepare_for_write");

  txn_sm->q_pending_action = NULL;

  switch (event) {
  case INK_EVENT_CACHE_OPEN_WRITE:
    txn_sm->q_cache_vc = vc;
    break;
  default:
    INKError("can't open cache write_vc, aborting txn");
    txn_sm->q_cache_vc = NULL;
    return prepare_to_die(contp);
    break;
  }
  return state_build_and_send_request(contp, 0, NULL);
}

/* Cache miss or error case. Start the process to send the request
   the origin server. */
int
state_build_and_send_request(INKCont contp, INKEvent event, void *data)
{
  TxnSM *txn_sm = (TxnSM *) INKContDataGet(contp);

  INKDebug("protocol", "enter state_build_and_send_request");

  txn_sm->q_pending_action = NULL;

  txn_sm->q_server_request_buffer = INKIOBufferCreate();
  txn_sm->q_server_request_buffer_reader = INKIOBufferReaderAlloc(txn_sm->q_server_request_buffer);

  txn_sm->q_server_response_buffer = INKIOBufferCreate();
  txn_sm->q_cache_response_buffer_reader = INKIOBufferReaderAlloc(txn_sm->q_server_response_buffer);

  if (!txn_sm->q_server_request_buffer ||
      !txn_sm->q_server_request_buffer_reader ||
      !txn_sm->q_server_response_buffer || !txn_sm->q_cache_response_buffer_reader) {
    return prepare_to_die(contp);
  }

  /* Marshal request */
  INKIOBufferWrite(txn_sm->q_server_request_buffer, txn_sm->q_client_request, strlen(txn_sm->q_client_request));

  /* First thing to do is to get the server IP from the server host name. */
  set_handler(txn_sm->q_current_handler, &state_dns_lookup);
  INKAssert(txn_sm->q_pending_action == NULL);
  txn_sm->q_pending_action = INKHostLookup(contp, txn_sm->q_server_name, strlen(txn_sm->q_server_name));

  INKAssert(txn_sm->q_pending_action);
  INKDebug("protocol", "initiating host lookup");

  return INK_SUCCESS;
}

/* If Host lookup is successfully, connect to that IP. */
int
state_dns_lookup(INKCont contp, INKEvent event, INKHostLookupResult host_info)
{
  TxnSM *txn_sm = (TxnSM *) INKContDataGet(contp);

  INKDebug("protocol", "enter state_dns_lookup");

  /* Can't find the server IP. */
  if (event != INK_EVENT_HOST_LOOKUP || !host_info) {
    return prepare_to_die(contp);
  }
  txn_sm->q_pending_action = NULL;

  /* Get the server IP from data structure INKHostLookupResult. */
  INKHostLookupResultIPGet(host_info, &(txn_sm->q_server_ip));

  /* Connect to the server using its IP. */
  set_handler(txn_sm->q_current_handler, &state_connect_to_server);
  INKAssert(txn_sm->q_pending_action == NULL);
  txn_sm->q_pending_action = INKNetConnect(contp, txn_sm->q_server_ip, txn_sm->q_server_port);

  return INK_SUCCESS;
}

/* Net Processor calls back, if succeeded, the net_vc is returned.
   Note here, even if the event is INK_EVENT_NET_CONNECT, it doesn't
   mean the net connection is set up because INKNetConnect is non-blocking.
   Do VConnWrite to the net_vc, if fails, that means there is no net
   connection. */
int
state_connect_to_server(INKCont contp, INKEvent event, INKVConn vc)
{
  TxnSM *txn_sm = (TxnSM *) INKContDataGet(contp);

  INKDebug("protocol", "enter state_connect_to_server");

  /* INKNetConnect failed. */
  if (event != INK_EVENT_NET_CONNECT) {
    return prepare_to_die(contp);
  }
  txn_sm->q_pending_action = NULL;

  txn_sm->q_server_vc = vc;

  /* server_vc will be used to write request and read response. */
  set_handler(txn_sm->q_current_handler, &state_send_request_to_server);

  /* Actively write the request to the net_vc. */
  txn_sm->q_server_write_vio = INKVConnWrite(txn_sm->q_server_vc, contp,
                                             txn_sm->q_server_request_buffer_reader, strlen(txn_sm->q_client_request));
  return INK_SUCCESS;
}

/* Net Processor calls back, if write complete, wait for the response
   coming in, otherwise, reenable the write_vio. */
int
state_send_request_to_server(INKCont contp, INKEvent event, INKVIO vio)
{
  TxnSM *txn_sm = (TxnSM *) INKContDataGet(contp);

  INKDebug("protocol", "enter state_send_request_to_server");

  switch (event) {
  case INK_EVENT_VCONN_WRITE_READY:
    INKVIOReenable(vio);
    break;
  case INK_EVENT_VCONN_WRITE_COMPLETE:
    vio = NULL;

    /* Waiting for the incoming response. */
    set_handler(txn_sm->q_current_handler, &state_interface_with_server);
    txn_sm->q_server_read_vio = INKVConnRead(txn_sm->q_server_vc, contp, txn_sm->q_server_response_buffer, INT_MAX);
    break;

    /* it could be failure of INKNetConnect */
  default:
    return prepare_to_die(contp);
  }
  return INK_SUCCESS;
}

/* Call correct handler according to the vio type. */
int
state_interface_with_server(INKCont contp, INKEvent event, INKVIO vio)
{
  TxnSM *txn_sm = (TxnSM *) INKContDataGet(contp);

  INKDebug("protocol", "enter state_interface_with_server");

  txn_sm->q_pending_action = NULL;

  switch (event) {
    /* This is returned from cache_vc. */
  case INK_EVENT_VCONN_WRITE_READY:
  case INK_EVENT_VCONN_WRITE_COMPLETE:
    return state_write_to_cache(contp, event, vio);
    /* Otherwise, handle events from server. */
  case INK_EVENT_VCONN_READ_READY:
    /* Actually, we shouldn't get READ_COMPLETE because we set bytes
       count to be INT_MAX. */
  case INK_EVENT_VCONN_READ_COMPLETE:
    return state_read_response_from_server(contp, event, vio);

    /* all data of the response come in. */
  case INK_EVENT_VCONN_EOS:
    INKDebug("protocol", "get server eos");
    /* There is no more use of server_vc, close it. */
    if (txn_sm->q_server_vc) {
      INKVConnClose(txn_sm->q_server_vc);
      txn_sm->q_server_vc = NULL;
    }
    txn_sm->q_server_read_vio = NULL;
    txn_sm->q_server_write_vio = NULL;

    /* Check if the response is good */
    if (txn_sm->q_server_response_length == 0) {
      /* This is the bad response. Close client_vc. */
      if (txn_sm->q_client_vc) {
        INKVConnClose(txn_sm->q_client_vc);
        txn_sm->q_client_vc = NULL;
      }
      txn_sm->q_client_read_vio = NULL;
      txn_sm->q_client_write_vio = NULL;

      /* Close cache_vc as well. */
      if (txn_sm->q_cache_vc) {
        INKVConnClose(txn_sm->q_cache_vc);
        txn_sm->q_cache_vc = NULL;
      }
      txn_sm->q_cache_write_vio = NULL;
      return state_done(contp, 0, NULL);
    }

    if (txn_sm->q_cache_response_length >= txn_sm->q_server_response_length) {
      /* Write is complete, close the cache_vc. */
      INKVConnClose(txn_sm->q_cache_vc);
      txn_sm->q_cache_vc = NULL;
      txn_sm->q_cache_write_vio = NULL;
      INKIOBufferReaderFree(txn_sm->q_cache_response_buffer_reader);

      /* Open cache_vc to read data and send to client. */
      set_handler(txn_sm->q_current_handler, &state_handle_cache_lookup);
      txn_sm->q_pending_action = INKCacheRead(contp, txn_sm->q_key);
    } else {                    /* not done with writing into cache */

      INKDebug("protocol", "cache_response_length is %d, server response length is %d", txn_sm->q_cache_response_length,
               txn_sm->q_server_response_length);
      INKVIOReenable(txn_sm->q_cache_write_vio);
    }

  default:
    break;
  }

  return INK_SUCCESS;
}

/* The response comes in. If the origin server finishes writing, it
   will close the socket, so the event returned from the net_vc is
   INK_EVENT_VCONN_EOS. By this event, TxnSM knows all data of the
   response arrives and so parse it, save a copy in the cache and
   send the doc to the client. If reading is not done, reenable the
   read_vio. */
int
state_read_response_from_server(INKCont contp, INKEvent event, INKVIO vio)
{
  TxnSM *txn_sm = (TxnSM *) INKContDataGet(contp);
  int bytes_read = 0;

  INKDebug("protocol", "enter state_read_response_from_server");

  bytes_read = INKIOBufferReaderAvail(txn_sm->q_cache_response_buffer_reader);

  if ((bytes_read > 0) && (txn_sm->q_cache_vc)) {
    /* If this is the first write, do INKVConnWrite, otherwise, simply
       reenable q_cache_write_vio. */
    if (txn_sm->q_server_response_length == 0) {
      txn_sm->q_cache_write_vio = INKVConnWrite(txn_sm->q_cache_vc,
                                                contp, txn_sm->q_cache_response_buffer_reader, bytes_read);
    } else {
      INKAssert(txn_sm->q_server_response_length > 0);
      INKVIOReenable(txn_sm->q_cache_write_vio);
      txn_sm->q_block_bytes_read = bytes_read;
/*
	    txn_sm->q_cache_write_vio = INKVConnWrite (txn_sm->q_cache_vc,
						       contp,
						       txn_sm->q_cache_response_buffer_reader,
						       bytes_read);
						       */
    }
  }

  txn_sm->q_server_response_length += bytes_read;
  INKDebug("protocol", "bytes read is %d, total response length is %d", bytes_read, txn_sm->q_server_response_length);

  return INK_SUCCESS;
}

/* If the whole doc has been written into the cache, send the response
   to the client, otherwise, reenable the read_vio. */
int
state_write_to_cache(INKCont contp, INKEvent event, INKVIO vio)
{
  TxnSM *txn_sm = (TxnSM *) INKContDataGet(contp);

  INKDebug("protocol", "enter state_write_to_cache");

  switch (event) {
  case INK_EVENT_VCONN_WRITE_READY:
    INKVIOReenable(txn_sm->q_cache_write_vio);
    return INK_SUCCESS;

  case INK_EVENT_VCONN_WRITE_COMPLETE:
    INKDebug("protocol", "nbytes %d, ndone %d", INKVIONBytesGet(vio), INKVIONDoneGet(vio));
    /* Since the first write is through INKVConnWrite, which aleady consume
       the data in cache_buffer_reader, don't consume it again. */
    if (txn_sm->q_cache_response_length > 0 && txn_sm->q_block_bytes_read > 0)
      INKIOBufferReaderConsume(txn_sm->q_cache_response_buffer_reader, txn_sm->q_block_bytes_read);

    txn_sm->q_cache_response_length += INKVIONBytesGet(vio);

    /* If not all data have been read in, we have to reenable the read_vio */
    if (txn_sm->q_server_vc != NULL) {
      INKDebug("protocol", "reenable server_read_vio");
      INKVIOReenable(txn_sm->q_server_read_vio);
      return INK_SUCCESS;
    }

    if (txn_sm->q_cache_response_length >= txn_sm->q_server_response_length) {
      /* Write is complete, close the cache_vc. */
      INKDebug("protocol", "close cache_vc, cache_response_length is %d, server_response_lenght is %d",
               txn_sm->q_cache_response_length, txn_sm->q_server_response_length);
      INKVConnClose(txn_sm->q_cache_vc);
      txn_sm->q_cache_vc = NULL;
      txn_sm->q_cache_write_vio = NULL;
      INKIOBufferReaderFree(txn_sm->q_cache_response_buffer_reader);

      /* Open cache_vc to read data and send to client. */
      set_handler(txn_sm->q_current_handler, &state_handle_cache_lookup);
      txn_sm->q_pending_action = INKCacheRead(contp, txn_sm->q_key);
    } else {                    /* not done with writing into cache */

      INKDebug("protocol", "reenable cache_write_vio");
      INKVIOReenable(txn_sm->q_cache_write_vio);
    }
    return INK_SUCCESS;
  default:
    break;
  }

  /* Something wrong if getting here. */
  return prepare_to_die(contp);
}

/* If the response has been fully written into the client_vc,
   which means this txn is done, close the client_vc. Otherwise,
   reenable the write_vio. */
int
state_send_response_to_client(INKCont contp, INKEvent event, INKVIO vio)
{
  TxnSM *txn_sm = (TxnSM *) INKContDataGet(contp);

  INKDebug("protocol", "enter state_send_response_to_client");

  switch (event) {
  case INK_EVENT_VCONN_WRITE_READY:
    INKDebug("protocol", " . wr ready");
    INKDebug("protocol", "write_ready: nbytes %d, ndone %d", INKVIONBytesGet(vio), INKVIONDoneGet(vio));
    INKVIOReenable(txn_sm->q_client_write_vio);
    break;

  case INK_EVENT_VCONN_WRITE_COMPLETE:
    INKDebug("protocol", " . wr complete");
    INKDebug("protocol", "write_complete: nbytes %d, ndone %d", INKVIONBytesGet(vio), INKVIONDoneGet(vio));
    /* Finished sending all data to client, close client_vc. */
    if (txn_sm->q_client_vc) {
      INKVConnClose(txn_sm->q_client_vc);
      txn_sm->q_client_vc = NULL;
    }
    txn_sm->q_client_read_vio = NULL;
    txn_sm->q_client_write_vio = NULL;

    return state_done(contp, 0, NULL);

  default:
    INKDebug("protocol", " . default handler");
    return prepare_to_die(contp);

  }

  INKDebug("protocol", "leaving send_response_to_client");

  return INK_SUCCESS;
}


/* There is something wrong, abort client, server and cache vc
   if they exist. */
int
prepare_to_die(INKCont contp)
{
  TxnSM *txn_sm = (TxnSM *) INKContDataGet(contp);

  INKDebug("protocol", "enter prepare_to_die");
  if (txn_sm->q_client_vc) {
    INKVConnAbort(txn_sm->q_client_vc, 1);
    txn_sm->q_client_vc = NULL;
  }
  txn_sm->q_client_read_vio = NULL;
  txn_sm->q_client_write_vio = NULL;

  if (txn_sm->q_server_vc) {
    INKVConnAbort(txn_sm->q_server_vc, 1);
    txn_sm->q_server_vc = NULL;
  }
  txn_sm->q_server_read_vio = NULL;
  txn_sm->q_server_write_vio = NULL;

  if (txn_sm->q_cache_vc) {
    INKVConnAbort(txn_sm->q_cache_vc, 1);
    txn_sm->q_cache_vc = NULL;
  }
  txn_sm->q_cache_read_vio = NULL;
  txn_sm->q_cache_write_vio = NULL;

  return state_done(contp, 0, NULL);
}

int
state_done(INKCont contp, INKEvent event, INKVIO vio)
{
  TxnSM *txn_sm = (TxnSM *) INKContDataGet(contp);

  INKDebug("protocol", "enter state_done");

  if (txn_sm->q_pending_action && !INKActionDone(txn_sm->q_pending_action)) {
    INKDebug("protocol", "cancelling pending action %p", txn_sm->q_pending_action);
    INKActionCancel(txn_sm->q_pending_action);
  } else if (txn_sm->q_pending_action) {
    INKDebug("protocol", "action is done %p", txn_sm->q_pending_action);
  }

  txn_sm->q_pending_action = NULL;
  txn_sm->q_mutex = NULL;

  if (txn_sm->q_client_request_buffer) {
    if (txn_sm->q_client_request_buffer_reader)
      INKIOBufferReaderFree(txn_sm->q_client_request_buffer_reader);
    INKIOBufferDestroy(txn_sm->q_client_request_buffer);
    txn_sm->q_client_request_buffer = NULL;
    txn_sm->q_client_request_buffer_reader = NULL;
  }

  if (txn_sm->q_client_response_buffer) {
    if (txn_sm->q_client_response_buffer_reader)
      INKIOBufferReaderFree(txn_sm->q_client_response_buffer_reader);

    INKIOBufferDestroy(txn_sm->q_client_response_buffer);
    txn_sm->q_client_response_buffer = NULL;
    txn_sm->q_client_response_buffer_reader = NULL;
  }

  if (txn_sm->q_cache_read_buffer) {
    if (txn_sm->q_cache_read_buffer_reader)
      INKIOBufferReaderFree(txn_sm->q_cache_read_buffer_reader);
    INKIOBufferDestroy(txn_sm->q_cache_read_buffer);
    txn_sm->q_cache_read_buffer = NULL;
    txn_sm->q_cache_read_buffer_reader = NULL;
  }

  if (txn_sm->q_server_request_buffer) {
    if (txn_sm->q_server_request_buffer_reader)
      INKIOBufferReaderFree(txn_sm->q_server_request_buffer_reader);
    INKIOBufferDestroy(txn_sm->q_server_request_buffer);
    txn_sm->q_server_request_buffer = NULL;
    txn_sm->q_server_request_buffer_reader = NULL;
  }

  if (txn_sm->q_server_response_buffer) {
    INKIOBufferDestroy(txn_sm->q_server_response_buffer);
    txn_sm->q_server_response_buffer = NULL;
  }

  if (txn_sm->q_server_name) {
    INKfree(txn_sm->q_server_name);
    txn_sm->q_server_name = NULL;
  }

  if (txn_sm->q_file_name) {
    INKfree(txn_sm->q_file_name);
    txn_sm->q_file_name = NULL;
  }

  if (txn_sm->q_key)
    INKCacheKeyDestroy(txn_sm->q_key);

  if (txn_sm->q_client_request) {
    INKfree(txn_sm->q_client_request);
    txn_sm->q_client_request = NULL;
  }

  if (txn_sm->q_server_response) {
    INKfree(txn_sm->q_server_response);
    txn_sm->q_server_response = NULL;
  }

  if (txn_sm) {
    txn_sm->q_magic = TXN_SM_DEAD;
    INKfree(txn_sm);
  }
  INKContDestroy(contp);
  return INK_EVENT_NONE;
}

/* Write the data into the client_vc. */
int
send_response_to_client(INKCont contp)
{
  TxnSM *txn_sm;
  int response_len;

  INKDebug("protocol", "enter send_response_to_client");

  txn_sm = (TxnSM *) INKContDataGet(contp);
  response_len = INKIOBufferReaderAvail(txn_sm->q_client_response_buffer_reader);

  INKDebug("protocol", " . resp_len is %d, response_len");

  set_handler(txn_sm->q_current_handler, &state_interface_with_client);
  txn_sm->q_client_write_vio = INKVConnWrite(txn_sm->q_client_vc, (INKCont) contp,
                                             txn_sm->q_client_response_buffer_reader, response_len);
  return INK_SUCCESS;
}

/* Read data out through the_reader and save it in a char buffer. */
char *
get_info_from_buffer(INKIOBufferReader the_reader)
{
  char *info;
  char *info_start;

  int read_avail, read_done;
  INKIOBufferBlock blk;
  char *buf;

  if (!the_reader)
    return NULL;

  read_avail = INKIOBufferReaderAvail(the_reader);

  info = (char *) INKmalloc(sizeof(char) * read_avail);
  if (info == NULL)
    return NULL;
  info_start = info;

  /* Read the data out of the reader */
  while (read_avail > 0) {
    blk = INKIOBufferReaderStart(the_reader);
    buf = (char *) INKIOBufferBlockReadStart(blk, the_reader, &read_done);
    memcpy(info, buf, read_done);
    if (read_done > 0) {
      INKIOBufferReaderConsume(the_reader, read_done);
      read_avail -= read_done;
      info += read_done;
    }
  }

  return info_start;
}

/* Check if the end token is in the char buffer. */
int
is_request_end(char *buf)
{
  char *temp = strstr(buf, " \n\n");
  if (!temp)
    return 0;
  return 1;
}

/* Parse the server_name and file name from the request. */
int
parse_request(char *request, char *server_name, char *file_name)
{
  char *temp = strtok(request, " ");
  if (temp != NULL)
    strcpy(server_name, temp);
  else
    return 0;

  temp = strtok(NULL, " ");
  if (temp != NULL)
    strcpy(file_name, temp);
  else
    return 0;

  return 1;
}

/* Create 128-bit cache key based on the input string, in this case,
   the file_name of the requested doc. */
INKCacheKey
CacheKeyCreate(char *file_name)
{
  INKCacheKey key;
  INKReturnCode return_code;

  /* INKCacheKeyCreate is to allocate memory space for the key */
  return_code = INKCacheKeyCreate(&key);
  if (return_code != INK_SUCCESS) {
    INKError("Can't create cache key");
    return (INKCacheKey) INK_ERROR_PTR;
  }

  /* INKCacheKeyDigestSet is to compute INKCackeKey from the input string */
  INKCacheKeyDigestSet(key, (unsigned char *) file_name, strlen(file_name));
  return key;
}
