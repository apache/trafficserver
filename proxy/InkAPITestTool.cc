/** @file

  Implements unit test for SDK APIs

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

#include "Regression.h"
#include "api/ts/ts.h"

#include <arpa/inet.h>          /* For htonl */
#include "P_Net.h"

#define SDBG_TAG "SockServer"
#define CDBG_TAG "SockClient"

#ifndef MIN
#define MIN(_x, _y) ((_x < _y) ? _x : _y)
#endif
#define IP(a,b,c,d) htonl((a) << 24 | (b) << 16 | (c) << 8 | (d))

#define SET_TEST_HANDLER(_d, _s) {_d = _s;}

#define MAGIC_ALIVE 0xfeedbaba
#define MAGIC_DEAD  0xdeadbeef

#define SYNSERVER_LISTEN_PORT  3300

#define PROXY_CONFIG_NAME_HTTP_PORT "proxy.config.http.server_port"
#define PROXY_HTTP_DEFAULT_PORT 8080

#define REQUEST_MAX_SIZE  4095
#define RESPONSE_MAX_SIZE 4095

#define HTTP_REQUEST_END "\r\n\r\n"

// each request/response includes an identifier as a Mime field
#define X_REQUEST_ID  "X-Request-ID"
#define X_RESPONSE_ID "X-Response-ID"

#define ERROR_BODY "TESTING ERROR PAGE"
#define TRANSFORM_APPEND_STRING "This is a transformed response"

//////////////////////////////////////////////////////////////////////////////
// STRUCTURES
//////////////////////////////////////////////////////////////////////////////

typedef int (*TxnHandler) (INKCont contp, INKEvent event, void *data);

/* Server transaction structure */
typedef struct
{
  INKVConn vconn;

  INKVIO read_vio;
  INKIOBuffer req_buffer;
  INKIOBufferReader req_reader;

  INKVIO write_vio;
  INKIOBuffer resp_buffer;
  INKIOBufferReader resp_reader;

  char request[REQUEST_MAX_SIZE + 1];
  int request_len;

  TxnHandler current_handler;
  unsigned int magic;
} ServerTxn;

/* Server structure */
typedef struct
{
  int accept_port;
  INKAction accept_action;
  INKCont accept_cont;
  unsigned int magic;
} SocketServer;

typedef enum
{
  REQUEST_SUCCESS,
  REQUEST_INPROGRESS,
  REQUEST_FAILURE
} RequestStatus;

/* Client structure */
typedef struct
{
  INKVConn vconn;

  INKVIO read_vio;
  INKIOBuffer req_buffer;
  INKIOBufferReader req_reader;

  INKVIO write_vio;
  INKIOBuffer resp_buffer;
  INKIOBufferReader resp_reader;

  char *request;
  char response[RESPONSE_MAX_SIZE + 1];
  int response_len;

  RequestStatus status;

  int connect_port;
  int local_port;
  uint64 connect_ip;
  INKAction connect_action;

  TxnHandler current_handler;

  unsigned int magic;
} ClientTxn;


//////////////////////////////////////////////////////////////////////////////
// DECLARATIONS
//////////////////////////////////////////////////////////////////////////////

/* utility */
static char *get_body_ptr(const char *request);
static char *generate_request(int test_case);
static char *generate_response(const char *request);
static int get_request_id(INKHttpTxn txnp);


/* client side */
static ClientTxn *synclient_txn_create(void);
static int synclient_txn_delete(ClientTxn * txn);
static int synclient_txn_close(INKCont contp);
static int synclient_txn_send_request(ClientTxn * txn, char *request);
static int synclient_txn_send_request_to_vc(ClientTxn * txn, char *request, INKVConn vc);
static int synclient_txn_read_response(INKCont contp);
static int synclient_txn_read_response_handler(INKCont contp, INKEvent event, void *data);
static int synclient_txn_write_request(INKCont contp);
static int synclient_txn_write_request_handler(INKCont contp, INKEvent event, void *data);
static int synclient_txn_connect_handler(INKCont contp, INKEvent event, void *data);
static int synclient_txn_main_handler(INKCont contp, INKEvent event, void *data);

/* Server side */
SocketServer *synserver_create(int port);
static int synserver_start(SocketServer * s);
static int synserver_stop(SocketServer * s);
static int synserver_delete(SocketServer * s);
static int synserver_accept_handler(INKCont contp, INKEvent event, void *data);
static int synserver_txn_close(INKCont contp);
static int synserver_txn_write_response(INKCont contp);
static int synserver_txn_write_response_handler(INKCont contp, INKEvent event, void *data);
static int synserver_txn_read_request(INKCont contp);
static int synserver_txn_read_request_handler(INKCont contp, INKEvent event, void *data);
static int synserver_txn_main_handler(INKCont contp, INKEvent event, void *data);

//////////////////////////////////////////////////////////////////////////////
// REQUESTS/RESPONSES GENERATION
//////////////////////////////////////////////////////////////////////////////

static char *
get_body_ptr(const char *request)
{
  char *ptr = (char *) strstr((const char *) request, (const char *) "\r\n\r\n");
  return (ptr != NULL) ? (ptr + 4) : NULL;
}


/* Caller must free returned request */
static char *
generate_request(int test_case)
{

// We define request formats.
// Each format has an X-Request-ID field that contains the id of the testcase
#define HTTP_REQUEST_DEFAULT_FORMAT  "GET http://localhost:%d/default.html HTTP/1.0\r\n" \
                                     "X-Request-ID: %d\r\n" \
				     "\r\n"

#define HTTP_REQUEST_FORMAT1 "GET http://localhost:%d/format1.html HTTP/1.0\r\n" \
			     "X-Request-ID: %d\r\n" \
			     "\r\n"

#define HTTP_REQUEST_FORMAT2 "GET http://localhost:%d/format2.html HTTP/1.0\r\n" \
			     "X-Request-ID: %d\r\n" \
                             "Content-Type: text/html\r\n" \
			     "\r\n"
#define HTTP_REQUEST_FORMAT3 "GET http://localhost:%d/format3.html/ HTTP/1.0\r\n" \
			     "X-Request-ID: %d\r\n" \
			     "Response: Error\r\n" \
			     "\r\n"
#define HTTP_REQUEST_FORMAT4 "GET http://localhost:%d/format4.html/ HTTP/1.0\r\n" \
			     "X-Request-ID: %d\r\n" \
                             "Request:%d\r\n" \
                             "\r\n"
#define HTTP_REQUEST_FORMAT5 "GET http://localhost:%d/format5.html/ HTTP/1.0\r\n" \
			     "X-Request-ID: %d\r\n" \
                             "Request:%d\r\n" \
                             "\r\n"
#define HTTP_REQUEST_FORMAT6 "GET http://localhost:%d/format.html/ HTTP/1.0\r\n" \
			     "X-Request-ID: %d\r\n" \
                             "Accept-Language:English\r\n" \
                             "\r\n"
#define HTTP_REQUEST_FORMAT7 "GET http://localhost:%d/format.html/ HTTP/1.0\r\n" \
			     "X-Request-ID: %d\r\n" \
                             "Accept-Language:French\r\n" \
                             "\r\n"
#define HTTP_REQUEST_FORMAT8 "GET http://localhost:%d/format.html/ HTTP/1.0\r\n" \
			     "X-Request-ID: %d\r\n" \
                             "Accept-Language:English,French\r\n" \
                             "\r\n"
#define HTTP_REQUEST_FORMAT9 "GET http://www.inktomi.com/format9.html HTTP/1.0\r\n" \
			     "X-Request-ID: %d\r\n" \
                             "\r\n"
#define HTTP_REQUEST_FORMAT10 "GET http://www.inktomi.com/format10.html HTTP/1.0\r\n" \
			      "X-Request-ID: %d\r\n" \
                              "\r\n"

  char *request = (char *) INKmalloc(REQUEST_MAX_SIZE + 1);

  switch (test_case) {
  case 1:
    snprintf(request, REQUEST_MAX_SIZE + 1, HTTP_REQUEST_FORMAT1, SYNSERVER_LISTEN_PORT, test_case);
    break;
  case 2:
    snprintf(request, REQUEST_MAX_SIZE + 1, HTTP_REQUEST_FORMAT2, SYNSERVER_LISTEN_PORT, test_case);
    break;
  case 3:
    snprintf(request, REQUEST_MAX_SIZE + 1, HTTP_REQUEST_FORMAT3, SYNSERVER_LISTEN_PORT, test_case);
    break;
  case 4:
    snprintf(request, REQUEST_MAX_SIZE + 1, HTTP_REQUEST_FORMAT4, SYNSERVER_LISTEN_PORT, test_case, 1);
    break;
  case 5:
    snprintf(request, REQUEST_MAX_SIZE + 1, HTTP_REQUEST_FORMAT5, SYNSERVER_LISTEN_PORT, test_case, 2);
    break;
  case 6:
    snprintf(request, REQUEST_MAX_SIZE + 1, HTTP_REQUEST_FORMAT6, SYNSERVER_LISTEN_PORT, test_case);
    break;
  case 7:
    snprintf(request, REQUEST_MAX_SIZE + 1, HTTP_REQUEST_FORMAT7, SYNSERVER_LISTEN_PORT, test_case - 1);
    break;
  case 8:
    snprintf(request, REQUEST_MAX_SIZE + 1, HTTP_REQUEST_FORMAT8, SYNSERVER_LISTEN_PORT, test_case - 2);
    break;
  case 9:
    snprintf(request, REQUEST_MAX_SIZE + 1, HTTP_REQUEST_FORMAT9, test_case);
    break;
  case 10:
    snprintf(request, REQUEST_MAX_SIZE + 1, HTTP_REQUEST_FORMAT10, test_case);
    break;
  default:
    snprintf(request, REQUEST_MAX_SIZE + 1, HTTP_REQUEST_DEFAULT_FORMAT, SYNSERVER_LISTEN_PORT, test_case);
    break;
  }

  return request;
}


/* Caller must free returned response */
static char *
generate_response(const char *request)
{
// define format for response
// Each response contains a field X-Response-ID that contains the id of the testcase
#define HTTP_REQUEST_TESTCASE_FORMAT "GET %1024s HTTP/1.%d\r\n" \
                                     "X-Request-ID: %d\r\n"

#define HTTP_RESPONSE_DEFAULT_FORMAT "HTTP/1.0 200 OK\r\n" \
			      "X-Response-ID: %d\r\n" \
                              "Content-Type: text/html\r\n" \
			      "\r\n" \
			      "Default body"

#define HTTP_RESPONSE_FORMAT1 "HTTP/1.0 200 OK\r\n" \
			      "X-Response-ID: %d\r\n" \
                              "Content-Type: text/html\r\n" \
			      "Cache-Control: no-cache\r\n" \
			      "\r\n" \
			      "Body for response 1"

#define HTTP_RESPONSE_FORMAT2 "HTTP/1.0 200 OK\r\n" \
			      "X-Response-ID: %d\r\n" \
                              "Content-Type: text/html\r\n" \
			      "\r\n" \
			      "Body for response 2"
#define HTTP_RESPONSE_FORMAT4 "HTTP/1.0 200 OK\r\n" \
			      "X-Response-ID: %d\r\n" \
                              "Content-Type: text/html\r\n" \
                              "\r\n" \
                              "Body for response 4"
#define HTTP_RESPONSE_FORMAT5 "HTTP/1.0 200 OK\r\n" \
			      "X-Response-ID: %d\r\n" \
                              "Content-Type: text/html\r\n" \
                              "\r\n" \
                              "Body for response 5"
#define HTTP_RESPONSE_FORMAT6 "HTTP/1.0 200 OK\r\n" \
			      "X-Response-ID: %d\r\n" \
                              "Content-Language:English\r\n" \
                              "\r\n" \
                              "Body for response 6"
#define HTTP_RESPONSE_FORMAT7 "HTTP/1.0 200 OK\r\n" \
			      "X-Response-ID: %d\r\n" \
                              "Content-Language:French\r\n" \
                              "\r\n" \
                              "Body for response 7"

#define HTTP_RESPONSE_FORMAT8 "HTTP/1.0 200 OK\r\n" \
			      "X-Response-ID: %d\r\n" \
                              "Content-Language:French, English\r\n" \
                              "\r\n" \
                              "Body for response 8"

#define HTTP_RESPONSE_FORMAT9 "HTTP/1.0 200 OK\r\n" \
			      "X-Response-ID: %d\r\n" \
                              "\r\n" \
                              "Body for response 9"

#define HTTP_RESPONSE_FORMAT10 "HTTP/1.0 200 OK\r\n" \
			      "X-Response-ID: %d\r\n" \
                              "\r\n" \
                              "Body for response 10"


  int test_case, match, http_version;

  char *response = (char *) INKmalloc(RESPONSE_MAX_SIZE + 1);
  char url[1024];

  // coverity[secure_coding]
  match = sscanf(request, HTTP_REQUEST_TESTCASE_FORMAT, url, &http_version, &test_case);
  if (match == 3) {
    switch (test_case) {
    case 1:
      snprintf(response, RESPONSE_MAX_SIZE + 1, HTTP_RESPONSE_FORMAT1, test_case);
      break;
    case 2:
      snprintf(response, RESPONSE_MAX_SIZE + 1, HTTP_RESPONSE_FORMAT2, test_case);
      break;
    case 4:
      snprintf(response, RESPONSE_MAX_SIZE + 1, HTTP_RESPONSE_FORMAT4, test_case);
      break;
    case 5:
      snprintf(response, RESPONSE_MAX_SIZE + 1, HTTP_RESPONSE_FORMAT5, test_case);
      break;
    case 6:
      snprintf(response, RESPONSE_MAX_SIZE + 1, HTTP_RESPONSE_FORMAT6, test_case);
      break;
    case 7:
      snprintf(response, RESPONSE_MAX_SIZE + 1, HTTP_RESPONSE_FORMAT7, test_case);
      break;
    case 8:
      snprintf(response, RESPONSE_MAX_SIZE + 1, HTTP_RESPONSE_FORMAT8, test_case);
      break;
    case 9:
      snprintf(response, RESPONSE_MAX_SIZE + 1, HTTP_RESPONSE_FORMAT9, test_case);
      break;
    case 10:
      snprintf(response, RESPONSE_MAX_SIZE + 1, HTTP_RESPONSE_FORMAT10, test_case);
      break;
    default:
      snprintf(response, RESPONSE_MAX_SIZE + 1, HTTP_RESPONSE_DEFAULT_FORMAT, test_case);
      break;
    }
  } else {
    /* Didin't recognize a testcase request. send the default response */
    snprintf(response, RESPONSE_MAX_SIZE + 1, HTTP_RESPONSE_DEFAULT_FORMAT, test_case);
  }

  return response;
}


// This routine can be called by tests, from the READ_REQUEST_HDR_HOOK
// to figure out the id of a test message
// Returns id/-1 in case of error
static int
get_request_id(INKHttpTxn txnp)
{
  INKMBuffer bufp;
  INKMLoc hdr_loc, id_loc;
  int id = -1;
  int ret_val;

  if (!INKHttpTxnClientReqGet(txnp, &bufp, &hdr_loc)) {
    return -1;
  }

  id_loc = INKMimeHdrFieldFind(bufp, hdr_loc, X_REQUEST_ID, -1);
  if ((id_loc == INK_NULL_MLOC) || (id_loc == INK_ERROR_PTR)) {
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
    return -1;
  }

  ret_val = INKMimeHdrFieldValueIntGet(bufp, hdr_loc, id_loc, 0, &id);
  if (ret_val == INK_ERROR) {
    INKHandleMLocRelease(bufp, hdr_loc, id_loc);
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
    return -1;
  }

  INKHandleMLocRelease(bufp, hdr_loc, id_loc);
  INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
  return id;
}



//////////////////////////////////////////////////////////////////////////////
// SOCKET CLIENT
//////////////////////////////////////////////////////////////////////////////

static ClientTxn *
synclient_txn_create(void)
{
  INKMgmtInt proxy_port;

  ClientTxn *txn = (ClientTxn *) INKmalloc(sizeof(ClientTxn));
  if (!INKMgmtIntGet(PROXY_CONFIG_NAME_HTTP_PORT, &proxy_port)) {
    proxy_port = PROXY_HTTP_DEFAULT_PORT;
  }
  txn->connect_port = (int) proxy_port;
  txn->local_port = (int) 0;
  txn->connect_ip = IP(127, 0, 0, 1);
  txn->status = REQUEST_INPROGRESS;
  txn->request = NULL;
  txn->vconn = NULL;
  txn->req_buffer = NULL;
  txn->req_reader = NULL;
  txn->resp_buffer = NULL;
  txn->resp_reader = NULL;
  txn->magic = MAGIC_ALIVE;
  txn->connect_action = NULL;

  INKDebug(CDBG_TAG, "Connecting to proxy localhost on port %d", (int) proxy_port);
  return txn;
}

static int
synclient_txn_delete(ClientTxn * txn)
{
  INKAssert(txn->magic == MAGIC_ALIVE);
  if (txn->connect_action && !INKActionDone(txn->connect_action)) {
    INKActionCancel(txn->connect_action);
    txn->connect_action = NULL;
  }
  if (txn->request) {
    free(txn->request);
  }
  txn->magic = MAGIC_DEAD;
  INKfree(txn);
  return 1;
}

static int
synclient_txn_close(INKCont contp)
{
  ClientTxn *txn = (ClientTxn *) INKContDataGet(contp);
  INKAssert(txn->magic == MAGIC_ALIVE);

  if (txn->vconn != NULL) {
    INKVConnClose(txn->vconn);
  }
  if (txn->req_buffer != NULL) {
    INKIOBufferDestroy(txn->req_buffer);
  }
  if (txn->resp_buffer != NULL) {
    INKIOBufferDestroy(txn->resp_buffer);
  }

  INKContDestroy(contp);

  INKDebug(CDBG_TAG, "Client Txn destroyed");
  return INK_EVENT_IMMEDIATE;
}

static int
synclient_txn_send_request(ClientTxn * txn, char *request)
{
  INKCont cont;
  INKAssert(txn->magic == MAGIC_ALIVE);
  txn->request = strdup(request);
  SET_TEST_HANDLER(txn->current_handler, synclient_txn_connect_handler);

  cont = INKContCreate(synclient_txn_main_handler, INKMutexCreate());
  INKContDataSet(cont, txn);
  INKNetConnect(cont, txn->connect_ip, txn->connect_port);
  return 1;
}

/* This can be used to send a request to a specific VC */
static int
synclient_txn_send_request_to_vc(ClientTxn * txn, char *request, INKVConn vc)
{
  INKCont cont;
  INKAssert(txn->magic == MAGIC_ALIVE);
  txn->request = strdup(request);
  SET_TEST_HANDLER(txn->current_handler, synclient_txn_connect_handler);

  cont = INKContCreate(synclient_txn_main_handler, INKMutexCreate());
  INKContDataSet(cont, txn);

  INKContCall(cont, INK_EVENT_NET_CONNECT, vc);
  return 1;
}


static int
synclient_txn_read_response(INKCont contp)
{
  ClientTxn *txn = (ClientTxn *) INKContDataGet(contp);
  INKAssert(txn->magic == MAGIC_ALIVE);

  INKIOBufferBlock block = INKIOBufferReaderStart(txn->resp_reader);
  while (block != NULL) {
    int64 blocklen;
    const char *blockptr = INKIOBufferBlockReadStart(block, txn->resp_reader, &blocklen);

    if (txn->response_len+blocklen <= RESPONSE_MAX_SIZE) {
      memcpy((char *) (txn->response + txn->response_len), blockptr, blocklen);
      txn->response_len += blocklen;
    } else {
      INKError("Error: Response length %d > response buffer size %d", txn->response_len+blocklen, RESPONSE_MAX_SIZE);
    }

    block = INKIOBufferBlockNext(block);
  }

  txn->response[txn->response_len + 1] = '\0';
  INKDebug(CDBG_TAG, "Response = |%s|, req len = %d", txn->response, txn->response_len);

  return 1;
}

static int
synclient_txn_read_response_handler(INKCont contp, INKEvent event, void *data)
{
  NOWARN_UNUSED(data);
  ClientTxn *txn = (ClientTxn *) INKContDataGet(contp);
  INKAssert(txn->magic == MAGIC_ALIVE);

  int avail;

  switch (event) {
  case INK_EVENT_VCONN_READ_READY:
  case INK_EVENT_VCONN_READ_COMPLETE:
    if (event == INK_EVENT_VCONN_READ_READY) {
      INKDebug(CDBG_TAG, "READ_READY");
    } else {
      INKDebug(CDBG_TAG, "READ_COMPLETE");
    }

    avail = INKIOBufferReaderAvail(txn->resp_reader);
    INKDebug(CDBG_TAG, "%d bytes available in buffer", avail);

    if (avail > 0) {
      synclient_txn_read_response(contp);
      INKIOBufferReaderConsume(txn->resp_reader, avail);
    }

    INKVIOReenable(txn->read_vio);
    break;

  case INK_EVENT_VCONN_EOS:
    INKDebug(CDBG_TAG, "READ_EOS");
    // Connection closed. In HTTP/1.0 it means we're done for this request.
    txn->status = REQUEST_SUCCESS;
    return synclient_txn_close(contp);
    break;

  case INK_EVENT_ERROR:
    INKDebug(CDBG_TAG, "READ_ERROR");
    txn->status = REQUEST_FAILURE;
    return synclient_txn_close(contp);
    break;

  default:
    INKAssert(!"Invalid event");
    break;
  }
  return 1;
}


static int
synclient_txn_write_request(INKCont contp)
{
  ClientTxn *txn = (ClientTxn *) INKContDataGet(contp);
  INKAssert(txn->magic == MAGIC_ALIVE);

  INKIOBufferBlock block;
  char *ptr_block;
  int64 len, ndone, ntodo, towrite, avail;

  len = strlen(txn->request);

  ndone = 0;
  ntodo = len;
  while (ntodo > 0) {
    block = INKIOBufferStart(txn->req_buffer);
    ptr_block = INKIOBufferBlockWriteStart(block, &avail);
    towrite = MIN(ntodo, avail);
    memcpy(ptr_block, txn->request + ndone, towrite);
    INKIOBufferProduce(txn->req_buffer, towrite);
    ntodo -= towrite;
    ndone += towrite;
  }

  /* Start writing the response */
  INKDebug(CDBG_TAG, "Writing |%s| (%d) bytes", txn->request, len);
  txn->write_vio = INKVConnWrite(txn->vconn, contp, txn->req_reader, len);

  return 1;
}

static int
synclient_txn_write_request_handler(INKCont contp, INKEvent event, void *data)
{
  NOWARN_UNUSED(data);
  ClientTxn *txn = (ClientTxn *) INKContDataGet(contp);
  INKAssert(txn->magic == MAGIC_ALIVE);

  switch (event) {
  case INK_EVENT_VCONN_WRITE_READY:
    INKDebug(CDBG_TAG, "WRITE_READY");
    INKVIOReenable(txn->write_vio);
    break;

  case INK_EVENT_VCONN_WRITE_COMPLETE:
    INKDebug(CDBG_TAG, "WRITE_COMPLETE");
    // Weird: synclient should not close the write part of vconn.
    // Otherwise some strangeness...

    /* Start reading */
    SET_TEST_HANDLER(txn->current_handler, synclient_txn_read_response_handler);
    txn->read_vio = INKVConnRead(txn->vconn, contp, txn->resp_buffer, INT_MAX);
    break;

  case INK_EVENT_VCONN_EOS:
    INKDebug(CDBG_TAG, "WRITE_EOS");
    txn->status = REQUEST_FAILURE;
    return synclient_txn_close(contp);
    break;

  case INK_EVENT_ERROR:
    INKDebug(CDBG_TAG, "WRITE_ERROR");
    txn->status = REQUEST_FAILURE;
    return synclient_txn_close(contp);
    break;

  default:
    INKAssert(!"Invalid event");
    break;
  }
  return INK_EVENT_IMMEDIATE;
}


static int
synclient_txn_connect_handler(INKCont contp, INKEvent event, void *data)
{
  INKAssert((event == INK_EVENT_NET_CONNECT) || (event == INK_EVENT_NET_CONNECT_FAILED));

  ClientTxn *txn = (ClientTxn *) INKContDataGet(contp);
  INKAssert(txn->magic == MAGIC_ALIVE);

  if (event == INK_EVENT_NET_CONNECT) {
    INKDebug(CDBG_TAG, "NET_CONNECT");

    txn->req_buffer = INKIOBufferCreate();
    txn->req_reader = INKIOBufferReaderAlloc(txn->req_buffer);
    txn->resp_buffer = INKIOBufferCreate();
    txn->resp_reader = INKIOBufferReaderAlloc(txn->resp_buffer);

    txn->response[0] = '\0';
    txn->response_len = 0;

    txn->vconn = (INKVConn) data;
    txn->local_port = (int) ((NetVConnection *) data)->get_local_port();

    txn->write_vio = NULL;
    txn->read_vio = NULL;

    /* start writing */
    SET_TEST_HANDLER(txn->current_handler, synclient_txn_write_request_handler);
    synclient_txn_write_request(contp);

    return INK_EVENT_IMMEDIATE;
  } else {
    INKDebug(CDBG_TAG, "NET_CONNECT_FAILED");
    txn->status = REQUEST_FAILURE;
    synclient_txn_close(contp);
  }

  return INK_EVENT_IMMEDIATE;
}


static int
synclient_txn_main_handler(INKCont contp, INKEvent event, void *data)
{
  ClientTxn *txn = (ClientTxn *) INKContDataGet(contp);
  INKAssert(txn->magic == MAGIC_ALIVE);

  TxnHandler handler = txn->current_handler;
  return (*handler) (contp, event, data);
}


//////////////////////////////////////////////////////////////////////////////
// SOCKET SERVER
//////////////////////////////////////////////////////////////////////////////

SocketServer *
synserver_create(int port)
{
  SocketServer *s = (SocketServer *) INKmalloc(sizeof(SocketServer));
  s->magic = MAGIC_ALIVE;
  s->accept_port = port;
  s->accept_action = NULL;
  s->accept_cont = INKContCreate(synserver_accept_handler, INKMutexCreate());
  INKContDataSet(s->accept_cont, s);
  return s;
}

static int
synserver_start(SocketServer * s)
{
  INKAssert(s->magic == MAGIC_ALIVE);
  s->accept_action = INKNetAccept(s->accept_cont, s->accept_port);
  return 1;
}

static int
synserver_stop(SocketServer * s)
{
  INKAssert(s->magic == MAGIC_ALIVE);
  if (s->accept_action && !INKActionDone(s->accept_action)) {
    INKActionCancel(s->accept_action);
    s->accept_action = NULL;
    INKDebug(SDBG_TAG, "Had to cancel action");
  }
  INKDebug(SDBG_TAG, "stopped");
  return 1;
}

static int
synserver_delete(SocketServer * s)
{
  INKAssert(s->magic == MAGIC_ALIVE);
  synserver_stop(s);

  if (s->accept_cont) {
    INKContDestroy(s->accept_cont);
    s->accept_cont = NULL;
    INKDebug(SDBG_TAG, "destroyed accept cont");
  }
  s->magic = MAGIC_DEAD;
  INKfree(s);
  INKDebug(SDBG_TAG, "deleted server");
  return 1;
}

static int
synserver_accept_handler(INKCont contp, INKEvent event, void *data)
{
  INKAssert((event == INK_EVENT_NET_ACCEPT) || (event == INK_EVENT_NET_ACCEPT_FAILED));

  SocketServer *s = (SocketServer *) INKContDataGet(contp);
  INKAssert(s->magic == MAGIC_ALIVE);

  if (event == INK_EVENT_NET_ACCEPT_FAILED) {
    ink_release_assert(!"Synserver must be able to bind to a port, check system netstat");
    INKDebug(SDBG_TAG, "NET_ACCEPT_FAILED");
    return INK_EVENT_IMMEDIATE;
  }

  INKDebug(SDBG_TAG, "NET_ACCEPT");

  /* Create a new transaction */
  ServerTxn *txn = (ServerTxn *) INKmalloc(sizeof(ServerTxn));
  txn->magic = MAGIC_ALIVE;

  SET_TEST_HANDLER(txn->current_handler, synserver_txn_read_request_handler);

  INKCont txn_cont = INKContCreate(synserver_txn_main_handler, INKMutexCreate());
  INKContDataSet(txn_cont, txn);

  txn->req_buffer = INKIOBufferCreate();
  txn->req_reader = INKIOBufferReaderAlloc(txn->req_buffer);

  txn->resp_buffer = INKIOBufferCreate();
  txn->resp_reader = INKIOBufferReaderAlloc(txn->resp_buffer);

  txn->request[0] = '\0';
  txn->request_len = 0;

  txn->vconn = (INKVConn) data;

  txn->write_vio = NULL;

  /* start reading */
  txn->read_vio = INKVConnRead(txn->vconn, txn_cont, txn->req_buffer, INT_MAX);

  return INK_EVENT_IMMEDIATE;
}


static int
synserver_txn_close(INKCont contp)
{
  ServerTxn *txn = (ServerTxn *) INKContDataGet(contp);
  INKAssert(txn->magic == MAGIC_ALIVE);

  if (txn->vconn != NULL) {
    INKVConnClose(txn->vconn);
  }
  if (txn->req_buffer) {
    INKIOBufferDestroy(txn->req_buffer);
  }
  if (txn->resp_buffer) {
    INKIOBufferDestroy(txn->resp_buffer);
  }

  txn->magic = MAGIC_DEAD;
  INKfree(txn);
  INKContDestroy(contp);

  INKDebug(SDBG_TAG, "Server Txn destroyed");
  return INK_EVENT_IMMEDIATE;
}


static int
synserver_txn_write_response(INKCont contp)
{
  ServerTxn *txn = (ServerTxn *) INKContDataGet(contp);
  INKAssert(txn->magic == MAGIC_ALIVE);

  SET_TEST_HANDLER(txn->current_handler, synserver_txn_write_response_handler);

  INKIOBufferBlock block;
  char *ptr_block;
  int64 len, ndone, ntodo, towrite, avail;
  char *response;

  response = generate_response(txn->request);
  len = strlen(response);

  ndone = 0;
  ntodo = len;
  while (ntodo > 0) {
    block = INKIOBufferStart(txn->resp_buffer);
    ptr_block = INKIOBufferBlockWriteStart(block, &avail);
    towrite = MIN(ntodo, avail);
    memcpy(ptr_block, response + ndone, towrite);
    INKIOBufferProduce(txn->resp_buffer, towrite);
    ntodo -= towrite;
    ndone += towrite;
  }

  /* Start writing the response */
  INKDebug(SDBG_TAG, "Writing response: |%s| (%d) bytes)", response, len);
  txn->write_vio = INKVConnWrite(txn->vconn, contp, txn->resp_reader, len);

  /* Now that response is in IOBuffer, free up response */
  INKfree(response);

  return INK_EVENT_IMMEDIATE;
}


static int
synserver_txn_write_response_handler(INKCont contp, INKEvent event, void *data)
{
  NOWARN_UNUSED(data);
  ServerTxn *txn = (ServerTxn *) INKContDataGet(contp);
  INKAssert(txn->magic == MAGIC_ALIVE);

  switch (event) {
  case INK_EVENT_VCONN_WRITE_READY:
    INKDebug(SDBG_TAG, "WRITE_READY");
    INKVIOReenable(txn->write_vio);
    break;

  case INK_EVENT_VCONN_WRITE_COMPLETE:
    INKDebug(SDBG_TAG, "WRITE_COMPLETE");
    INKVConnShutdown(txn->vconn, 0, 1);
    return synserver_txn_close(contp);
    break;

  case INK_EVENT_VCONN_EOS:
    INKDebug(SDBG_TAG, "WRITE_EOS");
    return synserver_txn_close(contp);
    break;

  case INK_EVENT_ERROR:
    INKDebug(SDBG_TAG, "WRITE_ERROR");
    return synserver_txn_close(contp);
    break;

  default:
    INKAssert(!"Invalid event");
    break;
  }
  return INK_EVENT_IMMEDIATE;
}


static int
synserver_txn_read_request(INKCont contp)
{
  ServerTxn *txn = (ServerTxn *) INKContDataGet(contp);
  INKAssert(txn->magic == MAGIC_ALIVE);

  int end;
  INKIOBufferBlock block = INKIOBufferReaderStart(txn->req_reader);

  while (block != NULL) {
    int64 blocklen;
    const char *blockptr = INKIOBufferBlockReadStart(block, txn->req_reader, &blocklen);

    if (txn->request_len+blocklen <= REQUEST_MAX_SIZE) {
      memcpy((char *) (txn->request + txn->request_len), blockptr, blocklen);
      txn->request_len += blocklen;
    } else {
      INKError("Error: Request length %d > request buffer size %d", txn->request_len+blocklen, REQUEST_MAX_SIZE);
    }

    block = INKIOBufferBlockNext(block);
  }

  txn->request[txn->request_len] = '\0';
  INKDebug(SDBG_TAG, "Request = |%s|, req len = %d", txn->request, txn->request_len);

  end = (strstr(txn->request, HTTP_REQUEST_END) != NULL);
  INKDebug(SDBG_TAG, "End of request = %d", end);

  return end;
}

static int
synserver_txn_read_request_handler(INKCont contp, INKEvent event, void *data)
{
  NOWARN_UNUSED(data);
  ServerTxn *txn = (ServerTxn *) INKContDataGet(contp);
  INKAssert(txn->magic == MAGIC_ALIVE);

  int avail, end_of_request;

  switch (event) {
  case INK_EVENT_VCONN_READ_READY:
  case INK_EVENT_VCONN_READ_COMPLETE:
    INKDebug(SDBG_TAG, (event == INK_EVENT_VCONN_READ_READY) ? "READ_READY" : "READ_COMPLETE");
    avail = INKIOBufferReaderAvail(txn->req_reader);
    INKDebug(SDBG_TAG, "%d bytes available in buffer", avail);

    if (avail > 0) {
      end_of_request = synserver_txn_read_request(contp);
      INKIOBufferReaderConsume(txn->req_reader, avail);

      if (end_of_request) {
        INKVConnShutdown(txn->vconn, 1, 0);
        return synserver_txn_write_response(contp);
      }
    }

    INKVIOReenable(txn->read_vio);
    break;

  case INK_EVENT_VCONN_EOS:
    INKDebug(SDBG_TAG, "READ_EOS");
    return synserver_txn_close(contp);
    break;

  case INK_EVENT_ERROR:
    INKDebug(SDBG_TAG, "READ_ERROR");
    return synserver_txn_close(contp);
    break;

  default:
    INKAssert(!"Invalid event");
    break;
  }
  return INK_EVENT_IMMEDIATE;
}


static int
synserver_txn_main_handler(INKCont contp, INKEvent event, void *data)
{
  ServerTxn *txn = (ServerTxn *) INKContDataGet(contp);
  INKAssert(txn->magic == MAGIC_ALIVE);

  TxnHandler handler = txn->current_handler;
  return (*handler) (contp, event, data);
}

