/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "quic_client.h"

#include <iostream>
#include <fstream>
#include <string_view>

#include "Http3Transaction.h"

// OpenSSL protocol-lists format (vector of 8-bit length-prefixed, byte strings)
// https://www.openssl.org/docs/manmaster/man3/SSL_CTX_set_alpn_protos.html
// Should be integrate with IP_PROTO_TAG_HTTP_QUIC in ts/ink_inet.h ?
using namespace std::literals;
static constexpr std::string_view HQ_ALPN_PROTO_LIST("\5hq-20"sv);
static constexpr std::string_view H3_ALPN_PROTO_LIST("\5h3-20"sv);

QUICClient::QUICClient(const QUICClientConfig *config) : Continuation(new_ProxyMutex()), _config(config)
{
  SET_HANDLER(&QUICClient::start);
}

QUICClient::~QUICClient()
{
  freeaddrinfo(this->_remote_addr_info);
}

int
QUICClient::start(int, void *)
{
  SET_HANDLER(&QUICClient::state_http_server_open);

  struct addrinfo hints;

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family   = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags    = 0;
  hints.ai_protocol = 0;

  int res = getaddrinfo(this->_config->addr, this->_config->port, &hints, &this->_remote_addr_info);
  if (res < 0) {
    Debug("quic_client", "Error: %s (%d)", strerror(errno), errno);
    return EVENT_DONE;
  }

  std::string_view alpn_protos;
  if (this->_config->http3) {
    alpn_protos = H3_ALPN_PROTO_LIST;
  } else {
    alpn_protos = HQ_ALPN_PROTO_LIST;
  }

  for (struct addrinfo *info = this->_remote_addr_info; info != nullptr; info = info->ai_next) {
    NetVCOptions opt;
    opt.ip_proto            = NetVCOptions::USE_UDP;
    opt.ip_family           = info->ai_family;
    opt.etype               = ET_NET;
    opt.socket_recv_bufsize = 1048576;
    opt.socket_send_bufsize = 1048576;
    opt.alpn_protos         = alpn_protos;
    opt.set_sni_servername(this->_config->addr, strnlen(this->_config->addr, 1023));

    SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());

    Action *action = quic_NetProcessor.connect_re(this, info->ai_addr, &opt);
    if (action == ACTION_RESULT_DONE) {
      break;
    }
  }
  return EVENT_CONT;
}

// Similar to HttpSM::state_http_server_open(int event, void *data)
int
QUICClient::state_http_server_open(int event, void *data)
{
  switch (event) {
  case NET_EVENT_OPEN: {
    // TODO: create ProxyServerSession / ProxyServerTransaction
    Debug("quic_client", "start proxy server ssn/txn");

    QUICNetVConnection *conn = static_cast<QUICNetVConnection *>(data);

    if (this->_config->http0_9) {
      Http09ClientApp *app = new Http09ClientApp(conn, this->_config);
      app->start();
    } else if (this->_config->http3) {
      // TODO: see what server session is doing with IpAllow::ACL
      IpAllow::ACL session_acl;
      Http3ClientApp *app = new Http3ClientApp(conn, std::move(session_acl), options, this->_config);
      SCOPED_MUTEX_LOCK(lock, app->mutex, this_ethread());
      app->start();
    } else {
      ink_abort("invalid config");
    }

    break;
  }
  case NET_EVENT_OPEN_FAILED: {
    ink_assert(false);
    break;
  }
  case NET_EVENT_ACCEPT: {
    // do nothing
    break;
  }
  default:
    ink_assert(false);
  }

  return 0;
}

//
// Http09ClientApp
//
#define Http09ClientAppDebug(fmt, ...) Debug("quic_client_app", "[%s] " fmt, this->_qc->cids().data(), ##__VA_ARGS__)
#define Http09ClientAppVDebug(fmt, ...) Debug("v_quic_client_app", "[%s] " fmt, this->_qc->cids().data(), ##__VA_ARGS__)

Http09ClientApp::Http09ClientApp(QUICNetVConnection *qvc, const QUICClientConfig *config) : QUICApplication(qvc), _config(config)
{
  this->_qc->stream_manager()->set_default_application(this);

  SET_HANDLER(&Http09ClientApp::main_event_handler);
}

void
Http09ClientApp::start()
{
  if (this->_config->output[0] != 0x0) {
    this->_filename = this->_config->output;
  }

  if (this->_filename) {
    // Destroy contents if file already exists
    std::ofstream f_stream(this->_filename, std::ios::binary | std::ios::trunc);
  }

  this->_do_http_request();
}

void
Http09ClientApp::_do_http_request()
{
  QUICStreamId stream_id;
  QUICConnectionErrorUPtr error = this->_qc->stream_manager()->create_bidi_stream(stream_id);

  if (error != nullptr) {
    Error("%s", error->msg);
    ink_abort("Could not create bidi stream : %s", error->msg);
  }

  // TODO: move to transaction
  char request[1024] = {0};
  int request_len    = snprintf(request, sizeof(request), "GET %s\r\n", this->_config->path);

  Http09ClientAppDebug("\n%s", request);

  QUICStreamIO *stream_io = this->_find_stream_io(stream_id);

  stream_io->write(reinterpret_cast<uint8_t *>(request), request_len);
  stream_io->write_done();
  stream_io->write_reenable();
}

int
Http09ClientApp::main_event_handler(int event, Event *data)
{
  Http09ClientAppVDebug("%s (%d)", get_vc_event_name(event), event);

  VIO *vio                = reinterpret_cast<VIO *>(data);
  QUICStreamIO *stream_io = this->_find_stream_io(vio);

  if (stream_io == nullptr) {
    Http09ClientAppDebug("Unknown Stream");
    return -1;
  }

  switch (event) {
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE: {
    std::streambuf *default_stream = nullptr;
    std::ofstream f_stream;

    if (this->_filename) {
      default_stream = std::cout.rdbuf();
      f_stream       = std::ofstream(this->_filename, std::ios::binary | std::ios::app);
      std::cout.rdbuf(f_stream.rdbuf());
    }

    uint8_t buf[8192] = {0};
    int64_t nread;
    while ((nread = stream_io->read(buf, sizeof(buf))) > 0) {
      std::cout.write(reinterpret_cast<char *>(buf), nread);
    }
    std::cout.flush();

    if (this->_filename) {
      f_stream.close();
      std::cout.rdbuf(default_stream);
    }

    if (stream_io->is_read_done() && this->_config->close) {
      // Connection Close Exercise
      this->_qc->close(QUICConnectionErrorUPtr(new QUICConnectionError(QUICTransErrorCode::NO_ERROR, "Close Exercise")));
    }

    break;
  }
  case VC_EVENT_WRITE_READY:
  case VC_EVENT_WRITE_COMPLETE:
    break;
  case VC_EVENT_EOS:
  case VC_EVENT_ERROR:
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
    ink_assert(false);
    break;
  default:
    break;
  }

  return EVENT_CONT;
}

//
// Http3ClientApp
//
Http3ClientApp::Http3ClientApp(QUICNetVConnection *qvc, IpAllow::ACL &&session_acl, const HttpSessionAccept::Options &options,
                               const QUICClientConfig *config)
  : super(qvc, std::move(session_acl), options), _config(config)
{
}

Http3ClientApp::~Http3ClientApp()
{
  free_MIOBuffer(this->_req_buf);
  this->_req_buf = nullptr;

  free_MIOBuffer(this->_resp_buf);
  this->_resp_buf = nullptr;

  delete this->_resp_handler;
}

void
Http3ClientApp::start()
{
  this->_req_buf                  = new_MIOBuffer();
  this->_resp_buf                 = new_MIOBuffer();
  IOBufferReader *resp_buf_reader = _resp_buf->alloc_reader();

  this->_resp_handler = new RespHandler(this->_config, resp_buf_reader);

  super::start();
  this->_do_http_request();
}

void
Http3ClientApp::_do_http_request()
{
  QUICConnectionErrorUPtr error;
  QUICStreamId stream_id;
  error = this->_qc->stream_manager()->create_bidi_stream(stream_id);
  if (error != nullptr) {
    Error("%s", error->msg);
    ink_abort("Could not create bidi stream : %s", error->msg);
  }

  QUICStreamIO *stream_io = this->_find_stream_io(stream_id);

  // TODO: create Http3ServerTransaction
  Http3Transaction *txn = new Http3Transaction(this->_ssn, stream_io);
  SCOPED_MUTEX_LOCK(lock, txn->mutex, this_ethread());

  // TODO: fix below issue with H2 origin conn stuff
  // Do not call ProxyClientTransaction::new_transaction(), but need to setup txn - e.g. do_io_write / do_io_read
  VIO *read_vio = txn->do_io_read(this->_resp_handler, INT64_MAX, this->_resp_buf);
  this->_resp_handler->set_read_vio(read_vio);

  // Write HTTP Request to write_vio
  char request[1024] = {0};
  std::string format;
  if (this->_config->path[0] == '/') {
    format = "GET https://%s%s HTTP/1.1\r\n\r\n";
  } else {
    format = "GET https://%s/%s HTTP/1.1\r\n\r\n";
  }

  int request_len = snprintf(request, sizeof(request), format.c_str(), this->_config->addr, this->_config->path);

  Http09ClientAppDebug("\n%s", request);

  // TODO: check write avail size
  int64_t nbytes            = this->_req_buf->write(request, request_len);
  IOBufferReader *buf_start = this->_req_buf->alloc_reader();
  txn->do_io_write(this, nbytes, buf_start);
}

//
// Response Handler
//
RespHandler::RespHandler(const QUICClientConfig *config, IOBufferReader *reader)
  : Continuation(new_ProxyMutex()), _config(config), _reader(reader)
{
  if (this->_config->output[0] != 0x0) {
    this->_filename = this->_config->output;
  }

  if (this->_filename) {
    // Destroy contents if file already exists
    std::ofstream f_stream(this->_filename, std::ios::binary | std::ios::trunc);
  }

  SET_HANDLER(&RespHandler::main_event_handler);
}

void
RespHandler::set_read_vio(VIO *vio)
{
  this->_read_vio = vio;
}

int
RespHandler::main_event_handler(int event, Event *data)
{
  Debug("v_http3", "%s", get_vc_event_name(event));
  switch (event) {
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE: {
    std::streambuf *default_stream = nullptr;
    std::ofstream f_stream;

    if (this->_filename) {
      default_stream = std::cout.rdbuf();
      f_stream       = std::ofstream(this->_filename, std::ios::binary | std::ios::app);
      std::cout.rdbuf(f_stream.rdbuf());
    }

    uint8_t buf[8192] = {0};
    int64_t nread;
    while ((nread = this->_reader->read(buf, sizeof(buf))) > 0) {
      std::cout.write(reinterpret_cast<char *>(buf), nread);
      this->_read_vio->ndone += nread;
    }
    std::cout.flush();

    if (this->_filename) {
      f_stream.close();
      std::cout.rdbuf(default_stream);
    }

    break;
  }
  case VC_EVENT_WRITE_READY:
  case VC_EVENT_WRITE_COMPLETE:
  default:
    break;
  }

  return EVENT_CONT;
}
