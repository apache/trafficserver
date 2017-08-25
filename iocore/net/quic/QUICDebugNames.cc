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
  case QUICPacketType::CLIENT_INITIAL:
    return "CLIENT_INITIAL";
  case QUICPacketType::SERVER_STATELESS_RETRY:
    return "SERVER_STATELESS_RETRY";
  case QUICPacketType::SERVER_CLEARTEXT:
    return "SERVER_CLEARTEXT";
  case QUICPacketType::CLIENT_CLEARTEXT:
    return "CLIENT_CLEARTEXT";
  case QUICPacketType::ZERO_RTT_PROTECTED:
    return "ZERO_RTT_PROTECTED";
  case QUICPacketType::ONE_RTT_PROTECTED_KEY_PHASE_0:
    return "ONE_RTT_PROTECTED_KEY_PHASE_0";
  case QUICPacketType::ONE_RTT_PROTECTED_KEY_PHASE_1:
    return "ONE_RTT_PROTECTED_KEY_PHASE_1";
  case QUICPacketType::PUBLIC_RESET:
    return "PUBLIC_RESET";
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
  case QUICFrameType::GOAWAY:
    return "GOAWAY";
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
  case QUICFrameType::STREAM_ID_NEEDED:
    return "STREAM_ID_NEEDED";
  case QUICFrameType::NEW_CONNECTION_ID:
    return "NEW_CONNECTION_ID";
  case QUICFrameType::ACK:
    return "ACK";
  case QUICFrameType::STREAM:
    return "STREAM";
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
  case QUICErrorClass::AQPPLICATION_SPECIFIC:
    return "AQPPLICATION_SPECIFIC";
  case QUICErrorClass::HOST_LOCAL:
    return "HOST_LOCAL";
  case QUICErrorClass::QUIC_TRANSPORT:
    return "QUIC_TRANSPORT";
  case QUICErrorClass::CRYPTOGRAPHIC:
    return "CRYPTOGRAPHIC";
  default:
    return "UNKNOWN";
  }
}

const char *
QUICDebugNames::error_code(QUICErrorCode code)
{
  switch (code) {
  case QUICErrorCode::APPLICATION_SPECIFIC_ERROR:
    return "APPLICATION_SPECIFIC_ERROR";
  case QUICErrorCode::HOST_LOCAL_ERROR:
    return "HOST_LOCAL_ERROR";
  case QUICErrorCode::QUIC_TRANSPORT_ERROR:
    return "QUIC_TRANSPORT_ERROR";
  case QUICErrorCode::QUIC_INTERNAL_ERROR:
    return "QUIC_INTERNAL_ERROR";
  case QUICErrorCode::CRYPTOGRAPHIC_ERROR:
    return "CRYPTOGRAPHIC_ERROR";
  case QUICErrorCode::TLS_HANDSHAKE_FAILED:
    return "TLS_HANDSHAKE_FAILED";
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
  case QUICTransportParameterId::INITIAL_MAX_STREAM_ID:
    return "INITIAL_MAX_STREAM_ID";
  case QUICTransportParameterId::IDLE_TIMEOUT:
    return "IDLE_TIMEOUT";
  case QUICTransportParameterId::TRUNCATE_CONNECTION_ID:
    return "TRUNCATE_CONNECTION_ID";
  case QUICTransportParameterId::MAX_PACKET_SIZE:
    return "MAX_PACKET_SIZE";
  default:
    return "UNKNOWN";
  }
}
