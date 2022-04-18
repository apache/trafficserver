/** @file

  @brief A plugin that sends the http body through an ICAP request to a
         scanner server. If malicious content is detected, then the scanner
         will return an error message body, which we will pass to the user
         agent. Otherwise it will return the same content that was passed to
         it, in which case we will pass the content to user agent.

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

#include <string>
#include <cstring>
#include <regex>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <cinttypes>
#include <iostream>
#include <sstream>

#include "ts/ts.h"

#define PLUGIN_NAME "icap_plugin"

enum class State {
  BEGIN = 1,
  CONNECT,
  WRITE_HEADER,
  WRITE_BODY,
  READ_ICAP_HEADER,
  READ_HTTP_HEADER,
  READ_HTTP_BODY,
  SEND_ERROR_MSG,
  BYPASS,
  BUFFER_OS_RESP,
  SEND_OS_RESP
};

#define ICAP_SERVICE_URL "icap://127.0.0.1/avscan"
#define ICAP_VERSION "1.0"

struct TransformData {
  State state = State::BEGIN;
  const TSHttpTxn txn;

  int64_t server_reply_content_length;

  TSIOBuffer input_buf          = nullptr;
  TSIOBufferReader input_reader = nullptr;

  TSIOBuffer os_resp_buf          = nullptr;
  TSIOBufferReader os_resp_reader = nullptr;

  int64_t done_write = false;

  TSIOBuffer icap_resp_buf          = nullptr;
  TSIOBufferReader icap_resp_reader = nullptr;

  TSIOBuffer output_buf          = nullptr;
  TSIOBufferReader output_reader = nullptr;
  TSVConn output_vc              = nullptr;
  TSVIO output_vio               = nullptr;

  TSAction pending_action = nullptr;
  TSVConn icap_vc         = nullptr;
  TSVIO icap_vio          = nullptr;

  std::string icap_header;
  std::string http_header;
  std::string chunk_length_str;
  int64_t icap_reply_content_length = 0;

  int64_t http_body_chunk_length         = -1;
  int64_t http_body_total_length_written = 0;

  bool eos_detected = false;

  std::string err_msg;

  TransformData(TSHttpTxn txnp);
  ~TransformData();
};

/* Configurable parameters */
static std::string server_ip;
static int server_port;
static int carp_port;
static int debug_enabled;

/* Stats for debug */
static int scan_passed;
static int scan_failed;
static int icap_conn_failed;
static int total_icap_invalid;
static int icap_response_err;
static int icap_write_failed;

static int transform_handler(TSCont contp, TSEvent event, void *edata);
static int transform_read_http_header_event(TSCont contp, TransformData *data, TSEvent event, void *edata);
static int transform_send_error_msg(TSCont contp, TransformData *data);
static int transform_bypass(TSCont contp, TransformData *data);
static int transform_send_os_resp(TSCont contp, TransformData *data);

TransformData::TransformData(TSHttpTxn txnp) : txn(txnp) {}

TransformData::~TransformData()
{
  if (icap_vc) {
    TSVConnAbort(icap_vc, 1);
  }
  if (input_reader) {
    TSIOBufferReaderFree(input_reader);
  }
  if (input_buf) {
    TSIOBufferDestroy(input_buf);
  }
  if (os_resp_reader) {
    TSIOBufferReaderFree(os_resp_reader);
  }
  if (os_resp_buf) {
    TSIOBufferDestroy(os_resp_buf);
  }
  if (icap_resp_reader) {
    TSIOBufferReaderFree(icap_resp_reader);
  }
  if (icap_resp_buf) {
    TSIOBufferDestroy(icap_resp_buf);
  }
  if (output_reader) {
    TSIOBufferReaderFree(output_reader);
  }
  if (output_buf) {
    TSIOBufferDestroy(output_buf);
  }
  if (pending_action) {
    TSActionCancel(pending_action);
  }
}

/*
 * get_port
 * Description: Return the port of a sockaddr
 */
uint16_t
get_port(sockaddr const *s_sockaddr)
{
  switch (s_sockaddr->sa_family) {
  case AF_INET: {
    const struct sockaddr_in *s_sockaddr_in = reinterpret_cast<const struct sockaddr_in *>(s_sockaddr);
    return ntohs(s_sockaddr_in->sin_port);
  } break;
  case AF_INET6: {
    const struct sockaddr_in6 *s_sockaddr_in6 = reinterpret_cast<const struct sockaddr_in6 *>(s_sockaddr);
    return ntohs(s_sockaddr_in6->sin6_port);
  } break;
  default:
    return 0;
    break;
  }
}

/*
 * setup_icap_status_header (Used only in debug-mode)
 * Description: This function is called to add a customized header
 *              indicating ICAP server status for logging.
 */
static void
setup_icap_status_header(TransformData *data, const char *header, const char *value)
{
  TSMBuffer bufp;
  TSMLoc resp_loc, field_loc;

  if (TSHttpTxnTransformRespGet(data->txn, &bufp, &resp_loc) != TS_SUCCESS) {
    TSError("[%s] Couldn't retrieve transform response header", PLUGIN_NAME);
    return;
  }

  if (TSMimeHdrFieldCreate(bufp, resp_loc, &field_loc) != TS_SUCCESS) {
    TSError("[%s] Unable to create field", PLUGIN_NAME);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, resp_loc);
    return;
  }

  TSMimeHdrFieldNameSet(bufp, resp_loc, field_loc, header, strlen(header));
  TSMimeHdrFieldValueStringInsert(bufp, resp_loc, field_loc, 0, value, strlen(value));
  TSMimeHdrFieldAppend(bufp, resp_loc, field_loc);

  TSHandleMLocRelease(bufp, resp_loc, field_loc);
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, resp_loc);
}

/*
 * handle_invalid_icap_behavior
 * Description: This function is called when abnormal behavior of ICAP server,
 *              for instance, unsuccessful connection, is detected.
 */
static int
handle_invalid_icap_behavior(TSCont contp, TransformData *data, const char *msg)
{
  if (data->icap_vc) {
    TSVConnAbort(data->icap_vc, 1);
    data->icap_vc  = nullptr;
    data->icap_vio = nullptr;
  }
  TSStatIntIncrement(total_icap_invalid, 1);
  TSDebug(PLUGIN_NAME, "\n%s\n", data->icap_header.c_str());
  data->err_msg = std::string(msg);
  /* Signal the upstream vconn if still exists to stop sending data */
  TSVIO write_vio = TSVConnWriteVIOGet(contp);
  if (TSVIOBufferGet(write_vio)) {
    TSContCall(TSVIOContGet(write_vio), TS_EVENT_VCONN_WRITE_COMPLETE, write_vio);
  }

  TSMBuffer bufp;
  TSMLoc hdr_loc;

  if (TSHttpTxnTransformRespGet(data->txn, &bufp, &hdr_loc) != TS_SUCCESS) {
    TSError("[%s] Couldn't retrieve transform response header", PLUGIN_NAME);
    return 0;
  }
  /* Clear all headers from the transform response */
  if (TSMimeHdrFieldsClear(bufp, hdr_loc) == TS_ERROR) {
    TSError("[%s] Couldn't clear client response header", PLUGIN_NAME);
    return 0;
  }
  TSHttpHdrStatusSet(bufp, hdr_loc, TS_HTTP_STATUS_BAD_GATEWAY);
  TSHttpHdrReasonSet(bufp, hdr_loc, TSHttpHdrReasonLookup(TS_HTTP_STATUS_BAD_GATEWAY),
                     strlen(TSHttpHdrReasonLookup(TS_HTTP_STATUS_BAD_GATEWAY)));
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
  transform_send_error_msg(contp, data);

  return 0;
}

/*
 * handle_icap_headers
 * Description: This is a good place to determine what to do next based on
 *              icap eaders from response of icap server.
 */
static int
handle_icap_headers(TSCont contp, TransformData *data)
{
  int64_t pos = data->icap_header.find("\r\n");
  std::string icap_status_line =
    pos != static_cast<int64_t>(std::string::npos) ? data->icap_header.substr(0, pos) : data->icap_header;
  /* Check icap header to determine whether the scan passed or not */
  if (data->icap_header.find("X-Infection-Found") != std::string::npos ||
      data->icap_header.find("X-Violations-Found") != std::string::npos) {
    TSStatIntIncrement(scan_failed, 1);
  } else {
    TSStatIntIncrement(scan_passed, 1);
  }
  /* If debug-mode is enabled, add header to log ICAP status */
  if (debug_enabled) {
    if (icap_status_line.find("506") != std::string::npos) {
      setup_icap_status_header(data, "@ICAP-Status", "ICAP server is too busy");
      TSDebug(PLUGIN_NAME, "Sending OS response body.");
      return 1;
    }
  }

  return 0;
}

/*
 * handle_icap_http_header
 * Description: This is a good place to determine what to do next based on
 *              modified http headers from response of icap server.
 */
static void
handle_icap_http_header(TransformData *data)
{
  // TSDebug(PLUGIN_NAME, "Handling http header");
  int64_t pos = data->http_header.find("\r\n");
  std::string http_status_line =
    pos != static_cast<int64_t>(std::string::npos) ? data->http_header.substr(0, pos) : data->http_header;
  /* find content length from header if any */
  std::smatch sm;
  std::regex e("(Content-Length: )([[:digit:]]+)");
  regex_search(data->http_header, sm, e);
  if (sm.size()) {
    data->icap_reply_content_length = std::stoll(sm[2].str().c_str(), nullptr, 10);
  }
  /* Replace header with the returned header from icap server */
  TSMBuffer bufp;
  TSMLoc hdr_loc;
  TSHttpParser parser;
  const char *raw_resp = data->http_header.c_str();

  if (TSHttpTxnTransformRespGet(data->txn, &bufp, &hdr_loc) != TS_SUCCESS) {
    TSError("[%s] Couldn't retrieve transform response header", PLUGIN_NAME);
    return;
  }
  /* Clear all headers from the transform response */
  if (TSMimeHdrFieldsClear(bufp, hdr_loc) == TS_ERROR) {
    TSError("[%s] Couldn't clear client response header", PLUGIN_NAME);
    return;
  }
  /* Create the new header using http header in icap response */
  parser = TSHttpParserCreate();
  TSHttpHdrParseResp(parser, bufp, hdr_loc, &raw_resp, raw_resp + data->http_header.size());

  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
}

/*
 * handle_read_http_body
 * Description: This function handles reading http body from icap server, as
 *              well as writing body to downstream. It should be called whenever
 *              new data is available to read or WRITE_READY is received from
 *              downstream.
 */
static int
handle_read_http_body(TSCont contp, TransformData *data)
{
  int64_t avail = TSIOBufferReaderAvail(data->icap_resp_reader);

  if (avail > 0) {
    /* Read the chunk length if one is not available */
    if (data->http_body_chunk_length <= 0) {
      int64_t data_len;
      const char *buf;
      int64_t consumed    = data->chunk_length_str.size();
      TSIOBufferBlock blk = TSIOBufferReaderStart(data->icap_resp_reader);

      while (blk != nullptr) {
        buf               = TSIOBufferBlockReadStart(blk, data->icap_resp_reader, &data_len);
        std::string chunk = std::string(buf, data_len);
        /* keep the read string in body_length in case complete data haven't arrived */
        data->chunk_length_str += chunk;
        /* Look for end of reply token */
        if (data->chunk_length_str.find("\r\n0\r\n\r\n") != std::string::npos) {
          TSVIONBytesSet(data->output_vio, data->http_body_total_length_written);
          return 0;
        }
        /* TODO replace this regex with more direct (and cheaper) parsing */
        /* Look for hex string indicating chunk length */
        std::smatch sm;
        std::regex e("(\r\n)([[:xdigit:]]+)(\r\n)");
        regex_search(data->chunk_length_str, sm, e);
        /* A match means we have finished reading the length */
        if (sm.size()) {
          int64_t pos          = sm.position(0);
          int64_t token_length = sm[0].length();

          data->http_body_chunk_length = std::stoi(sm[2].str().c_str(), nullptr, 16);
          data->http_body_total_length_written += data->http_body_chunk_length;
          TSIOBufferReaderConsume(data->icap_resp_reader, pos + token_length - consumed);
          break;
        }

        TSIOBufferReaderConsume(data->icap_resp_reader, data_len);
        consumed += data_len;
        blk = TSIOBufferBlockNext(blk);
      }
      if (blk == nullptr) {
        return 0;
      }
    }

    /* Write the chunk to downstream */
    int64_t towrite;

    avail   = TSIOBufferReaderAvail(data->icap_resp_reader);
    towrite = data->http_body_chunk_length < avail ? data->http_body_chunk_length : avail;
    data->http_body_chunk_length -= towrite;
    TSIOBufferCopy(TSVIOBufferGet(data->output_vio), data->icap_resp_reader, towrite, 0);
    TSIOBufferReaderConsume(data->icap_resp_reader, towrite);

    if (data->http_body_chunk_length <= 0) {
      data->chunk_length_str.clear();
      return 0;
    }
  } else {
    /* If no more data is to be read for now. Check for eos to determine whether data is incomplete. */
    if (data->eos_detected) {
      TSVConnAbort(data->icap_vc, 1);
      data->icap_vc  = nullptr;
      data->icap_vio = nullptr;

      TSVConnAbort(data->output_vc, 1);
      data->output_vc  = nullptr;
      data->output_vio = nullptr;
      return 0;
    }
  }

  return 0;
}

static TSCont
transform_create(TSHttpTxn txnp)
{
  TSCont contp;
  TransformData *data;

  contp = TSTransformCreate(transform_handler, txnp);
  data  = new TransformData(txnp);

  TSContDataSet(contp, data);
  // TSDebug(PLUGIN_NAME, "Initialization complete.");
  return contp;
}

static void
transform_destroy(TSCont contp)
{
  TransformData *data;

  data = static_cast<TransformData *>(TSContDataGet(contp));

  if (data != nullptr) {
    delete data;
  } else {
    TSError("[%s] Unable to get Continuation's Data. TSContDataGet returns NULL", PLUGIN_NAME);
  }

  TSContDestroy(contp);
}

/*
 * transform_connect
 * Description: Issue a socket connection to icap server.
 */
static int
transform_connect(TSCont contp, TransformData *data)
{
  TSAction action;
  struct sockaddr_in ip_addr;
  data->state = State::CONNECT;

  /* Only support IPv4 at this point */
  memset(&ip_addr, 0, sizeof(ip_addr));
  ip_addr.sin_family = AF_INET;
  ip_addr.sin_port   = htons(server_port);
  if (inet_pton(AF_INET, server_ip.c_str(), &ip_addr.sin_addr) <= 0) {
    TSError("[%s] Invalid address: %s", PLUGIN_NAME, server_ip.c_str());
    return 0;
  }
  action = TSNetConnect(contp, reinterpret_cast<struct sockaddr const *>(&ip_addr));

  if (!TSActionDone(action)) {
    data->pending_action = action;
  }

  return 0;
}

static int
transform_write_body(TSCont contp, TransformData *data)
{
  data->state = State::WRITE_BODY;
  /* If debug-mode enabled, allocate buffer to store origin response */
  if (debug_enabled) {
    data->os_resp_buf    = TSIOBufferCreate();
    data->os_resp_reader = TSIOBufferReaderAlloc(data->os_resp_buf);
  }
  return 0;
}

static int
transform_read_icap_header(TSCont contp, TransformData *data)
{
  data->state = State::READ_ICAP_HEADER;

  data->icap_resp_buf    = TSIOBufferCreate();
  data->icap_resp_reader = TSIOBufferReaderAlloc(data->icap_resp_buf);

  if (data->icap_resp_reader != nullptr) {
    data->icap_vio = TSVConnRead(data->icap_vc, contp, data->icap_resp_buf, INT64_MAX);
  } else {
    TSError("[%s] Error in Allocating a Reader to output buffer. TSIOBufferReaderAlloc returns NULL", PLUGIN_NAME);
  }

  return 0;
}

static int
transform_read_http_header(TSCont contp, TransformData *data)
{
  data->state = State::READ_HTTP_HEADER;

  if (TSIOBufferReaderAvail(data->icap_resp_reader)) {
    transform_read_http_header_event(contp, data, TS_EVENT_VCONN_READ_READY, nullptr);
  }

  return 0;
}

static int
transform_read_http_body(TSCont contp, TransformData *data)
{
  data->state = State::READ_HTTP_BODY;

  data->output_buf    = TSIOBufferCreate();
  data->output_reader = TSIOBufferReaderAlloc(data->output_buf);
  data->output_vc     = TSTransformOutputVConnGet(static_cast<TSVConn>(contp));
  // data->output_vio = TSVConnWrite(data->output_vc, contp, data->output_reader, INT64_MAX);
  if (data->icap_reply_content_length) {
    data->output_vio = TSVConnWrite(data->output_vc, contp, data->output_reader, data->icap_reply_content_length);
  } else {
    data->output_vio = TSVConnWrite(data->output_vc, contp, data->output_reader, INT64_MAX);
  }

  if (TSIOBufferReaderAvail(data->icap_resp_reader)) {
    return handle_read_http_body(contp, data);
  }

  return 0;
}

static int
handle_write_header(TSCont contp, TransformData *data)
{
  data->state        = State::WRITE_HEADER;
  data->input_buf    = TSIOBufferCreate();
  data->input_reader = TSIOBufferReaderAlloc(data->input_buf);
  data->icap_vio     = TSVConnWrite(data->icap_vc, contp, data->input_reader, INT64_MAX);

  /* Acquire client request and server response header */
  TSMBuffer bufp_c, bufp_s;
  TSMLoc req_loc, resp_loc;

  if (TSHttpTxnClientReqGet(data->txn, &bufp_c, &req_loc) != TS_SUCCESS) {
    TSError("[%s] Couldn't retrieve client request header", PLUGIN_NAME);
    return 0;
  }

  if (TSHttpTxnServerRespGet(data->txn, &bufp_s, &resp_loc) != TS_SUCCESS) {
    TSError("[%s] Couldn't retrieve server response header", PLUGIN_NAME);
    TSHandleMLocRelease(bufp_c, TS_NULL_MLOC, req_loc);
    return 0;
  }

  int64_t client_req_size  = TSHttpHdrLengthGet(bufp_c, req_loc);
  int64_t server_resp_size = TSHttpHdrLengthGet(bufp_s, resp_loc);
  /* formulate the ICAP request header */
  char res_buf[1000];
  memset(res_buf, 0, 1000);
  sprintf(res_buf,
          "RESPMOD %s ICAP/%s\r\n"
          "Host: %s\r\n"
          "Connection: close\r\n" // "Connection: close" is used since each scan creates a new connection
          "Encapsulated: req-hdr=0, res-hdr=%" PRIu64 ", res-body=%" PRIu64 "\r\n\r\n",
          ICAP_SERVICE_URL, ICAP_VERSION, server_ip.c_str(), client_req_size, server_resp_size + client_req_size);

  TSIOBufferWrite(data->input_buf, (const char *)res_buf, strlen(res_buf));
  TSHttpHdrPrint(bufp_c, req_loc, data->input_buf);
  TSHttpHdrPrint(bufp_s, resp_loc, data->input_buf);
  data->done_write += TSIOBufferReaderAvail(data->input_reader);

  TSHandleMLocRelease(bufp_c, TS_NULL_MLOC, req_loc);
  TSHandleMLocRelease(bufp_s, TS_NULL_MLOC, resp_loc);

  return transform_write_body(contp, data);
}

static int
handle_write_body(TSCont contp, TransformData *data)
{
  TSVIO write_vio;
  int64_t towrite;
  char *end_of_request = (char *)"\r\n0; ieof\r\n\r\n";

  write_vio = TSVConnWriteVIOGet(contp);
  /* check if the write VIO's buffer is non-NULL. */
  if (!TSVIOBufferGet(write_vio)) {
    /* Check if there is no body to scan. Skip scanning if no body */
    if (!data->server_reply_content_length) {
      TSVIONBytesSet(data->icap_vio, 0);
      if (TSVIOBufferGet(write_vio)) {
        TSContCall(TSVIOContGet(write_vio), TS_EVENT_VCONN_WRITE_COMPLETE, write_vio);
      }
      return transform_bypass(contp, data);
    }
    TSIOBufferWrite(TSVIOBufferGet(data->icap_vio), (const char *)end_of_request, strlen(end_of_request));
    data->done_write += strlen(end_of_request);
    TSVIONBytesSet(data->icap_vio, data->done_write);
    TSVIOReenable(data->icap_vio);
    return 0;
  }

  /* Determine how much data we have left to read. */
  towrite = TSVIONTodoGet(write_vio);

  if (towrite > 0) {
    /* The amount of data left to read needs to be truncated by
       the amount of data actually in the read buffer. */
    int64_t avail = TSIOBufferReaderAvail(TSVIOReaderGet(write_vio));
    if (towrite > avail) {
      towrite = avail;
    }
    if (towrite > 0) {
      /* Copy the data from the read buffer to the input buffer. */
      std::stringstream ss;
      ss << std::hex << towrite;
      std::string chunk_size            = data->server_reply_content_length <= 0 ? ss.str() + "\r\n" : "\r\n" + ss.str() + "\r\n";
      data->server_reply_content_length = towrite;
      TSIOBufferWrite(TSVIOBufferGet(data->icap_vio), chunk_size.c_str(), chunk_size.size());
      data->done_write += chunk_size.size();
      TSIOBufferCopy(TSVIOBufferGet(data->icap_vio), TSVIOReaderGet(write_vio), towrite, 0);
      if (debug_enabled) {
        /* If debug-mode enabled, buffer origin response */
        TSIOBufferCopy(data->os_resp_buf, TSVIOReaderGet(write_vio), towrite, 0);
      }
      /* Tell the read buffer that we have read the data and are no
         longer interested in it. */
      TSIOBufferReaderConsume(TSVIOReaderGet(write_vio), towrite);
      /* Modify the write VIO to reflect how much data we've
         completed. */
      TSVIONDoneSet(write_vio, TSVIONDoneGet(write_vio) + towrite);
      data->done_write += towrite;
    }
  }

  /* Now we check the write VIO to see if there is data left to
     read. */
  if (TSVIONTodoGet(write_vio) > 0) {
    /* Call back the write VIO continuation to let it know that we
       are ready for more data. */
    TSContCall(TSVIOContGet(write_vio), TS_EVENT_VCONN_WRITE_READY, write_vio);
  } else {
    TSIOBufferWrite(TSVIOBufferGet(data->icap_vio), (const char *)end_of_request, strlen(end_of_request));
    data->done_write += strlen(end_of_request);
    TSVIONBytesSet(data->icap_vio, data->done_write);
    TSVIOReenable(data->icap_vio);
    /* Call back the write VIO continuation to let it know that we
       have completed the write operation. */
    TSContCall(TSVIOContGet(write_vio), TS_EVENT_VCONN_WRITE_COMPLETE, write_vio);

    return 0;
  }

  return 0;
}

/*
 * transform_send_error_msg
 * Description: Send the error message to user agent
 */
static int
transform_send_error_msg(TSCont contp, TransformData *data)
{
  data->state         = State::SEND_ERROR_MSG;
  data->output_buf    = TSIOBufferCreate();
  data->output_reader = TSIOBufferReaderAlloc(data->output_buf);
  data->output_vc     = TSTransformOutputVConnGet(static_cast<TSVConn>(contp));

  TSIOBufferWrite(data->output_buf, data->err_msg.c_str(), data->err_msg.size());

  if (data->output_vc == nullptr) {
    TSError("[%s] TSTransformOutputVConnGet returns NULL", PLUGIN_NAME);
  } else {
    data->output_vio = TSVConnWrite(data->output_vc, contp, data->output_reader, TSIOBufferReaderAvail(data->output_reader));
    if (data->output_vio == nullptr) {
      TSError("[%s] TSVConnWrite returns NULL", PLUGIN_NAME);
    }
  }
  return 1;
}

/*
 * transform_bypass
 * Description: In the case there is no body to transform, bypass scan and
 *              initiate a write of 0 bytes to downstream.
 */
static int
transform_bypass(TSCont contp, TransformData *data)
{
  data->state         = State::BYPASS;
  data->output_buf    = TSIOBufferCreate();
  data->output_reader = TSIOBufferReaderAlloc(data->output_buf);
  data->output_vc     = TSTransformOutputVConnGet(static_cast<TSVConn>(contp));

  if (data->output_vc == nullptr) {
    TSError("[%s] TSTransformOutputVConnGet returns NULL", PLUGIN_NAME);
  } else {
    data->output_vio = TSVConnWrite(data->output_vc, contp, data->output_reader, 0);
    if (data->output_vio == nullptr) {
      TSError("[%s] TSVConnWrite returns NULL", PLUGIN_NAME);
    }
  }

  return 0;
}

/*
 * transform_buffer_os_resp (Used only in debug-mode)
 * Description: Buffer response body from origin server.
 */
static int
transform_buffer_os_resp(TSCont contp, TransformData *data)
{
  data->state = State::BUFFER_OS_RESP;
  TSDebug(PLUGIN_NAME, "Buffer os response.");
  if (!data->os_resp_buf) {
    data->os_resp_buf = TSIOBufferCreate();
  }

  if (!data->os_resp_reader) {
    data->os_resp_reader = TSIOBufferReaderAlloc(data->os_resp_buf);
  }

  return 0;
}

/*
 * transform_send_os_resp (Used only in debug-mode)
 * Description: Send buffered response body from origin to
 *              user-agent without scanning.
 */
static int
transform_send_os_resp(TSCont contp, TransformData *data)
{
  data->state         = State::SEND_OS_RESP;
  data->output_buf    = TSIOBufferCreate();
  data->output_reader = TSIOBufferReaderAlloc(data->output_buf);
  data->output_vc     = TSTransformOutputVConnGet(static_cast<TSVConn>(contp));

  if (data->output_vc == nullptr) {
    TSError("[%s] TSTransformOutputVConnGet returns NULL", PLUGIN_NAME);
  } else {
    data->output_vio = TSVConnWrite(data->output_vc, contp, data->os_resp_reader, TSIOBufferReaderAvail(data->os_resp_reader));
    if (data->output_vio == nullptr) {
      TSError("[%s] TSVConnWrite returns NULL", PLUGIN_NAME);
    }
  }

  return 0;
}

static int
transform_connect_event(TSCont contp, TransformData *data, TSEvent event, void *edata)
{
  switch (event) {
  case TS_EVENT_NET_CONNECT:
    data->pending_action = nullptr;
    data->icap_vc        = static_cast<TSVConn>(edata);
    return handle_write_header(contp, data);
  case TS_EVENT_NET_CONNECT_FAILED:
    TSStatIntIncrement(icap_conn_failed, 1);
    data->pending_action = nullptr;
    return handle_invalid_icap_behavior(contp, data, "Cannot connect to ICAP scanner.");
  default:
    break;
  }

  return 0;
}

static int
transform_write_header_event(TSCont contp, TransformData *data, TSEvent event, void *edata)
{
  switch (event) {
  case TS_EVENT_VCONN_WRITE_COMPLETE:
    return transform_write_body(contp, data);
  case TS_EVENT_ERROR:
    return handle_invalid_icap_behavior(contp, data, "Error writing header to ICAP scanner");
  case TS_EVENT_IMMEDIATE:
    TSVIOReenable(data->icap_vio);
  default:
    break;
  }

  return 0;
}

static int
transform_write_event(TSCont contp, TransformData *data, TSEvent event, void *edata)
{
  switch (event) {
  case TS_EVENT_VCONN_WRITE_COMPLETE:
    return transform_read_icap_header(contp, data);
  case TS_EVENT_ERROR:
    TSStatIntIncrement(icap_write_failed, 1);
    /* In case of not able to write to icap, if debug-mode enabled,
     * setup header to log icap status and proceed to buffer origin
     * response to return to user. If not enabled,  return HTTP 502
     * to client.
     */
    if (debug_enabled) {
      setup_icap_status_header(data, "@ICAP-Status", "Cannot connect to ICAP server");
      return transform_buffer_os_resp(contp, data);
    } else {
      return handle_invalid_icap_behavior(contp, data, "Error writing body to ICAP scanner");
    }
  case TS_EVENT_VCONN_WRITE_READY:
  default:
    return handle_write_body(contp, data);
  }

  return 0;
}

static int
transform_read_icap_header_event(TSCont contp, TransformData *data, TSEvent event, void *edata)
{
  switch (event) {
  case TS_EVENT_ERROR:
  case TS_EVENT_VCONN_EOS:
    data->eos_detected = true;
    TSStatIntIncrement(icap_response_err, 1);
    return handle_invalid_icap_behavior(contp, data, "Invalid ICAP server reply: reading icap header");
  case TS_EVENT_VCONN_READ_READY: {
    TSIOBufferReader reader = data->icap_resp_reader;
    int64_t avail;
    int64_t consumed    = data->icap_header.size();
    int64_t read_nbytes = INT64_MAX;

    while (read_nbytes > 0) {
      TSIOBufferBlock blk = TSIOBufferReaderStart(reader);
      char *buf           = const_cast<char *>(TSIOBufferBlockReadStart(blk, reader, &avail));
      int64_t read_ndone  = (avail >= read_nbytes) ? read_nbytes : avail;
      int64_t consume     = read_ndone;
      std::string chunk   = std::string(buf, read_ndone);

      /* Read in the icap header */
      data->icap_header += chunk;
      // TSDebug(PLUGIN_NAME, "Headers: \n%s", icap_header.c_str());
      int64_t pos          = data->icap_header.find("\r\n\r\n");
      int64_t token_length = std::string("\r\n\r\n").size();

      if (pos != static_cast<int64_t>(std::string::npos)) {
        data->icap_header.resize(pos);
        consume = pos + token_length - consumed;
        TSIOBufferReaderConsume(reader, consume);
        if (handle_icap_headers(contp, data)) {
          return transform_send_os_resp(contp, data);
        } else {
          return transform_read_http_header(contp, data);
        }
      }

      if (read_ndone > 0) {
        read_nbytes -= consume;
        TSIOBufferReaderConsume(reader, consume);
        consumed += consume;
      } else {
        break;
      }
    }
    break;
  }
  default:
    break;
  }

  return 0;
}

static int
transform_read_http_header_event(TSCont contp, TransformData *data, TSEvent event, void *edata)
{
  switch (event) {
  case TS_EVENT_ERROR:
    return handle_invalid_icap_behavior(contp, data, "Error when reading http header");
  case TS_EVENT_VCONN_EOS:
    data->eos_detected = true;
    TSStatIntIncrement(icap_response_err, 1);
    return handle_invalid_icap_behavior(contp, data, "Error when reading http header");
  case TS_EVENT_VCONN_READ_READY: {
    TSIOBufferReader reader = data->icap_resp_reader;
    int64_t avail;
    int64_t consumed    = data->http_header.size();
    int64_t read_nbytes = INT64_MAX;

    while (read_nbytes > 0) {
      TSIOBufferBlock blk = TSIOBufferReaderStart(reader);
      char *buf           = const_cast<char *>(TSIOBufferBlockReadStart(blk, reader, &avail));
      int64_t read_ndone  = (avail >= read_nbytes) ? read_nbytes : avail;
      int64_t consume     = read_ndone;
      std::string chunk   = std::string(buf, read_ndone);

      data->http_header += chunk;
      // TSDebug(PLUGIN_NAME, "Headers: \n%s", icap_header.c_str());
      int64_t pos          = data->http_header.find("\r\n\r\n");
      int64_t token_length = std::string("\r\n").size();

      if (pos != static_cast<int64_t>(std::string::npos)) {
        data->http_header.resize(pos);
        consume = pos + token_length - consumed;
        TSIOBufferReaderConsume(reader, consume);
        handle_icap_http_header(data);
        return transform_read_http_body(contp, data);
      }

      if (read_ndone > 0) {
        read_nbytes -= consume;
        TSIOBufferReaderConsume(reader, consume);
        consumed += consume;
      } else {
        break;
      }
    }

    if (read_nbytes <= 0) {
      /* In case of finish reading http header, start reading http body length */
      return transform_read_http_body(contp, data);
    }
  }
  default:
    break;
  }

  return 0;
}

static int
transform_read_http_body_event(TSCont contp, TransformData *data, TSEvent event, void *edata)
{
  switch (event) {
  case TS_EVENT_ERROR:
    TSVConnAbort(data->icap_vc, 1);
    data->icap_vc  = nullptr;
    data->icap_vio = nullptr;

    TSVConnAbort(data->output_vc, 1);
    data->output_vc  = nullptr;
    data->output_vio = nullptr;
    break;
  case TS_EVENT_VCONN_EOS:
    TSVConnShutdown(data->icap_vc, 1, 0);
    TSVIOReenable(data->output_vio);
    data->eos_detected = true;
    break;
  case TS_EVENT_VCONN_READ_READY:
    handle_read_http_body(contp, data);
    TSVIOReenable(data->output_vio);
    break;
  case TS_EVENT_VCONN_WRITE_COMPLETE:
    TSVConnShutdown(data->output_vc, 0, 1);
    break;
  case TS_EVENT_VCONN_WRITE_READY:
    TSVIOReenable(data->icap_vio);
    handle_read_http_body(contp, data);
    break;
  default:
    break;
  }

  return 0;
}

static int
transform_send_error_msg_event(TSCont contp, TransformData *data, TSEvent event, void *edata)
{
  switch (event) {
  case TS_EVENT_VCONN_WRITE_COMPLETE:
    TSVConnShutdown(data->output_vc, 0, 1);
    break;
  case TS_EVENT_VCONN_WRITE_READY:
  default:
    TSVIOReenable(data->output_vio);
    break;
  }

  return 0;
}

static int
transform_bypass_event(TSCont contp, TransformData *data, TSEvent event, void *edata)
{
  switch (event) {
  case TS_EVENT_VCONN_WRITE_COMPLETE:
    TSVConnShutdown(data->output_vc, 0, 1);
    break;
  default:
    TSVIOReenable(data->output_vio);
    break;
  }

  return 0;
}

/* Used only for debug-mode */
static int
transform_buffer_os_resp_event(TSCont contp, TransformData *data, TSEvent event, void *edata)
{
  TSVIO write_vio;
  int64_t towrite;

  write_vio = TSVConnWriteVIOGet(contp);
  /* check if the write VIO's buffer is non-NULL. */
  if (!TSVIOBufferGet(write_vio)) {
    return transform_send_os_resp(contp, data);
  }

  towrite = TSVIONTodoGet(write_vio);

  if (towrite > 0) {
    /* The amount of data left to read needs to be truncated by
       the amount of data actually in the read buffer. */
    int64_t avail = TSIOBufferReaderAvail(TSVIOReaderGet(write_vio));
    if (towrite > avail) {
      towrite = avail;
    }
    if (towrite > 0) {
      /* Copy the data from the read buffer to the input buffer. */
      TSIOBufferCopy(data->os_resp_buf, TSVIOReaderGet(write_vio), towrite, 0);

      /* Tell the read buffer that we have read the data and are no
         longer interested in it. */
      TSIOBufferReaderConsume(TSVIOReaderGet(write_vio), towrite);

      /* Modify the write VIO to reflect how much data we've
         completed. */
      TSVIONDoneSet(write_vio, TSVIONDoneGet(write_vio) + towrite);
    }
  }

  /* Now we check the write VIO to see if there is data left to
     read. */
  if (TSVIONTodoGet(write_vio) > 0) {
    /* Call back the write VIO continuation to let it know that we
       are ready for more data. */
    TSContCall(TSVIOContGet(write_vio), TS_EVENT_VCONN_WRITE_READY, write_vio);
  } else {
    /* Call back the write VIO continuation to let it know that we
       have completed the write operation. */
    TSContCall(TSVIOContGet(write_vio), TS_EVENT_VCONN_WRITE_COMPLETE, write_vio);

    return transform_send_os_resp(contp, data);
  }

  return 0;
}

/* Used only for debug-mode */
static int
transform_send_os_resp_event(TSCont contp, TransformData *data, TSEvent event, void *edata)
{
  switch (event) {
  case TS_EVENT_VCONN_WRITE_COMPLETE:
    TSVConnShutdown(data->output_vc, 0, 1);
    break;
  default:
    TSVIOReenable(data->output_vio);
    break;
  }

  return 0;
}

static int
transform_handler(TSCont contp, TSEvent event, void *edata)
{
  /* Check to see if the transformation has been closed by a call to TSVConnClose. */
  if (TSVConnClosedGet(contp)) {
    TSDebug(PLUGIN_NAME, "transformation closed");
    transform_destroy(contp);
    return 0;
  } else {
    TransformData *data;

    data = static_cast<TransformData *>(TSContDataGet(contp));
    if (data == nullptr) {
      TSError("[%s] Didn't get Continuation's Data, ignoring event", PLUGIN_NAME);
      return 0;
    }
    TSDebug(PLUGIN_NAME, "transform handler event [%d], data->state = [%d]", event, static_cast<int>(data->state));

    switch (data->state) {
    case State::BEGIN:
      transform_connect(contp, data);
      break;
    case State::CONNECT:
      transform_connect_event(contp, data, event, edata);
      break;
    case State::WRITE_HEADER:
      transform_write_header_event(contp, data, event, edata);
      break;
    case State::WRITE_BODY:
      transform_write_event(contp, data, event, edata);
      break;
    case State::READ_ICAP_HEADER:
      transform_read_icap_header_event(contp, data, event, edata);
      break;
    case State::READ_HTTP_HEADER:
      transform_read_http_header_event(contp, data, event, edata);
      break;
    case State::READ_HTTP_BODY:
      transform_read_http_body_event(contp, data, event, edata);
      break;
    case State::SEND_ERROR_MSG:
      transform_send_error_msg_event(contp, data, event, edata);
      break;
    case State::BYPASS:
      transform_bypass_event(contp, data, event, edata);
      break;
    case State::BUFFER_OS_RESP:
      transform_buffer_os_resp_event(contp, data, event, edata);
      break;
    case State::SEND_OS_RESP:
      transform_send_os_resp_event(contp, data, event, edata);
      break;
    }
  }

  return 0;
}

static int
request_ok(TSHttpTxn txnp)
{
  /* Is the initial client request OK for transformation. This is a
     good place to check accept headers to see if the client can
     accept a transformed document. */
  return 1;
}

static int
server_response_ok(TSHttpTxn txnp)
{
  /* Is the response the server sent OK for transformation. This is
   * a good place to check the server's response to see if it is
   * transformable. In this example, we will transform only "200 OK"
   * responses.
   */

  TSMBuffer bufp;
  TSMLoc hdr_loc;
  TSHttpStatus resp_status;

  /* Check if incoming port is carp port, in which case don't initiate
   * transform.
   */
  if (carp_port == get_port(TSHttpTxnServerAddrGet(txnp))) {
    return 0;
  }

  if (TSHttpTxnServerRespGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    TSError("[%s] Unable to get handle to Server Response", PLUGIN_NAME);
    return 0;
  }

  resp_status = TSHttpHdrStatusGet(bufp, hdr_loc);
  if (TS_HTTP_STATUS_OK == resp_status) {
    if (TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc) != TS_SUCCESS) {
      TSError("[%s] Unable to release handle to server request", PLUGIN_NAME);
    }
    return 1;
  } else {
    if (TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc) != TS_SUCCESS) {
      TSError("[%s] Unable to release handle to server request", PLUGIN_NAME);
    }
    return 0;
  }
}

static int
transform_plugin(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    if (request_ok(txnp)) {
      TSHttpTxnHookAdd(txnp, TS_HTTP_READ_RESPONSE_HDR_HOOK, contp);
    }
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;
  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    if (server_response_ok(txnp)) {
      TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, transform_create(txnp));
    }
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;
  default:
    break;
  }
  return 0;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;
  TSCont cont;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);
  }

  server_ip     = std::string(argv[1]);
  server_port   = std::stoi(argv[2]);
  carp_port     = std::stoi(argv[3]);
  debug_enabled = std::stoi(argv[4]);

  /* Initialize stats */
  if (TSStatFindName("plugin." PLUGIN_NAME ".scan_passed", &scan_passed) == TS_ERROR) {
    scan_passed = TSStatCreate("plugin." PLUGIN_NAME ".scan_passed", TS_RECORDDATATYPE_INT, TS_STAT_PERSISTENT, TS_STAT_SYNC_COUNT);
  }

  if (TSStatFindName("plugin." PLUGIN_NAME ".scan_failed", &scan_failed) == TS_ERROR) {
    scan_failed = TSStatCreate("plugin." PLUGIN_NAME ".scan_failed", TS_RECORDDATATYPE_INT, TS_STAT_PERSISTENT, TS_STAT_SYNC_COUNT);
  }

  if (TSStatFindName("plugin." PLUGIN_NAME ".icap_conn_failed", &icap_conn_failed) == TS_ERROR) {
    icap_conn_failed =
      TSStatCreate("plugin." PLUGIN_NAME ".icap_conn_failed", TS_RECORDDATATYPE_INT, TS_STAT_PERSISTENT, TS_STAT_SYNC_COUNT);
  }

  if (TSStatFindName("plugin." PLUGIN_NAME ".total_icap_invalid", &total_icap_invalid) == TS_ERROR) {
    total_icap_invalid =
      TSStatCreate("plugin." PLUGIN_NAME ".total_icap_invalid", TS_RECORDDATATYPE_INT, TS_STAT_PERSISTENT, TS_STAT_SYNC_COUNT);
  }

  if (TSStatFindName("plugin." PLUGIN_NAME ".icap_response_err", &icap_response_err) == TS_ERROR) {
    icap_response_err =
      TSStatCreate("plugin." PLUGIN_NAME ".icap_response_err", TS_RECORDDATATYPE_INT, TS_STAT_PERSISTENT, TS_STAT_SYNC_COUNT);
  }

  if (TSStatFindName("plugin." PLUGIN_NAME ".icap_write_failed", &icap_write_failed) == TS_ERROR) {
    icap_write_failed =
      TSStatCreate("plugin." PLUGIN_NAME ".icap_write_failed", TS_RECORDDATATYPE_INT, TS_STAT_PERSISTENT, TS_STAT_SYNC_COUNT);
  }

  TSStatIntSet(scan_passed, 0);
  TSStatIntSet(scan_failed, 0);
  TSStatIntSet(icap_conn_failed, 0);
  TSStatIntSet(icap_write_failed, 0);
  TSStatIntSet(icap_response_err, 0);
  TSStatIntSet(total_icap_invalid, 0);

  cont = TSContCreate(transform_plugin, nullptr);
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, cont);
}
