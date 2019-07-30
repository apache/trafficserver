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
  case QUICFrameType::RESET_STREAM:
    return "RESET_STREAM";
  case QUICFrameType::CONNECTION_CLOSE:
    return "CONNECTION_CLOSE";
  case QUICFrameType::MAX_DATA:
    return "MAX_DATA";
  case QUICFrameType::MAX_STREAM_DATA:
    return "MAX_STREAM_DATA";
  case QUICFrameType::MAX_STREAMS:
    return "MAX_STREAMS";
  case QUICFrameType::PING:
    return "PING";
  case QUICFrameType::DATA_BLOCKED:
    return "DATA_BLOCKED";
  case QUICFrameType::STREAM_DATA_BLOCKED:
    return "STREAM_DATA_BLOCKED";
  case QUICFrameType::STREAMS_BLOCKED:
    return "STREAMS_BLOCKED";
  case QUICFrameType::NEW_CONNECTION_ID:
    return "NEW_CONNECTION_ID";
  case QUICFrameType::STOP_SENDING:
    return "STOP_SENDING";
  case QUICFrameType::ACK:
    return "ACK";
  case QUICFrameType::PATH_CHALLENGE:
    return "PATH_CHALLENGE";
  case QUICFrameType::PATH_RESPONSE:
    return "PATH_RESPONSE";
  case QUICFrameType::STREAM:
    return "STREAM";
  case QUICFrameType::CRYPTO:
    return "CRYPTO";
  case QUICFrameType::RETIRE_CONNECTION_ID:
    return "RETIRE_CONNECTION_ID";
  case QUICFrameType::NEW_TOKEN:
    return "NEW_TOKEN";
  case QUICFrameType::UNKNOWN:
  default:
    return "UNKNOWN";
  }
}

const char *
QUICDebugNames::error_class(QUICErrorClass cls)
{
  switch (cls) {
  case QUICErrorClass::UNDEFINED:
    return "UNDEFINED";
  case QUICErrorClass::TRANSPORT:
    return "TRANSPORT";
  case QUICErrorClass::APPLICATION:
    return "APPLICATION";
  default:
    return "UNKNOWN";
  }
}

const char *
QUICDebugNames::error_code(uint16_t code)
{
  switch (code) {
  case static_cast<uint16_t>(QUICTransErrorCode::NO_ERROR):
    return "NO_ERROR";
  case static_cast<uint16_t>(QUICTransErrorCode::INTERNAL_ERROR):
    return "INTERNAL_ERROR";
  case static_cast<uint16_t>(QUICTransErrorCode::FLOW_CONTROL_ERROR):
    return "FLOW_CONTROL_ERROR";
  case static_cast<uint16_t>(QUICTransErrorCode::STREAM_ID_ERROR):
    return "STREAM_ID_ERROR";
  case static_cast<uint16_t>(QUICTransErrorCode::STREAM_STATE_ERROR):
    return "STREAM_STATE_ERROR";
  case static_cast<uint16_t>(QUICTransErrorCode::FINAL_OFFSET_ERROR):
    return "FINAL_OFFSET_ERROR";
  case static_cast<uint16_t>(QUICTransErrorCode::FRAME_ENCODING_ERROR):
    return "FRAME_ENCODING_ERROR";
  case static_cast<uint16_t>(QUICTransErrorCode::TRANSPORT_PARAMETER_ERROR):
    return "TRANSPORT_PARAMETER_ERROR";
  case static_cast<uint16_t>(QUICTransErrorCode::VERSION_NEGOTIATION_ERROR):
    return "VERSION_NEGOTIATION_ERROR";
  case static_cast<uint16_t>(QUICTransErrorCode::PROTOCOL_VIOLATION):
    return "PROTOCOL_VIOLATION";
  case static_cast<uint16_t>(QUICTransErrorCode::INVALID_MIGRATION):
    return "INVALID_MIGRATION";
  default:
    if (0x0100 <= code && code <= 0x01FF) {
      return "CRYPTO_ERROR";
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
  case QUIC_EVENT_HANDSHAKE_PACKET_WRITE_COMPLETE:
    return "QUIC_EVENT_HANDSHAKE_PACKET_WRITE_COMPLETE";
  case QUIC_EVENT_CLOSING_TIMEOUT:
    return "QUIC_EVENT_CLOSING_TIMEOUT";
  case QUIC_EVENT_PATH_VALIDATION_TIMEOUT:
    return "QUIC_EVENT_PATH_VALIDATION_TIMEOUT";
  case QUIC_EVENT_SHUTDOWN:
    return "QUIC_EVENT_SHUTDOWN";
  case QUIC_EVENT_LD_SHUTDOWN:
    return "QUIC_EVENT_LD_SHUTDOWN";
  case QUIC_EVENT_ACK_PERIODIC:
    return "QUIC_EVENT_ACK_PERIODIC";
  default:
    return "UNKNOWN";
  }
}

const char *
QUICDebugNames::transport_parameter_id(QUICTransportParameterId id)
{
  switch (id) {
  case QUICTransportParameterId::INITIAL_MAX_STREAM_DATA_BIDI_LOCAL:
    return "INITIAL_MAX_STREAM_DATA_BIDI_LOCAL";
  case QUICTransportParameterId::INITIAL_MAX_DATA:
    return "INITIAL_MAX_DATA";
  case QUICTransportParameterId::INITIAL_MAX_STREAMS_BIDI:
    return "INITIAL_MAX_STREAMS_BIDI";
  case QUICTransportParameterId::IDLE_TIMEOUT:
    return "IDLE_TIMEOUT";
  case QUICTransportParameterId::PREFERRED_ADDRESS:
    return "PREFERRED_ADDRESS";
  case QUICTransportParameterId::MAX_PACKET_SIZE:
    return "MAX_PACKET_SIZE";
  case QUICTransportParameterId::STATELESS_RESET_TOKEN:
    return "STATELESS_RESET_TOKEN";
  case QUICTransportParameterId::ACK_DELAY_EXPONENT:
    return "ACK_DELAY_EXPONENT";
  case QUICTransportParameterId::INITIAL_MAX_STREAMS_UNI:
    return "INITIAL_MAX_STREAMS_UNI";
  case QUICTransportParameterId::DISABLE_MIGRATION:
    return "DISABLE_MIGRATION";
  case QUICTransportParameterId::INITIAL_MAX_STREAM_DATA_BIDI_REMOTE:
    return "INITIAL_MAX_STREAM_DATA_BIDI_REMOTE";
  case QUICTransportParameterId::INITIAL_MAX_STREAM_DATA_UNI:
    return "INITIAL_MAX_STREAM_DATA_UNI";
  case QUICTransportParameterId::MAX_ACK_DELAY:
    return "INITIAL_MAX_ACK_DELAY";
  case QUICTransportParameterId::ORIGINAL_CONNECTION_ID:
    return "INITIAL_ORIGINAL_CONNECTION_ID";
  default:
    return "UNKNOWN";
  }
}

const char *
QUICDebugNames::stream_state(const QUICSendStreamState state)
{
  switch (state) {
  case QUICSendStreamState::Init:
    return "QUICSendStreamState::Init";
  case QUICSendStreamState::Ready:
    return "QUICSendStreamState::Ready";
  case QUICSendStreamState::Send:
    return "QUICSendStreamState::Send";
  case QUICSendStreamState::DataSent:
    return "QUICSendStreamState::DataSent";
  case QUICSendStreamState::DataRecvd:
    return "QUICSendStreamState::DataRecvd";
  case QUICSendStreamState::ResetSent:
    return "QUICSendStreamState::ResetSent";
  case QUICSendStreamState::ResetRecvd:
    return "QUICSendStreamState::ResetRecvd";
  default:
    return "UNKNOWN";
  }
}

const char *
QUICDebugNames::stream_state(const QUICReceiveStreamState state)
{
  switch (state) {
  case QUICReceiveStreamState::Init:
    return "QUICReceiveStreamState::Init";
  case QUICReceiveStreamState::Recv:
    return "QUICReceiveStreamState::Recv";
  case QUICReceiveStreamState::SizeKnown:
    return "QUICReceiveStreamState::SizeKnown";
  case QUICReceiveStreamState::DataRecvd:
    return "QUICReceiveStreamState::DataRecvd";
  case QUICReceiveStreamState::ResetRecvd:
    return "QUICReceiveStreamState::ResetRecvd";
  case QUICReceiveStreamState::DataRead:
    return "QUICReceiveStreamState::DataRead";
  case QUICReceiveStreamState::ResetRead:
    return "QUICReceiveStreamState::ResetRead";
  default:
    return "UNKNOWN";
  }
}

const char *
QUICDebugNames::stream_state(const QUICBidirectionalStreamState state)
{
  switch (state) {
  case QUICBidirectionalStreamState::Init:
    return "QUICBidirectionalStreamState::Init";
  case QUICBidirectionalStreamState::Idle:
    return "QUICBidirectionalStreamState::Idle";
  case QUICBidirectionalStreamState::Open:
    return "QUICBidirectionalStreamState::Open";
  case QUICBidirectionalStreamState::HC_R:
    return "QUICBidirectionalStreamState::HC_R";
  case QUICBidirectionalStreamState::HC_L:
    return "QUICBidirectionalStreamState::HC_L";
  case QUICBidirectionalStreamState::Closed:
    return "QUICBidirectionalStreamState::Closed";
  case QUICBidirectionalStreamState::Invalid:
    return "QUICBidirectionalStreamState::Invalid";
  default:
    return "UNKNOWN";
  }
}

const char *
QUICDebugNames::key_phase(QUICKeyPhase phase)
{
  switch (phase) {
  case QUICKeyPhase::PHASE_0:
    return "PHASE_0";
  case QUICKeyPhase::PHASE_1:
    return "PHASE_1";
  case QUICKeyPhase::INITIAL:
    return "INITIAL";
  case QUICKeyPhase::ZERO_RTT:
    return "ZERO_RTT";
  case QUICKeyPhase::HANDSHAKE:
    return "HANDSHAKE";
  default:
    return "UNKNOWN";
  }
}

const char *
QUICDebugNames::encryption_level(QUICEncryptionLevel level)
{
  switch (level) {
  case QUICEncryptionLevel::INITIAL:
    return "INITIAL";
  case QUICEncryptionLevel::ZERO_RTT:
    return "ZERO_RTT";
  case QUICEncryptionLevel::HANDSHAKE:
    return "HANDSHAKE";
  case QUICEncryptionLevel::ONE_RTT:
    return "ONE_RTT";
  default:
    return "UNKNOWN";
  }
}

const char *
QUICDebugNames::pn_space(QUICPacketNumberSpace pn_space)
{
  switch (pn_space) {
  case QUICPacketNumberSpace::Initial:
    return "QUICPacketNumberSpace::Initial";
  case QUICPacketNumberSpace::Handshake:
    return "QUICPacketNumberSpace::Handshake";
  case QUICPacketNumberSpace::ApplicationData:
    return "QUICPacketNumberSpace::ApplicationData";
  default:
    return "UNKNOWN";
  }
}
