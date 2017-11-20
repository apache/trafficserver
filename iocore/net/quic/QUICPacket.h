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
#include <cstddef>

#include "ts/List.h"
#include "I_IOBuffer.h"

#include "QUICTypes.h"
#include "QUICCrypto.h"

#define QUIC_FIELD_OFFSET_CONNECTION_ID 1
#define QUIC_FIELD_OFFSET_PACKET_NUMBER 4
#define QUIC_FIELD_OFFSET_PAYLOAD 5

class QUICPacketHeader
{
public:
  QUICPacketHeader(const uint8_t *buf, size_t len, QUICPacketNumber base) : _buf(buf), _buf_len(len), _base_packet_number(base) {}
  const uint8_t *buf();
  virtual QUICPacketType type() const            = 0;
  virtual QUICConnectionId connection_id() const = 0;
  virtual QUICPacketNumber packet_number() const = 0;
  virtual QUICVersion version() const            = 0;

  /*
   * Returns a pointer for the payload
   */
  virtual const uint8_t *payload() const = 0;

  /*
   * Returns a payload size based on header length and buffer size that is specified to the constructo.
   */
  uint16_t payload_size() const;

  /*
   * Returns a header size
   */
  virtual uint16_t length() const = 0;

  /*
   * Returns a key phase
   */
  virtual QUICKeyPhase key_phase() const = 0;

  /*
   * Stores serialized header
   *
   * The serialized data doesn't contain a payload part even if it was created with a buffer that contains payload data.
   */
  virtual void store(uint8_t *buf, size_t *len) const = 0;

  QUICPacketHeader *clone() const;

  virtual bool has_key_phase() const     = 0;
  virtual bool has_connection_id() const = 0;
  virtual bool has_version() const       = 0;

  /***** STATIC members *****/

  /*
   * Load data from a buffer and create a QUICPacketHeader
   *
   * This creates either a QUICPacketShortHeader or a QUICPacketLongHeader.
   */
  static QUICPacketHeader *load(const uint8_t *buf, size_t len, QUICPacketNumber base);

  /*
   * Build a QUICPacketHeader
   *
   * This creates a QUICPacketLongHeader.
   */
  static QUICPacketHeader *build(QUICPacketType type, QUICConnectionId connection_id, QUICPacketNumber packet_number,
                                 QUICPacketNumber base_packet_number, QUICVersion version, ats_unique_buf payload, size_t len);

  /*
   * Build a QUICPacketHeader
   *
   * This creates a QUICPacketShortHeader that contains a ConnectionID.
   */
  static QUICPacketHeader *build(QUICPacketType type, QUICPacketNumber packet_number, QUICPacketNumber base_packet_number,
                                 ats_unique_buf payload, size_t len);

  /*
   * Build a QUICPacketHeader
   *
   * This creates a QUICPacketShortHeader that doesn't contain a ConnectionID..
   */
  static QUICPacketHeader *build(QUICPacketType type, QUICConnectionId connection_id, QUICPacketNumber packet_number,
                                 QUICPacketNumber base_packet_number, ats_unique_buf payload, size_t len);

protected:
  QUICPacketHeader(){};

  // These two are used only if the instance was created with a buffer
  const uint8_t *_buf = nullptr;
  size_t _buf_len     = 0;

  ats_unique_buf _payload              = ats_unique_buf(nullptr, [](void *p) { ats_free(p); });
  QUICPacketType _type                 = QUICPacketType::UNINITIALIZED;
  QUICKeyPhase _key_phase              = QUICKeyPhase::CLEARTEXT;
  QUICConnectionId _connection_id      = 0;
  QUICPacketNumber _packet_number      = 0;
  QUICPacketNumber _base_packet_number = 0;
  QUICVersion _version                 = 0;
  size_t _payload_len                  = 0;
  bool _has_key_phase                  = false;
  bool _has_connection_id              = false;
  bool _has_version                    = false;
};

class QUICPacketLongHeader : public QUICPacketHeader
{
public:
  QUICPacketLongHeader() : QUICPacketHeader(){};
  QUICPacketLongHeader(const uint8_t *buf, size_t len, QUICPacketNumber base) : QUICPacketHeader(buf, len, base) {}
  QUICPacketLongHeader(QUICPacketType type, QUICConnectionId connection_id, QUICPacketNumber packet_number,
                       QUICPacketNumber base_packet_number, QUICVersion version, ats_unique_buf buf, size_t len);
  QUICPacketType type() const;
  QUICConnectionId connection_id() const;
  QUICPacketNumber packet_number() const;
  bool has_version() const;
  QUICVersion version() const;
  const uint8_t *payload() const;
  bool has_connection_id() const;
  QUICKeyPhase key_phase() const;
  bool has_key_phase() const;
  uint16_t length() const;
  void store(uint8_t *buf, size_t *len) const;
};

class QUICPacketShortHeader : public QUICPacketHeader
{
public:
  QUICPacketShortHeader() : QUICPacketHeader(){};
  QUICPacketShortHeader(const uint8_t *buf, size_t len, QUICPacketNumber base) : QUICPacketHeader(buf, len, base) {}
  QUICPacketShortHeader(QUICPacketType type, QUICPacketNumber packet_number, QUICPacketNumber base_packet_number,
                        ats_unique_buf buf, size_t len);
  QUICPacketShortHeader(QUICPacketType type, QUICConnectionId connection_id, QUICPacketNumber packet_number,
                        QUICPacketNumber base_packet_number, ats_unique_buf buf, size_t len);
  QUICPacketType type() const;
  QUICConnectionId connection_id() const;
  QUICPacketNumber packet_number() const;
  bool has_version() const;
  QUICVersion version() const;
  const uint8_t *payload() const;
  bool has_connection_id() const;
  QUICKeyPhase key_phase() const;
  bool has_key_phase() const;
  uint16_t length() const;
  void store(uint8_t *buf, size_t *len) const;

private:
  QUICPacketShortHeaderType _discover_packet_number_type(QUICPacketNumber packet_number, QUICPacketNumber base_packet_number) const;
  int _packet_number_len() const;
  QUICPacketShortHeaderType _packet_number_type = QUICPacketShortHeaderType::UNINITIALIZED;
};

class QUICPacket
{
public:
  QUICPacket(){};

  /*
   * Creates a QUICPacket with a QUICPacketHeader and a buffer that contains payload
   *
   * QUICPacket class doesn't care about whether the payload is protected (encrypted) or not.
   */
  QUICPacket(QUICPacketHeader *header, ats_unique_buf payload, size_t payload_len, QUICPacketNumber base_packet_number);

  /*
   * Creates a QUICPacket that has a Long Header
   */
  QUICPacket(QUICPacketType type, QUICConnectionId connection_id, QUICPacketNumber packet_number,
             QUICPacketNumber base_packet_number, QUICVersion version, ats_unique_buf payload, size_t len, bool retransmittable);

  /*
   * Creates a QUICPacket that has a Short Header
   */
  QUICPacket(QUICPacketType type, QUICPacketNumber packet_number, QUICPacketNumber base_packet_number, ats_unique_buf payload,
             size_t len, bool retransmittable);

  /*
   * Creates a QUICPacket that has a Short Header with a Connection ID
   */
  QUICPacket(QUICPacketType type, QUICConnectionId connection_id, QUICPacketNumber packet_number,
             QUICPacketNumber base_packet_number, ats_unique_buf payload, size_t len, bool retransmittabl);

  /*
   * Creates a QUICpacket for stateless reset
   */
  QUICPacket(QUICPacketType type, QUICConnectionId connection_id, QUICStatelessToken stateless_reset_token);
  ~QUICPacket();

  QUICPacketType type() const;
  QUICConnectionId connection_id() const;
  QUICPacketNumber packet_number() const;
  QUICVersion version() const;
  const QUICPacketHeader *header() const;
  const uint8_t *payload() const;
  bool is_retransmittable() const;
  uint16_t size() const;
  uint16_t header_size() const;
  uint16_t payload_size() const;
  void store(uint8_t *buf, size_t *len) const;
  QUICKeyPhase key_phase() const;

  /***** STATIC MEMBERS *****/

  static uint8_t calc_packet_number_len(QUICPacketNumber num, QUICPacketNumber base);
  static bool encode_packet_number(QUICPacketNumber &dst, QUICPacketNumber src, size_t len);
  static bool decode_packet_number(QUICPacketNumber &dst, QUICPacketNumber src, size_t len, QUICPacketNumber largest_acked);

  LINK(QUICPacket, link);

private:
  QUICPacketHeader *_header;
  ats_unique_buf _payload  = ats_unique_buf(nullptr, [](void *p) { ats_free(p); });
  size_t _payload_size     = 0;
  size_t _size             = 0;
  bool _is_retransmittable = false;
};

class QUICPacketNumberGenerator
{
public:
  QUICPacketNumberGenerator();
  QUICPacketNumber randomize();
  QUICPacketNumber next();

private:
  QUICPacketNumber _current = 0;
};

using QUICPacketDeleterFunc = void (*)(QUICPacket *p);
using QUICPacketUPtr        = std::unique_ptr<QUICPacket, QUICPacketDeleterFunc>;

extern ClassAllocator<QUICPacket> quicPacketAllocator;
extern ClassAllocator<QUICPacketLongHeader> quicPacketLongHeaderAllocator;
extern ClassAllocator<QUICPacketShortHeader> quicPacketShortHeaderAllocator;

class QUICPacketDeleter
{
public:
  // TODO Probably these methods should call destructor
  static void
  delete_null_packet(QUICPacket *packet)
  {
  }

  static void
  delete_packet(QUICPacket *packet)
  {
    quicPacketAllocator.free(packet);
  }
};

class QUICPacketFactory
{
public:
  QUICPacketUPtr create(ats_unique_buf buf, size_t len, QUICPacketNumber base_packet_number, QUICPacketCreationResult &result);
  QUICPacketUPtr create_version_negotiation_packet(const QUICPacket *packet_sent_by_client, QUICPacketNumber base_packet_number);
  QUICPacketUPtr create_server_cleartext_packet(QUICConnectionId connection_id, QUICPacketNumber base_packet_number,
                                                ats_unique_buf payload, size_t len, bool retransmittable);
  QUICPacketUPtr create_server_protected_packet(QUICConnectionId connection_id, QUICPacketNumber base_packet_number,
                                                ats_unique_buf payload, size_t len, bool retransmittable);
  QUICPacketUPtr create_client_initial_packet(QUICConnectionId connection_id, QUICPacketNumber base_packet_number,
                                              QUICVersion version, ats_unique_buf payload, size_t len);
  static QUICPacketUPtr create_stateless_reset_packet(QUICConnectionId connection_id, QUICStatelessToken stateless_reset_token);
  void set_version(QUICVersion negotiated_version);
  void set_crypto_module(QUICCrypto *crypto);

private:
  QUICVersion _version = 0;
  QUICCrypto *_crypto  = nullptr;
  QUICPacketNumberGenerator _packet_number_generator;
};
