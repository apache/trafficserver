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

  int res = getaddrinfo(this->_remote_addr, this->_remote_port, &hints, &this->_remote_addr_info);
  if (res < 0) {
    Debug("quic_client", "Error: %s (%d)", strerror(errno), errno);
    return EVENT_DONE;
  }

  for (struct addrinfo *info = this->_remote_addr_info; info != nullptr; info = info->ai_next) {
    NetVCOptions opt;
    opt.ip_proto            = NetVCOptions::USE_UDP;
    opt.ip_family           = info->ai_family;
    opt.etype               = ET_NET;
    opt.socket_recv_bufsize = 1048576;
    opt.socket_send_bufsize = 1048576;

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
    QUICClientApp *app       = new QUICClientApp(conn);
    app->start(this->_path);

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
// QUICClientApp
//
#define QUICClientAppDebug(fmt, ...) \
  Debug("quic_client_app", "[%" PRIx64 "] " fmt, static_cast<uint64_t>(this->_qc->connection_id()), ##__VA_ARGS__)

QUICClientApp::QUICClientApp(QUICNetVConnection *qvc) : QUICApplication(qvc)
{
  this->_qc->stream_manager()->set_default_application(this);

  SET_HANDLER(&QUICClientApp::main_event_handler);
}

void
QUICClientApp::start(const char *path)
{
  QUICStreamId stream_id;
  QUICErrorUPtr error = this->_qc->stream_manager()->create_bidi_stream(stream_id);

  if (error->cls != QUICErrorClass::NONE) {
    Error("%s", error->msg);
    ink_assert(abort);
  }

  // TODO: move to transaction
  char request[1024] = {0};
  int request_len    = snprintf(request, sizeof(request), "GET %s\r\n", path);

  QUICClientAppDebug("\n%s", request);

  QUICStreamIO *stream_io = this->_find_stream_io(stream_id);

  stream_io->write(reinterpret_cast<uint8_t *>(request), request_len);
  stream_io->write_reenable();
}

int
QUICClientApp::main_event_handler(int event, Event *data)
{
  QUICClientAppDebug("%s (%d)", get_vc_event_name(event), event);

  VIO *vio                = reinterpret_cast<VIO *>(data);
  QUICStreamIO *stream_io = this->_find_stream_io(vio);

  if (stream_io == nullptr) {
    QUICClientAppDebug("Unknown Stream");
    return -1;
  }

  switch (event) {
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE:
    if (stream_io->is_read_avail_more_than(0)) {
      uint8_t response[1024] = {0};
      stream_io->read(response, sizeof(response));
      QUICClientAppDebug("\n%s", response);
    }
    break;
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
