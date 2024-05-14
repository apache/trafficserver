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

#include "proxy/http3/Http3App.h"

#include <utility>

#include "tscore/ink_resolver.h"

#include "../../iocore/net/P_Net.h"
#include "../../iocore/eventsystem/P_VConnection.h"
#include "iocore/net/quic/QUICStreamManager.h"
#include "iocore/net/quic/QUICStreamVCAdapter.h"

#include "proxy/http3/Http3.h"
#include "proxy/http3/Http3Config.h"
#include "proxy/http3/Http3DebugNames.h"
#include "proxy/http3/Http3Session.h"
#include "proxy/http3/Http3Transaction.h"
#include "proxy/http3/Http3ProtocolEnforcer.h"
#include "proxy/http3/Http3SettingsHandler.h"
#include "proxy/http3/Http3SettingsFramer.h"

namespace
{
constexpr char debug_tag[]   = "http3";
constexpr char debug_tag_v[] = "v_http3";

DbgCtl dbg_ctl{debug_tag};
DbgCtl dbg_ctl_v{debug_tag_v};

} // end anonymous namespace

Http3App::Http3App(NetVConnection *client_vc, QUICConnection *qc, IpAllow::ACL &&session_acl,
                   const HttpSessionAccept::Options &options)
  : QUICApplication(qc)
{
  this->_ssn                 = new Http3Session(client_vc);
  this->_ssn->acl            = std::move(session_acl);
  this->_ssn->accept_options = &options;
  this->_ssn->new_connection(client_vc, nullptr, nullptr);

  this->_qc->stream_manager()->set_default_application(this);

  this->_control_stream_dispatcher.add_handler(this->_ssn->get_received_frame_counter());

  this->_protocol_enforcer = new Http3ProtocolEnforcer();
  this->_control_stream_dispatcher.add_handler(this->_protocol_enforcer);

  this->_settings_handler = new Http3SettingsHandler(this->_ssn);
  this->_control_stream_dispatcher.add_handler(this->_settings_handler);

  this->_settings_framer = new Http3SettingsFramer(client_vc->get_context());
  this->_control_stream_collector.add_generator(this->_settings_framer);

  SET_HANDLER(&Http3App::main_event_handler);
}

Http3App::~Http3App()
{
  delete this->_ssn;
  delete this->_settings_handler;
  delete this->_settings_framer;
}

void
Http3App::start()
{
  QUICStreamId            stream_id{};
  QUICConnectionErrorUPtr error;

  error = this->create_uni_stream(stream_id, Http3StreamType::CONTROL);

  // TODO: Open uni streams for QPACK when dynamic table is used
  // error = this->create_uni_stream(stream_id, Http3StreamType::QPACK_ENCODER);
  // if (error == nullptr) {
  //   this->_handle_uni_stream_on_write_ready(VC_EVENT_WRITE_READY, this->_find_stream_io(stream_id));
  // }

  // error = this->create_uni_stream(stream_id, Http3StreamType::QPACK_DECODER);
  // if (error == nullptr) {
  //   this->_handle_uni_stream_on_write_ready(VC_EVENT_WRITE_READY, this->_find_stream_io(stream_id));
  // }
}

void
Http3App::on_stream_open(QUICStream &stream)
{
  auto  ret  = this->_streams.emplace(stream.id(), stream);
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

void
Http3App::on_stream_close(QUICStream &stream)
{
  this->_streams.erase(stream.id());
}

int
Http3App::main_event_handler(int event, Event *data)
{
  Dbg(dbg_ctl_v, "[%s] %s (%d)", this->_qc->cids().data(), get_vc_event_name(event), event);

  VIO                 *vio     = reinterpret_cast<VIO *>(data->cookie);
  QUICStreamVCAdapter *adapter = static_cast<QUICStreamVCAdapter *>(vio->vc_server);

  if (adapter == nullptr) {
    Dbg(dbg_ctl, "[%s] Unknown Stream", this->_qc->cids().data());
    return -1;
  }

  bool is_bidirectional = adapter->stream().is_bidirectional();

  switch (event) {
  case VC_EVENT_READ_READY:
    adapter->clear_read_ready_event(data);
    if (is_bidirectional) {
      this->_handle_bidi_stream_on_read_ready(event, vio);
    } else {
      this->_handle_uni_stream_on_read_ready(event, vio);
    }
    break;
  case VC_EVENT_READ_COMPLETE:
    adapter->clear_read_complete_event(data);
    if (is_bidirectional) {
      this->_handle_bidi_stream_on_read_complete(event, vio);
    } else {
      this->_handle_uni_stream_on_read_complete(event, vio);
    }
    break;
  case VC_EVENT_WRITE_READY:
    adapter->clear_write_ready_event(data);
    if (is_bidirectional) {
      this->_handle_bidi_stream_on_write_ready(event, vio);
    } else {
      this->_handle_uni_stream_on_write_ready(event, vio);
    }
    break;
  case VC_EVENT_WRITE_COMPLETE:
    adapter->clear_write_complete_event(data);
    if (is_bidirectional) {
      this->_handle_bidi_stream_on_write_complete(event, vio);
    } else {
      this->_handle_uni_stream_on_write_complete(event, vio);
    }
    break;
  case VC_EVENT_EOS:
    adapter->clear_eos_event(data);
    if (is_bidirectional) {
      this->_handle_bidi_stream_on_eos(event, vio);
    } else {
      this->_handle_uni_stream_on_eos(event, vio);
    }
    break;
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

QUICConnectionErrorUPtr
Http3App::create_uni_stream(QUICStreamId &new_stream_id, Http3StreamType type)
{
  QUICConnectionErrorUPtr error = this->_qc->stream_manager()->create_uni_stream(new_stream_id);

  if (error == nullptr) {
    this->_local_uni_stream_map.insert(std::make_pair(new_stream_id, type));

    Dbg(dbg_ctl, "[%" PRIu64 "] %s stream is created", new_stream_id, Http3DebugNames::stream_type(type));
  } else {
    Dbg(dbg_ctl, "Could not create %s stream", Http3DebugNames::stream_type(type));
  }

  return error;
}

void
Http3App::_handle_uni_stream_on_read_ready(int /* event */, VIO *vio)
{
  Http3ErrorUPtr       error = Http3ErrorUPtr(nullptr);
  Http3StreamType      type;
  QUICStreamVCAdapter *adapter = static_cast<QUICStreamVCAdapter *>(vio->vc_server);
  auto                 it      = this->_remote_uni_stream_map.find(adapter->stream().id());
  if (it == this->_remote_uni_stream_map.end()) {
    // Set uni stream suitable app (HTTP/3 or QPACK) by stream type
    uint8_t buf;
    vio->get_reader()->read(&buf, 1);
    type = Http3Stream::type(&buf);

    Dbg(dbg_ctl, "[%" PRIu64 "] %s stream is opened", adapter->stream().id(), Http3DebugNames::stream_type(type));

    auto ret = this->_remote_uni_stream_map.insert(std::make_pair(adapter->stream().id(), type));
    if (!ret.second) {
      // A stream for the type is already exists
      // TODO Return an error
    }
  } else {
    type = it->second;
  }

  switch (type) {
  case Http3StreamType::CONTROL: {
    if (this->_control_stream_id == 0) {
      this->_control_stream_id = adapter->stream().id();
    } else if (this->_control_stream_id != adapter->stream().id()) {
      error = std::make_unique<Http3Error>(Http3ErrorClass::CONNECTION, Http3ErrorCode::H3_STREAM_CREATION_ERROR,
                                           "Only one control stream per peer is permitted");
      Dbg(dbg_ctl, "CONTROL stream [%" PRIu64 "] error: %hu, %s", this->_control_stream_id, error->get_code(), error->msg);
      break;
    }
    uint64_t nread = 0;
    error          = this->_control_stream_dispatcher.on_read_ready(adapter->stream().id(), type, *vio->get_reader(), nread);
    if (error && error->cls != Http3ErrorClass::UNDEFINED) {
      Dbg(dbg_ctl, "CONTROL stream [%" PRIu64 "] error: %hu, %s", this->_control_stream_id, error->get_code(), error->msg);
    }
    // The sender MUST NOT close the control stream, and the receiver MUST NOT request that the sender close the control stream.
    // If either control stream is closed at any point, this MUST be treated as a connection error of type
    // H3_CLOSED_CRITICAL_STREAM.
    break;
  }
  case Http3StreamType::PUSH: {
    error =
      std::make_unique<Http3Error>(Http3ErrorClass::CONNECTION, Http3ErrorCode::H3_STREAM_CREATION_ERROR, "Only servers can push");
    Dbg(dbg_ctl, "PUSH stream [%" PRIu64 "] error: %hu, %s", adapter->stream().id(), error->get_code(), error->msg);
    // if a server receives a client-initiated push stream, this MUST be treated as a connection error of type
    // H3_STREAM_CREATION_ERROR
    break;
  }
  case Http3StreamType::QPACK_ENCODER:
  case Http3StreamType::QPACK_DECODER: {
    this->_set_qpack_stream(type, adapter);
    break;
  }
  case Http3StreamType::UNKNOWN: {
    // Recipients of unknown stream types MUST either abort reading of the stream or discard incoming data without further
    // processing. If reading is aborted, the recipient SHOULD use the H3_STREAM_CREATION_ERROR error code or a reserved error code
    // (Section 8.1). The recipient MUST NOT consider unknown stream types to be a connection error of any kind.
    error = std::make_unique<Http3Error>(Http3ErrorClass::STREAM, Http3ErrorCode::H3_STREAM_CREATION_ERROR, "Stream type unkown");
    Dbg(dbg_ctl, "UNKNOWN stream [%" PRIu64 "] error: %hu, %s", adapter->stream().id(), error->get_code(), error->msg);
    break;
  }
  default:
    break;
  }
}

void
Http3App::_handle_uni_stream_on_read_complete(int event, VIO *vio)
{
  this->_handle_uni_stream_on_read_ready(event, vio);

  QUICStreamVCAdapter *adapter = static_cast<QUICStreamVCAdapter *>(vio->vc_server);
  if (!adapter->is_writable()) {
    this->_qc->stream_manager()->delete_stream(adapter->stream().id());
  }
}

void
Http3App::_handle_bidi_stream_on_read_ready(int event, VIO *vio)
{
  QUICStreamVCAdapter *adapter = static_cast<QUICStreamVCAdapter *>(vio->vc_server);

  QUICStreamId      stream_id = adapter->stream().id();
  Http3Transaction *txn       = static_cast<Http3Transaction *>(this->_ssn->get_transaction(stream_id));

  if (txn == nullptr) {
    if (auto ret = this->_streams.find(stream_id); ret != this->_streams.end()) {
      txn = new Http3Transaction(this->_ssn, ret->second);
      SCOPED_MUTEX_LOCK(lock, txn->mutex, this_ethread());
      txn->new_transaction();
    } else {
      ink_assert(!"Stream info should exist");
    }
  } else {
    SCOPED_MUTEX_LOCK(lock, txn->mutex, this_ethread());
    txn->handleEvent(event);
  }
}

void
Http3App::_handle_bidi_stream_on_read_complete(int event, VIO *vio)
{
  this->_handle_bidi_stream_on_read_ready(event, vio);

  QUICStreamVCAdapter *adapter = static_cast<QUICStreamVCAdapter *>(vio->vc_server);
  if (!adapter->is_writable()) {
    this->_qc->stream_manager()->delete_stream(adapter->stream().id());
  }
}

void
Http3App::_handle_uni_stream_on_write_ready(int /* event */, VIO *vio)
{
  QUICStreamVCAdapter *adapter = static_cast<QUICStreamVCAdapter *>(vio->vc_server);

  auto it = this->_local_uni_stream_map.find(adapter->stream().id());
  if (it == this->_local_uni_stream_map.end()) {
    ink_abort("stream not found");
    return;
  }

  switch (it->second) {
  case Http3StreamType::CONTROL:
    if (!this->_is_control_stream_initialized) {
      uint8_t buf[] = {static_cast<uint8_t>(it->second)};
      vio->get_writer()->write(buf, sizeof(uint8_t));
      this->_is_control_stream_initialized = true;
    } else {
      size_t nwritten = 0;
      bool   all_done = false;
      this->_control_stream_collector.on_write_ready(adapter->stream().id(), *vio->get_writer(), nwritten, all_done);
      vio->nbytes += nwritten;
      if (all_done) {
        vio->done();
      }
    }
    break;
  case Http3StreamType::QPACK_ENCODER:
  case Http3StreamType::QPACK_DECODER: {
    this->_set_qpack_stream(it->second, adapter);
  }
  case Http3StreamType::UNKNOWN:
  case Http3StreamType::PUSH:
  default:
    break;
  }
}

void
Http3App::_handle_uni_stream_on_write_complete(int event, VIO *vio)
{
  this->_handle_uni_stream_on_write_ready(event, vio);

  QUICStreamVCAdapter *adapter = static_cast<QUICStreamVCAdapter *>(vio->vc_server);
  if (!adapter->is_readable()) {
    this->_qc->stream_manager()->delete_stream(adapter->stream().id());
  }
}

void
Http3App::_handle_bidi_stream_on_eos(int /* event */, VIO *vio)
{
  // TODO: handle eos
}

void
Http3App::_handle_uni_stream_on_eos(int /* event */, VIO *v)
{
  // TODO: handle eos
}

void
Http3App::_set_qpack_stream(Http3StreamType type, QUICStreamVCAdapter *adapter)
{
  // Change app to QPACK from Http3
  if (type == Http3StreamType::QPACK_ENCODER) {
    if (this->_qc->direction() == NET_VCONNECTION_IN) {
      this->_ssn->remote_qpack()->set_encoder_stream(adapter->stream().id());
      this->_update_vio_cont_to_QPACK(this->_ssn->remote_qpack(), adapter);
    } else {
      this->_ssn->local_qpack()->set_encoder_stream(adapter->stream().id());
      this->_update_vio_cont_to_QPACK(this->_ssn->local_qpack(), adapter);
    }
  } else if (type == Http3StreamType::QPACK_DECODER) {
    if (this->_qc->direction() == NET_VCONNECTION_IN) {
      this->_ssn->local_qpack()->set_decoder_stream(adapter->stream().id());
      this->_update_vio_cont_to_QPACK(this->_ssn->local_qpack(), adapter);
    } else {
      this->_ssn->remote_qpack()->set_decoder_stream(adapter->stream().id());
      this->_update_vio_cont_to_QPACK(this->_ssn->remote_qpack(), adapter);
    }
  } else {
    ink_abort("unknown stream type");
  }
}

QUICStreamVCAdapter::IOInfo &
Http3App::_get_stream_info(QUICStreamId stream_id)
{
  auto it = this->_streams.find(stream_id);
  if (it == this->_streams.end()) {
    ink_abort("stream not found");
  }
  return it->second;
}

void
Http3App::_update_vio_cont_to_QPACK(QPACK *qpack, QUICStreamVCAdapter *adapter)
{
  switch (adapter->stream().direction()) {
  case QUICStreamDirection::BIDIRECTIONAL:
    this->_get_stream_info(adapter->stream().id()).update_read_vio(qpack);
    this->_get_stream_info(adapter->stream().id()).update_write_vio(qpack);
    break;
  case QUICStreamDirection::SEND:
    this->_get_stream_info(adapter->stream().id()).update_write_vio(qpack);
    break;
  case QUICStreamDirection::RECEIVE:
    this->_get_stream_info(adapter->stream().id()).update_read_vio(qpack);
    break;
  default:
    ink_assert(false);
    break;
  }
}

void
Http3App::_handle_bidi_stream_on_write_ready(int event, VIO *vio)
{
  QUICStreamVCAdapter *adapter = static_cast<QUICStreamVCAdapter *>(vio->vc_server);

  QUICStreamId      stream_id = adapter->stream().id();
  Http3Transaction *txn       = static_cast<Http3Transaction *>(this->_ssn->get_transaction(stream_id));
  if (txn != nullptr) {
    SCOPED_MUTEX_LOCK(lock, txn->mutex, this_ethread());
    txn->handleEvent(event);
  }
}

void
Http3App::_handle_bidi_stream_on_write_complete(int event, VIO *vio)
{
  this->_handle_bidi_stream_on_write_ready(event, vio);

  QUICStreamVCAdapter *adapter = static_cast<QUICStreamVCAdapter *>(vio->vc_server);
  if (!adapter->is_readable()) {
    this->_qc->stream_manager()->delete_stream(adapter->stream().id());
  }
}
