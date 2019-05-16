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
#include "tscore/ink_endian.h"
#include "tscore/ink_hrtime.h"
#include "tscore/Ptr.h"
#include "I_EventSystem.h"

#include "I_NetVConnection.h"

#include <memory>
#include <random>
#include <cstdint>
#include "tscore/ink_memory.h"
#include "tscore/ink_inet.h"
#include "openssl/evp.h"

using QUICPacketNumber = uint64_t;
using QUICVersion      = uint32_t;
using QUICStreamId     = uint64_t;
using QUICOffset       = uint64_t;

static constexpr uint8_t kPacketNumberSpace = 3;

// TODO: Update version number
// Note: Prefix for drafts (0xff000000) + draft number
// Note: Fix "Supported Version" field in test case of QUICPacketFactory_Create_VersionNegotiationPacket
// Note: Fix QUIC_ALPN_PROTO_LIST in QUICConfig.cc
// Note: Change ExtensionType (QUICTransportParametersHandler::TRANSPORT_PARAMETER_ID) if it's changed
constexpr QUICVersion QUIC_SUPPORTED_VERSIONS[] = {
  0xff000014,
};
constexpr QUICVersion QUIC_EXERCISE_VERSION = 0x1a2a3a4a;

enum class QUICEncryptionLevel {
  NONE      = -1,
  INITIAL   = 0,
  ZERO_RTT  = 1,
  HANDSHAKE = 2,
  ONE_RTT   = 3,
};

// For range-based for loop. This starts from INITIAL to ONE_RTT. It doesn't include NONE.
// Defining begin, end, operator*, operator++ doen't work for duplicate symbol issue with libmgmt_p.a :(
// TODO: support ZERO_RTT
constexpr QUICEncryptionLevel QUIC_ENCRYPTION_LEVELS[] = {
  QUICEncryptionLevel::INITIAL,
  QUICEncryptionLevel::ZERO_RTT,
  QUICEncryptionLevel::HANDSHAKE,
  QUICEncryptionLevel::ONE_RTT,
};

// introduce by draft-19 kPacketNumberSpace
enum class QUICPacketNumberSpace {
  Initial,
  Handshake,
  ApplicationData,
};

// Devide to QUICPacketType and QUICPacketLongHeaderType ?
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
  UNKNOWN = 0x1e,
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

enum class QUICTransErrorCode : uint16_t {
  NO_ERROR = 0x00,
  INTERNAL_ERROR,
  SERVER_BUSY,
  FLOW_CONTROL_ERROR,
  STREAM_ID_ERROR,
  STREAM_STATE_ERROR,
  FINAL_OFFSET_ERROR,
  FRAME_ENCODING_ERROR,
  TRANSPORT_PARAMETER_ERROR,
  VERSION_NEGOTIATION_ERROR,
  PROTOCOL_VIOLATION,
  INVALID_MIGRATION = 0x0C,
  CRYPTO_ERROR      = 0x0100, // 0x100 - 0x1FF
};

// Application Protocol Error Codes defined in application
using QUICAppErrorCode                          = uint16_t;
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

  static const int MIN_LENGTH_FOR_INITIAL = 8;
  static const int MAX_LENGTH             = 18;
  static const size_t MAX_HEX_STR_LENGTH  = MAX_LENGTH * 2 + 1;
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
  int hex(char *buf, size_t len) const;

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

  bool
  operator==(const QUICStatelessResetToken &x) const
  {
    return memcmp(this->_token, x._token, QUICStatelessResetToken::LEN) == 0;
  }

  const uint8_t *
  buf() const
  {
    return _token;
  }

private:
  uint8_t _token[LEN] = {0};

  void _generate(uint64_t data);
};

class QUICAddressValidationToken
{
public:
  enum class Type : uint8_t {
    RESUMPTION,
    RETRY,
  };

  virtual ~QUICAddressValidationToken(){};

  static Type
  type(const uint8_t *buf)
  {
    ink_assert(static_cast<Type>(buf[0]) == Type::RESUMPTION || static_cast<Type>(buf[0]) == Type::RETRY);
    return static_cast<Type>(buf[0]) == Type::RESUMPTION ? Type::RESUMPTION : Type::RETRY;
  }

  virtual const uint8_t *buf() const = 0;
  virtual uint8_t length() const     = 0;
};

class QUICResumptionToken : public QUICAddressValidationToken
{
public:
  QUICResumptionToken() {}
  QUICResumptionToken(const uint8_t *buf, uint8_t len) : _token_len(len) { memcpy(this->_token, buf, len); }
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
  const ink_hrtime expire_time() const;

  const uint8_t *
  buf() const override
  {
    return this->_token;
  }

  uint8_t
  length() const override
  {
    return this->_token_len;
  }

private:
  uint8_t _token[1 + EVP_MAX_MD_SIZE + QUICConnectionId::MAX_LENGTH + 4];
  unsigned int _token_len;
};

class QUICRetryToken : public QUICAddressValidationToken
{
public:
  QUICRetryToken() {}
  QUICRetryToken(const uint8_t *buf, uint8_t len) : _token_len(len) { memcpy(this->_token, buf, len); }
  QUICRetryToken(const IpEndpoint &src, QUICConnectionId original_dcid);

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

  const uint8_t *
  buf() const override
  {
    return this->_token;
  }

  uint8_t
  length() const override
  {
    return this->_token_len;
  }

private:
  uint8_t _token[1 + EVP_MAX_MD_SIZE + QUICConnectionId::MAX_LENGTH] = {};
  unsigned int _token_len                                            = 0;
  QUICConnectionId _original_dcid;
};

class QUICPreferredAddress
{
public:
  constexpr static int16_t MIN_LEN = 26;
  constexpr static int16_t MAX_LEN = 295;

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
  IpEndpoint _endpoint_ipv4;
  IpEndpoint _endpoint_ipv6;
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

class QUICTPConfig
{
public:
  virtual uint32_t no_activity_timeout() const                 = 0;
  virtual const IpEndpoint *preferred_address_ipv4() const     = 0;
  virtual const IpEndpoint *preferred_address_ipv6() const     = 0;
  virtual uint32_t initial_max_data() const                    = 0;
  virtual uint32_t initial_max_stream_data_bidi_local() const  = 0;
  virtual uint32_t initial_max_stream_data_bidi_remote() const = 0;
  virtual uint32_t initial_max_stream_data_uni() const         = 0;
  virtual uint64_t initial_max_streams_bidi() const            = 0;
  virtual uint64_t initial_max_streams_uni() const             = 0;
  virtual uint8_t ack_delay_exponent() const                   = 0;
  virtual uint8_t max_ack_delay() const                        = 0;
};

class QUICLDConfig
{
public:
  virtual uint32_t packet_threshold() const = 0;
  virtual float time_threshold() const      = 0;
  virtual ink_hrtime granularity() const    = 0;
  virtual ink_hrtime initial_rtt() const    = 0;
};

class QUICCCConfig
{
public:
  virtual uint32_t max_datagram_size() const               = 0;
  virtual uint32_t initial_window() const                  = 0;
  virtual uint32_t minimum_window() const                  = 0;
  virtual float loss_reduction_factor() const              = 0;
  virtual uint32_t persistent_congestion_threshold() const = 0;
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
  static QUICStreamId read_QUICStreamId(const uint8_t *buf);
  static QUICOffset read_QUICOffset(const uint8_t *buf);
  static uint16_t read_QUICTransErrorCode(const uint8_t *buf);
  static QUICAppErrorCode read_QUICAppErrorCode(const uint8_t *buf);
  static uint64_t read_QUICMaxData(const uint8_t *buf);

  static void write_QUICConnectionId(QUICConnectionId connection_id, uint8_t *buf, size_t *len);
  static void write_QUICPacketNumberLen(int len, uint8_t *buf);
  static void write_QUICPacketNumber(QUICPacketNumber packet_number, uint8_t n, uint8_t *buf, size_t *len);
  static void write_QUICVersion(QUICVersion version, uint8_t *buf, size_t *len);
  static void write_QUICStreamId(QUICStreamId stream_id, uint8_t *buf, size_t *len);
  static void write_QUICOffset(QUICOffset offset, uint8_t *buf, size_t *len);
  static void write_QUICTransErrorCode(uint16_t error_code, uint8_t *buf, size_t *len);
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
  /**
   * This function returns the raw value. You'll need to add 3 to the returned value to get the actual connection id length.
   */
  static bool dcil(uint8_t &dst, const uint8_t *buf, uint64_t buf_len);
  /**
   * This function returns the raw value. You'll need to add 3 to the returned value to get the actual connection id length.
   */
  static bool scil(uint8_t &dst, const uint8_t *buf, uint64_t buf_len);
  static bool dcid(QUICConnectionId &dst, const uint8_t *buf, uint64_t buf_len);
  static bool scid(QUICConnectionId &dst, const uint8_t *buf, uint64_t buf_len);

  static const size_t CIL_BASE          = 3;
  static const size_t LH_VERSION_OFFSET = 1;
  static const size_t LH_CIL_OFFSET     = 5;
  static const size_t LH_DCID_OFFSET    = 6;
  static const size_t SH_DCID_OFFSET    = 1;
  static const size_t LH_MIN_LEN        = 6;
  static const size_t SH_MIN_LEN        = 1;
};

int to_hex_str(char *dst, size_t dst_len, const uint8_t *src, size_t src_len);
