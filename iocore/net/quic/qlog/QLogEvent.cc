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

#include "QLogEvent.h"

namespace QLog
{
void
check_and_set(YAML::Node &node, std::string key, std::string val)
{
  if (val.length() > 0) {
    node[key] = val;
  }
}

void
check_and_set(YAML::Node &node, std::string key, std::vector<std::string> val)
{
  if (val.size() > 0) {
    node[key] = val;
  }
}

template <typename T>
void
check_and_set(YAML::Node &node, std::string key, T val)
{
  if (val) {
    node[key] = val;
  }
}

namespace Connectivity
{
  void
  ServerListening::encode(YAML::Node &node)
  {
    check_and_set(node, "ip_v4", _ip_v4);
    check_and_set(node, "ip_v6", _ip_v6);
    check_and_set(node, "port_v4", _port_v4);
    check_and_set(node, "port_v6", _port_v6);
    check_and_set(node, "stateless_reset_required", _port_v6);
    check_and_set(node, "quic_version", _quic_version);
    check_and_set(node, "alpn_values", _alpn_values);
  }

  void
  ConnectionStarted::encode(YAML::Node &node)
  {
    check_and_set(node, "quic_version", _quic_version);
    check_and_set(node, "ip_version", _ip_version);
    check_and_set(node, "src_ip", _src_ip);
    check_and_set(node, "dst_ip", _dst_ip);
    check_and_set(node, "protocol", _protocol);
    check_and_set(node, "src_port", _src_port);
    check_and_set(node, "dst_port", _dst_port);
    check_and_set(node, "src_cid", _src_cid);
    check_and_set(node, "dst_cid", _dst_cid);
    check_and_set(node, "alpn_values", _alpn_values);
  }

  void
  ConnectionIdUpdated::encode(YAML::Node &node)
  {
    check_and_set(node, "src_old", _src_old);
    check_and_set(node, "src_new", _src_new);
    check_and_set(node, "dst_old", _dst_old);
    check_and_set(node, "dst_new", _dst_new);
  }

  void
  SpinBitUpdated::encode(YAML::Node &node)
  {
    check_and_set(node, "state", _state);
  }

  void
  ConnectionStateUpdated::encode(YAML::Node &node)
  {
    check_and_set(node, "new", static_cast<int>(_new));
    check_and_set(node, "old", static_cast<int>(_old));
    check_and_set(node, "trigger", trigger_name(_trigger));
  }

} // namespace Connectivity

namespace Security
{
  void
  KeyEvent::encode(YAML::Node &node)
  {
    node["key_type"] = static_cast<int>(_key_type);
    node["new"]      = _new;
    check_and_set(node, "generation", _generation);
    check_and_set(node, "old", _old);
    check_and_set(node, "trigger", trigger_name(_trigger));
  }

} // namespace Security

namespace Transport
{
  void
  ParametersSet::encode(YAML::Node &node)
  {
    node["owner"] = _owner ? "local" : "remote";
    check_and_set(node, "resumption_allowed", _resumption_allowed);
    check_and_set(node, "early_data_enabled", _early_data_enabled);
    check_and_set(node, "alpn", _alpn);
    check_and_set(node, "version", _version);
    check_and_set(node, "tls_cipher", _tls_cipher);
    check_and_set(node, "original_connection_id", _original_connection_id);
    check_and_set(node, "stateless_reset_token", _stateless_reset_token);
    check_and_set(node, "disable_active_migration", _disable_active_migration);
    check_and_set(node, "max_idle_timeout", _max_idle_timeout);
    check_and_set(node, "max_udp_payload_size", _max_udp_payload_size);
    check_and_set(node, "ack_delay_exponent", _ack_delay_exponent);
    check_and_set(node, "max_ack_delay", _max_ack_delay);
    check_and_set(node, "active_connection_id_limit", _active_connection_id_limit);
    check_and_set(node, "initial_max_data", _initial_max_data);
    check_and_set(node, "initial_max_stream_data_bidi_local", _initial_max_stream_data_bidi_local);
    check_and_set(node, "initial_max_stream_data_bidi_remote", _initial_max_stream_data_bidi_remote);
    check_and_set(node, "initial_max_stream_data_uni", _initial_max_stream_data_uni);
    check_and_set(node, "initial_max_streams_bidi", _initial_max_streams_bidi);
    check_and_set(node, "initial_max_streams_uni", _initial_max_streams_uni);

    if (_preferred_address.ip.length() > 0) {
      YAML::Node sub;
      check_and_set(sub, _preferred_address.ipv4 ? "ip_v4" : "ip_v6", _preferred_address.ip);
      check_and_set(sub, _preferred_address.ipv4 ? "port_v4" : "port_v6", _preferred_address.port);
      check_and_set(sub, "connection_id", _preferred_address.connection_id);
      check_and_set(sub, "stateless_reset_token", _preferred_address.stateless_reset_token);
      node["preferred_address"] = sub;
    }
  }

  void
  PacketEvent::encode(YAML::Node &node)
  {
    node["packet_type"] = _packet_type;
    for (auto &&it : this->_frames) {
      YAML::Node sub;
      it->encode(sub);
      node["frames"].push_back(sub);
    }
    check_and_set(node, "is_coalesced", _is_coalesced);
    check_and_set(node, "stateless_reset_token", _stateless_reset_token);
    check_and_set(node, "supported_version", _supported_version);
    check_and_set(node, "raw_encrypted", _raw_encrypted);
    check_and_set(node, "raw_decrypted", _raw_decrypted);
    check_and_set(node, "supported_version", _supported_version);
    check_and_set(node, "supported_version", trigger_name(_trigger));

    node["header"]["packet_number"]  = _header.packet_number;
    node["header"]["packet_size"]    = _header.packet_size;
    node["header"]["payload_length"] = _header.payload_length;
    node["header"]["version"]        = _header.version;
    node["header"]["scil"]           = _header.scil;
    node["header"]["dcil"]           = _header.dcil;
    node["header"]["scid"]           = _header.scid;
    node["header"]["dcid"]           = _header.dcid;
  }

  void
  PacketDropped::encode(YAML::Node &node)
  {
    node["packet_type"] = _packet_type;
    check_and_set(node, "packet_size", _packet_size);
    check_and_set(node, "raw", _raw);
    check_and_set(node, "trigger", trigger_name(_trigger));
  }

  void
  PacketBuffered::encode(YAML::Node &node)
  {
    node["packet_type"] = _packet_type;
    check_and_set(node, "trigger", trigger_name(_trigger));
    check_and_set(node, "packet_number", trigger_name(_trigger));
  }

  void
  DatagramsEvent::encode(YAML::Node &node)
  {
    check_and_set(node, "count", _count);
    check_and_set(node, "byte_length", _byte_length);
  }

  void
  DatagramsDropped::encode(YAML::Node &node)
  {
    check_and_set(node, "byte_length", _byte_length);
  }

  void
  StreamStateUpdated::encode(YAML::Node &node)
  {
    node["new"]       = static_cast<int>(_new);
    node["stream_id"] = _stream_id;
    // FIXME
    // node["stream_type"] = bidi ? "bidirectional" : "unidirectional";
    // node["stream_side"] = "sending";
  }

  void
  FrameProcessed::encode(YAML::Node &node)
  {
    for (auto &&it : _frames) {
      YAML::Node sub;
      it->encode(sub);
      node["frames"].push_back(sub);
    }
  }

} // namespace Transport

namespace Recovery
{
  void
  ParametersSet::encode(YAML::Node &node)
  {
    check_and_set(node, "reordering_threshold", _reordering_threshold);
    check_and_set(node, "time_threshold", _time_threshold);
    check_and_set(node, "timer_granularity", _timer_granularity);
    check_and_set(node, "initial_rtt", _initial_rtt);
    check_and_set(node, "max_datagram_size", _max_datagram_size);
    check_and_set(node, "initial_congestion_window", _initial_congestion_window);
    check_and_set(node, "minimum_congestion_window", _minimum_congestion_window);
    check_and_set(node, "loss_reduction_factor", _loss_reduction_factor);
    check_and_set(node, "persistent_congestion_threshold", _persistent_congestion_threshold);
  }

  void
  MetricsUpdated::encode(YAML::Node &node)
  {
    check_and_set(node, "min_rtt", _min_rtt);
    check_and_set(node, "smoothed_rtt", _smoothed_rtt);
    check_and_set(node, "latest_rtt", _latest_rtt);
    check_and_set(node, "rtt_variance", _rtt_variance);
    check_and_set(node, "max_ack_delay", _max_ack_delay);
    check_and_set(node, "pto_count", _pto_count);
    check_and_set(node, "congestion_window", _congestion_window);
    check_and_set(node, "bytes_in_flight", _bytes_in_flight);
    check_and_set(node, "ssthresh", _ssthresh);
    check_and_set(node, "packets_in_flight", _packets_in_flight);
    check_and_set(node, "in_recovery", _in_recovery);
    check_and_set(node, "pacing_rate", _pacing_rate);
  }

  void
  CongestionStateUpdated::encode(YAML::Node &node)
  {
    node["new"] = state_to_string(_new);
    check_and_set(node, "old", state_to_string(_old));
    check_and_set(node, "old", trigger_name(_trigger));
  }

  void
  LossTimerUpdated::encode(YAML::Node &node)
  {
    node["timer_type"] = _timer_type_ack ? "ack" : "pto";
    check_and_set(node, "event_type", event_type_name(_event_type));
    check_and_set(node, "packet_number_space", _packet_number_space);
    if (_event_type == EventType::set) {
      check_and_set(node, "delta", _delta);
    }
  }

  void
  PacketLost::encode(YAML::Node &node)
  {
    node["packet_number"] = _packet_number;
    node["packet_type"]   = _packet_type;
    check_and_set(node, "trigger", trigger_name(_trigger));
    YAML::Node sub;
    _header.encode(sub);
    node["header"] = sub;

    for (auto &&it : _frames) {
      YAML::Node sub;
      it->encode(sub);
      node["frames"].push_back(sub);
    }
  }

  void
  MarkedForRetransmit::encode(YAML::Node &node)
  {
    for (auto &&it : _frames) {
      YAML::Node sub;
      it->encode(sub);
      node["frames"].push_back(sub);
    }
  }

} // namespace Recovery

} // namespace QLog
