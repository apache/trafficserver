/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#include "algorithm"
#include "tscore/ink_assert.h"
#include "tscore/ink_defs.h"
#include "QUICAltConnectionManager.h"
#include "QUICConnectionTable.h"
#include "QUICResetTokenTable.h"

static constexpr char V_DEBUG_TAG[] = "v_quic_alt_con";

#define QUICACMVDebug(fmt, ...) Debug(V_DEBUG_TAG, "[%s] " fmt, this->_qc->cids().data(), ##__VA_ARGS__)

QUICAltConnectionManager::QUICAltConnectionManager(QUICConnection *qc, QUICConnectionTable &ctable, QUICResetTokenTable &rtable,
                                                   const QUICConnectionId &peer_initial_cid, uint32_t instance_id,
                                                   uint8_t local_active_cid_limit, const IpEndpoint *preferred_endpoint_ipv4,
                                                   const IpEndpoint *preferred_endpoint_ipv6)
  : _qc(qc), _ctable(ctable), _rtable(rtable), _instance_id(instance_id), _local_active_cid_limit(local_active_cid_limit)
{
  // Sequence number of the initial CID is 0
  this->_alt_quic_connection_ids_remote.push_back({0, peer_initial_cid, {}, {true}});
  this->_alt_quic_connection_ids_local[0].seq_num    = 0;
  this->_alt_quic_connection_ids_local[0].advertised = true;

  if ((preferred_endpoint_ipv4 && !ats_ip_addr_port_eq(*preferred_endpoint_ipv4, qc->five_tuple().source())) ||
      (preferred_endpoint_ipv6 && !ats_ip_addr_port_eq(*preferred_endpoint_ipv6, qc->five_tuple().source()))) {
    this->_alt_quic_connection_ids_local[1] = this->_generate_next_alt_con_info();
    // This alt cid will be advertised via Transport Parameter, so no need to advertise it via NCID frame
    this->_alt_quic_connection_ids_local[1].advertised = true;

    IpEndpoint empty_endpoint_ipv4;
    IpEndpoint empty_endpoint_ipv6;
    empty_endpoint_ipv4.sa.sa_family = AF_UNSPEC;
    empty_endpoint_ipv6.sa.sa_family = AF_UNSPEC;
    if (preferred_endpoint_ipv4 == nullptr) {
      preferred_endpoint_ipv4 = &empty_endpoint_ipv4;
    }
    if (preferred_endpoint_ipv6 == nullptr) {
      preferred_endpoint_ipv6 = &empty_endpoint_ipv6;
    }

    // FIXME Check nullptr dereference
    this->_local_preferred_address =
      new QUICPreferredAddress(*preferred_endpoint_ipv4, *preferred_endpoint_ipv6, this->_alt_quic_connection_ids_local[1].id,
                               this->_alt_quic_connection_ids_local[1].token);
  }
}

QUICAltConnectionManager::~QUICAltConnectionManager()
{
  ats_free(this->_alt_quic_connection_ids_local);
  delete this->_local_preferred_address;
}

const QUICPreferredAddress *
QUICAltConnectionManager::preferred_address() const
{
  return this->_local_preferred_address;
}

std::vector<QUICFrameType>
QUICAltConnectionManager::interests()
{
  return {QUICFrameType::NEW_CONNECTION_ID, QUICFrameType::RETIRE_CONNECTION_ID};
}

QUICConnectionErrorUPtr
QUICAltConnectionManager::handle_frame(QUICEncryptionLevel level, const QUICFrame &frame)
{
  QUICConnectionErrorUPtr error = nullptr;

  switch (frame.type()) {
  case QUICFrameType::NEW_CONNECTION_ID:
    error = this->_register_remote_connection_id(static_cast<const QUICNewConnectionIdFrame &>(frame));
    break;
  case QUICFrameType::RETIRE_CONNECTION_ID:
    error = this->_retire_remote_connection_id(static_cast<const QUICRetireConnectionIdFrame &>(frame));
    break;
  default:
    QUICACMVDebug("Unexpected frame type: %02x", static_cast<unsigned int>(frame.type()));
    ink_assert(false);
    break;
  }

  return error;
}

QUICAltConnectionManager::AltConnectionInfo
QUICAltConnectionManager::_generate_next_alt_con_info()
{
  QUICConnectionId conn_id;
  conn_id.randomize();
  QUICStatelessResetToken token(conn_id, this->_instance_id);
  AltConnectionInfo aci = {++this->_alt_quic_connection_id_seq_num, conn_id, token, {false}};

  if (this->_qc->direction() == NET_VCONNECTION_IN) {
    this->_ctable.insert(conn_id, this->_qc);
  }

  if (is_debug_tag_set(V_DEBUG_TAG)) {
    QUICACMVDebug("alt-cid=%s", conn_id.hex().c_str());
  }

  return aci;
}

void
QUICAltConnectionManager::_init_alt_connection_ids()
{
  for (int i = this->_alt_quic_connection_id_seq_num + 1; i < this->_remote_active_cid_limit; ++i) {
    this->_alt_quic_connection_ids_local[i] = this->_generate_next_alt_con_info();
  }
  this->_need_advertise = true;
}

void
QUICAltConnectionManager::_update_alt_connection_id(uint64_t chosen_seq_num)
{
  for (int i = 0; i < this->_remote_active_cid_limit; ++i) {
    if (this->_alt_quic_connection_ids_local[i].seq_num == chosen_seq_num) {
      this->_alt_quic_connection_ids_local[i] = this->_generate_next_alt_con_info();
      this->_need_advertise                   = true;
    }
  }
}

QUICConnectionErrorUPtr
QUICAltConnectionManager::_register_remote_connection_id(const QUICNewConnectionIdFrame &frame)
{
  QUICConnectionErrorUPtr error = nullptr;

  if (frame.connection_id() == QUICConnectionId::ZERO()) {
    error = std::make_unique<QUICConnectionError>(QUICTransErrorCode::PROTOCOL_VIOLATION, "received zero-length cid",
                                                  QUICFrameType::NEW_CONNECTION_ID);
  } else {
    int unused = 0;
    for (auto &&x : this->_alt_quic_connection_ids_remote) {
      if (x.seq_num == frame.sequence()) {
        return error;
      }
      if (x.used == false && x.seq_num != 1) {
        ++unused;
      }
    }
    if (unused > this->_local_active_cid_limit) {
      error = std::make_unique<QUICConnectionError>(QUICTransErrorCode::CONNECTION_ID_LIMIT_ERROR, "received too many alt CIDs",
                                                    QUICFrameType::NEW_CONNECTION_ID);
    } else {
      this->_alt_quic_connection_ids_remote.push_back(
        {frame.sequence(), frame.connection_id(), frame.stateless_reset_token(), {false}});
    }
  }

  return error;
}

QUICConnectionErrorUPtr
QUICAltConnectionManager::_retire_remote_connection_id(const QUICRetireConnectionIdFrame &frame)
{
  QUICConnectionErrorUPtr error = nullptr;

  if (frame.seq_num() > this->_alt_quic_connection_id_seq_num) {
    error = std::make_unique<QUICConnectionError>(QUICTransErrorCode::PROTOCOL_VIOLATION, "received unused sequence number",
                                                  QUICFrameType::RETIRE_CONNECTION_ID);
  } else {
    this->_update_alt_connection_id(frame.seq_num());
  }
  return error;
}

bool
QUICAltConnectionManager::is_ready_to_migrate() const
{
  if (this->_alt_quic_connection_ids_remote.empty()) {
    return false;
  }

  for (auto &info : this->_alt_quic_connection_ids_remote) {
    if (!info.used) {
      return true;
    }
  }
  return false;
}

QUICConnectionId
QUICAltConnectionManager::migrate_to_alt_cid()
{
  for (auto &info : this->_alt_quic_connection_ids_remote) {
    if (info.used) {
      continue;
    }
    info.used = true;
    this->_rtable.insert(info.token, this->_qc);
    return info.id;
  }

  ink_assert(!"Could not find CID available");
  return QUICConnectionId::ZERO();
}

bool
QUICAltConnectionManager::migrate_to(const QUICConnectionId &cid, QUICStatelessResetToken &new_reset_token)
{
  if (this->_local_preferred_address) {
    if (cid == this->_local_preferred_address->cid()) {
      new_reset_token = this->_local_preferred_address->token();
      return true;
    }
  }

  for (int i = 0; i < this->_remote_active_cid_limit; ++i) {
    AltConnectionInfo &info = this->_alt_quic_connection_ids_local[i];
    if (info.id == cid) {
      // Migrate connection
      new_reset_token = info.token;
      return true;
    }
  }
  return false;
}

void
QUICAltConnectionManager::drop_cid(const QUICConnectionId &cid)
{
  for (auto it = this->_alt_quic_connection_ids_remote.begin(); it != this->_alt_quic_connection_ids_remote.end(); ++it) {
    if (it->id == cid) {
      QUICACMVDebug("Dropping advertised CID %" PRIx32 " seq# %" PRIu64, it->id.h32(), it->seq_num);
      this->_retired_seq_nums.push(it->seq_num);
      this->_rtable.erase(it->token);
      this->_alt_quic_connection_ids_remote.erase(it);
      return;
    }
  }
}

void
QUICAltConnectionManager::invalidate_alt_connections()
{
  int n = this->_remote_active_cid_limit + ((this->_local_preferred_address == nullptr) ? 1 : 0);

  for (int i = 0; i < n; ++i) {
    this->_ctable.erase(this->_alt_quic_connection_ids_local[i].id, this->_qc);
  }
}

void
QUICAltConnectionManager::set_remote_preferred_address(const QUICPreferredAddress &preferred_address)
{
  ink_assert(preferred_address.is_available());

  // Sequence number of the preferred address is 1 if available
  this->_alt_quic_connection_ids_remote.push_back({1, preferred_address.cid(), preferred_address.token(), {false}});
}

void
QUICAltConnectionManager::set_remote_active_cid_limit(uint8_t active_cid_limit)
{
  this->_remote_active_cid_limit =
    std::min(static_cast<unsigned int>(active_cid_limit), countof(this->_alt_quic_connection_ids_local));
  this->_init_alt_connection_ids();
}

bool
QUICAltConnectionManager::will_generate_frame(QUICEncryptionLevel level, size_t current_packet_size, bool ack_eliciting,
                                              uint32_t seq_num)
{
  if (!this->_is_level_matched(level)) {
    return false;
  }

  return this->_need_advertise || !this->_retired_seq_nums.empty();
}

/**
 * @param connection_credit This is not used. Because NEW_CONNECTION_ID frame is not flow-controlled
 */
QUICFrame *
QUICAltConnectionManager::generate_frame(uint8_t *buf, QUICEncryptionLevel level, uint64_t /* connection_credit */,
                                         uint16_t maximum_frame_size, size_t current_packet_size, uint32_t seq_num)
{
  QUICFrame *frame = nullptr;
  if (!this->_is_level_matched(level)) {
    return frame;
  }

  if (this->_need_advertise) {
    for (int i = 0; i < this->_remote_active_cid_limit; ++i) {
      if (!this->_alt_quic_connection_ids_local[i].advertised) {
        // FIXME Should send a sequence number for retire_prior_to. Sending 0 for now.
        frame = QUICFrameFactory::create_new_connection_id_frame(buf, this->_alt_quic_connection_ids_local[i].seq_num, 0,
                                                                 this->_alt_quic_connection_ids_local[i].id,
                                                                 this->_alt_quic_connection_ids_local[i].token);

        if (frame && frame->size() > maximum_frame_size) {
          // Cancel generating frame
          frame = nullptr;
        } else if (frame != nullptr) {
          this->_records_new_connection_id_frame(level, static_cast<const QUICNewConnectionIdFrame &>(*frame));
          this->_alt_quic_connection_ids_local[i].advertised = true;
        }

        return frame;
      }
    }
    this->_need_advertise = false;
  }

  if (!this->_retired_seq_nums.empty()) {
    auto s = this->_retired_seq_nums.front();
    frame  = QUICFrameFactory::create_retire_connection_id_frame(buf, s);
    this->_records_retire_connection_id_frame(level, static_cast<const QUICRetireConnectionIdFrame &>(*frame));
    this->_retired_seq_nums.pop();
    return frame;
  }

  return frame;
}

void
QUICAltConnectionManager::_records_new_connection_id_frame(QUICEncryptionLevel level, const QUICNewConnectionIdFrame &frame)
{
  QUICFrameInformationUPtr info = QUICFrameInformationUPtr(quicFrameInformationAllocator.alloc());
  info->type                    = frame.type();
  info->level                   = level;

  AltConnectionInfo *frame_info = reinterpret_cast<AltConnectionInfo *>(info->data);
  frame_info->seq_num           = frame.sequence();
  frame_info->token             = frame.stateless_reset_token();
  frame_info->id                = frame.connection_id();
  this->_records_frame(frame.id(), std::move(info));
}

void
QUICAltConnectionManager::_records_retire_connection_id_frame(QUICEncryptionLevel level, const QUICRetireConnectionIdFrame &frame)
{
  QUICFrameInformationUPtr info            = QUICFrameInformationUPtr(quicFrameInformationAllocator.alloc());
  info->type                               = frame.type();
  info->level                              = level;
  *reinterpret_cast<int64_t *>(info->data) = frame.seq_num();
  this->_records_frame(frame.id(), std::move(info));
}

void
QUICAltConnectionManager::_on_frame_lost(QUICFrameInformationUPtr &info)
{
  switch (info->type) {
  case QUICFrameType::NEW_CONNECTION_ID: {
    AltConnectionInfo *frame_info = reinterpret_cast<AltConnectionInfo *>(info->data);
    for (int i = 0; i < this->_remote_active_cid_limit; ++i) {
      if (this->_alt_quic_connection_ids_local[i].seq_num == frame_info->seq_num) {
        ink_assert(this->_alt_quic_connection_ids_local[i].advertised);
        this->_alt_quic_connection_ids_local[i].advertised = false;
        this->_need_advertise                              = true;
        return;
      }
    }
    break;
  }
  case QUICFrameType::RETIRE_CONNECTION_ID: {
    this->_retired_seq_nums.push(*reinterpret_cast<int64_t *>(info->data));
    break;
  }
  default:
    ink_assert(0);
  }
}
