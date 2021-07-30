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

#include "Http3App.h"

#include <utility>

#include "tscore/ink_resolver.h"

#include "P_Net.h"
#include "P_VConnection.h"
#include "P_QUICNetVConnection.h"
#include "QUICStreamVCAdapter.h"

#include "Http3.h"
#include "Http3Config.h"
#include "Http3DebugNames.h"
#include "Http3Session.h"
#include "Http3Transaction.h"

static constexpr char debug_tag[]   = "http3";
static constexpr char debug_tag_v[] = "v_http3";

Http3App::Http3App(QUICNetVConnection *client_vc, IpAllow::ACL &&session_acl, const HttpSessionAccept::Options &options)
  : QUICApplication(client_vc)
{
  this->_ssn                 = new Http3Session(client_vc);
  this->_ssn->acl            = std::move(session_acl);
  this->_ssn->accept_options = &options;
  this->_ssn->new_connection(client_vc, nullptr, nullptr);

  this->_qc->stream_manager()->set_default_application(this);

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
  QUICStreamId stream_id;
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
Http3App::on_new_stream(QUICStream &stream)
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
Http3App::main_event_handler(int event, Event *data)
{
  Debug(debug_tag_v, "[%s] %s (%d)", this->_qc->cids().data(), get_vc_event_name(event), event);

  VIO *vio                     = reinterpret_cast<VIO *>(data->cookie);
  QUICStreamVCAdapter *adapter = static_cast<QUICStreamVCAdapter *>(vio->vc_server);

  if (adapter == nullptr) {
    Debug(debug_tag, "[%s] Unknown Stream", this->_qc->cids().data());
    return -1;
  }

  bool is_bidirectional = adapter->stream().is_bidirectional();

  switch (event) {
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE:
    if (is_bidirectional) {
      this->_handle_bidi_stream_on_read_ready(event, vio);
    } else {
      this->_handle_uni_stream_on_read_ready(event, vio);
    }
    break;
  case VC_EVENT_WRITE_READY:
  case VC_EVENT_WRITE_COMPLETE:
    if (is_bidirectional) {
      this->_handle_bidi_stream_on_write_ready(event, vio);
    } else {
      this->_handle_uni_stream_on_write_ready(event, vio);
    }
    break;
  case VC_EVENT_EOS:
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

    Debug("http3", "[%" PRIu64 "] %s stream is created", new_stream_id, Http3DebugNames::stream_type(type));
  } else {
    Debug("http3", "Could not create %s stream", Http3DebugNames::stream_type(type));
  }

  return error;
}

void
Http3App::_handle_uni_stream_on_read_ready(int /* event */, VIO *vio)
{
  Http3StreamType type;
  QUICStreamVCAdapter *adapter = static_cast<QUICStreamVCAdapter *>(vio->vc_server);
  auto it                      = this->_remote_uni_stream_map.find(adapter->stream().id());
  if (it == this->_remote_uni_stream_map.end()) {
    // Set uni stream suitable app (HTTP/3 or QPACK) by stream type
    uint8_t buf;
    vio->get_reader()->read(&buf, 1);
    type = Http3Stream::type(&buf);

    Debug("http3", "[%" PRIu64 "] %s stream is opened", adapter->stream().id(), Http3DebugNames::stream_type(type));

    auto ret = this->_remote_uni_stream_map.insert(std::make_pair(adapter->stream().id(), type));
    if (!ret.second) {
      // A stream for the type is already exisits
      // TODO Return an error
    }
  } else {
    type = it->second;
  }

  switch (type) {
  case Http3StreamType::CONTROL:
  case Http3StreamType::PUSH: {
    uint64_t nread = 0;
    this->_control_stream_dispatcher.on_read_ready(adapter->stream().id(), *vio->get_reader(), nread);
    // TODO: when PUSH comes from client, send stream error with HTTP_WRONG_STREAM_DIRECTION
    break;
  }
  case Http3StreamType::QPACK_ENCODER:
  case Http3StreamType::QPACK_DECODER: {
    this->_set_qpack_stream(type, adapter);
  }
  case Http3StreamType::UNKNOWN:
  default:
    // TODO: just ignore or trigger QUIC STOP_SENDING frame with HTTP_UNKNOWN_STREAM_TYPE
    break;
  }
}

void
Http3App::_handle_bidi_stream_on_read_ready(int event, VIO *vio)
{
  QUICStreamVCAdapter *adapter = static_cast<QUICStreamVCAdapter *>(vio->vc_server);

  QUICStreamId stream_id = adapter->stream().id();
  Http3Transaction *txn  = static_cast<Http3Transaction *>(this->_ssn->get_transaction(stream_id));

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
      bool all_done   = false;
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
    } else {
      this->_ssn->local_qpack()->set_encoder_stream(adapter->stream().id());
    }
  } else if (type == Http3StreamType::QPACK_DECODER) {
    if (this->_qc->direction() == NET_VCONNECTION_IN) {
      this->_ssn->local_qpack()->set_decoder_stream(adapter->stream().id());
    } else {
      this->_ssn->remote_qpack()->set_decoder_stream(adapter->stream().id());
    }
  } else {
    ink_abort("unknown stream type");
  }
}

void
Http3App::_handle_bidi_stream_on_write_ready(int event, VIO *vio)
{
  QUICStreamVCAdapter *adapter = static_cast<QUICStreamVCAdapter *>(vio->vc_server);

  QUICStreamId stream_id = adapter->stream().id();
  Http3Transaction *txn  = static_cast<Http3Transaction *>(this->_ssn->get_transaction(stream_id));
  if (txn != nullptr) {
    SCOPED_MUTEX_LOCK(lock, txn->mutex, this_ethread());
    txn->handleEvent(event);
  }
}

//
// SETTINGS frame handler
//
std::vector<Http3FrameType>
Http3SettingsHandler::interests()
{
  return {Http3FrameType::SETTINGS};
}

Http3ErrorUPtr
Http3SettingsHandler::handle_frame(std::shared_ptr<const Http3Frame> frame)
{
  ink_assert(frame->type() == Http3FrameType::SETTINGS);

  const Http3SettingsFrame *settings_frame = dynamic_cast<const Http3SettingsFrame *>(frame.get());

  if (!settings_frame) {
    // make error
    return Http3ErrorUPtr(new Http3NoError());
  }

  if (settings_frame->is_valid()) {
    return settings_frame->get_error();
  }

  // TODO: Add length check: the maximum number of values are 2^62 - 1, but some fields have shorter maximum than it.
  if (settings_frame->contains(Http3SettingsId::HEADER_TABLE_SIZE)) {
    uint64_t header_table_size = settings_frame->get(Http3SettingsId::HEADER_TABLE_SIZE);
    this->_session->remote_qpack()->update_max_table_size(header_table_size);

    Debug("http3", "SETTINGS_HEADER_TABLE_SIZE: %" PRId64, header_table_size);
  }

  if (settings_frame->contains(Http3SettingsId::MAX_HEADER_LIST_SIZE)) {
    uint64_t max_header_list_size = settings_frame->get(Http3SettingsId::MAX_HEADER_LIST_SIZE);
    this->_session->remote_qpack()->update_max_header_list_size(max_header_list_size);

    Debug("http3", "SETTINGS_MAX_HEADER_LIST_SIZE: %" PRId64, max_header_list_size);
  }

  if (settings_frame->contains(Http3SettingsId::QPACK_BLOCKED_STREAMS)) {
    uint64_t qpack_blocked_streams = settings_frame->get(Http3SettingsId::QPACK_BLOCKED_STREAMS);
    this->_session->remote_qpack()->update_max_blocking_streams(qpack_blocked_streams);

    Debug("http3", "SETTINGS_QPACK_BLOCKED_STREAMS: %" PRId64, qpack_blocked_streams);
  }

  if (settings_frame->contains(Http3SettingsId::NUM_PLACEHOLDERS)) {
    uint64_t num_placeholders = settings_frame->get(Http3SettingsId::NUM_PLACEHOLDERS);
    // TODO: update settings for priority tree

    Debug("http3", "SETTINGS_NUM_PLACEHOLDERS: %" PRId64, num_placeholders);
  }

  return Http3ErrorUPtr(new Http3NoError());
}

//
// SETTINGS frame framer
//
Http3FrameUPtr
Http3SettingsFramer::generate_frame()
{
  if (this->_is_sent) {
    return Http3FrameFactory::create_null_frame();
  }

  this->_is_sent = true;

  Http3Config::scoped_config params;

  Http3SettingsFrame *frame = http3SettingsFrameAllocator.alloc();
  new (frame) Http3SettingsFrame();

  if (params->header_table_size() != HTTP3_DEFAULT_HEADER_TABLE_SIZE) {
    frame->set(Http3SettingsId::HEADER_TABLE_SIZE, params->header_table_size());
  }

  if (params->max_header_list_size() != HTTP3_DEFAULT_MAX_HEADER_LIST_SIZE) {
    frame->set(Http3SettingsId::MAX_HEADER_LIST_SIZE, params->max_header_list_size());
  }

  if (params->qpack_blocked_streams() != HTTP3_DEFAULT_QPACK_BLOCKED_STREAMS) {
    frame->set(Http3SettingsId::QPACK_BLOCKED_STREAMS, params->qpack_blocked_streams());
  }

  // Server side only
  if (this->_context == NET_VCONNECTION_IN) {
    if (params->num_placeholders() != HTTP3_DEFAULT_NUM_PLACEHOLDERS) {
      frame->set(Http3SettingsId::NUM_PLACEHOLDERS, params->num_placeholders());
    }
  }

  return Http3SettingsFrameUPtr(frame, &Http3FrameDeleter::delete_settings_frame);
}

bool
Http3SettingsFramer::is_done() const
{
  return this->_is_done;
}
