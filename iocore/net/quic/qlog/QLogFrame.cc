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

#include "QLogFrame.h"

namespace QLog
{
template <typename Real>
const Real &
Convert(const QUICFrame *frame)
{
  // FIXME: dangerous
  auto tmp = static_cast<const QUICFrame *>(frame);
#if defined(DEBUG)
  auto ref = dynamic_cast<const Real *>(tmp);
  ink_assert(ref != nullptr);
  return *ref;
#endif
  return *static_cast<const Real *>(tmp);
}

QLogFrameUPtr
QLogFrameFactory::create(const QUICFrame *frame)
{
  switch (frame->type()) {
  case QUICFrameType::ACK:
    return std::make_unique<Frame::AckFrame>(Convert<QUICAckFrame>(frame));
  case QUICFrameType::PADDING:
    return std::make_unique<Frame::PaddingFrame>(Convert<QUICPaddingFrame>(frame));
  case QUICFrameType::PING:
    return std::make_unique<Frame::PingFrame>(Convert<QUICPingFrame>(frame));
  case QUICFrameType::RESET_STREAM:
    return std::make_unique<Frame::RstStreamFrame>(Convert<QUICRstStreamFrame>(frame));
  case QUICFrameType::STOP_SENDING:
    return std::make_unique<Frame::StopSendingFrame>(Convert<QUICStopSendingFrame>(frame));
  case QUICFrameType::CRYPTO:
    return std::make_unique<Frame::CryptoFrame>(Convert<QUICCryptoFrame>(frame));
  case QUICFrameType::NEW_TOKEN:
    return std::make_unique<Frame::NewTokenFrame>(Convert<QUICNewTokenFrame>(frame));
  case QUICFrameType::STREAM:
    return std::make_unique<Frame::StreamFrame>(Convert<QUICStreamFrame>(frame));
  case QUICFrameType::MAX_DATA:
    return std::make_unique<Frame::MaxDataFrame>(Convert<QUICMaxDataFrame>(frame));
  case QUICFrameType::MAX_STREAM_DATA:
    return std::make_unique<Frame::MaxStreamDataFrame>(Convert<QUICMaxStreamDataFrame>(frame));
  case QUICFrameType::MAX_STREAMS:
    return std::make_unique<Frame::MaxStreamsFrame>(Convert<QUICMaxStreamsFrame>(frame));
  case QUICFrameType::DATA_BLOCKED:
    return std::make_unique<Frame::DataBlockedFrame>(Convert<QUICDataBlockedFrame>(frame));
  case QUICFrameType::STREAM_DATA_BLOCKED:
    return std::make_unique<Frame::StreamDataBlockedFrame>(Convert<QUICStreamDataBlockedFrame>(frame));
  case QUICFrameType::STREAMS_BLOCKED:
    return std::make_unique<Frame::StreamsBlockedFrame>(Convert<QUICStreamIdBlockedFrame>(frame));
  case QUICFrameType::NEW_CONNECTION_ID:
    return std::make_unique<Frame::NewConnectionIDFrame>(Convert<QUICNewConnectionIdFrame>(frame));
  case QUICFrameType::RETIRE_CONNECTION_ID:
    return std::make_unique<Frame::RetireConnectionIDFrame>(Convert<QUICRetireConnectionIdFrame>(frame));
  case QUICFrameType::PATH_CHALLENGE:
    return std::make_unique<Frame::PathChallengeFrame>(Convert<QUICPathChallengeFrame>(frame));
  case QUICFrameType::PATH_RESPONSE:
    return std::make_unique<Frame::PathResponseFrame>(Convert<QUICPathResponseFrame>(frame));
  case QUICFrameType::CONNECTION_CLOSE:
    return std::make_unique<Frame::ConnectionCloseFrame>(Convert<QUICConnectionCloseFrame>(frame));
  case QUICFrameType::HANDSHAKE_DONE:
    return std::make_unique<Frame::HandshakeDoneFrame>(Convert<QUICHandshakeDoneFrame>(frame));
  default:
    ink_release_assert(0);
    return nullptr;
  }
}

namespace Frame
{
  template <typename T>
  std::string
  convert_to_string(T a)
  {
    return std::to_string(static_cast<uint64_t>(a));
  }

  void
  AckFrame::encode(YAML::Node &node)
  {
    node["frame_type"] = "ack";
    node["ack_delay"]  = std::to_string(ack_delay);
    for (auto &it : acked_range) {
      YAML::Node sub;
      sub.push_back(convert_to_string(it.first()));
      sub.push_back(convert_to_string(it.last()));
      node["acked_ranges"].push_back(sub);
    }

    if (ect1) {
      node["ect1"] = ect1;
    }

    if (ect1) {
      node["ect0"] = ect0;
    }

    if (ce) {
      node["ce"] = ce;
    }
  }

  void
  StreamFrame::encode(YAML::Node &node)
  {
    node["frame_type"] = "stream";
    node["stream_id"]  = stream_id;
    node["offset"]     = offset;
    node["length"]     = length;
    node["fin"]        = fin;
  }

  void
  PaddingFrame::encode(YAML::Node &node)
  {
    node["frame_type"] = "padding";
  }

  void
  PingFrame::encode(YAML::Node &node)
  {
    node["frame_type"] = "ping";
  }

  void
  RstStreamFrame::encode(YAML::Node &node)
  {
    node["frame_type"] = "reset_stream";
    node["stream_id"]  = stream_id;
    node["error_code"] = error_code;
    node["final_size"] = final_size;
  }

  void
  StopSendingFrame::encode(YAML::Node &node)
  {
    node["frame_type"] = "stop_sending";
    node["stream_id"]  = stream_id;
    node["error_code"] = error_code;
  }

  void
  CryptoFrame::encode(YAML::Node &node)
  {
    node["frame_type"] = "crypto";
    node["offset"]     = offset;
    node["length"]     = length;
  }

  void
  NewTokenFrame::encode(YAML::Node &node)
  {
    node["frame_type"] = "new_token";
    node["token"]      = token;
    node["length"]     = length;
  }

  void
  MaxDataFrame::encode(YAML::Node &node)
  {
    node["frame_type"] = "max_data";
    node["maximum"]    = maximum;
  }

  void
  MaxStreamDataFrame::encode(YAML::Node &node)
  {
    node["frame_type"] = "max_stream_data";
    node["maximum"]    = maximum;
    node["stream_id"]  = stream_id;
  }

  void
  MaxStreamsFrame::encode(YAML::Node &node)
  {
    node["frame_type"]  = "max_streams";
    node["maximum"]     = maximum;
    node["stream_type"] = stream_type;
  }

  void
  DataBlockedFrame::encode(YAML::Node &node)
  {
    node["frame_type"] = "data_blocked";
    node["limit"]      = limit;
  }

  void
  StreamDataBlockedFrame::encode(YAML::Node &node)
  {
    node["frame_type"] = "stream_data_blocked";
    node["limit"]      = limit;
    node["stream_id"]  = stream_id;
  }

  void
  StreamsBlockedFrame::encode(YAML::Node &node)
  {
    node["frame_type"]  = "streams_blocked";
    node["stream_id"]   = stream_id;
    node["stream_type"] = stream_type;
  }

  void
  NewConnectionIDFrame::encode(YAML::Node &node)
  {
    node["frame_type"]            = "new_connection_id";
    node["sequence_number"]       = sequence_number;
    node["retire_prior_to"]       = retire_prior_to;
    node["stateless_reset_token"] = stateless_reset_token;
    node["length"]                = length;
  }

  void
  RetireConnectionIDFrame::encode(YAML::Node &node)
  {
    node["frame_type"]      = "retire_connection_id";
    node["sequence_number"] = sequence_number;
  }

  void
  PathChallengeFrame::encode(YAML::Node &node)
  {
    node["frame_type"] = "path_challenge";
    node["data"]       = data;
  }

  void
  PathResponseFrame::encode(YAML::Node &node)
  {
    node["frame_type"] = "path_response";
    node["data"]       = data;
  }

  void
  ConnectionCloseFrame::encode(YAML::Node &node)
  {
    node["frame_type"]     = "connection_close";
    node["error_space"]    = error_space;
    node["error_code"]     = error_code;
    node["raw_error_code"] = raw_error_code;
    node["reason"]         = reason;
  }

  void
  HandshakeDoneFrame::encode(YAML::Node &node)
  {
    node["frame_type"] = "handshake_done";
  }

  void
  UnknownFrame::encode(YAML::Node &node)
  {
    node["frame_type"]     = "unknown";
    node["raw_frame_type"] = raw_frame_type;
  }

} // namespace Frame
} // namespace QLog
