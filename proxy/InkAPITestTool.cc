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

#include "ts/Regression.h"
#include "api/ts/ts.h"

#include <arpa/inet.h> /* For htonl */
#include "P_Net.h"
#include <records/I_RecHttp.h>

#define SDBG_TAG "SockServer"
#define CDBG_TAG "SockClient"

#define IP(a, b, c, d) htonl((a) << 24 | (b) << 16 | (c) << 8 | (d))

#define SET_TEST_HANDLER(_d, _s) \
  {                              \
    _d = _s;                     \
  }

#define MAGIC_ALIVE 0xfeedbaba
#define MAGIC_DEAD 0xdeadbeef

#define SYNSERVER_LISTEN_PORT 3300
#define SYNSERVER_DUMMY_PORT -1

#define PROXY_CONFIG_NAME_HTTP_PORT "proxy.config.http.server_port"
#define PROXY_HTTP_DEFAULT_PORT 8080

#define REQUEST_MAX_SIZE 4095
#define RESPONSE_MAX_SIZE 4095

#define HTTP_REQUEST_END "\r\n\r\n"

// each request/response includes an identifier as a Mime field
#define X_REQUEST_ID "X-Request-ID"
#define X_RESPONSE_ID "X-Response-ID"

#define ERROR_BODY "TESTING ERROR PAGE"
#define TRANSFORM_APPEND_STRING "This is a transformed response"

//////////////////////////////////////////////////////////////////////////////
// STRUCTURES
//////////////////////////////////////////////////////////////////////////////

typedef int (*TxnHandler)(TSCont contp, TSEvent event, void *data);

/* Server transaction structure */
typedef struct {
  TSVConn vconn;

  TSVIO read_vio;
  TSIOBuffer req_buffer;
  TSIOBufferReader req_reader;

  TSVIO write_vio;
  TSIOBuffer resp_buffer;
  TSIOBufferReader resp_reader;

  char request[REQUEST_MAX_SIZE + 1];
  int request_len;

  TxnHandler current_handler;
  unsigned int magic;
} ServerTxn;

/* Server structure */
typedef struct {
  int accept_port;
  TSAction accept_action;
  TSCont accept_cont;
  unsigned int magic;
} SocketServer;

typedef enum {
  REQUEST_SUCCESS,
  REQUEST_INPROGRESS,
  REQUEST_FAILURE,
} RequestStatus;

/* Client structure */
typedef struct {
  TSVConn vconn;

  TSVIO read_vio;
  TSIOBuffer req_buffer;
  TSIOBufferReader req_reader;

  TSVIO write_vio;
  TSIOBuffer resp_buffer;
  TSIOBufferReader resp_reader;

  char *request;
  char response[RESPONSE_MAX_SIZE + 1];
  int response_len;

  RequestStatus status;

  int connect_port;
  int local_port;
  uint64_t connect_ip;
  TSAction connect_action;

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
static int get_request_id(TSHttpTxn txnp);

/* client side */
static ClientTxn *synclient_txn_create(void);
static int synclient_txn_delete(ClientTxn *txn);
static void synclient_txn_close(ClientTxn *txn);
static int synclient_txn_send_request(ClientTxn *txn, char *request);
static int synclient_txn_send_request_to_vc(ClientTxn *txn, char *request, TSVConn vc);
static int synclient_txn_read_response(TSCont contp);
static int synclient_txn_read_response_handler(TSCont contp, TSEvent event, void *data);
static int synclient_txn_write_request(TSCont contp);
static int synclient_txn_write_request_handler(TSCont contp, TSEvent event, void *data);
static int synclient_txn_connect_handler(TSCont contp, TSEvent event, void *data);
static int synclient_txn_main_handler(TSCont contp, TSEvent event, void *data);

/* Server side */
SocketServer *synserver_create(int port);
static int synserver_start(SocketServer *s);
static int synserver_stop(SocketServer *s);
static int synserver_delete(SocketServer *s);
static int synserver_vc_accept(TSCont contp, TSEvent event, void *data);
static int synserver_vc_refuse(TSCont contp, TSEvent event, void *data);
static int synserver_txn_close(TSCont contp);
static int synserver_txn_write_response(TSCont contp);
static int synserver_txn_write_response_handler(TSCont contp, TSEvent event, void *data);
static int synserver_txn_read_request(TSCont contp);
static int synserver_txn_read_request_handler(TSCont contp, TSEvent event, void *data);
static int synserver_txn_main_handler(TSCont contp, TSEvent event, void *data);

//////////////////////////////////////////////////////////////////////////////
// REQUESTS/RESPONSES GENERATION
//////////////////////////////////////////////////////////////////////////////

static char *
get_body_ptr(const char *request)
{
  char *ptr = (char *)strstr((const char *)request, (const char *)"\r\n\r\n");
  return (ptr != NULL) ? (ptr + 4) : NULL;
}

/* Caller must free returned request */
static char *
generate_request(int test_case)
{
// We define request formats.
// Each format has an X-Request-ID field that contains the id of the testcase
#define HTTP_REQUEST_DEFAULT_FORMAT                   \
  "GET http://127.0.0.1:%d/default.html HTTP/1.0\r\n" \
  "X-Request-ID: %d\r\n"                              \
  "\r\n"

#define HTTP_REQUEST_FORMAT1                          \
  "GET http://127.0.0.1:%d/format1.html HTTP/1.0\r\n" \
  "X-Request-ID: %d\r\n"                              \
  "\r\n"

#define HTTP_REQUEST_FORMAT2                          \
  "GET http://127.0.0.1:%d/format2.html HTTP/1.0\r\n" \
  "X-Request-ID: %d\r\n"                              \
  "Content-Type: text/html\r\n"                       \
  "\r\n"
#define HTTP_REQUEST_FORMAT3                          \
  "GET http://127.0.0.1:%d/format3.html HTTP/1.0\r\n" \
  "X-Request-ID: %d\r\n"                              \
  "Response: Error\r\n"                               \
  "\r\n"
#define HTTP_REQUEST_FORMAT4                          \
  "GET http://127.0.0.1:%d/format4.html HTTP/1.0\r\n" \
  "X-Request-ID: %d\r\n"                              \
  "Request:%d\r\n"                                    \
  "\r\n"
#define HTTP_REQUEST_FORMAT5                          \
  "GET http://127.0.0.1:%d/format5.html HTTP/1.0\r\n" \
  "X-Request-ID: %d\r\n"                              \
  "Request:%d\r\n"                                    \
  "\r\n"
#define HTTP_REQUEST_FORMAT6                         \
  "GET http://127.0.0.1:%d/format.html HTTP/1.0\r\n" \
  "X-Request-ID: %d\r\n"                             \
  "Accept-Language: English\r\n"                     \
  "\r\n"
#define HTTP_REQUEST_FORMAT7                         \
  "GET http://127.0.0.1:%d/format.html HTTP/1.0\r\n" \
  "X-Request-ID: %d\r\n"                             \
  "Accept-Language: French\r\n"                      \
  "\r\n"
#define HTTP_REQUEST_FORMAT8                         \
  "GET http://127.0.0.1:%d/format.html HTTP/1.0\r\n" \
  "X-Request-ID: %d\r\n"                             \
  "Accept-Language: English,French\r\n"              \
  "\r\n"
#define HTTP_REQUEST_FORMAT9                                      \
  "GET http://trafficserver.apache.org/format9.html HTTP/1.0\r\n" \
  "X-Request-ID: %d\r\n"                                          \
  "\r\n"
#define HTTP_REQUEST_FORMAT10                                      \
  "GET http://trafficserver.apache.org/format10.html HTTP/1.0\r\n" \
  "X-Request-ID: %d\r\n"                                           \
  "\r\n"
#define HTTP_REQUEST_FORMAT11                                      \
  "GET http://trafficserver.apache.org/format11.html HTTP/1.0\r\n" \
  "X-Request-ID: %d\r\n"                                           \
  "\r\n"
  char *request = (char *)TSmalloc(REQUEST_MAX_SIZE + 1);

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
  case 11:
    snprintf(request, REQUEST_MAX_SIZE + 1, HTTP_REQUEST_FORMAT11, test_case);
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
#define HTTP_REQUEST_TESTCASE_FORMAT \
  "GET %1024s HTTP/1.%d\r\n"         \
  "X-Request-ID: %d\r\n"

#define HTTP_RESPONSE_DEFAULT_FORMAT \
  "HTTP/1.0 200 OK\r\n"              \
  "X-Response-ID: %d\r\n"            \
  "Cache-Control: max-age=86400\r\n" \
  "Content-Type: text/html\r\n"      \
  "\r\n"                             \
  "Default body"

#define HTTP_RESPONSE_FORMAT1   \
  "HTTP/1.0 200 OK\r\n"         \
  "X-Response-ID: %d\r\n"       \
  "Content-Type: text/html\r\n" \
  "Cache-Control: no-cache\r\n" \
  "\r\n"                        \
  "Body for response 1"

#define HTTP_RESPONSE_FORMAT2        \
  "HTTP/1.0 200 OK\r\n"              \
  "X-Response-ID: %d\r\n"            \
  "Cache-Control: max-age=86400\r\n" \
  "Content-Type: text/html\r\n"      \
  "\r\n"                             \
  "Body for response 2"
#define HTTP_RESPONSE_FORMAT4        \
  "HTTP/1.0 200 OK\r\n"              \
  "X-Response-ID: %d\r\n"            \
  "Cache-Control: max-age=86400\r\n" \
  "Content-Type: text/html\r\n"      \
  "\r\n"                             \
  "Body for response 4"
#define HTTP_RESPONSE_FORMAT5   \
  "HTTP/1.0 200 OK\r\n"         \
  "X-Response-ID: %d\r\n"       \
  "Content-Type: text/html\r\n" \
  "\r\n"                        \
  "Body for response 5"
#define HTTP_RESPONSE_FORMAT6        \
  "HTTP/1.0 200 OK\r\n"              \
  "X-Response-ID: %d\r\n"            \
  "Cache-Control: max-age=86400\r\n" \
  "Content-Language: English\r\n"    \
  "\r\n"                             \
  "Body for response 6"
#define HTTP_RESPONSE_FORMAT7        \
  "HTTP/1.0 200 OK\r\n"              \
  "X-Response-ID: %d\r\n"            \
  "Cache-Control: max-age=86400\r\n" \
  "Content-Language: French\r\n"     \
  "\r\n"                             \
  "Body for response 7"

#define HTTP_RESPONSE_FORMAT8             \
  "HTTP/1.0 200 OK\r\n"                   \
  "X-Response-ID: %d\r\n"                 \
  "Cache-Control: max-age=86400\r\n"      \
  "Content-Language: French, English\r\n" \
  "\r\n"                                  \
  "Body for response 8"

#define HTTP_RESPONSE_FORMAT9        \
  "HTTP/1.0 200 OK\r\n"              \
  "Cache-Control: max-age=86400\r\n" \
  "X-Response-ID: %d\r\n"            \
  "\r\n"                             \
  "Body for response 9"

#define HTTP_RESPONSE_FORMAT10       \
  "HTTP/1.0 200 OK\r\n"              \
  "Cache-Control: max-age=86400\r\n" \
  "X-Response-ID: %d\r\n"            \
  "\r\n"                             \
  "Body for response 10"

#define HTTP_RESPONSE_FORMAT11          \
  "HTTP/1.0 200 OK\r\n"                 \
  "Cache-Control: private,no-store\r\n" \
  "X-Response-ID: %d\r\n"               \
  "\r\n"                                \
  "Body for response 11"

  int test_case, match, http_version;

  char *response = (char *)TSmalloc(RESPONSE_MAX_SIZE + 1);
  char url[1025];

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
    case 11:
      snprintf(response, RESPONSE_MAX_SIZE + 1, HTTP_RESPONSE_FORMAT11, test_case);
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

static int
get_request_id_value(const char *name, TSMBuffer buf, TSMLoc hdr)
{
  int id = -1;
  TSMLoc field;

  field = TSMimeHdrFieldFind(buf, hdr, name, -1);
  if (field != TS_NULL_MLOC) {
    id = TSMimeHdrFieldValueIntGet(buf, hdr, field, 0);
  }

  TSHandleMLocRelease(buf, hdr, field);
  return id;
}

// This routine can be called by tests, from the READ_REQUEST_HDR_HOOK
// to figure out the id of a test message
// Returns id/-1 in case of error
static int
get_request_id(TSHttpTxn txnp)
{
  TSMBuffer bufp;
  TSMLoc hdr_loc;
  int id = -1;

  if (TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    return -1;
  }

  id = get_request_id_value(X_REQUEST_ID, bufp, hdr_loc);
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
  return id;
}

// This routine can be called by tests, from the READ_RESPONSE_HDR_HOOK
// to figure out the id of a test message
// Returns id/-1 in case of error
static int
get_response_id(TSHttpTxn txnp)
{
  TSMBuffer bufp;
  TSMLoc hdr_loc;
  int id = -1;

  if (TSHttpTxnClientRespGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    return -1;
  }

  id = get_request_id_value(X_RESPONSE_ID, bufp, hdr_loc);
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
  return id;
}

//////////////////////////////////////////////////////////////////////////////
// SOCKET CLIENT
//////////////////////////////////////////////////////////////////////////////

static ClientTxn *
synclient_txn_create(void)
{
  HttpProxyPort *proxy_port;

  ClientTxn *txn = (ClientTxn *)TSmalloc(sizeof(ClientTxn));
  if (0 == (proxy_port = HttpProxyPort::findHttp(AF_INET)))
    txn->connect_port = PROXY_HTTP_DEFAULT_PORT;
  else
    txn->connect_port = proxy_port->m_port;

  txn->local_port     = (int)0;
  txn->connect_ip     = IP(127, 0, 0, 1);
  txn->status         = REQUEST_INPROGRESS;
  txn->request        = NULL;
  txn->vconn          = NULL;
  txn->req_buffer     = NULL;
  txn->req_reader     = NULL;
  txn->resp_buffer    = NULL;
  txn->resp_reader    = NULL;
  txn->magic          = MAGIC_ALIVE;
  txn->connect_action = NULL;

  TSDebug(CDBG_TAG, "Connecting to proxy 127.0.0.1 on port %d", txn->connect_port);
  return txn;
}

static int
synclient_txn_delete(ClientTxn *txn)
{
  TSAssert(txn->magic == MAGIC_ALIVE);
  if (txn->connect_action && !TSActionDone(txn->connect_action)) {
    TSActionCancel(txn->connect_action);
    txn->connect_action = NULL;
  }

  ats_free(txn->request);
  txn->magic = MAGIC_DEAD;
  TSfree(txn);
  return 1;
}

static void
synclient_txn_close(ClientTxn *txn)
{
  if (txn) {
    if (txn->vconn != NULL) {
      TSVConnClose(txn->vconn);
      txn->vconn = NULL;
    }

    if (txn->req_buffer != NULL) {
      TSIOBufferDestroy(txn->req_buffer);
      txn->req_buffer = NULL;
    }

    if (txn->resp_buffer != NULL) {
      TSIOBufferDestroy(txn->resp_buffer);
      txn->resp_buffer = NULL;
    }

    TSDebug(CDBG_TAG, "Client Txn destroyed");
  }
}

static int
synclient_txn_send_request(ClientTxn *txn, char *request)
{
  TSCont cont;
  sockaddr_in addr;

  TSAssert(txn->magic == MAGIC_ALIVE);
  txn->request = ats_strdup(request);
  SET_TEST_HANDLER(txn->current_handler, synclient_txn_connect_handler);

  cont = TSContCreate(synclient_txn_main_handler, TSMutexCreate());
  TSContDataSet(cont, txn);

  ats_ip4_set(&addr, txn->connect_ip, htons(txn->connect_port));
  TSNetConnect(cont, ats_ip_sa_cast(&addr));
  return 1;
}

/* This can be used to send a request to a specific VC */
static int
synclient_txn_send_request_to_vc(ClientTxn *txn, char *request, TSVConn vc)
{
  TSCont cont;
  TSAssert(txn->magic == MAGIC_ALIVE);
  txn->request = ats_strdup(request);
  SET_TEST_HANDLER(txn->current_handler, synclient_txn_connect_handler);

  cont = TSContCreate(synclient_txn_main_handler, TSMutexCreate());
  TSContDataSet(cont, txn);

  TSContCall(cont, TS_EVENT_NET_CONNECT, vc);
  return 1;
}

static int
synclient_txn_read_response(TSCont contp)
{
  ClientTxn *txn = (ClientTxn *)TSContDataGet(contp);
  TSAssert(txn->magic == MAGIC_ALIVE);

  TSIOBufferBlock block = TSIOBufferReaderStart(txn->resp_reader);
  while (block != NULL) {
    int64_t blocklen;
    const char *blockptr = TSIOBufferBlockReadStart(block, txn->resp_reader, &blocklen);

    if (txn->response_len + blocklen <= RESPONSE_MAX_SIZE) {
      memcpy((char *)(txn->response + txn->response_len), blockptr, blocklen);
      txn->response_len += blocklen;
    } else {
      TSError("Error: Response length %" PRId64 " > response buffer size %d", txn->response_len + blocklen, RESPONSE_MAX_SIZE);
    }

    block = TSIOBufferBlockNext(block);
  }

  txn->response[txn->response_len + 1] = '\0';
  TSDebug(CDBG_TAG, "Response = |%s|, req len = %d", txn->response, txn->response_len);

  return 1;
}

static int
synclient_txn_read_response_handler(TSCont contp, TSEvent event, void * /* data ATS_UNUSED */)
{
  ClientTxn *txn = (ClientTxn *)TSContDataGet(contp);
  TSAssert(txn->magic == MAGIC_ALIVE);

  int64_t avail;

  switch (event) {
  case TS_EVENT_VCONN_READ_READY:
  case TS_EVENT_VCONN_READ_COMPLETE:
    if (event == TS_EVENT_VCONN_READ_READY) {
      TSDebug(CDBG_TAG, "READ_READY");
    } else {
      TSDebug(CDBG_TAG, "READ_COMPLETE");
    }

    avail = TSIOBufferReaderAvail(txn->resp_reader);
    TSDebug(CDBG_TAG, "%" PRId64 " bytes available in buffer", avail);

    if (avail > 0) {
      synclient_txn_read_response(contp);
      TSIOBufferReaderConsume(txn->resp_reader, avail);
    }

    TSVIOReenable(txn->read_vio);
    break;

  case TS_EVENT_VCONN_EOS:
    TSDebug(CDBG_TAG, "READ_EOS");
    // Connection closed. In HTTP/1.0 it means we're done for this request.
    txn->status = REQUEST_SUCCESS;
    synclient_txn_close((ClientTxn *)TSContDataGet(contp));
    TSContDestroy(contp);
    return 1;

  case TS_EVENT_ERROR:
    TSDebug(CDBG_TAG, "READ_ERROR");
    txn->status = REQUEST_FAILURE;
    synclient_txn_close((ClientTxn *)TSContDataGet(contp));
    TSContDestroy(contp);
    return 1;

  default:
    TSAssert(!"Invalid event");
    break;
  }
  return 1;
}

static int
synclient_txn_write_request(TSCont contp)
{
  ClientTxn *txn = (ClientTxn *)TSContDataGet(contp);
  TSAssert(txn->magic == MAGIC_ALIVE);

  TSIOBufferBlock block;
  char *ptr_block;
  int64_t len, ndone, ntodo, towrite, avail;

  len = strlen(txn->request);

  ndone = 0;
  ntodo = len;
  while (ntodo > 0) {
    block     = TSIOBufferStart(txn->req_buffer);
    ptr_block = TSIOBufferBlockWriteStart(block, &avail);
    towrite   = MIN(ntodo, avail);
    memcpy(ptr_block, txn->request + ndone, towrite);
    TSIOBufferProduce(txn->req_buffer, towrite);
    ntodo -= towrite;
    ndone += towrite;
  }

  /* Start writing the response */
  TSDebug(CDBG_TAG, "Writing |%s| (%" PRId64 ") bytes", txn->request, len);
  txn->write_vio = TSVConnWrite(txn->vconn, contp, txn->req_reader, len);

  return 1;
}

static int
synclient_txn_write_request_handler(TSCont contp, TSEvent event, void * /* data ATS_UNUSED */)
{
  ClientTxn *txn = (ClientTxn *)TSContDataGet(contp);
  TSAssert(txn->magic == MAGIC_ALIVE);

  switch (event) {
  case TS_EVENT_VCONN_WRITE_READY:
    TSDebug(CDBG_TAG, "WRITE_READY");
    TSVIOReenable(txn->write_vio);
    break;

  case TS_EVENT_VCONN_WRITE_COMPLETE:
    TSDebug(CDBG_TAG, "WRITE_COMPLETE");
    // Weird: synclient should not close the write part of vconn.
    // Otherwise some strangeness...

    /* Start reading */
    SET_TEST_HANDLER(txn->current_handler, synclient_txn_read_response_handler);
    txn->read_vio = TSVConnRead(txn->vconn, contp, txn->resp_buffer, INT64_MAX);
    break;

  case TS_EVENT_VCONN_EOS:
    TSDebug(CDBG_TAG, "WRITE_EOS");
    txn->status = REQUEST_FAILURE;
    synclient_txn_close((ClientTxn *)TSContDataGet(contp));
    TSContDestroy(contp);
    break;

  case TS_EVENT_ERROR:
    TSDebug(CDBG_TAG, "WRITE_ERROR");
    txn->status = REQUEST_FAILURE;
    synclient_txn_close((ClientTxn *)TSContDataGet(contp));
    TSContDestroy(contp);
    break;

  default:
    TSAssert(!"Invalid event");
    break;
  }
  return TS_EVENT_IMMEDIATE;
}

static int
synclient_txn_connect_handler(TSCont contp, TSEvent event, void *data)
{
  TSAssert((event == TS_EVENT_NET_CONNECT) || (event == TS_EVENT_NET_CONNECT_FAILED));

  ClientTxn *txn = (ClientTxn *)TSContDataGet(contp);
  TSAssert(txn->magic == MAGIC_ALIVE);

  if (event == TS_EVENT_NET_CONNECT) {
    TSDebug(CDBG_TAG, "NET_CONNECT");

    txn->req_buffer  = TSIOBufferCreate();
    txn->req_reader  = TSIOBufferReaderAlloc(txn->req_buffer);
    txn->resp_buffer = TSIOBufferCreate();
    txn->resp_reader = TSIOBufferReaderAlloc(txn->resp_buffer);

    txn->response[0]  = '\0';
    txn->response_len = 0;

    txn->vconn      = (TSVConn)data;
    txn->local_port = (int)((NetVConnection *)data)->get_local_port();

    txn->write_vio = NULL;
    txn->read_vio  = NULL;

    /* start writing */
    SET_TEST_HANDLER(txn->current_handler, synclient_txn_write_request_handler);
    synclient_txn_write_request(contp);

    return TS_EVENT_IMMEDIATE;
  } else {
    TSDebug(CDBG_TAG, "NET_CONNECT_FAILED");
    txn->status = REQUEST_FAILURE;
    synclient_txn_close((ClientTxn *)TSContDataGet(contp));
    TSContDestroy(contp);
  }

  return TS_EVENT_IMMEDIATE;
}

static int
synclient_txn_main_handler(TSCont contp, TSEvent event, void *data)
{
  ClientTxn *txn = (ClientTxn *)TSContDataGet(contp);
  TSAssert(txn->magic == MAGIC_ALIVE);

  TxnHandler handler = txn->current_handler;
  return (*handler)(contp, event, data);
}

//////////////////////////////////////////////////////////////////////////////
// SOCKET SERVER
//////////////////////////////////////////////////////////////////////////////

SocketServer *
synserver_create(int port, TSCont cont)
{
  if (port != SYNSERVER_DUMMY_PORT) {
    TSAssert(port > 0);
    TSAssert(port < INT16_MAX);
  }

  SocketServer *s  = (SocketServer *)TSmalloc(sizeof(SocketServer));
  s->magic         = MAGIC_ALIVE;
  s->accept_port   = port;
  s->accept_action = NULL;
  s->accept_cont   = cont;
  TSContDataSet(s->accept_cont, s);
  return s;
}

SocketServer *
synserver_create(int port)
{
  return synserver_create(port, TSContCreate(synserver_vc_accept, TSMutexCreate()));
}

static int
synserver_start(SocketServer *s)
{
  TSAssert(s->magic == MAGIC_ALIVE);
  TSAssert(s->accept_action == NULL);

  if (s->accept_port != SYNSERVER_DUMMY_PORT) {
    TSAssert(s->accept_port > 0);
    TSAssert(s->accept_port < INT16_MAX);

    s->accept_action = TSNetAccept(s->accept_cont, s->accept_port, AF_INET, 0);
  }

  return 1;
}

static int
synserver_stop(SocketServer *s)
{
  TSAssert(s->magic == MAGIC_ALIVE);
  if (s->accept_action && !TSActionDone(s->accept_action)) {
    TSActionCancel(s->accept_action);
    s->accept_action = NULL;
    TSDebug(SDBG_TAG, "Had to cancel action");
  }
  TSDebug(SDBG_TAG, "stopped");
  return 1;
}

static int
synserver_delete(SocketServer *s)
{
  if (s != NULL) {
    TSAssert(s->magic == MAGIC_ALIVE);
    synserver_stop(s);

    if (s->accept_cont) {
      TSContDestroy(s->accept_cont);
      s->accept_cont = NULL;
      TSDebug(SDBG_TAG, "destroyed accept cont");
    }

    s->magic = MAGIC_DEAD;
    TSfree(s);
    TSDebug(SDBG_TAG, "deleted server");
  }

  return 1;
}

static int
synserver_vc_refuse(TSCont contp, TSEvent event, void *data)
{
  TSAssert((event == TS_EVENT_NET_ACCEPT) || (event == TS_EVENT_NET_ACCEPT_FAILED));

  SocketServer *s = (SocketServer *)TSContDataGet(contp);
  TSAssert(s->magic == MAGIC_ALIVE);

  TSDebug(SDBG_TAG, "%s: NET_ACCEPT", __func__);

  if (event == TS_EVENT_NET_ACCEPT_FAILED) {
    Warning("Synserver failed to bind to port %d.", ntohs(s->accept_port));
    ink_release_assert(!"Synserver must be able to bind to a port, check system netstat");
    TSDebug(SDBG_TAG, "%s: NET_ACCEPT_FAILED", __func__);
    return TS_EVENT_IMMEDIATE;
  }

  TSVConnClose((TSVConn)data);
  return TS_EVENT_IMMEDIATE;
}

static int
synserver_vc_accept(TSCont contp, TSEvent event, void *data)
{
  TSAssert((event == TS_EVENT_NET_ACCEPT) || (event == TS_EVENT_NET_ACCEPT_FAILED));

  SocketServer *s = (SocketServer *)TSContDataGet(contp);
  TSAssert(s->magic == MAGIC_ALIVE);

  if (event == TS_EVENT_NET_ACCEPT_FAILED) {
    Warning("Synserver failed to bind to port %d.", ntohs(s->accept_port));
    ink_release_assert(!"Synserver must be able to bind to a port, check system netstat");
    TSDebug(SDBG_TAG, "%s: NET_ACCEPT_FAILED", __func__);
    return TS_EVENT_IMMEDIATE;
  }

  TSDebug(SDBG_TAG, "%s: NET_ACCEPT", __func__);

  /* Create a new transaction */
  ServerTxn *txn = (ServerTxn *)TSmalloc(sizeof(ServerTxn));
  txn->magic     = MAGIC_ALIVE;

  SET_TEST_HANDLER(txn->current_handler, synserver_txn_read_request_handler);

  TSCont txn_cont = TSContCreate(synserver_txn_main_handler, TSMutexCreate());
  TSContDataSet(txn_cont, txn);

  txn->req_buffer = TSIOBufferCreate();
  txn->req_reader = TSIOBufferReaderAlloc(txn->req_buffer);

  txn->resp_buffer = TSIOBufferCreate();
  txn->resp_reader = TSIOBufferReaderAlloc(txn->resp_buffer);

  txn->request[0]  = '\0';
  txn->request_len = 0;

  txn->vconn = (TSVConn)data;

  txn->write_vio = NULL;

  /* start reading */
  txn->read_vio = TSVConnRead(txn->vconn, txn_cont, txn->req_buffer, INT64_MAX);

  return TS_EVENT_IMMEDIATE;
}

static int
synserver_txn_close(TSCont contp)
{
  ServerTxn *txn = (ServerTxn *)TSContDataGet(contp);
  TSAssert(txn->magic == MAGIC_ALIVE);

  if (txn->vconn != NULL) {
    TSVConnClose(txn->vconn);
  }
  if (txn->req_buffer) {
    TSIOBufferDestroy(txn->req_buffer);
  }
  if (txn->resp_buffer) {
    TSIOBufferDestroy(txn->resp_buffer);
  }

  txn->magic = MAGIC_DEAD;
  TSfree(txn);
  TSContDestroy(contp);

  TSDebug(SDBG_TAG, "Server Txn destroyed");
  return TS_EVENT_IMMEDIATE;
}

static int
synserver_txn_write_response(TSCont contp)
{
  ServerTxn *txn = (ServerTxn *)TSContDataGet(contp);
  TSAssert(txn->magic == MAGIC_ALIVE);

  SET_TEST_HANDLER(txn->current_handler, synserver_txn_write_response_handler);

  TSIOBufferBlock block;
  char *ptr_block;
  int64_t len, ndone, ntodo, towrite, avail;
  char *response;

  response = generate_response(txn->request);
  len      = strlen(response);

  ndone = 0;
  ntodo = len;
  while (ntodo > 0) {
    block     = TSIOBufferStart(txn->resp_buffer);
    ptr_block = TSIOBufferBlockWriteStart(block, &avail);
    towrite   = MIN(ntodo, avail);
    memcpy(ptr_block, response + ndone, towrite);
    TSIOBufferProduce(txn->resp_buffer, towrite);
    ntodo -= towrite;
    ndone += towrite;
  }

  /* Start writing the response */
  TSDebug(SDBG_TAG, "Writing response: |%s| (%" PRId64 ") bytes)", response, len);
  txn->write_vio = TSVConnWrite(txn->vconn, contp, txn->resp_reader, len);

  /* Now that response is in IOBuffer, free up response */
  TSfree(response);

  return TS_EVENT_IMMEDIATE;
}

static int
synserver_txn_write_response_handler(TSCont contp, TSEvent event, void * /* data ATS_UNUSED */)
{
  ServerTxn *txn = (ServerTxn *)TSContDataGet(contp);
  TSAssert(txn->magic == MAGIC_ALIVE);

  switch (event) {
  case TS_EVENT_VCONN_WRITE_READY:
    TSDebug(SDBG_TAG, "WRITE_READY");
    TSVIOReenable(txn->write_vio);
    break;

  case TS_EVENT_VCONN_WRITE_COMPLETE:
    TSDebug(SDBG_TAG, "WRITE_COMPLETE");
    TSVConnShutdown(txn->vconn, 0, 1);
    return synserver_txn_close(contp);
    break;

  case TS_EVENT_VCONN_EOS:
    TSDebug(SDBG_TAG, "WRITE_EOS");
    return synserver_txn_close(contp);
    break;

  case TS_EVENT_ERROR:
    TSDebug(SDBG_TAG, "WRITE_ERROR");
    return synserver_txn_close(contp);
    break;

  default:
    TSAssert(!"Invalid event");
    break;
  }
  return TS_EVENT_IMMEDIATE;
}

static int
synserver_txn_read_request(TSCont contp)
{
  ServerTxn *txn = (ServerTxn *)TSContDataGet(contp);
  TSAssert(txn->magic == MAGIC_ALIVE);

  int end;
  TSIOBufferBlock block = TSIOBufferReaderStart(txn->req_reader);

  while (block != NULL) {
    int64_t blocklen;
    const char *blockptr = TSIOBufferBlockReadStart(block, txn->req_reader, &blocklen);

    if (txn->request_len + blocklen <= REQUEST_MAX_SIZE) {
      memcpy((char *)(txn->request + txn->request_len), blockptr, blocklen);
      txn->request_len += blocklen;
    } else {
      TSError("Error: Request length %" PRId64 " > request buffer size %d", txn->request_len + blocklen, REQUEST_MAX_SIZE);
    }

    block = TSIOBufferBlockNext(block);
  }

  txn->request[txn->request_len] = '\0';
  TSDebug(SDBG_TAG, "Request = |%s|, req len = %d", txn->request, txn->request_len);

  end = (strstr(txn->request, HTTP_REQUEST_END) != NULL);
  TSDebug(SDBG_TAG, "End of request = %d", end);

  return end;
}

static int
synserver_txn_read_request_handler(TSCont contp, TSEvent event, void * /* data ATS_UNUSED */)
{
  ServerTxn *txn = (ServerTxn *)TSContDataGet(contp);
  TSAssert(txn->magic == MAGIC_ALIVE);

  int64_t avail;
  int end_of_request;

  switch (event) {
  case TS_EVENT_VCONN_READ_READY:
  case TS_EVENT_VCONN_READ_COMPLETE:
    TSDebug(SDBG_TAG, (event == TS_EVENT_VCONN_READ_READY) ? "READ_READY" : "READ_COMPLETE");
    avail = TSIOBufferReaderAvail(txn->req_reader);
    TSDebug(SDBG_TAG, "%" PRId64 " bytes available in buffer", avail);

    if (avail > 0) {
      end_of_request = synserver_txn_read_request(contp);
      TSIOBufferReaderConsume(txn->req_reader, avail);

      if (end_of_request) {
        TSVConnShutdown(txn->vconn, 1, 0);
        return synserver_txn_write_response(contp);
      }
    }

    TSVIOReenable(txn->read_vio);
    break;

  case TS_EVENT_VCONN_EOS:
    TSDebug(SDBG_TAG, "READ_EOS");
    return synserver_txn_close(contp);
    break;

  case TS_EVENT_ERROR:
    TSDebug(SDBG_TAG, "READ_ERROR");
    return synserver_txn_close(contp);
    break;

  default:
    TSAssert(!"Invalid event");
    break;
  }
  return TS_EVENT_IMMEDIATE;
}

static int
synserver_txn_main_handler(TSCont contp, TSEvent event, void *data)
{
  ServerTxn *txn = (ServerTxn *)TSContDataGet(contp);
  TSAssert(txn->magic == MAGIC_ALIVE);

  TxnHandler handler = txn->current_handler;
  return (*handler)(contp, event, data);
}
