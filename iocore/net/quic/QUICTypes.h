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

#if defined(darwin)
#include <libkern/OSByteOrder.h>
#define be64toh(x) OSSwapBigToHostInt64(x)
#define htobe64(x) OSSwapHostToBigInt64(x)
#elif defined(freebsd)
#include <sys/endian.h>
#else
#include <endian.h>
#endif
#include <cstring>

#include <memory>
#include <random>
#include <cstdint>
#include "ts/INK_MD5.h"
#include "ts/ink_memory.h"

// These magical defines should be removed when we implement seriously
#define MAGIC_NUMBER_0 0
#define MAGIC_NUMBER_1 1
#define MAGIC_NUMBER_TRUE true

// TODO: move to lib/ts/ink_memory.h?
using ats_unique_buf = std::unique_ptr<uint8_t, decltype(&ats_free)>;
ats_unique_buf ats_unique_malloc(size_t size);

using QUICPacketNumber = uint64_t;
using QUICVersion      = uint32_t;
using QUICStreamId     = uint64_t;
using QUICOffset       = uint64_t;

// TODO: Update version number
// Note: You also need to update tests for VersionNegotiationPacket creation, if you change the number of versions
// Prefix for drafts (0xff000000) + draft number
constexpr QUICVersion QUIC_SUPPORTED_VERSIONS[] = {
  0xff000008,
};
constexpr QUICStreamId STREAM_ID_FOR_HANDSHAKE = 0;

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
  ONE           = 0x1F,
  TWO           = 0x1E,
  THREE         = 0x1D,
  UNINITIALIZED = 0x1C,
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
  PONG,
  ACK,
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
};

enum class QUICPacketCreationResult {
  SUCCESS,
  FAILED,
  NOT_READY,
};

enum class QUICErrorClass {
  NONE,
  TRANSPORT,
  APPLICATION,
};

enum class QUICTransErrorCode : uint16_t {
  NO_ERROR = 0x00,
  INTERNAL_ERROR,
  FLOW_CONTROL_ERROR = 0x03,
  STREAM_ID_ERROR,
  STREAM_STATE_ERROR,
  FINAL_OFFSET_ERROR,
  FRAME_FORMAT_ERROR,
  TRANSPORT_PARAMETER_ERROR,
  VERSION_NEGOTIATION_ERROR,
  PROTOCOL_VIOLATION   = 0x0A,
  FRAME_ERROR          = 0x0100, // 0x100 - 0x1FF
  TLS_HANDSHAKE_FAILED = 0x0201,
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
  explicit operator bool() const { return true; }
  operator uint64_t() const { return _id; };
  QUICConnectionId() { this->randomize(); };
  QUICConnectionId(uint64_t id) : _id(id){};

  void
  randomize()
  {
    std::random_device rnd;
    this->_id = (static_cast<uint64_t>(rnd()) << 32) + rnd();
  };

private:
  uint64_t _id;
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

enum class QUICStreamType {
  CLIENT_BIDI,
  SERVER_BIDI,
  CLIENT_UNI,
  SERVER_UNI,
  HANDSHAKE,
};

class QUICTypeUtil
{
public:
  static bool has_long_header(const uint8_t *buf);
  static bool has_connection_id(const uint8_t *buf);
  static QUICStreamType detect_stream_type(QUICStreamId id);

  static QUICConnectionId read_QUICConnectionId(const uint8_t *buf, uint8_t n);
  static QUICPacketNumber read_QUICPacketNumber(const uint8_t *buf, uint8_t n);
  static QUICVersion read_QUICVersion(const uint8_t *buf);
  static QUICStreamId read_QUICStreamId(const uint8_t *buf);
  static QUICOffset read_QUICOffset(const uint8_t *buf);
  static QUICTransErrorCode read_QUICTransErrorCode(const uint8_t *buf);
  static QUICAppErrorCode read_QUICAppErrorCode(const uint8_t *buf);
  static uint64_t read_QUICMaxData(const uint8_t *buf);
  static uint64_t read_QUICVariableInt(const uint8_t *buf);

  static void write_QUICConnectionId(QUICConnectionId connection_id, uint8_t n, uint8_t *buf, size_t *len);
  static void write_QUICPacketNumber(QUICPacketNumber packet_number, uint8_t n, uint8_t *buf, size_t *len);
  static void write_QUICVersion(QUICVersion version, uint8_t *buf, size_t *len);
  static void write_QUICStreamId(QUICStreamId stream_id, uint8_t *buf, size_t *len);
  static void write_QUICOffset(QUICOffset offset, uint8_t *buf, size_t *len);
  static void write_QUICTransErrorCode(QUICTransErrorCode error_code, uint8_t *buf, size_t *len);
  static void write_QUICAppErrorCode(QUICAppErrorCode error_code, uint8_t *buf, size_t *len);
  static void write_QUICMaxData(uint64_t max_data, uint8_t *buf, size_t *len);

  static void write_QUICVariableInt(uint64_t data, uint8_t *buf, size_t *len);

  static uint64_t read_nbytes_as_uint(const uint8_t *buf, uint8_t n);
  static void write_uint_as_nbytes(uint64_t value, uint8_t n, uint8_t *buf, size_t *len);

private:
};

class QUICVariableInt
{
public:
  static size_t size(const uint8_t *src);
  static size_t size(uint64_t src);
  static int encode(uint8_t *dst, size_t dst_len, size_t &len, uint64_t src);
  static int decode(uint64_t &dst, size_t &len, const uint8_t *src, size_t src_len = 8);
};
