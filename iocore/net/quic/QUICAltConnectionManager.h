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

#include <inttypes.h>
#include <queue>

#include "QUICFrameGenerator.h"
#include "QUICTypes.h"
#include "QUICConnection.h"

class QUICConnectionTable;
class QUICResetTokenTable;

class QUICAltConnectionManager : public QUICFrameHandler, public QUICFrameGenerator
{
public:
  QUICAltConnectionManager(QUICConnection *qc, QUICConnectionTable &ctable, QUICResetTokenTable &rtable,
                           const QUICConnectionId &peer_initial_cid, uint32_t instance_id, uint8_t active_cid_limit,
                           const IpEndpoint *preferred_endpoint_ipv4 = nullptr,
                           const IpEndpoint *preferred_endpoint_ipv6 = nullptr);
  ~QUICAltConnectionManager();

  /**
   * Check if AltConnectionManager has at least one CID advertised by the peer.
   */
  bool is_ready_to_migrate() const;

  /**
   * Prepare for new CID for the peer, and return one of CIDs advertised by the peer.
   * New CID for the peer will be sent on next call for generate_frame()
   */
  QUICConnectionId migrate_to_alt_cid();

  /**
   * Migrate to new CID
   *
   * cid need to match with one of alt CID that AltConnectionManager prepared.
   */
  bool migrate_to(const QUICConnectionId &cid, QUICStatelessResetToken &new_reset_token);

  void drop_cid(const QUICConnectionId &cid);

  void set_remote_preferred_address(const QUICPreferredAddress &preferred_address);
  void set_remote_active_cid_limit(uint8_t active_cid_limit);

  /**
   * Invalidate all CIDs prepared
   */
  void invalidate_alt_connections();

  /**
   * Returns server preferred address if available
   */
  const QUICPreferredAddress *preferred_address() const;

  // QUICFrameHandler
  virtual std::vector<QUICFrameType> interests() override;
  virtual QUICConnectionErrorUPtr handle_frame(QUICEncryptionLevel level, const QUICFrame &frame) override;

  // QUICFrameGenerator
  bool will_generate_frame(QUICEncryptionLevel level, size_t current_packet_size, bool ack_eliciting, uint32_t seq_num) override;
  QUICFrame *generate_frame(uint8_t *buf, QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size,
                            size_t current_packet_size, uint32_t seq_num) override;

private:
  struct AltConnectionInfo {
    uint64_t seq_num;
    QUICConnectionId id;
    QUICStatelessResetToken token;
    union {
      bool advertised; // For local info
      bool used;       // For remote info
    };
  };

  QUICConnection *_qc = nullptr;
  QUICConnectionTable &_ctable;
  QUICResetTokenTable &_rtable;
  AltConnectionInfo _alt_quic_connection_ids_local[8]; // 8 is perhaps enough
  std::vector<AltConnectionInfo> _alt_quic_connection_ids_remote;
  std::queue<uint64_t> _retired_seq_nums;
  uint32_t _instance_id                          = 0;
  uint8_t _local_active_cid_limit                = 0;
  uint8_t _remote_active_cid_limit               = 0;
  uint64_t _alt_quic_connection_id_seq_num       = 0;
  bool _need_advertise                           = false;
  QUICPreferredAddress *_local_preferred_address = nullptr;

  AltConnectionInfo _generate_next_alt_con_info();
  void _init_alt_connection_ids();
  void _update_alt_connection_id(uint64_t chosen_seq_num);

  void _records_new_connection_id_frame(QUICEncryptionLevel level, const QUICNewConnectionIdFrame &frame);
  void _records_retire_connection_id_frame(QUICEncryptionLevel, const QUICRetireConnectionIdFrame &frame);

  void _on_frame_lost(QUICFrameInformationUPtr &info) override;

  QUICConnectionErrorUPtr _register_remote_connection_id(const QUICNewConnectionIdFrame &frame);
  QUICConnectionErrorUPtr _retire_remote_connection_id(const QUICRetireConnectionIdFrame &frame);
};
