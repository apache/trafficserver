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

#include <cstring>
#include <unordered_map>
#include "tscore/ink_endian.h"
#include "tscore/ink_hrtime.h"
#include "tscore/Ptr.h"
#include "I_EventSystem.h"

#include "I_NetVConnection.h"

#include <memory>
#include <random>
#include <cstdint>
#include <string>
#include "tscore/ink_memory.h"
#include "tscore/ink_inet.h"
#include <openssl/evp.h>

using QUICPacketNumber = uint64_t;
using QUICVersion      = uint32_t;
using QUICStreamId     = uint64_t;
using QUICOffset       = uint64_t;
using QUICFrameId      = uint64_t;

// TODO: Update version number
// Note: Prefix for drafts (0xff000000) + draft number
// Note: Fix "Supported Version" field in test case of QUICPacketFactory_Create_VersionNegotiationPacket
// Note: Fix QUIC_ALPN_PROTO_LIST in QUICConfig.cc
constexpr QUICVersion QUIC_SUPPORTED_VERSIONS[] = {
  0xff00001d,
  0xff00001b,
};
constexpr QUICVersion QUIC_EXERCISE_VERSION1 = 0x1a2a3a4a;
constexpr QUICVersion QUIC_EXERCISE_VERSION2 = 0x5a6a7a8a;

enum class QUICEncryptionLevel {
  NONE      = -1,
  INITIAL   = 0,
  ZERO_RTT  = 1,
  HANDSHAKE = 2,
  ONE_RTT   = 3,
};

// For range-based for loop. This starts from INITIAL to ONE_RTT. It doesn't include NONE.
// Defining begin, end, operator*, operator++ doesn't work for duplicate symbol issue with libmgmt_p.a :(
// TODO: support ZERO_RTT
constexpr QUICEncryptionLevel QUIC_ENCRYPTION_LEVELS[] = {
  QUICEncryptionLevel::INITIAL,
  QUICEncryptionLevel::ZERO_RTT,
  QUICEncryptionLevel::HANDSHAKE,
  QUICEncryptionLevel::ONE_RTT,
};

// kPacketNumberSpace on Recovery A.2.Constants of Interest
enum class QUICPacketNumberSpace : int { INITIAL, HANDSHAKE, APPLICATION_DATA, N_SPACES };
// For conveniece (this removes neccesity of static_cast)
constexpr int QUIC_N_PACKET_SPACES = static_cast<int>(QUICPacketNumberSpace::N_SPACES);

// Divide to QUICPacketType and QUICPacketLongHeaderType ?
enum class QUICPacketType : uint8_t {
  INITIAL             = 0x00, // draft-17 version-specific type
  ZERO_RTT_PROTECTED  = 0x01, // draft-17 version-specific type
  HANDSHAKE           = 0x02, // draft-17 version-specific type
  RETRY               = 0x03, // draft-17 version-specific type
  VERSION_NEGOTIATION = 0xF0, // Not on the spec. but just for convenience
  PROTECTED,                  // Not on the spec. but just for convenience
  STATELESS_RESET,            // Not on the spec. but just for convenience
  UNINITIALIZED = 0xFF,       // Not on the spec. but just for convenience
};

// XXX If you add or remove QUICFrameType, you might also need to change QUICFrame::type(const uint8_t *)
enum class QUICFrameType : uint8_t {
  PADDING = 0x00,
  PING,
  ACK,
  ACK_WITH_ECN,
  RESET_STREAM = 0x04,
  STOP_SENDING,
  CRYPTO,
  NEW_TOKEN,
  STREAM, // 0x08 - 0x0f
  MAX_DATA = 0x10,
  MAX_STREAM_DATA,
  MAX_STREAMS, // 0x12 - 0x13
  DATA_BLOCKED = 0x14,
  STREAM_DATA_BLOCKED,
  STREAMS_BLOCKED, // 0x16 - 0x17
  NEW_CONNECTION_ID = 0x18,
  RETIRE_CONNECTION_ID,
  PATH_CHALLENGE,
  PATH_RESPONSE,
  CONNECTION_CLOSE, // 0x1c - 0x1d
  HANDSHAKE_DONE = 0x1e,
  UNKNOWN        = 0x1f,
};

enum class QUICVersionNegotiationStatus {
  NOT_NEGOTIATED, // Haven't negotiated yet
  NEGOTIATED,     // Negotiated
  VALIDATED,      // Validated with a one in transport parameters
  FAILED,         // Negotiation failed
};

enum class QUICKeyPhase : int {
  PHASE_0 = 0,
  PHASE_1,
  INITIAL,
  ZERO_RTT,
  HANDSHAKE,
};

enum class QUICPacketCreationResult {
  SUCCESS,
  FAILED,
  NO_PACKET,
  NOT_READY,
  IGNORED,
  UNSUPPORTED,
};

enum class QUICErrorClass {
  UNDEFINED,
  TRANSPORT,
  APPLICATION,
};

enum class QUICTransErrorCode : uint64_t {
  NO_ERROR = 0x00,
  INTERNAL_ERROR,
  CONNECTION_REFUSED,
  FLOW_CONTROL_ERROR,
  STREAM_LIMIT_ERROR,
  STREAM_STATE_ERROR,
  FINAL_SIZE_ERROR,
  FRAME_ENCODING_ERROR,
  TRANSPORT_PARAMETER_ERROR,
  CONNECTION_ID_LIMIT_ERROR,
  PROTOCOL_VIOLATION,
  INVALID_TOKEN,
  APPLICATION_ERROR,
  CRYPTO_BUFFER_EXCEEDED,
  CRYPTO_ERROR = 0x0100, // 0x100 - 0x1FF
};

// Application Protocol Error Codes defined in application
using QUICAppErrorCode                          = uint64_t;
constexpr uint16_t QUIC_APP_ERROR_CODE_STOPPING = 0;

class QUICError
{
public:
  virtual ~QUICError() {}

  QUICErrorClass cls = QUICErrorClass::UNDEFINED;
  uint16_t code      = 0;
  const char *msg    = nullptr;

protected:
  QUICError(){};
  QUICError(QUICErrorClass error_class, uint16_t error_code, const char *error_msg = nullptr)
    : cls(error_class), code(error_code), msg(error_msg)
  {
  }
};

class QUICConnectionError : public QUICError
{
public:
  QUICConnectionError() : QUICError() {}
  QUICConnectionError(QUICTransErrorCode error_code, const char *error_msg = nullptr,
                      QUICFrameType frame_type = QUICFrameType::UNKNOWN)
    : QUICError(QUICErrorClass::TRANSPORT, static_cast<uint16_t>(error_code), error_msg), _frame_type(frame_type){};
  QUICConnectionError(QUICErrorClass error_class, uint16_t error_code, const char *error_msg = nullptr,
                      QUICFrameType frame_type = QUICFrameType::UNKNOWN)
    : QUICError(error_class, error_code, error_msg), _frame_type(frame_type){};

  QUICFrameType frame_type() const;

private:
  QUICFrameType _frame_type = QUICFrameType::UNKNOWN;
};

class QUICStream;

class QUICStreamError : public QUICError
{
public:
  QUICStreamError() : QUICError() {}
  QUICStreamError(const QUICStream *s, const QUICTransErrorCode error_code, const char *error_msg = nullptr)
    : QUICError(QUICErrorClass::TRANSPORT, static_cast<uint16_t>(error_code), error_msg), stream(s){};
  QUICStreamError(const QUICStream *s, const QUICAppErrorCode error_code, const char *error_msg = nullptr)
    : QUICError(QUICErrorClass::APPLICATION, static_cast<uint16_t>(error_code), error_msg), stream(s){};

  const QUICStream *stream;
};

using QUICErrorUPtr           = std::unique_ptr<QUICError>;
using QUICConnectionErrorUPtr = std::unique_ptr<QUICConnectionError>;
using QUICStreamErrorUPtr     = std::unique_ptr<QUICStreamError>;

class QUICConnectionId
{
public:
  static uint8_t SCID_LEN;

  static constexpr int MIN_LENGTH_FOR_INITIAL = 8;
  static constexpr int MAX_LENGTH             = 20;
  static constexpr size_t MAX_HEX_STR_LENGTH  = MAX_LENGTH * 2 + 1;
  static QUICConnectionId ZERO();
  QUICConnectionId();
  QUICConnectionId(const uint8_t *buf, uint8_t len);

  explicit operator bool() const { return true; }
  /**
   * Note that this returns a kind of hash code so we can use a ConnectionId as a key for a hashtable.
   */
  operator uint64_t() const { return this->_hashcode(); }
  operator const uint8_t *() const { return this->_id; }
  bool
  operator==(const QUICConnectionId &x) const
  {
    if (this->_len != x._len) {
      return false;
    }
    return memcmp(this->_id, x._id, this->_len) == 0;
  }

  bool
  operator!=(const QUICConnectionId &x) const
  {
    if (this->_len != x._len) {
      return true;
    }
    return memcmp(this->_id, x._id, this->_len) != 0;
  }

  /*
   * This is just for debugging.
   */
  uint32_t h32() const;
  std::string hex() const;

  uint8_t length() const;
  bool is_zero() const;
  void randomize();

private:
  uint64_t _hashcode() const;
  uint8_t _id[MAX_LENGTH];
  uint8_t _len = 0;
};

class QUICStatelessResetToken
{
public:
  constexpr static int8_t LEN = 16;

  QUICStatelessResetToken() {}
  QUICStatelessResetToken(const QUICConnectionId &conn_id, uint32_t instance_id);
  QUICStatelessResetToken(const uint8_t *buf) { memcpy(this->_token, buf, QUICStatelessResetToken::LEN); }

  /**
   * Note that this returns a kind of hash code so we can use a StatelessResetToken as a key for a hashtable.
   */
  operator uint64_t() const { return this->_hashcode(); }

  bool
  operator==(const QUICStatelessResetToken &x) const
  {
    return memcmp(this->_token, x._token, QUICStatelessResetToken::LEN) == 0;
  }

  bool
  operator!=(const QUICStatelessResetToken &x) const
  {
    return memcmp(this->_token, x._token, QUICStatelessResetToken::LEN) != 0;
  }

  const uint8_t *
  buf() const
  {
    return _token;
  }

  std::string hex() const;

private:
  uint8_t _token[LEN] = {0};

  void _generate(uint64_t data);
  uint64_t _hashcode() const;
};

class QUICAddressValidationToken
{
public:
  enum class Type : uint8_t {
    RESUMPTION,
    RETRY,
  };

  // FIXME Check token length
  QUICAddressValidationToken(const uint8_t *buf, size_t len) : _token_len(len) { memcpy(this->_token, buf, len); }
  virtual ~QUICAddressValidationToken(){};

  static Type
  type(const uint8_t *buf)
  {
    ink_assert(static_cast<Type>(buf[0]) == Type::RESUMPTION || static_cast<Type>(buf[0]) == Type::RETRY);
    return static_cast<Type>(buf[0]) == Type::RESUMPTION ? Type::RESUMPTION : Type::RETRY;
  }

  virtual const uint8_t *
  buf() const
  {
    return this->_token;
  }

  virtual uint8_t
  length() const
  {
    return this->_token_len;
  }

protected:
  QUICAddressValidationToken() {}

  // The size should be smaller than maximum size of Retry packet
  uint8_t _token[1200] = {0};
  unsigned int _token_len;
};

class QUICResumptionToken : public QUICAddressValidationToken
{
public:
  QUICResumptionToken(const uint8_t *buf, uint8_t len) : QUICAddressValidationToken(buf, len) {}
  QUICResumptionToken(const IpEndpoint &src, QUICConnectionId cid, ink_hrtime expire_time);

  bool
  operator==(const QUICResumptionToken &x) const
  {
    if (this->_token_len != x._token_len) {
      return false;
    }
    return memcmp(this->_token, x._token, this->_token_len) == 0;
  }

  bool is_valid(const IpEndpoint &src) const;

  const QUICConnectionId cid() const;
  ink_hrtime expire_time() const;
};

class QUICRetryToken : public QUICAddressValidationToken
{
public:
  QUICRetryToken(const uint8_t *buf, size_t len) : QUICAddressValidationToken(buf, len) {}
  QUICRetryToken(const IpEndpoint &src, QUICConnectionId original_dcid, QUICConnectionId scid);

  bool
  operator==(const QUICRetryToken &x) const
  {
    if (this->_token_len != x._token_len) {
      return false;
    }
    return memcmp(this->_token, x._token, this->_token_len) == 0;
  }

  bool is_valid(const IpEndpoint &src) const;

  const QUICConnectionId original_dcid() const;
  const QUICConnectionId scid() const;
};

class QUICPreferredAddress
{
public:
  constexpr static int16_t MIN_LEN = 41;
  constexpr static int16_t MAX_LEN = 61;

  QUICPreferredAddress(IpEndpoint endpoint_ipv4, IpEndpoint endpoint_ipv6, const QUICConnectionId &cid,
                       QUICStatelessResetToken token)
    : _endpoint_ipv4(endpoint_ipv4), _endpoint_ipv6(endpoint_ipv6), _cid(cid), _token(token), _valid(true)
  {
  }
  QUICPreferredAddress(const uint8_t *buf, uint16_t len);

  bool is_available() const;
  bool has_ipv4() const;
  bool has_ipv6() const;
  const IpEndpoint endpoint_ipv4() const;
  const IpEndpoint endpoint_ipv6() const;
  const QUICConnectionId cid() const;
  const QUICStatelessResetToken token() const;

  void store(uint8_t *buf, uint16_t &len) const;

private:
  IpEndpoint _endpoint_ipv4 = {};
  IpEndpoint _endpoint_ipv6 = {};
  QUICConnectionId _cid;
  QUICStatelessResetToken _token;
  bool _valid = false;
};

enum class QUICStreamType : uint8_t {
  CLIENT_BIDI = 0x00,
  SERVER_BIDI,
  CLIENT_UNI,
  SERVER_UNI,
};

enum class QUICStreamDirection : uint8_t {
  UNKNOWN = 0,
  SEND,
  RECEIVE,
  BIDIRECTIONAL,
};

class QUICFiveTuple
{
public:
  QUICFiveTuple(){};
  QUICFiveTuple(IpEndpoint src, IpEndpoint dst, int protocol);
  void update(IpEndpoint src, IpEndpoint dst, int protocol);
  IpEndpoint source() const;
  IpEndpoint destination() const;
  int protocol() const;

private:
  IpEndpoint _source;
  IpEndpoint _destination;
  int _protocol;
  uint64_t _hash_code = 0;
};

class QUICPath
{
public:
  QUICPath(IpEndpoint local_ep, IpEndpoint remote_ep);
  const IpEndpoint &local_ep() const;
  const IpEndpoint &remote_ep() const;

  inline bool
  operator==(const QUICPath &x) const
  {
    if ((this->_local_ep.network_order_port() != 0 && x._local_ep.network_order_port() != 0) &&
        this->_local_ep.network_order_port() != x._local_ep.network_order_port()) {
      return false;
    }

    if ((this->_remote_ep.network_order_port() != 0 && x._remote_ep.network_order_port() != 0) &&
        this->_remote_ep.network_order_port() != x._remote_ep.network_order_port()) {
      return false;
    }

    if ((!IpAddr(this->_local_ep).isAnyAddr() && !IpAddr(x._local_ep).isAnyAddr()) && this->_local_ep != x._local_ep) {
      return false;
    }

    if ((!IpAddr(this->_remote_ep).isAnyAddr() || !IpAddr(x._remote_ep).isAnyAddr()) && this->_remote_ep != x._remote_ep) {
      return false;
    }

    return true;
  }

private:
  IpEndpoint _local_ep;
  IpEndpoint _remote_ep;
};

class QUICPathHasher
{
public:
  std::size_t
  operator()(const QUICPath &k) const
  {
    return k.remote_ep().network_order_port();
  }
};

class QUICPathValidationData
{
public:
  QUICPathValidationData(const uint8_t *data) { memcpy(this->_data, data, sizeof(this->_data)); }

  inline operator const uint8_t *() const { return this->_data; }

private:
  uint8_t _data[8];
};

class QUICTPConfig
{
public:
  virtual ~QUICTPConfig()                                                                                 = default; // required
  virtual uint32_t no_activity_timeout() const                                                            = 0;
  virtual const IpEndpoint *preferred_address_ipv4() const                                                = 0;
  virtual const IpEndpoint *preferred_address_ipv6() const                                                = 0;
  virtual uint32_t initial_max_data() const                                                               = 0;
  virtual uint32_t initial_max_stream_data_bidi_local() const                                             = 0;
  virtual uint32_t initial_max_stream_data_bidi_remote() const                                            = 0;
  virtual uint32_t initial_max_stream_data_uni() const                                                    = 0;
  virtual uint64_t initial_max_streams_bidi() const                                                       = 0;
  virtual uint64_t initial_max_streams_uni() const                                                        = 0;
  virtual uint8_t ack_delay_exponent() const                                                              = 0;
  virtual uint8_t max_ack_delay() const                                                                   = 0;
  virtual uint8_t active_cid_limit() const                                                                = 0;
  virtual bool disable_active_migration() const                                                           = 0;
  virtual const std::unordered_map<uint16_t, std::pair<const uint8_t *, uint16_t>> &additional_tp() const = 0;
};

class QUICLDConfig
{
public:
  virtual ~QUICLDConfig() {}
  virtual uint32_t packet_threshold() const = 0;
  virtual float time_threshold() const      = 0;
  virtual ink_hrtime granularity() const    = 0;
  virtual ink_hrtime initial_rtt() const    = 0;
};

class QUICCCConfig
{
public:
  virtual ~QUICCCConfig() {}
  virtual uint32_t initial_window() const                  = 0;
  virtual uint32_t minimum_window() const                  = 0;
  virtual float loss_reduction_factor() const              = 0;
  virtual uint32_t persistent_congestion_threshold() const = 0;
};

class QUICFrameGenerator;

struct QUICSentPacketInfo {
  class FrameInfo
  {
  public:
    FrameInfo(QUICFrameId id, QUICFrameGenerator *generator) : _id(id), _generator(generator) {}

    QUICFrameId id() const;
    QUICFrameGenerator *generated_by() const;

  private:
    QUICFrameId _id = 0;
    QUICFrameGenerator *_generator;
  };

  // Recovery A.1.1.  Sent Packet Fields
  QUICPacketNumber packet_number;
  bool ack_eliciting;
  bool in_flight;
  size_t sent_bytes;
  ink_hrtime time_sent;

  // Additional fields
  QUICPacketType type;
  std::vector<FrameInfo> frames;
  QUICPacketNumberSpace pn_space;
  // End of additional fields
};

using QUICSentPacketInfoUPtr = std::unique_ptr<QUICSentPacketInfo>;

class QUICRTTProvider
{
public:
  virtual ~QUICRTTProvider() {} // required - class has virtual methods.
  virtual ink_hrtime smoothed_rtt() const = 0;
  virtual ink_hrtime rttvar() const       = 0;
  virtual ink_hrtime latest_rtt() const   = 0;

  virtual ink_hrtime congestion_period(uint32_t threshold) const = 0;
};

// TODO: move version independent functions to QUICInvariants
class QUICTypeUtil
{
public:
  static bool is_supported_version(QUICVersion version);
  static QUICStreamType detect_stream_type(QUICStreamId id);
  static QUICStreamDirection detect_stream_direction(QUICStreamId id, NetVConnectionContext_t context);
  static QUICEncryptionLevel encryption_level(QUICPacketType type);
  static QUICPacketType packet_type(QUICEncryptionLevel level);
  static QUICKeyPhase key_phase(QUICPacketType type);
  static QUICPacketNumberSpace pn_space(QUICEncryptionLevel level);

  static QUICConnectionId read_QUICConnectionId(const uint8_t *buf, uint8_t n);
  static int read_QUICPacketNumberLen(const uint8_t *buf);
  static QUICPacketNumber read_QUICPacketNumber(const uint8_t *buf, int len);
  static QUICVersion read_QUICVersion(const uint8_t *buf);
  static QUICStreamId read_QUICStreamId(const uint8_t *buf, size_t buf_len);
  static QUICOffset read_QUICOffset(const uint8_t *buf, size_t buf_len);
  static uint16_t read_QUICTransErrorCode(const uint8_t *buf);
  static QUICAppErrorCode read_QUICAppErrorCode(const uint8_t *buf);
  static uint64_t read_QUICMaxData(const uint8_t *buf, size_t buf_len);

  static void write_QUICConnectionId(QUICConnectionId connection_id, uint8_t *buf, size_t *len);
  static void write_QUICPacketNumberLen(int len, uint8_t *buf);
  static void write_QUICPacketNumber(QUICPacketNumber packet_number, uint8_t n, uint8_t *buf, size_t *len);
  static void write_QUICVersion(QUICVersion version, uint8_t *buf, size_t *len);
  static void write_QUICStreamId(QUICStreamId stream_id, uint8_t *buf, size_t *len);
  static void write_QUICOffset(QUICOffset offset, uint8_t *buf, size_t *len);
  static void write_QUICTransErrorCode(uint64_t error_code, uint8_t *buf, size_t *len);
  static void write_QUICAppErrorCode(QUICAppErrorCode error_code, uint8_t *buf, size_t *len);
  static void write_QUICMaxData(uint64_t max_data, uint8_t *buf, size_t *len);

private:
};

class QUICInvariants
{
public:
  static bool is_long_header(const uint8_t *buf);
  static bool is_version_negotiation(QUICVersion v);
  static bool version(QUICVersion &dst, const uint8_t *buf, uint64_t buf_len);
  static bool dcil(uint8_t &dst, const uint8_t *buf, uint64_t buf_len);
  static bool scil(uint8_t &dst, const uint8_t *buf, uint64_t buf_len);
  static bool dcid(QUICConnectionId &dst, const uint8_t *buf, uint64_t buf_len);
  static bool scid(QUICConnectionId &dst, const uint8_t *buf, uint64_t buf_len);

  static const size_t LH_VERSION_OFFSET = 1;
  static const size_t LH_CIL_OFFSET     = 5;
  static const size_t LH_DCID_OFFSET    = 6;
  static const size_t SH_DCID_OFFSET    = 1;
  static const size_t LH_MIN_LEN        = 6;
  static const size_t SH_MIN_LEN        = 1;
};

int to_hex_str(char *dst, size_t dst_len, const uint8_t *src, size_t src_len);

namespace QUICBase
{
std::string to_hex(const uint8_t *buf, size_t len);

} // namespace QUICBase
