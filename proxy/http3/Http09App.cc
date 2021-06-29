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
#include "P_QUICNetVConnection.h"
#include "QUICDebugNames.h"
#include "QUICStreamVCAdapter.h"

#include "Http3Session.h"
#include "Http3Transaction.h"

static constexpr char debug_tag[]   = "quic_simple_app";
static constexpr char debug_tag_v[] = "v_quic_simple_app";

Http09App::Http09App(QUICNetVConnection *client_vc, IpAllow::ACL &&session_acl, const HttpSessionAccept::Options &options)
  : QUICApplication(client_vc)
{
  this->_ssn                 = new Http09Session(client_vc);
  this->_ssn->acl            = std::move(session_acl);
  this->_ssn->accept_options = &options;
  this->_ssn->new_connection(client_vc, nullptr, nullptr);

  this->_qc->stream_manager()->set_default_application(this);

  SET_HANDLER(&Http09App::main_event_handler);
}

Http09App::~Http09App()
{
  delete this->_ssn;
}

void
Http09App::on_new_stream(QUICStream &stream)
{
  auto ret   = this->_streams.emplace(stream.id(), stream);
  auto &info = ret.first->second;

  switch (stream.direction()) {
  case QUICStreamDirection::BIDIRECTIONAL:
    info.setup_read_vio(this);
    info.setup_write_vio(this);
    break;
  case QUICStreamDirection::SEND:
    info.setup_write_vio(this);
    break;
  case QUICStreamDirection::RECEIVE:
    info.setup_read_vio(this);
    break;
  default:
    ink_assert(false);
    break;
  }

  stream.set_io_adapter(&info.adapter);
}

int
Http09App::main_event_handler(int event, Event *data)
{
  Debug(debug_tag_v, "[%s] %s (%d)", this->_qc->cids().data(), get_vc_event_name(event), event);

  VIO *vio                     = reinterpret_cast<VIO *>(data->cookie);
  QUICStreamVCAdapter *adapter = static_cast<QUICStreamVCAdapter *>(vio->vc_server);

  if (adapter == nullptr) {
    Debug(debug_tag, "[%s] Unknown Stream", this->_qc->cids().data());
    return -1;
  }

  bool is_bidirectional = adapter->stream().is_bidirectional();

  QUICStreamId stream_id = adapter->stream().id();
  Http09Transaction *txn = static_cast<Http09Transaction *>(this->_ssn->get_transaction(stream_id));

  switch (event) {
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE:
    if (!is_bidirectional) {
      // FIXME Ignore unidirectional streams for now
      break;
    }

    if (txn == nullptr) {
      if (auto ret = this->_streams.find(stream_id); ret != this->_streams.end()) {
        txn = new Http09Transaction(this->_ssn, ret->second);
        SCOPED_MUTEX_LOCK(lock, txn->mutex, this_ethread());
        txn->new_transaction();
      } else {
        ink_assert(!"Stream info should exist");
      }
    } else {
      SCOPED_MUTEX_LOCK(lock, txn->mutex, this_ethread());
      txn->handleEvent(event);
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
