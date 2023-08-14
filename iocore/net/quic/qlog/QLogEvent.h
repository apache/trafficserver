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

#pragma once

#include <string>
#include <yaml-cpp/yaml.h>

#include "QUICTypes.h"
#include "QLogEvent.h"
#include "QLogFrame.h"

namespace QLog
{
class QLogEvent
{
public:
  virtual ~QLogEvent() {}

  virtual std::string category() const = 0;
  virtual std::string event() const    = 0;
  virtual void encode(YAML::Node &)    = 0;

  virtual ink_hrtime
  get_time() const
  {
    return this->_time;
  };

protected:
  ink_hrtime _time = ink_get_hrtime();
};

using QLogEventUPtr = std::unique_ptr<QLogEvent>;

#define SET(field, type) \
  void set_##field(type v) { this->_node[#field] = v; }

// enum class PacketType : uint8_t { initial, handshake, zerortt, onertt, retry, version_negotiation, unknown };
using PacketType = std::string;

struct PacketHeader {
  std::string packet_number;
  uint64_t packet_size;
  uint64_t payload_length;

  // only if present in the header
  // if correctly using NEW_CONNECTION_ID events,
  // dcid can be skipped for 1RTT packets
  std::string version;
  std::string scil;
  std::string dcil;
  std::string scid;
  std::string dcid;

  // Note: short vs long header is implicit through PacketType
  void
  encode(YAML::Node &node) const
  {
    node["packet_number"]  = packet_number;
    node["packet_size"]    = packet_size;
    node["payload_length"] = payload_length;
    node["version"]        = version;
    node["scil"]           = scil;
    node["dcil"]           = dcil;
    node["scid"]           = scid;
    node["dcid"]           = dcid;
  }
};

#define SET_FUNC(cla, field, type) \
public:                            \
  cla &set_##field(const type &v)  \
  {                                \
    this->_##field = v;            \
    return *this;                  \
  }                                \
                                   \
private:                           \
  type _##field;

#define APPEND_FUNC(cla, field, type) \
public:                               \
  cla &append_##field(type v)         \
  {                                   \
    this->_##field.push_back(v);      \
    return *this;                     \
  }                                   \
                                      \
private:                              \
  std::vector<type> _##field;

#define APPEND_FRAME_FUNC(cla)             \
public:                                    \
  cla &append_frames(QLogFrameUPtr v)      \
  {                                        \
    this->_frames.push_back(std::move(v)); \
    return *this;                          \
  }                                        \
                                           \
private:                                   \
  std::vector<QLogFrameUPtr> _frames;

//
// connectivity
//
namespace Connectivity
{
  class ConnectivityEvent : public QLogEvent
  {
  public:
    std::string
    category() const override
    {
      return "connectivity";
    }
  };

  class ServerListening : public ConnectivityEvent
  {
  public:
    ServerListening(int port, bool v6 = false)
    {
      if (v6) {
        set_port_v6(port);
      } else {
        set_port_v4(port);
      }
    }

#define _SET(a, b) SET_FUNC(ServerListening, a, b)
#define _APPEND(a, b) APPEND_FUNC(ServerListening, a, b)
    _SET(port_v4, int)
    _SET(port_v6, int)
    _SET(ip_v4, std::string)
    _SET(ip_v6, std::string)
    _SET(stateless_reset_required, bool)
    _APPEND(quic_version, std::string)
    _APPEND(alpn_values, std::string)

#undef _SET
#undef _APPEND

    void encode(YAML::Node &) override;

    std::string
    event() const override
    {
      return "server_listening";
    }
  };

  class ConnectionStarted : public ConnectivityEvent
  {
  public:
    ConnectionStarted(const std::string &version, const std::string &sip, const std::string &dip, int sport, int dport,
                      const std::string &protocol = "QUIC")
    {
      set_ip_version(version);
      set_protocol(protocol);
      set_src_ip(sip);
      set_dst_ip(dip);
      set_src_port(sport);
      set_dst_port(dport);
    }

#define _SET(a, b) SET_FUNC(ConnectionStarted, a, b)
#define _APPEND(a, b) APPEND_FUNC(ConnectionStarted, a, b)
    _SET(quic_version, std::string);
    _SET(src_cid, std::string);
    _SET(dst_cid, std::string);
    _SET(protocol, std::string);
    _SET(ip_version, std::string)
    _SET(src_ip, std::string)
    _SET(dst_ip, std::string)
    _SET(src_port, int)
    _SET(dst_port, int)
    _APPEND(alpn_values, std::string)

#undef _SET
#undef _APPEND

    void encode(YAML::Node &) override;

    std::string
    event() const override
    {
      return "connection_started";
    }
  };

  class ConnectionIdUpdated : public ConnectivityEvent
  {
  public:
    ConnectionIdUpdated(const std::string &old, const std::string &n, bool peer = false)
    {
      if (peer) {
        set_dst_old(old);
        set_dst_new(n);
      } else {
        set_src_old(old);
        set_src_new(n);
      }
    }

#define _SET(a, b) SET_FUNC(ConnectionIdUpdated, a, b)
#define _APPEND(a, b) APPEND_FUNC(ConnectionIdUpdated, a, b)

    _SET(src_old, std::string);
    _SET(src_new, std::string);
    _SET(dst_old, std::string);
    _SET(dst_new, std::string);

#undef _SET
#undef _APPEND

    void encode(YAML::Node &) override;

    std::string
    event() const override
    {
      return "connection_id_updated";
    }
  };

  class SpinBitUpdated : public ConnectivityEvent
  {
  public:
    SpinBitUpdated(bool state) { set_state(state); }

#define _SET(a, b) SET_FUNC(SpinBitUpdated, a, b)
    _SET(state, bool);
#undef _SET

    void encode(YAML::Node &) override;

    std::string
    event() const override
    {
      return "spin_bit_updated";
    }
  };

  class ConnectionStateUpdated : public ConnectivityEvent
  {
  public:
    enum class ConnectionState : uint8_t {
      attempted, // client initial sent
      reset,     // stateless reset sent
      handshake, // handshake in progress
      active,    // handshake successful, data exchange
      keepalive, // no data for a longer period
      draining,  // CONNECTION_CLOSE sent
      closed     // connection actually fully closed, memory freed
    };

    enum class Triggered : uint8_t {
      unknown,
      error,      // when closing because of an unexpected event
      clean,      // when closing normally
      application // e.g., HTTP/3's GOAWAY frame
    };

    ConnectionStateUpdated(ConnectionState n, Triggered tr = Triggered::unknown)
    {
      set_new(n);
      set_trigger(tr);
    }

#define _SET(a, b) SET_FUNC(ConnectionStateUpdated, a, b)
    _SET(new, ConnectionState);
    _SET(old, ConnectionState);
    _SET(trigger, Triggered)

#undef _SET

    void encode(YAML::Node &) override;

    static const char *
    trigger_name(Triggered trigger)
    {
      switch (trigger) {
      case Triggered::error:
        return "error";
      case Triggered::clean:
        return "clean";
      case Triggered::application:
        return "application";
      default:
        return nullptr;
      }
    }

    std::string
    event() const override
    {
      return "connection_state_updated";
    }
  };

} // namespace Connectivity

namespace Security
{
  class KeyEvent : public QLogEvent
  {
  public:
    enum class KeyType : uint8_t {
      server_initial_secret,
      client_initial_secret,

      server_handshake_secret,
      client_handshake_secret,

      server_0rtt_secret,
      client_0rtt_secret,

      server_1rtt_secret,
      client_1rtt_secret
    };

    enum class Triggered : uint8_t {
      unknown,
      remote_update,
      local_update,
      tls,
    };

    KeyEvent(KeyType ty, const std::string &n, int generation, Triggered triggered = Triggered::unknown)
    {
      set_key_type(ty);
      set_new(n);
      set_generation(generation);
      set_trigger(triggered);
    }

#define _SET(a, b) SET_FUNC(KeyEvent, a, b)
    _SET(key_type, KeyType);
    _SET(new, std::string)
    _SET(old, std::string);
    _SET(generation, int)
    _SET(trigger, Triggered)
#undef _SET

    void encode(YAML::Node &) override;

    const char *
    trigger_name(Triggered triggered)
    {
      switch (triggered) {
      case Triggered::remote_update:
        return "remote_update";
      case Triggered::local_update:
        return "local_update";
      case Triggered::tls:
        return "tls";
      default:
        return nullptr;
      }
    }

    std::string
    category() const override
    {
      return "security";
    }
  };

  class KeyUpdated : public KeyEvent
  {
  public:
    KeyUpdated(KeyType ty, const std::string &n, int generation, Triggered triggered = KeyEvent::Triggered::unknown)
      : KeyEvent(ty, n, generation, triggered)
    {
    }

    std::string
    event() const override
    {
      return "key_updated";
    }
  };

  class KeyRetired : public KeyEvent
  {
  public:
    KeyRetired(KeyType ty, const std::string &n, int generation, Triggered triggered = KeyEvent::Triggered::unknown)
      : KeyEvent(ty, n, generation, triggered)
    {
    }

    std::string
    event() const override
    {
      return "key_retired";
    }
  };

} // namespace Security

//
// transport event
//
namespace Transport
{
  class TransportEvent : public QLogEvent
  {
  public:
    std::string
    category() const override
    {
      return "transport";
    }
  };

  class ParametersSet : public TransportEvent
  {
  public:
    struct PreferredAddress {
      std::string ip;
      int port;
      std::string connection_id;
      std::string stateless_reset_token;
      bool ipv4 = true;
    };

    ParametersSet(bool owner) : _owner(owner) {}

    std::string
    event() const override
    {
      return "parameters_set";
    }

#define _SET(a, b) SET_FUNC(ParametersSet, a, b)
    _SET(resumption_allowed, bool); // early data extension was enabled on the TLS layer
    _SET(early_data_enabled, bool); // early data extension was enabled on the TLS layer
    _SET(alpn, std::string);
    _SET(version, std::string);                // hex (e.g. 0x);
    _SET(tls_cipher, std::string);             // (e.g. AES_128_GCM_SHA256);
    _SET(original_connection_id, std::string); // hex
    _SET(stateless_reset_token, std::string);  // hex
    _SET(disable_active_migration, bool);
    _SET(idle_timeout, int);
    _SET(max_packet_size, int);
    _SET(ack_delay_exponent, int);
    _SET(max_ack_delay, int);
    _SET(active_connection_id_limit, int);
    _SET(initial_max_data, std::string);
    _SET(initial_max_stream_data_bidi_local, std::string);
    _SET(initial_max_stream_data_bidi_remote, std::string);
    _SET(initial_max_stream_data_uni, std::string);
    _SET(initial_max_streams_bidi, std::string);
    _SET(initial_max_streams_uni, std::string);
    _SET(max_idle_timeout, int64_t)
    _SET(max_udp_payload_size, size_t)
    _SET(preferred_address, PreferredAddress)
#undef _SET

    void encode(YAML::Node &) override;

  private:
    bool _owner = false;
  };

  class PacketEvent : public TransportEvent
  {
  public:
    enum class Triggered : uint8_t {
      unknown,
      keys_available,       // if packet was buffered because it couldn't be decrypted before
      retransmit_reordered, // draft-23 5.1.1
      retransmit_timeout,   // draft-23 5.1.2
      pto_probe,            // draft-23 5.3.1
      retransmit_crypto,    // draft-19 6.2
      cc_bandwidth_probe,   // needed for some CCs to figure out bandwidth allocations when there are no normal sends
    };

    PacketEvent(const PacketType &type, PacketHeader h, Triggered tr = Triggered::unknown)
    {
      set_packet_type(type).set_header(h).set_trigger(tr);
    }

#define _SET(a, b) SET_FUNC(PacketEvent, a, b)
#define _APPEND(a, b) APPEND_FUNC(PacketEvent, a, b)
    _SET(packet_type, PacketType)
    _SET(header, PacketHeader)
    _SET(is_coalesced, bool);
    _SET(raw_encrypted, std::string);
    _SET(raw_decrypted, std::string);
    _SET(stateless_reset_token, std::string);
    _SET(trigger, Triggered);
    _APPEND(supported_version, std::string);

#undef _SET
#undef _APPEND
    APPEND_FRAME_FUNC(PacketEvent)

    void encode(YAML::Node &) override;

    static const char *
    trigger_name(Triggered triggered)
    {
      switch (triggered) {
      case Triggered::retransmit_reordered:
        return "retransmit_reordered";
      case Triggered::retransmit_timeout:
        return "retransmit_timeout";
      case Triggered::pto_probe:
        return "pto_probe";
      case Triggered::retransmit_crypto:
        return "retransmit_crypto";
      case Triggered::cc_bandwidth_probe:
        return "cc_bandwidth_probe";
        break;
      case Triggered::keys_available:
        return "keys_available";
      default:
        return nullptr;
      }
    }
  };

  class PacketSent : public PacketEvent
  {
  public:
    PacketSent(const PacketType &type, const PacketHeader &h, Triggered tr = Triggered::unknown) : PacketEvent(type, h, tr) {}
    std::string
    event() const override
    {
      return "packet_sent";
    }
  };

  class PacketReceived : public PacketEvent
  {
  public:
    PacketReceived(const PacketType &type, const PacketHeader &h, Triggered tr = Triggered::unknown) : PacketEvent(type, h, tr) {}
    std::string
    event() const override
    {
      return "packet_received";
    }
  };

  class PacketDropped : public TransportEvent
  {
  public:
    enum class Triggered : uint8_t {
      unknown,
      key_unavailable,
      unknown_connection_id,
      header_decrypt_error,
      payload_decrypt_error,
      protocol_violation,
      dos_prevention,
      unsupported_version,
      unexpected_packet,
      unexpected_source_connection_id,
      unexpected_version,
    };

    PacketDropped(Triggered tr = Triggered::unknown) { set_trigger(tr); }

#define _SET(a, b) SET_FUNC(PacketDropped, a, b)
    _SET(packet_size, int);
    _SET(raw, std::string);
    _SET(trigger, Triggered);
    _SET(packet_type, PacketType)
#undef _SET

    void encode(YAML::Node &) override;

    std::string
    event() const override
    {
      return "packet_dropped";
    }

    static const char *
    trigger_name(Triggered tr)
    {
      switch (tr) {
      case Triggered::key_unavailable:
        return "key_unavailable";
      case Triggered::unknown_connection_id:
        return "unknown_connection_id";
      case Triggered::header_decrypt_error:
        return "header_decrypt_error";
      case Triggered::payload_decrypt_error:
        return "payload_decrypt_error";
      case Triggered::protocol_violation:
        return "protocol_violation";
      case Triggered::dos_prevention:
        return "dos_prevention";
      case Triggered::unsupported_version:
        return "unsupported_version";
      case Triggered::unexpected_packet:
        return "unexpected_packet";
      case Triggered::unexpected_source_connection_id:
        return "unexpected_source_connection_id";
      case Triggered::unexpected_version:
        return "unexpected_version";
      default:
        return nullptr;
      }
    }
  };

  class PacketBuffered : public TransportEvent
  {
  public:
    enum class Triggered : uint8_t {
      unknown,
      backpressure,
      keys_unavailable,
    };

    PacketBuffered(Triggered tr = Triggered::unknown) { set_trigger(tr); }

#define _SET(a, b) SET_FUNC(PacketBuffered, a, b)
    _SET(trigger, Triggered);
    _SET(packet_type, PacketType)
    _SET(packet_number, std::string)
#undef _SET

    void encode(YAML::Node &) override;

    std::string
    event() const override
    {
      return "packet_buffered";
    }

    static const char *
    trigger_name(Triggered tr)
    {
      switch (tr) {
      case Triggered::backpressure:
        return "backpressure";
      case Triggered::keys_unavailable:
        return "keys_unavailable";
      default:
        return nullptr;
      }
    }
  };

  class DatagramsEvent : public TransportEvent
  {
  public:
#define _SET(a, b) SET_FUNC(DatagramsEvent, a, b)
    _SET(count, int);
    _SET(byte_length, int);
#undef _SET
    void encode(YAML::Node &) override;
  };

  class DatagramsSent : public DatagramsEvent
  {
  public:
    std::string
    event() const override
    {
      return "datagrams_sent";
    }
  };
  class DatagramReceived : public DatagramsEvent
  {
  public:
    std::string
    event() const override
    {
      return "datagrams_received";
    }
  };

  class DatagramsDropped : public TransportEvent
  {
  public:
#define _SET(a, b) SET_FUNC(DatagramsDropped, a, b)
    _SET(byte_length, int);
#undef _SET

    void encode(YAML::Node &) override;

    std::string
    event() const override
    {
      return "datagrams_dropped";
    }
  };

  class StreamStateUpdated : public TransportEvent
  {
    enum class StreamState {
      // bidirectional stream states, draft-23 3.4.
      idle,
      open,
      half_closed_local,
      half_closed_remote,
      closed,

      // sending-side stream states, draft-23 3.1.
      ready,
      send,
      data_sent,
      reset_sent,
      reset_received,

      // receive-side stream states, draft-23 3.2.
      receive,
      size_known,
      data_read,
      reset_read,

      // both-side states
      data_received,

      // qlog-defined
      destroyed // memory actually freed
    };

    StreamStateUpdated(std::string stream_id, StreamState n) { set_new(n).set_stream_id(stream_id); }

    void encode(YAML::Node &) override;

#define _SET(a, b) SET_FUNC(StreamStateUpdated, a, b)
    _SET(new, StreamState);
    _SET(old, StreamState);
    _SET(stream_id, std::string);
    _SET(bidi, bool);
#undef _SET

    std::string
    event() const override
    {
      return "stream_state_updated";
    }
  };

  class FrameProcessed : public TransportEvent
  {
  public:
    APPEND_FRAME_FUNC(FrameProcessed)

    void encode(YAML::Node &) override;

    std::string
    event() const override
    {
      return "frame_processed";
    }
  };

} // namespace Transport

namespace Recovery
{
  class RecoveryEvent : public QLogEvent
  {
  public:
    std::string
    category() const override
    {
      return "recovery";
    }
  };

  class ParametersSet : public RecoveryEvent
  {
  public:
#define _SET(a, b) SET_FUNC(ParametersSet, a, b)
    _SET(reordering_threshold, int);
    _SET(time_threshold, int);
    _SET(timer_granularity, int);
    _SET(initial_rtt, int);
    _SET(max_datagram_size, int);
    _SET(initial_congestion_window, int);
    _SET(minimum_congestion_window, int);
    _SET(loss_reduction_factor, int);
    _SET(persistent_congestion_threshold, int);
#undef _SET
    void encode(YAML::Node &) override;

    std::string
    event() const override
    {
      return "parameters_set";
    }
  };

  class MetricsUpdated : public RecoveryEvent
  {
  public:
#define _SET(a, b) SET_FUNC(MetricsUpdated, a, b)
    _SET(min_rtt, int);
    _SET(smoothed_rtt, int);
    _SET(latest_rtt, int);
    _SET(rtt_variance, int);
    _SET(max_ack_delay, int);
    _SET(pto_count, int);
    _SET(congestion_window, int);
    _SET(bytes_in_flight, int);
    _SET(ssthresh, int);
    _SET(packets_in_flight, int);
    _SET(in_recovery, int);
    _SET(pacing_rate, int);
#undef _SET
    void encode(YAML::Node &) override;

    std::string
    event() const override
    {
      return "metrics_updated";
    }
  };

  class CongestionStateUpdated : public RecoveryEvent
  {
  public:
    enum class State : uint8_t {
      slow_start,
      congestion_avoidance,
      application_limited,
      recovery,
    };

    enum class Triggered : uint8_t {
      unknown,
      persistent_congestion,
      ECN,
    };

    CongestionStateUpdated(State n, Triggered tr = Triggered::unknown) { set_trigger(tr).set_new(n); }

#define _SET(a, b) SET_FUNC(CongestionStateUpdated, a, b)
    _SET(trigger, Triggered)
    _SET(new, State)
    _SET(old, State)
#undef _SET

    void encode(YAML::Node &) override;

    std::string
    event() const override
    {
      return "congestion_state_updated";
    }

    static const char *
    trigger_name(Triggered tr)
    {
      switch (tr) {
      case Triggered::persistent_congestion:
        return "persistent_congestion";
      case Triggered::ECN:
        return "ECN";
      default:
        return nullptr;
      }
    }

    static const char *
    state_to_string(State s)
    {
      switch (s) {
      case State::slow_start:
        return "slow_start";
      case State::congestion_avoidance:
        return "congestion_avoidance";
      case State::application_limited:
        return "application_limited";
      case State::recovery:
        return "recovery";
      default:
        break;
      }

      return nullptr;
    }
  };

  class LossTimerUpdated : public RecoveryEvent
  {
  public:
    enum class EventType : uint8_t {
      set,
      expired,
      cancelled,
    };

    void
    set_timer_type(bool ack)
    {
      this->_timer_type_ack = ack;
    }

    void encode(YAML::Node &) override;

#define _SET(a, b) SET_FUNC(LossTimerUpdated, a, b)
    _SET(event_type, EventType)
    _SET(packet_number_space, int);
    _SET(delta, int);
#undef _SET

    std::string
    event() const override
    {
      return "loss_timer_updated";
    }

    static const char *
    event_type_name(EventType et)
    {
      switch (et) {
      case EventType::set:
        return "set";
      case EventType::expired:
        return "expired";
      case EventType::cancelled:
        return "cancelled";
      default:
        break;
      }
      return nullptr;
    }

  private:
    bool _timer_type_ack = false;
  };

  class PacketLost : public RecoveryEvent
  {
  public:
    enum class Triggered : uint8_t {
      unknown,
      reordering_threshold,
      time_threshold,
      pto_expired,
    };

    PacketLost(PacketType pt, uint64_t pn, Triggered tr = Triggered::unknown)
    {
      set_trigger(tr).set_packet_type(pt).set_packet_number(pn);
    }

#define _SET(a, b) SET_FUNC(PacketLost, a, b)
    _SET(header, PacketHeader)
    _SET(packet_number, uint64_t);
    _SET(packet_type, PacketType);
    _SET(trigger, Triggered)
    APPEND_FRAME_FUNC(PacketLost)
#undef _SET

    void encode(YAML::Node &) override;

    std::string
    event() const override
    {
      return "packet_lost";
    }

    static const char *
    trigger_name(Triggered tr)
    {
      switch (tr) {
      case Triggered::pto_expired:
        return "pto_expired";
      case Triggered::reordering_threshold:
        return "reordering_threshold";
      case Triggered::time_threshold:
        return "time_threshold";
      default:
        return nullptr;
      }
    }
  };

  class MarkedForRetransmit : public RecoveryEvent
  {
  public:
    APPEND_FRAME_FUNC(MarkedForRetransmit)
    void encode(YAML::Node &) override;
    std::string
    event() const override
    {
      return "marked_for_retransmit";
    }
  };

} // namespace Recovery

} // namespace QLog
