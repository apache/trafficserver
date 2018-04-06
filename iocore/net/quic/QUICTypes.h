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
#include "ts/ink_endian.h"

#include <memory>
#include <random>
#include <cstdint>
#include "ts/INK_MD5.h"
#include "ts/ink_memory.h"
#include "ts/ink_inet.h"

// These magical defines should be removed when we implement seriously
#define MAGIC_NUMBER_0 0
#define MAGIC_NUMBER_1 1
#define MAGIC_NUMBER_TRUE true

using QUICPacketNumber = uint64_t;
using QUICVersion      = uint32_t;
using QUICStreamId     = uint64_t;
using QUICOffset       = uint64_t;

// TODO: Update version number
// Note: You also need to update tests for VersionNegotiationPacket creation, if you change the number of versions
// Prefix for drafts (0xff000000) + draft number
constexpr QUICVersion QUIC_SUPPORTED_VERSIONS[] = {
  0xff00000a,
};
constexpr QUICVersion QUIC_EXERCISE_VERSIONS = 0x1a2a3a4a;

constexpr QUICStreamId STREAM_ID_FOR_HANDSHAKE = 0;

enum class QUICHandshakeMsgType {
  NONE = 0,
  INITIAL,
  RETRY,
  HANDSHAKE,
};

// Devide to QUICPacketType and QUICPacketLongHeaderType ?
enum class QUICPacketType : uint8_t {
  VERSION_NEGOTIATION = 0,
  PROTECTED,                 // Not on the spec. but just for convenience // should be short header
  STATELESS_RESET,           // Not on the spec. but just for convenience
  INITIAL            = 0x7F, // draft-08 version-specific type
  RETRY              = 0x7E, // draft-08 version-specific type
  HANDSHAKE          = 0x7D, // draft-08 version-specific type
  ZERO_RTT_PROTECTED = 0x7C, // draft-08 version-specific type
  UNINITIALIZED      = 0xFF, // Not on the spec. but just for convenience
};

// To detect length of Packet Number
enum class QUICPacketShortHeaderType : int {
  ONE           = 0x0,
  TWO           = 0x1,
  THREE         = 0x2,
  UNINITIALIZED = 0x3,
};

// XXX If you add or remove QUICFrameType, you might also need to change QUICFrame::type(const uint8_t *)
enum class QUICFrameType : uint8_t {
  PADDING = 0x00,
  RST_STREAM,
  CONNECTION_CLOSE,
  APPLICATION_CLOSE,
  MAX_DATA,
  MAX_STREAM_DATA,
  MAX_STREAM_ID,
  PING,
  BLOCKED,
  STREAM_BLOCKED,
  STREAM_ID_BLOCKED,
  NEW_CONNECTION_ID,
  STOP_SENDING,
  ACK,
  PATH_CHALLENGE,
  PATH_RESPONSE,
  STREAM  = 0x10, // 0x10 - 0x17
  UNKNOWN = 0x18,
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
  CLEARTEXT,
  ZERORTT,
};

enum class QUICPacketCreationResult {
  SUCCESS,
  FAILED,
  NOT_READY,
  IGNORED,
  UNSUPPORTED,
};

enum class QUICErrorClass {
  NONE,
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
  FRAME_FORMAT_ERROR,
  TRANSPORT_PARAMETER_ERROR,
  VERSION_NEGOTIATION_ERROR,
  PROTOCOL_VIOLATION,
  UNSOLICITED_PATH_RESPONSE = 0x0B,
  FRAME_ERROR               = 0x0100, // 0x100 - 0x1FF
  TLS_HANDSHAKE_FAILED      = 0x0201,
  TLS_FATAL_ALERT_GENERATED,
  TLS_FATAL_ALERT_RECEIVED,
};

// Application Protocol Error Codes defined in application
using QUICAppErrorCode                          = uint16_t;
constexpr uint16_t QUIC_APP_ERROR_CODE_STOPPING = 0;

class QUICError
{
public:
  virtual ~QUICError() {}
  uint16_t code();

  QUICErrorClass cls = QUICErrorClass::NONE;
  union {
    QUICTransErrorCode trans_error_code = QUICTransErrorCode::NO_ERROR;
    QUICAppErrorCode app_error_code;
  };
  const char *msg = nullptr;

protected:
  QUICError(){};
  QUICError(const QUICTransErrorCode error_code, const char *error_msg = nullptr)
    : cls(QUICErrorClass::TRANSPORT), trans_error_code(error_code), msg(error_msg){};
  QUICError(const QUICAppErrorCode error_code, const char *error_msg = nullptr)
    : cls(QUICErrorClass::APPLICATION), app_error_code(error_code), msg(error_msg){};
};

class QUICNoError : public QUICError
{
public:
  QUICNoError() : QUICError() {}
};

class QUICConnectionError : public QUICError
{
public:
  QUICConnectionError() : QUICError() {}
  QUICConnectionError(const QUICTransErrorCode error_code, const char *error_msg = nullptr) : QUICError(error_code, error_msg){};
  QUICConnectionError(const QUICAppErrorCode error_code, const char *error_msg = nullptr) : QUICError(error_code, error_msg){};
};

class QUICStream;

class QUICStreamError : public QUICError
{
public:
  QUICStreamError() : QUICError() {}
  QUICStreamError(QUICStream *s, const QUICTransErrorCode error_code, const char *error_msg = nullptr)
    : QUICError(error_code, error_msg), stream(s){};
  QUICStreamError(QUICStream *s, const QUICAppErrorCode error_code, const char *error_msg = nullptr)
    : QUICError(error_code, error_msg), stream(s){};

  QUICStream *stream;
};

using QUICErrorUPtr           = std::unique_ptr<QUICError>;
using QUICConnectionErrorUPtr = std::unique_ptr<QUICConnectionError>;
using QUICStreamErrorUPtr     = std::unique_ptr<QUICStreamError>;

class QUICConnectionId
{
public:
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
    return memcmp(this->_id, x._id, sizeof(this->_id)) == 0;
  }

  bool
  operator!=(const QUICConnectionId &x) const
  {
    return memcmp(this->_id, x._id, sizeof(this->_id)) != 0;
  }

  bool is_zero() const;
  void randomize();

private:
  uint64_t _hashcode() const;
  uint8_t _id[8];
};

class QUICStatelessResetToken
{
public:
  constexpr static int8_t LEN = 16;

  QUICStatelessResetToken() {}
  QUICStatelessResetToken(const uint8_t *buf) { memcpy(this->_token, buf, QUICStatelessResetToken::LEN); }

  void
  generate(QUICConnectionId conn_id, uint32_t server_id)
  {
    this->_gen_token(conn_id ^ server_id);
  }

  const uint8_t *
  buf() const
  {
    return _token;
  }

private:
  uint8_t _token[16] = {0};

  void _gen_token(uint64_t data);
};

enum class QUICStreamType : uint8_t {
  CLIENT_BIDI = 0x00,
  SERVER_BIDI,
  CLIENT_UNI,
  SERVER_UNI,
  HANDSHAKE,
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

class QUICTypeUtil
{
public:
  static bool has_long_header(const uint8_t *buf);
  static bool has_connection_id(const uint8_t *buf);
  static bool is_supported_version(QUICVersion version);
  static QUICStreamType detect_stream_type(QUICStreamId id);

  static QUICConnectionId read_QUICConnectionId(const uint8_t *buf, uint8_t n);
  static QUICPacketNumber read_QUICPacketNumber(const uint8_t *buf, uint8_t n);
  static QUICVersion read_QUICVersion(const uint8_t *buf);
  static QUICStreamId read_QUICStreamId(const uint8_t *buf);
  static QUICOffset read_QUICOffset(const uint8_t *buf);
  static QUICTransErrorCode read_QUICTransErrorCode(const uint8_t *buf);
  static QUICAppErrorCode read_QUICAppErrorCode(const uint8_t *buf);
  static uint64_t read_QUICMaxData(const uint8_t *buf);

  static void write_QUICConnectionId(QUICConnectionId connection_id, uint8_t n, uint8_t *buf, size_t *len);
  static void write_QUICPacketNumber(QUICPacketNumber packet_number, uint8_t n, uint8_t *buf, size_t *len);
  static void write_QUICVersion(QUICVersion version, uint8_t *buf, size_t *len);
  static void write_QUICStreamId(QUICStreamId stream_id, uint8_t *buf, size_t *len);
  static void write_QUICOffset(QUICOffset offset, uint8_t *buf, size_t *len);
  static void write_QUICTransErrorCode(QUICTransErrorCode error_code, uint8_t *buf, size_t *len);
  static void write_QUICAppErrorCode(QUICAppErrorCode error_code, uint8_t *buf, size_t *len);
  static void write_QUICMaxData(uint64_t max_data, uint8_t *buf, size_t *len);

private:
};
