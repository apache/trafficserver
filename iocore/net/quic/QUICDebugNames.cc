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

#include "QUICDebugNames.h"
#include "I_VConnection.h"

const char *
QUICDebugNames::packet_type(QUICPacketType type)
{
  switch (type) {
  case QUICPacketType::VERSION_NEGOTIATION:
    return "VERSION_NEGOTIATION";
  case QUICPacketType::INITIAL:
    return "INITIAL";
  case QUICPacketType::RETRY:
    return "RETRY";
  case QUICPacketType::HANDSHAKE:
    return "HANDSHAKE";
  case QUICPacketType::ZERO_RTT_PROTECTED:
    return "ZERO_RTT_PROTECTED";
  case QUICPacketType::PROTECTED:
    return "PROTECTED";
  case QUICPacketType::STATELESS_RESET:
    return "STATELESS_RESET";
  case QUICPacketType::UNINITIALIZED:
  default:
    return "UNKNOWN";
  }
}

const char *
QUICDebugNames::frame_type(QUICFrameType type)
{
  switch (type) {
  case QUICFrameType::PADDING:
    return "PADDING";
  case QUICFrameType::RST_STREAM:
    return "RST_STREAM";
  case QUICFrameType::CONNECTION_CLOSE:
    return "CONNECTION_CLOSE";
  case QUICFrameType::MAX_DATA:
    return "MAX_DATA";
  case QUICFrameType::MAX_STREAM_DATA:
    return "MAX_STREAM_DATA";
  case QUICFrameType::MAX_STREAM_ID:
    return "MAX_STREAM_ID";
  case QUICFrameType::PING:
    return "PING";
  case QUICFrameType::BLOCKED:
    return "BLOCKED";
  case QUICFrameType::STREAM_BLOCKED:
    return "STREAM_BLOCKED";
  case QUICFrameType::STREAM_ID_BLOCKED:
    return "STREAM_ID_BLOCKED";
  case QUICFrameType::NEW_CONNECTION_ID:
    return "NEW_CONNECTION_ID";
  case QUICFrameType::ACK:
    return "ACK";
  case QUICFrameType::STREAM:
    return "STREAM";
  case QUICFrameType::PONG:
    return "PONG";
  case QUICFrameType::UNKNOWN:
  default:
    return "UNKNOWN";
  }
}

const char *
QUICDebugNames::error_class(QUICErrorClass cls)
{
  switch (cls) {
  case QUICErrorClass::NONE:
    return "NONE";
  case QUICErrorClass::TRANSPORT:
    return "TRANSPORT";
  case QUICErrorClass::APPLICATION:
    return "APPLICATION";
  default:
    return "UNKNOWN";
  }
}

const char *
QUICDebugNames::error_code(QUICTransErrorCode code)
{
  switch (code) {
  case QUICTransErrorCode::NO_ERROR:
    return "NO_ERROR";
  case QUICTransErrorCode::INTERNAL_ERROR:
    return "INTERNAL_ERROR";
  case QUICTransErrorCode::FLOW_CONTROL_ERROR:
    return "FLOW_CONTROL_ERROR";
  case QUICTransErrorCode::STREAM_ID_ERROR:
    return "STREAM_ID_ERROR";
  case QUICTransErrorCode::STREAM_STATE_ERROR:
    return "STREAM_STATE_ERROR";
  case QUICTransErrorCode::FINAL_OFFSET_ERROR:
    return "FINAL_OFFSET_ERROR";
  case QUICTransErrorCode::FRAME_FORMAT_ERROR:
    return "FRAME_FORMAT_ERROR";
  case QUICTransErrorCode::TRANSPORT_PARAMETER_ERROR:
    return "TRANSPORT_PARAMETER_ERROR";
  case QUICTransErrorCode::VERSION_NEGOTIATION_ERROR:
    return "VERSION_NEGOTIATION_ERROR";
  case QUICTransErrorCode::PROTOCOL_VIOLATION:
    return "PROTOCOL_VIOLATION";
  case QUICTransErrorCode::TLS_HANDSHAKE_FAILED:
    return "TLS_HANDSHAKE_FAILED";
  case QUICTransErrorCode::TLS_FATAL_ALERT_GENERATED:
    return "TLS_FATAL_ALERT_GENERATED";
  case QUICTransErrorCode::TLS_FATAL_ALERT_RECEIVED:
    return "TLS_FATAL_ALERT_RECEIVED";
  default:
    if (0x0100 <= static_cast<uint16_t>(code) && static_cast<uint16_t>(code) <= 0x01FF) {
      // TODO: Add frame types
      return "FRAME_ERROR";
    }
    return "UNKNOWN";
  }
}

const char *
QUICDebugNames::quic_event(int event)
{
  switch (event) {
  case QUIC_EVENT_PACKET_READ_READY:
    return "QUIC_EVENT_PACKET_READ_READY";
  case QUIC_EVENT_PACKET_WRITE_READY:
    return "QUIC_EVENT_PACKET_WRITE_READY";
  case QUIC_EVENT_SHUTDOWN:
    return "QUIC_EVENT_SHUTDOWN";
  case QUIC_EVENT_LD_SHUTDOWN:
    return "QUIC_EVENT_LD_SHUTDOWN";
  default:
    return "UNKNOWN";
  }
}

const char *
QUICDebugNames::vc_event(int event)
{
  switch (event) {
  case VC_EVENT_READ_READY:
    return "VC_EVENT_READ_READY";
  case VC_EVENT_READ_COMPLETE:
    return "VC_EVENT_READ_COMPLETE";
  case VC_EVENT_WRITE_READY:
    return "VC_EVENT_WRITE_READY";
  case VC_EVENT_WRITE_COMPLETE:
    return "VC_EVENT_WRITE_COMPLETE";
  case VC_EVENT_EOS:
    return "VC_EVENT_EOS";
  case VC_EVENT_ERROR:
    return "VC_EVENT_ERROR";
  case VC_EVENT_INACTIVITY_TIMEOUT:
    return "VC_EVENT_INACTIVITY_TIMEOUT";
  case VC_EVENT_ACTIVE_TIMEOUT:
    return "VC_EVENT_ACTIVE_TIMEOUT";
  case VC_EVENT_OOB_COMPLETE:
    return "VC_EVENT_OOB_COMPLETE";
  default:
    return "UNKNOWN";
  }
}

const char *
QUICDebugNames::transport_parameter_id(QUICTransportParameterId id)
{
  switch (id) {
  case QUICTransportParameterId::INITIAL_MAX_STREAM_DATA:
    return "INITIAL_MAX_STREAM_DATA";
  case QUICTransportParameterId::INITIAL_MAX_DATA:
    return "INITIAL_MAX_DATA";
  case QUICTransportParameterId::INITIAL_MAX_STREAM_ID_BIDI:
    return "INITIAL_MAX_STREAM_ID_BIDI";
  case QUICTransportParameterId::IDLE_TIMEOUT:
    return "IDLE_TIMEOUT";
  case QUICTransportParameterId::OMIT_CONNECTION_ID:
    return "OMIT_CONNECTION_ID";
  case QUICTransportParameterId::MAX_PACKET_SIZE:
    return "MAX_PACKET_SIZE";
  case QUICTransportParameterId::STATELESS_RESET_TOKEN:
    return "STATELESS_RESET_TOKEN";
  case QUICTransportParameterId::ACK_DELAY_EXPONENT:
    return "ACK_DELAY_EXPONENT";
  case QUICTransportParameterId::INITIAL_MAX_STREAM_ID_UNI:
    return "INITIAL_MAX_STREAM_ID_UNI";
  default:
    return "UNKNOWN";
  }
}

const char *
QUICDebugNames::stream_state(QUICStreamState state)
{
  switch (state.get()) {
  case QUICStreamState::State::idle:
    return "IDLE";
  case QUICStreamState::State::open:
    return "OPEN";
  case QUICStreamState::State::half_closed_remote:
    return "HC_REMOTE";
  case QUICStreamState::State::half_closed_local:
    return "HC_LOCAL";
  case QUICStreamState::State::closed:
    return "CLOSED";
  case QUICStreamState::State::illegal:
    return "ILLEGAL";
  default:
    return "UNKNOWN";
  }
}
