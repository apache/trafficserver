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
#include "QUICRetryIntegrityTag.h"

#define QUIC_FIELD_OFFSET_CONNECTION_ID 1
#define QUIC_FIELD_OFFSET_PACKET_NUMBER 4
#define QUIC_FIELD_OFFSET_PAYLOAD 5

class UDPConnection;

class QUICPacket
{
public:
  static constexpr int MAX_INSTANCE_SIZE = 1024;

  // Token field in Initial packet could be very long.
  static constexpr size_t MAX_PACKET_HEADER_LEN = 256;

  /**
   * Creates a QUICPacket for sending packets
   */
  QUICPacket(bool ack_eliciting, bool probing);

  virtual ~QUICPacket();

  virtual QUICPacketType type() const              = 0;
  virtual QUICConnectionId destination_cid() const = 0;
  virtual QUICPacketNumber packet_number() const   = 0;
  bool is_ack_eliciting() const;
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
  virtual uint16_t header_size() const;

  /*
   * Length of payload (payload + integrity check if exists)
   */
  virtual uint16_t payload_length() const;

  /**
   * Key phase
   */
  virtual QUICKeyPhase key_phase() const;

  // FIXME Remove this and use IOBufferBlock instead
  void store(uint8_t *buf, size_t *len) const;

  /***** STATIC MEMBERS *****/

  static uint8_t calc_packet_number_len(QUICPacketNumber num, QUICPacketNumber base);
  static bool encode_packet_number(QUICPacketNumber &dst, QUICPacketNumber src, size_t len);
  static bool decode_packet_number(QUICPacketNumber &dst, QUICPacketNumber src, size_t len, QUICPacketNumber largest_acked);

protected:
  QUICPacket();

private:
  bool _is_ack_eliciting  = false;
  bool _is_probing_packet = false;
};

class QUICPacketR : public QUICPacket
{
public:
  /**
   * Creates a QUICPacket for receiving packets
   */
  QUICPacketR(UDPConnection *udp_con, IpEndpoint from, IpEndpoint to);

  virtual QUICPacketType type() const override = 0;

  UDPConnection *udp_con() const;
  virtual const IpEndpoint &from() const;
  virtual const IpEndpoint &to() const;

  static bool read_essential_info(Ptr<IOBufferBlock> block, QUICPacketType &type, QUICVersion &version, QUICConnectionId &dcid,
                                  QUICConnectionId &scid, QUICPacketNumber &packet_number, QUICPacketNumber base_packet_number,
                                  QUICKeyPhase &key_phase);
  static bool type(QUICPacketType &type, const uint8_t *packet, size_t packet_len);

protected:
  Ptr<IOBufferBlock> _header_block;
  Ptr<IOBufferBlock> _payload_block;

private:
  UDPConnection *_udp_con = nullptr;
  const IpEndpoint _from  = {};
  const IpEndpoint _to    = {};
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
  /**
   * For sending packet
   */
  QUICLongHeaderPacket(QUICVersion version, const QUICConnectionId &dcid, const QUICConnectionId &scid, bool ack_eliciting,
                       bool probing, bool crypto);

  QUICConnectionId source_cid() const;

  QUICConnectionId destination_cid() const override;
  uint16_t payload_length() const override;
  virtual QUICVersion version() const;
  virtual bool is_crypto_packet() const;

protected:
  size_t _write_common_header(uint8_t *buf) const;

  Ptr<IOBufferBlock> _payload_block;
  size_t _payload_length = 0;

private:
  QUICVersion _version;
  QUICConnectionId _dcid;
  QUICConnectionId _scid;

  bool _is_crypto_packet;
};

class QUICLongHeaderPacketR : public QUICPacketR
{
public:
  /**
   * For receiving packet
   */
  QUICLongHeaderPacketR(UDPConnection *udp_con, IpEndpoint from, IpEndpoint to, Ptr<IOBufferBlock> blocks);

  virtual ~QUICLongHeaderPacketR(){};

  QUICConnectionId destination_cid() const override;
  QUICConnectionId source_cid() const;
  virtual QUICVersion version() const;

  static bool type(QUICPacketType &type, const uint8_t *packet, size_t packet_len);
  static bool version(QUICVersion &version, const uint8_t *packet, size_t packet_len);
  static bool key_phase(QUICKeyPhase &key_phase, const uint8_t *packet, size_t packet_len);
  static bool length(size_t &length, uint8_t &length_field_len, size_t &length_field_offset, const uint8_t *packet,
                     size_t packet_len);
  static bool packet_length(size_t &packet_len, const uint8_t *buf, size_t buf_len);
  static bool packet_number_offset(size_t &pn_offset, const uint8_t *packet, size_t packet_len);

protected:
  QUICVersion _version;
  QUICConnectionId _scid;
  QUICConnectionId _dcid;
};

class QUICShortHeaderPacket : public QUICPacket
{
public:
  /**
   * For sending packet
   */
  QUICShortHeaderPacket(const QUICConnectionId &dcid, QUICPacketNumber packet_number, QUICPacketNumber base_packet_number,
                        QUICKeyPhase key_phase, bool ack_eliciting, bool probing);

  QUICPacketType type() const override;
  QUICKeyPhase key_phase() const override;
  QUICPacketNumber packet_number() const override;
  QUICConnectionId destination_cid() const override;

  uint16_t payload_length() const override;
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

class QUICShortHeaderPacketR : public QUICPacketR
{
public:
  /**
   * For receiving packet
   */
  QUICShortHeaderPacketR(UDPConnection *udp_con, IpEndpoint from, IpEndpoint to, Ptr<IOBufferBlock> blocks,
                         QUICPacketNumber base_packet_number);

  void attach_payload(Ptr<IOBufferBlock> payload, bool unprotected);

  QUICPacketType type() const override;
  QUICKeyPhase key_phase() const override;
  QUICPacketNumber packet_number() const override;
  QUICConnectionId destination_cid() const override;

  Ptr<IOBufferBlock> header_block() const override;
  Ptr<IOBufferBlock> payload_block() const override;

  static bool packet_number_offset(size_t &pn_offset, const uint8_t *packet, size_t packet_len, int dcil);

private:
  QUICKeyPhase _key_phase;
  QUICPacketNumber _packet_number;
  int _packet_number_len;
  QUICConnectionId _dcid = QUICConnectionId::ZERO();
};

class QUICStatelessResetPacket : public QUICPacket
{
public:
  /**
   * For sending packet
   */
  QUICStatelessResetPacket(QUICStatelessResetToken token, size_t maximum_size);

  QUICPacketType type() const override;
  QUICPacketNumber packet_number() const override;
  QUICConnectionId destination_cid() const override;

  Ptr<IOBufferBlock> header_block() const override;
  Ptr<IOBufferBlock> payload_block() const override;

  QUICStatelessResetToken token() const;

private:
  QUICStatelessResetToken _token;
  size_t _maximum_size;
};

class QUICStatelessResetPacketR : public QUICPacketR
{
public:
  /**
   * For receiving packet
   */
  QUICStatelessResetPacketR(UDPConnection *udp_con, IpEndpoint from, IpEndpoint to, Ptr<IOBufferBlock> blocks);

  QUICPacketType type() const override;
  QUICPacketNumber packet_number() const override;
  QUICConnectionId destination_cid() const override;
};

class QUICVersionNegotiationPacket : public QUICLongHeaderPacket
{
public:
  /**
   * For sending packet
   */
  QUICVersionNegotiationPacket(const QUICConnectionId &dcid, const QUICConnectionId &scid, const QUICVersion versions[],
                               int nversions, QUICVersion version_in_initial);

  QUICPacketType type() const override;
  QUICVersion version() const override;
  QUICPacketNumber packet_number() const override;
  uint16_t payload_length() const override;

  Ptr<IOBufferBlock> header_block() const override;
  Ptr<IOBufferBlock> payload_block() const override;

  const QUICVersion *versions() const;
  int nversions() const;

private:
  const QUICVersion *_versions;
  int _nversions;
  const QUICVersion _version_in_initial;
};

class QUICVersionNegotiationPacketR : public QUICLongHeaderPacketR
{
public:
  /**
   * For receiving packet
   */
  QUICVersionNegotiationPacketR(UDPConnection *udp_con, IpEndpoint from, IpEndpoint to, Ptr<IOBufferBlock> blocks);

  QUICPacketType type() const override;
  QUICPacketNumber packet_number() const override;
  QUICConnectionId destination_cid() const override;

  Ptr<IOBufferBlock> header_block() const override;
  Ptr<IOBufferBlock> payload_block() const override;

  const QUICVersion supported_version(uint8_t index) const;
  int nversions() const;

private:
  QUICConnectionId _dcid;
  uint8_t *_versions;
  int _nversions;
};

class QUICInitialPacket : public QUICLongHeaderPacket
{
public:
  /**
   * For sending packet
   */
  QUICInitialPacket(QUICVersion version, const QUICConnectionId &dcid, const QUICConnectionId &scid, size_t token_len,
                    ats_unique_buf token, size_t length, QUICPacketNumber packet_number, bool ack_eliciting, bool probing,
                    bool crypto);

  QUICPacketType type() const override;
  QUICPacketNumber packet_number() const override;
  QUICKeyPhase key_phase() const override;

  Ptr<IOBufferBlock> header_block() const override;
  Ptr<IOBufferBlock> payload_block() const override;
  void attach_payload(Ptr<IOBufferBlock> payload, bool unprotected);

private:
  size_t _token_len     = 0;
  ats_unique_buf _token = ats_unique_buf(nullptr);
  QUICPacketNumber _packet_number;
};

class QUICInitialPacketR : public QUICLongHeaderPacketR
{
public:
  /**
   * For receiving packet
   */
  QUICInitialPacketR(UDPConnection *udp_con, IpEndpoint from, IpEndpoint to, Ptr<IOBufferBlock> blocks,
                     QUICPacketNumber base_packet_number);
  ~QUICInitialPacketR();

  Ptr<IOBufferBlock> header_block() const override;
  Ptr<IOBufferBlock> payload_block() const override;
  void attach_payload(Ptr<IOBufferBlock> payload, bool unprotected);

  QUICPacketType type() const override;
  QUICPacketNumber packet_number() const override;
  QUICKeyPhase key_phase() const override;

  const QUICAddressValidationToken &token() const;

  static bool token_length(size_t &token_length, uint8_t &field_len, size_t &token_length_filed_offset, const uint8_t *packet,
                           size_t packet_len);

protected:
  Ptr<IOBufferBlock> _payload_block;

private:
  QUICPacketNumber _packet_number;
  QUICAddressValidationToken *_token = nullptr;

  bool _parse();
};

class QUICZeroRttPacket : public QUICLongHeaderPacket
{
public:
  /**
   * For sending packet
   */
  QUICZeroRttPacket(QUICVersion version, const QUICConnectionId &dcid, const QUICConnectionId &scid, size_t length,
                    QUICPacketNumber packet_number, bool ack_eliciting, bool probing);

  QUICPacketType type() const override;
  QUICPacketNumber packet_number() const override;
  QUICKeyPhase key_phase() const override;

  Ptr<IOBufferBlock> header_block() const override;
  Ptr<IOBufferBlock> payload_block() const override;
  void attach_payload(Ptr<IOBufferBlock> payload, bool unprotected);

private:
  QUICPacketNumber _packet_number;
};

class QUICZeroRttPacketR : public QUICLongHeaderPacketR
{
public:
  /**
   * For receiving packet
   */
  QUICZeroRttPacketR(UDPConnection *udp_con, IpEndpoint from, IpEndpoint to, Ptr<IOBufferBlock> blocks,
                     QUICPacketNumber base_packet_number);

  Ptr<IOBufferBlock> header_block() const override;
  Ptr<IOBufferBlock> payload_block() const override;
  void attach_payload(Ptr<IOBufferBlock> payload, bool unprotected);

  QUICPacketType type() const override;
  QUICPacketNumber packet_number() const override;
  QUICKeyPhase key_phase() const override;

private:
  QUICPacketNumber _packet_number;
};

class QUICHandshakePacket : public QUICLongHeaderPacket
{
public:
  /**
   * For sending packet
   */
  QUICHandshakePacket(QUICVersion version, const QUICConnectionId &dcid, const QUICConnectionId &scid, size_t length,
                      QUICPacketNumber packet_number, bool ack_eliciting, bool probing, bool crypto);

  QUICPacketType type() const override;
  QUICKeyPhase key_phase() const override;
  QUICPacketNumber packet_number() const override;

  Ptr<IOBufferBlock> header_block() const override;
  Ptr<IOBufferBlock> payload_block() const override;
  void attach_payload(Ptr<IOBufferBlock> payload, bool unprotected);

private:
  QUICPacketNumber _packet_number;
};

class QUICHandshakePacketR : public QUICLongHeaderPacketR
{
public:
  /**
   * For receiving packet
   */
  QUICHandshakePacketR(UDPConnection *udp_con, IpEndpoint from, IpEndpoint to, Ptr<IOBufferBlock> blocks,
                       QUICPacketNumber base_packet_number);

  Ptr<IOBufferBlock> header_block() const override;
  Ptr<IOBufferBlock> payload_block() const override;
  void attach_payload(Ptr<IOBufferBlock> payload, bool unprotected);

  QUICPacketType type() const override;
  QUICKeyPhase key_phase() const override;
  QUICPacketNumber packet_number() const override;

private:
  QUICPacketNumber _packet_number;
};

class QUICRetryPacket : public QUICLongHeaderPacket
{
public:
  /**
   * For sending packet
   */
  QUICRetryPacket(QUICVersion version, const QUICConnectionId &dcid, const QUICConnectionId &scid, QUICRetryToken &token);

  QUICPacketType type() const override;
  QUICPacketNumber packet_number() const override;
  uint16_t payload_length() const override;

  Ptr<IOBufferBlock> header_block() const override;
  Ptr<IOBufferBlock> payload_block() const override;

  const QUICRetryToken &token() const;

private:
  QUICRetryToken _token;

  bool _compute_retry_integrity_tag(uint8_t *out, QUICConnectionId odcid, Ptr<IOBufferBlock> header,
                                    Ptr<IOBufferBlock> payload) const;
};

class QUICRetryPacketR : public QUICLongHeaderPacketR
{
public:
  /**
   * For receiving packet
   */
  QUICRetryPacketR(UDPConnection *udp_con, IpEndpoint from, IpEndpoint to, Ptr<IOBufferBlock> blocks);
  ~QUICRetryPacketR();

  Ptr<IOBufferBlock> header_block() const override;
  Ptr<IOBufferBlock> payload_block() const override;

  QUICPacketType type() const override;
  QUICPacketNumber packet_number() const override;

  const QUICAddressValidationToken &token() const;
  bool has_valid_tag(const QUICConnectionId &odcid) const;

private:
  QUICAddressValidationToken *_token = nullptr;
  uint8_t _integrity_tag[QUICRetryIntegrityTag::LEN];
  Ptr<IOBufferBlock> _payload_block_without_tag;
};
