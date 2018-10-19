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

class QUICAltConnectionManager : public QUICFrameHandler, public QUICFrameGenerator
{
public:
  QUICAltConnectionManager(QUICConnection *qc, QUICConnectionTable &ctable, QUICConnectionId peer_initial_cid);
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
   * cid need to match with one of alt CID that AltConnnectionManager prepared.
   */
  bool migrate_to(QUICConnectionId cid, QUICStatelessResetToken &new_reset_token);

  void drop_cid(QUICConnectionId cid);

  /**
   * Invalidate all CIDs prepared
   */
  void invalidate_alt_connections();

  // QUICFrameHandler
  virtual std::vector<QUICFrameType> interests() override;
  virtual QUICConnectionErrorUPtr handle_frame(QUICEncryptionLevel level, const QUICFrame &frame) override;

  // QUICFrameGenerator
  bool will_generate_frame(QUICEncryptionLevel level) override;
  QUICFrameUPtr generate_frame(QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size) override;

private:
  class AltConnectionInfo
  {
  public:
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
  AltConnectionInfo *_alt_quic_connection_ids_local;
  std::vector<AltConnectionInfo> _alt_quic_connection_ids_remote;
  std::queue<uint64_t> _retired_seq_nums;
  uint8_t _nids                            = 0;
  uint64_t _alt_quic_connection_id_seq_num = 0;
  bool _need_advertise                     = false;

  AltConnectionInfo _generate_next_alt_con_info();
  void _init_alt_connection_ids();
  bool _update_alt_connection_id(uint64_t chosen_seq_num);

  QUICConnectionErrorUPtr _register_remote_connection_id(const QUICNewConnectionIdFrame &frame);
  QUICConnectionErrorUPtr _retire_remote_connection_id(const QUICRetireConnectionIdFrame &frame);
};
