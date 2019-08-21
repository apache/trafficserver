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

#include "Http09App.h"

#include "tscore/ink_resolver.h"

#include "P_Net.h"
#include "P_VConnection.h"
#include "QUICDebugNames.h"

#include "Http3Session.h"
#include "Http3Transaction.h"

static constexpr char debug_tag[]   = "quic_simple_app";
static constexpr char debug_tag_v[] = "v_quic_simple_app";

Http09App::Http09App(QUICNetVConnection *client_vc, IpAllow::ACL &&session_acl, const HttpSessionAccept::Options &options)
  : QUICApplication(client_vc)
{
  this->_ssn      = new Http09Session(client_vc);
  this->_ssn->acl = std::move(session_acl);
  // TODO: avoid const cast
  this->_ssn->host_res_style =
    ats_host_res_from(client_vc->get_remote_addr()->sa_family, const_cast<HostResPreference *>(options.host_res_preference));
  this->_ssn->accept_options = &options;
  this->_ssn->new_connection(client_vc, nullptr, nullptr);

  this->_qc->stream_manager()->set_default_application(this);

  SET_HANDLER(&Http09App::main_event_handler);
}

Http09App::~Http09App()
{
  delete this->_ssn;
}

int
Http09App::main_event_handler(int event, Event *data)
{
  Debug(debug_tag_v, "[%s] %s (%d)", this->_qc->cids().data(), get_vc_event_name(event), event);

  VIO *vio                = reinterpret_cast<VIO *>(data);
  QUICStreamIO *stream_io = this->_find_stream_io(vio);

  if (stream_io == nullptr) {
    Debug(debug_tag, "[%s] Unknown Stream", this->_qc->cids().data());
    return -1;
  }

  QUICStreamId stream_id = stream_io->stream_id();
  Http09Transaction *txn = static_cast<Http09Transaction *>(this->_ssn->get_transaction(stream_id));

  uint8_t dummy;
  switch (event) {
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE:
    if (!stream_io->is_bidirectional()) {
      // FIXME Ignore unidirectional streams for now
      break;
    }
    if (stream_io->peek(&dummy, 1)) {
      if (txn == nullptr) {
        txn = new Http09Transaction(this->_ssn, stream_io);
        SCOPED_MUTEX_LOCK(lock, txn->mutex, this_ethread());

        txn->new_transaction();
      } else {
        SCOPED_MUTEX_LOCK(lock, txn->mutex, this_ethread());
        txn->handleEvent(event);
      }
    }
    break;
  case VC_EVENT_WRITE_READY:
  case VC_EVENT_WRITE_COMPLETE:
    if (txn != nullptr) {
      SCOPED_MUTEX_LOCK(lock, txn->mutex, this_ethread());
      txn->handleEvent(event);
    }
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
