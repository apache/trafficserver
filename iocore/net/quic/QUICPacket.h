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

#include <memory>
#include <atomic>
#include <cstddef>

#include "I_IOBuffer.h"

#include "QUICTypes.h"

#define QUIC_FIELD_OFFSET_CONNECTION_ID 1
#define QUIC_FIELD_OFFSET_PACKET_NUMBER 4
#define QUIC_FIELD_OFFSET_PAYLOAD 5

class UDPConnection;
class QUICPacketHeader;
class QUICPacket;
class QUICPacketLongHeader;
class QUICPacketShortHeader;

extern ClassAllocator<QUICPacket> quicPacketAllocator;
extern ClassAllocator<QUICPacketLongHeader> quicPacketLongHeaderAllocator;
extern ClassAllocator<QUICPacketShortHeader> quicPacketShortHeaderAllocator;

using QUICPacketHeaderDeleterFunc = void (*)(QUICPacketHeader *p);
using QUICPacketHeaderUPtr        = std::unique_ptr<QUICPacketHeader, QUICPacketHeaderDeleterFunc>;

class QUICPacketHeader
{
public:
  QUICPacketHeader(const IpEndpoint from, const IpEndpoint to, ats_unique_buf buf, size_t len, QUICPacketNumber base)
    : _from(from), _to(to), _buf(std::move(buf)), _buf_len(len), _base_packet_number(base)
  {
  }
  ~QUICPacketHeader() {}
  const uint8_t *buf();

  virtual bool is_crypto_packet() const;

  const IpEndpoint &from() const;
  const IpEndpoint &to() const;

  virtual QUICPacketType type() const = 0;

  /*
   * Returns a connection id
   */
  virtual QUICConnectionId destination_cid() const = 0;
  virtual QUICConnectionId source_cid() const      = 0;

  virtual QUICPacketNumber packet_number() const = 0;
  virtual QUICVersion version() const            = 0;

  /*
   * Returns a pointer for the payload
   */
  virtual const uint8_t *payload() const = 0;

  /*
   * Returns its payload size based on header length and buffer size that is specified to the constructo.
   */
  uint16_t payload_size() const;

  /*
   * Returns its header size
   */
  virtual uint16_t size() const = 0;

  /*
   * Returns its packet size
   */
  uint16_t packet_size() const;

  /*
   * Returns a key phase
   */
  virtual QUICKeyPhase key_phase() const = 0;

  virtual bool has_version() const = 0;
  virtual bool is_valid() const    = 0;

  /***** STATIC members *****/

  /*
   * Load data from a buffer and create a QUICPacketHeader
   *
   * This creates either a QUICPacketShortHeader or a QUICPacketLongHeader.
   */
  static QUICPacketHeaderUPtr load(const IpEndpoint from, const IpEndpoint to, ats_unique_buf buf, size_t len,
                                   QUICPacketNumber base);

protected:
  QUICPacketHeader(){};
  QUICPacketHeader(QUICPacketType type, QUICPacketNumber packet_number, QUICPacketNumber base_packet_number, bool has_version,
                   QUICVersion version, ats_unique_buf payload, size_t payload_length, QUICKeyPhase key_phase)
    : _payload(std::move(payload)),
      _type(type),
      _key_phase(key_phase),
      _packet_number(packet_number),
      _base_packet_number(base_packet_number),
      _version(version),
      _payload_length(payload_length),
      _has_version(has_version){};
  // Token field in Initial packet could be very long.
  static constexpr size_t MAX_PACKET_HEADER_LEN = 256;

  const IpEndpoint _from = {};
  const IpEndpoint _to   = {};

  // These two are used only if the instance was created with a buffer
  ats_unique_buf _buf = {nullptr};
  size_t _buf_len     = 0;

  // These are used only if the instance was created without a buffer
  uint8_t _serialized[MAX_PACKET_HEADER_LEN];
  ats_unique_buf _payload              = ats_unique_buf(nullptr);
  QUICPacketType _type                 = QUICPacketType::UNINITIALIZED;
  QUICKeyPhase _key_phase              = QUICKeyPhase::INITIAL;
  QUICConnectionId _connection_id      = QUICConnectionId::ZERO();
  QUICPacketNumber _packet_number      = 0;
  QUICPacketNumber _base_packet_number = 0;
  QUICVersion _version                 = 0;
  size_t _payload_length               = 0;
  bool _has_version                    = false;
};

class QUICPacketLongHeader : public QUICPacketHeader
{
public:
  QUICPacketLongHeader() : QUICPacketHeader(){};
  virtual ~QUICPacketLongHeader(){};
  QUICPacketLongHeader(const IpEndpoint from, const IpEndpoint to, ats_unique_buf buf, size_t len, QUICPacketNumber base);

  QUICPacketType type() const override;
  QUICConnectionId destination_cid() const override;
  QUICConnectionId source_cid() const override;
  QUICConnectionId original_dcid() const;
  QUICPacketNumber packet_number() const override;
  bool has_version() const override;
  bool is_valid() const override;
  bool is_crypto_packet() const override;
  QUICVersion version() const override;
  const uint8_t *payload() const override;
  const uint8_t *token() const;
  size_t token_len() const;
  QUICKeyPhase key_phase() const override;
  uint16_t size() const override;

  static bool type(QUICPacketType &type, const uint8_t *packet, size_t packet_len);
  static bool version(QUICVersion &version, const uint8_t *packet, size_t packet_len);
  static bool dcil(uint8_t &dcil, const uint8_t *packet, size_t packet_len);
  static bool scil(uint8_t &scil, const uint8_t *packet, size_t packet_len);
  static bool token_length(size_t &token_length, uint8_t &field_len, size_t &token_length_filed_offset, const uint8_t *packet,
                           size_t packet_len);
  static bool length(size_t &length, uint8_t &length_field_len, size_t &length_field_offset, const uint8_t *packet,
                     size_t packet_len);
  static bool key_phase(QUICKeyPhase &key_phase, const uint8_t *packet, size_t packet_len);
  static bool packet_number_offset(size_t &pn_offset, const uint8_t *packet, size_t packet_len);
  static bool packet_length(size_t &length, const uint8_t *buf, size_t buf_len);

private:
  QUICConnectionId _destination_cid = QUICConnectionId::ZERO();
  QUICConnectionId _source_cid      = QUICConnectionId::ZERO();
  QUICConnectionId _original_dcid   = QUICConnectionId::ZERO(); //< RETRY packet only
  size_t _token_len                 = 0;                        //< INITIAL packet only
  size_t _token_offset              = 0;                        //< INITIAL packet only
  ats_unique_buf _token             = ats_unique_buf(nullptr);  //< INITIAL packet only
  size_t _payload_offset            = 0;
  bool _is_crypto_packet            = false;
};

class QUICPacketShortHeader : public QUICPacketHeader
{
public:
  QUICPacketShortHeader() : QUICPacketHeader(){};
  virtual ~QUICPacketShortHeader(){};
  QUICPacketShortHeader(const IpEndpoint from, const IpEndpoint to, ats_unique_buf buf, size_t len, QUICPacketNumber base);
  QUICPacketType type() const override;
  QUICConnectionId destination_cid() const override;
  QUICConnectionId
  source_cid() const override
  {
    return QUICConnectionId::ZERO();
  }
  QUICPacketNumber packet_number() const override;
  bool has_version() const override;
  bool is_valid() const override;
  QUICVersion version() const override;
  const uint8_t *payload() const override;
  QUICKeyPhase key_phase() const override;
  uint16_t size() const override;

  static bool key_phase(QUICKeyPhase &key_phase, const uint8_t *packet, size_t packet_len);
  static bool packet_number_offset(size_t &pn_offset, const uint8_t *packet, size_t packet_len, int dcil);

private:
  int _packet_number_len;
};

class QUICPacketHeaderDeleter
{
public:
  static void
  delete_null_header(QUICPacketHeader *header)
  {
    ink_assert(header == nullptr);
  }

  static void
  delete_long_header(QUICPacketHeader *header)
  {
    QUICPacketLongHeader *long_header = dynamic_cast<QUICPacketLongHeader *>(header);
    ink_assert(long_header != nullptr);
    long_header->~QUICPacketLongHeader();
    quicPacketLongHeaderAllocator.free(long_header);
  }

  static void
  delete_short_header(QUICPacketHeader *header)
  {
    QUICPacketShortHeader *short_header = dynamic_cast<QUICPacketShortHeader *>(header);
    ink_assert(short_header != nullptr);
    short_header->~QUICPacketShortHeader();
    quicPacketShortHeaderAllocator.free(short_header);
  }
};

class QUICPacket
{
public:
  static constexpr int MAX_INSTANCE_SIZE = 1024;

  QUICPacket();

  /*
   * Creates a QUICPacket with a QUICPacketHeader and a buffer that contains payload
   *
   * This will be used for receiving packets. Therefore, it is expected that payload is already decrypted.
   * However,  QUICPacket class itself doesn't care about whether the payload is protected (encrypted) or not.
   */
  QUICPacket(UDPConnection *udp_con, QUICPacketHeaderUPtr header, ats_unique_buf payload, size_t payload_len);

  QUICPacket(bool ack_eliciting, bool probing);

  virtual ~QUICPacket();

  UDPConnection *udp_con() const;
  virtual const IpEndpoint &from() const;
  virtual const IpEndpoint &to() const;
  virtual QUICPacketType type() const;
  QUICConnectionId destination_cid() const;
  QUICConnectionId source_cid() const;
  virtual QUICPacketNumber packet_number() const;
  QUICVersion version() const;
  const QUICPacketHeader &header() const;
  const uint8_t *payload() const;
  bool is_ack_eliciting() const;
  virtual bool is_crypto_packet() const;
  bool is_probing_packet() const;

  // TODO These two should be pure virtual
  virtual Ptr<IOBufferBlock>
  header_block() const
  {
    return Ptr<IOBufferBlock>();
  };
  virtual Ptr<IOBufferBlock>
  payload_block() const
  {
    return Ptr<IOBufferBlock>();
  };

  /*
   * Size of whole QUIC packet (header + payload + integrity check)
   */
  virtual uint16_t size() const;

  /*
   * Size of header
   */
  uint16_t header_size() const;

  /*
   * Length of payload
   */
  uint16_t payload_length() const;

  void store(uint8_t *buf, size_t *len) const;
  virtual QUICKeyPhase key_phase() const;

  /***** STATIC MEMBERS *****/

  static uint8_t calc_packet_number_len(QUICPacketNumber num, QUICPacketNumber base);
  static bool encode_packet_number(QUICPacketNumber &dst, QUICPacketNumber src, size_t len);
  static bool decode_packet_number(QUICPacketNumber &dst, QUICPacketNumber src, size_t len, QUICPacketNumber largest_acked);

private:
  UDPConnection *_udp_con      = nullptr;
  QUICPacketHeaderUPtr _header = QUICPacketHeaderUPtr(nullptr, &QUICPacketHeaderDeleter::delete_null_header);
  ats_unique_buf _payload      = ats_unique_buf(nullptr);
  size_t _payload_size         = 0;
  bool _is_ack_eliciting       = false;
  bool _is_probing_packet      = false;
};

using QUICPacketDeleterFunc = void (*)(QUICPacket *p);
using QUICPacketUPtr        = std::unique_ptr<QUICPacket, QUICPacketDeleterFunc>;

class QUICPacketDeleter
{
public:
  static void
  delete_null_packet(QUICPacket *packet)
  {
    ink_assert(packet == nullptr);
  }

  static void
  delete_packet(QUICPacket *packet)
  {
    packet->~QUICPacket();
    quicPacketAllocator.free(packet);
  }

  static void
  delete_dont_free(QUICPacket *packet)
  {
    packet->~QUICPacket();
  }

  static void
  delete_packet_new(QUICPacket *packet)
  {
    delete packet;
  }
};

class QUICLongHeaderPacket : public QUICPacket
{
public:
  QUICLongHeaderPacket(QUICVersion version, QUICConnectionId dcid, QUICConnectionId scid, bool ack_eliciting, bool probing,
                       bool crypto);

  uint16_t size() const override;

  bool is_crypto_packet() const override;

protected:
  size_t _write_common_header(uint8_t *buf) const;

  Ptr<IOBufferBlock> _payload_block;
  size_t _payload_length;

private:
  QUICVersion _version;
  QUICConnectionId _dcid;
  QUICConnectionId _scid;

  bool _is_crypto_packet;
};

class QUICShortHeaderPacket : public QUICPacket
{
public:
  QUICShortHeaderPacket(QUICConnectionId dcid, QUICPacketNumber packet_number, QUICPacketNumber base_packet_number,
                        QUICKeyPhase key_phase, bool ack_eliciting, bool probing);

  QUICPacketType type() const override;
  QUICKeyPhase key_phase() const override;
  QUICPacketNumber packet_number() const override;
  uint16_t size() const override;
  bool is_crypto_packet() const override;

  Ptr<IOBufferBlock> header_block() const override;
  Ptr<IOBufferBlock> payload_block() const override;
  void attach_payload(Ptr<IOBufferBlock> payload, bool unprotected);

private:
  QUICConnectionId _dcid;
  QUICPacketNumber _packet_number;
  QUICKeyPhase _key_phase;
  int _packet_number_len;

  Ptr<IOBufferBlock> _payload_block;
  size_t _payload_length;
};

class QUICStatelessResetPacket : public QUICPacket
{
public:
  QUICStatelessResetPacket(QUICStatelessResetToken token);

  QUICPacketType type() const override;

  Ptr<IOBufferBlock> header_block() const override;
  Ptr<IOBufferBlock> payload_block() const override;

private:
  QUICStatelessResetToken _token;
};

class QUICVersionNegotiationPacket : public QUICLongHeaderPacket
{
public:
  QUICVersionNegotiationPacket(QUICConnectionId dcid, QUICConnectionId scid, const QUICVersion versions[], int nversions);

  QUICPacketType type() const override;

  Ptr<IOBufferBlock> header_block() const override;
  Ptr<IOBufferBlock> payload_block() const override;

private:
  const QUICVersion *_versions;
  int _nversions;
};

class QUICInitialPacket : public QUICLongHeaderPacket
{
public:
  QUICInitialPacket(QUICVersion version, QUICConnectionId dcid, QUICConnectionId scid, size_t token_len, ats_unique_buf token,
                    size_t length, QUICPacketNumber packet_number, bool ack_eliciting, bool probing, bool crypto);

  QUICPacketType type() const override;
  QUICKeyPhase key_phase() const override;

  Ptr<IOBufferBlock> header_block() const override;
  Ptr<IOBufferBlock> payload_block() const override;
  void attach_payload(Ptr<IOBufferBlock> payload, bool unprotected);

  QUICPacketNumber packet_number() const override;

private:
  size_t _token_len     = 0;
  ats_unique_buf _token = ats_unique_buf(nullptr);
  QUICPacketNumber _packet_number;
};

class QUICZeroRttPacket : public QUICLongHeaderPacket
{
public:
  QUICZeroRttPacket(QUICVersion version, QUICConnectionId dcid, QUICConnectionId scid, size_t length,
                    QUICPacketNumber packet_number, bool ack_eliciting, bool probing);

  QUICPacketType type() const override;
  QUICKeyPhase key_phase() const override;

  Ptr<IOBufferBlock> header_block() const override;
  Ptr<IOBufferBlock> payload_block() const override;
  void attach_payload(Ptr<IOBufferBlock> payload, bool unprotected);

  QUICPacketNumber packet_number() const override;

private:
  QUICPacketNumber _packet_number;
};

class QUICHandshakePacket : public QUICLongHeaderPacket
{
public:
  QUICHandshakePacket(QUICVersion version, QUICConnectionId dcid, QUICConnectionId scid, size_t length,
                      QUICPacketNumber packet_number, bool ack_eliciting, bool probing, bool crypto);

  QUICPacketType type() const override;
  QUICKeyPhase key_phase() const override;

  Ptr<IOBufferBlock> header_block() const override;
  Ptr<IOBufferBlock> payload_block() const override;
  void attach_payload(Ptr<IOBufferBlock> payload, bool unprotected);

  QUICPacketNumber packet_number() const override;

private:
  QUICPacketNumber _packet_number;
};

class QUICRetryPacket : public QUICLongHeaderPacket
{
public:
  QUICRetryPacket(QUICVersion version, QUICConnectionId dcid, QUICConnectionId scid, QUICConnectionId ocid, QUICRetryToken &token);

  QUICPacketType type() const override;
  Ptr<IOBufferBlock> header_block() const override;
  Ptr<IOBufferBlock> payload_block() const override;

  QUICConnectionId original_dcid() const;

private:
  QUICConnectionId _ocid;
  QUICRetryToken _token;
>>>>>>> d3d5256e1... Use individual classes for sending packets
};
