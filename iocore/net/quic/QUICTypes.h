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
using QUICStreamId     = uint32_t;
using QUICOffset       = uint64_t;

// TODO: Update version number
// Note: You also need to update tests for VersionNegotiationPacket, if you change the number of versions
// Prefix for drafts (0xff000000) + draft number
constexpr QUICVersion QUIC_SUPPORTED_VERSIONS[] = {
  0xff000005,
};
constexpr QUICStreamId STREAM_ID_FOR_HANDSHAKE = 0;

enum class QUICPacketType : int {
  VERSION_NEGOTIATION = 1,
  CLIENT_INITIAL,
  SERVER_STATELESS_RETRY,
  SERVER_CLEARTEXT,
  CLIENT_CLEARTEXT,
  ZERO_RTT_PROTECTED,
  ONE_RTT_PROTECTED_KEY_PHASE_0,
  ONE_RTT_PROTECTED_KEY_PHASE_1,
  STATELESS_RESET,
  UNINITIALIZED,
};

// To detect length of Packet Number
enum class QUICPacketShortHeaderType : int {
  ONE = 1,
  TWO,
  THREE,
  UNINITIALIZED,
};

// XXX If you add or remove QUICFrameType, you might also need to change QUICFrame::type(const uint8_t *)
enum class QUICFrameType : int {
  PADDING = 0x00,
  RST_STREAM,
  CONNECTION_CLOSE,
  MAX_DATA = 0x04,
  MAX_STREAM_DATA,
  MAX_STREAM_ID,
  PING,
  BLOCKED,
  STREAM_BLOCKED,
  STREAM_ID_NEEDED,
  NEW_CONNECTION_ID,
  STOP_SENDING,
  ACK     = 0xA0,
  STREAM  = 0xC0,
  UNKNOWN = 0x100,
};

enum class QUICVersionNegotiationStatus {
  NOT_NEGOTIATED, // Haven't negotiated yet
  NEGOTIATED,     // Negotiated
  REVALIDATED,    // Revalidated in cryptographic handshake
  FAILED,         // Negotiation failed
};

enum class QUICKeyPhase : int {
  PHASE_0 = 0,
  PHASE_1,
  PHASE_UNINITIALIZED,
};

enum class QUICErrorClass {
  NONE,
  AQPPLICATION_SPECIFIC,
  HOST_LOCAL,
  QUIC_TRANSPORT,
  CRYPTOGRAPHIC,
};

enum class QUICErrorCode : uint32_t {
  APPLICATION_SPECIFIC_ERROR = 0,
  HOST_LOCAL_ERROR           = 0x40000000,
  NO_ERROR                   = 0x80000000,
  INTERNAL_ERROR,
  CANCELLED,
  FLOW_CONTROL_ERROR,
  STREAM_ID_ERROR,
  STREAM_STATE_ERROR,
  FINAL_OFFSET_ERROR,
  FRAME_FORMAT_ERROR,
  TRANSPORT_PARAMETER_ERROR,
  VERSION_NEGOTIATION_ERROR,
  PROTOCOL_VIOLATION   = 0x8000000A,
  QUIC_RECEIVED_RST    = 0x80000035,
  FRAME_ERROR          = 0x80000100,
  CRYPTOGRAPHIC_ERROR  = 0xC0000000,
  TLS_HANDSHAKE_FAILED = 0xC000001C,
  TLS_FATAL_ALERT_GENERATED,
  TLS_FATAL_ALERT_RECEIVED,
  // TODO Add error codes
};

class QUICError
{
public:
  virtual ~QUICError() {}
  QUICErrorClass cls;
  QUICErrorCode code;
  const char *msg;

protected:
  QUICError(const QUICErrorClass error_class = QUICErrorClass::NONE, const QUICErrorCode error_code = QUICErrorCode::NO_ERROR,
            const char *error_msg = nullptr)
    : cls(error_class), code(error_code), msg(error_msg){};
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
  QUICConnectionError(const QUICErrorClass error_class, const QUICErrorCode error_code, const char *error_msg = nullptr)
    : QUICError(error_class, error_code, error_msg){};
};

class QUICStream;

class QUICStreamError : public QUICError
{
public:
  QUICStreamError(QUICStream *s, const QUICErrorClass error_class, const QUICErrorCode error_code, const char *error_msg = nullptr)
    : QUICError(error_class, error_code, error_msg), stream(s){};
  QUICStream *stream;
};

using QUICErrorUPtr           = std::unique_ptr<QUICError>;
using QUICConnectionErrorUPtr = std::unique_ptr<QUICConnectionError>;
using QUICStreamErrorUPtr     = std::unique_ptr<QUICStreamError>;

class QUICStatelessToken
{
public:
  void
  gen_token(uint64_t data)
  {
    static constexpr char STATELESS_RETRY_TOKEN_KEY[] = "stateless_token_retry_key";
    MD5Context ctx;
    ctx.update(STATELESS_RETRY_TOKEN_KEY, strlen(STATELESS_RETRY_TOKEN_KEY));
    ctx.update(reinterpret_cast<void *>(&data), 8);
    ctx.finalize(_md5);
  }

  const uint8_t *
  get_u8() const
  {
    return _md5.u8;
  }

  const uint64_t *
  get_u64() const
  {
    return _md5.u64;
  }

private:
  INK_MD5 _md5;
};

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

enum class QUICStreamType { CLIENT, SERVER, HANDSHAKE };

class QUICMaximumData
{
public:
  QUICMaximumData(uint64_t d) : _data(d) {}
  bool
  operator>(uint64_t r) const
  {
    return this->_data > (r / 1024);
  }

  bool
  operator<(uint64_t r) const
  {
    return this->_data < (r / 1024);
  }

  bool
  operator>=(uint64_t r) const
  {
    return this->_data >= (r / 1024);
  }

  bool
  operator<=(uint64_t r) const
  {
    return this->_data <= (r / 1024);
  }

  bool
  operator==(uint64_t r) const
  {
    return this->_data == (r / 1024);
  }

  QUICMaximumData &
  operator=(uint64_t d)
  {
    this->_data = d;
    return *this;
  }

  QUICMaximumData &
  operator+=(uint64_t d)
  {
    this->_data += d;
    return *this;
  }

  operator uint64_t() const { return _data; }

private:
  uint64_t _data = 0; // in units of 1024 octets
};

class QUICTypeUtil
{
public:
  static bool hasLongHeader(const uint8_t *buf);
  static QUICStreamType detect_stream_type(QUICStreamId id);

  static QUICConnectionId read_QUICConnectionId(const uint8_t *buf, uint8_t n);
  static QUICPacketNumber read_QUICPacketNumber(const uint8_t *buf, uint8_t n);
  static QUICVersion read_QUICVersion(const uint8_t *buf);
  static QUICStreamId read_QUICStreamId(const uint8_t *buf, uint8_t n);
  static QUICOffset read_QUICOffset(const uint8_t *buf, uint8_t n);
  static QUICErrorCode read_QUICErrorCode(const uint8_t *buf);

  static void write_QUICConnectionId(QUICConnectionId connection_id, uint8_t n, uint8_t *buf, size_t *len);
  static void write_QUICPacketNumber(QUICPacketNumber packet_number, uint8_t n, uint8_t *buf, size_t *len);
  static void write_QUICVersion(QUICVersion version, uint8_t *buf, size_t *len);
  static void write_QUICStreamId(QUICStreamId stream_id, uint8_t n, uint8_t *buf, size_t *len);
  static void write_QUICOffset(QUICOffset offset, uint8_t n, uint8_t *buf, size_t *len);
  static void write_QUICErrorCode(QUICErrorCode error_code, uint8_t *buf, size_t *len);

  static uint64_t read_nbytes_as_uint(const uint8_t *buf, uint8_t n);
  static void write_uint_as_nbytes(uint64_t value, uint8_t n, uint8_t *buf, size_t *len);

private:
};
