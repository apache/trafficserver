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

#include "QUICFrameGenerator.h"
#include "QUICTypes.h"
#include "QUICFrame.h"
#include <list>
#include <set>

class QUICConnection;

class QUICAckFrameManager : public QUICFrameGenerator
{
public:
  class QUICAckFrameCreator
  {
  public:
    struct RecvdPacket {
      bool ack_only                  = false;
      QUICPacketNumber packet_number = 0;
    };
    QUICAckFrameCreator(QUICEncryptionLevel level, QUICAckFrameManager *ack_manager);
    ~QUICAckFrameCreator();

    void push_back(QUICPacketNumber packet_number, size_t size, bool ack_only);
    size_t size();
    void clear();
    void sort();
    void forget(QUICPacketNumber largest_acknowledged);
    bool available() const;
    bool is_ack_frame_ready() const;
    std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc> create_ack_frame();

    // Checks maximum_frame_size and return _ack_frame
    std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc> generate_ack_frame(uint16_t maximum_frame_size);

    // timer event handler, refresh _ack_frame;
    void refresh_frame();

    QUICPacketNumber largest_ack_number();
    ink_hrtime largest_ack_received_time();

  private:
    uint64_t _calculate_delay();
    std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc> _create_ack_frame();

    std::list<RecvdPacket> _packet_numbers;
    bool _available                         = false;
    bool _should_send                       = false;
    size_t _size_unsend                     = 0;
    QUICPacketNumber _largest_ack_number    = 0;
    QUICPacketNumber _expect_next           = 0;
    ink_hrtime _largest_ack_received_time   = 0;
    ink_hrtime _latest_packet_received_time = 0;

    std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc> _ack_frame = QUICFrameFactory::create_null_ack_frame();
    QUICAckFrameManager *_ack_manager                              = nullptr;

    QUICEncryptionLevel _level = QUICEncryptionLevel::NONE;
  };

  static constexpr int MAXIMUM_PACKET_COUNT = 256;

  QUICAckFrameManager();
  ~QUICAckFrameManager();

  void set_ack_delay_exponent(uint8_t ack_delay_exponent);
  void set_force_to_send(bool on = true);
  bool force_to_send() const;

  /*
   * All packet numbers ATS received need to be passed to this method.
   * Returns 0 if updated successfully.
   */
  int update(QUICEncryptionLevel level, QUICPacketNumber packet_number, size_t size, bool akc_only);

  /*
   * Returns true only if should send ack.
   */
  bool will_generate_frame(QUICEncryptionLevel level) override;

  /*
   * Calls create directly.
   */
  QUICFrameUPtr generate_frame(QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size) override;

  int timer_fired();
  QUICFrameId issue_frame_id();
  uint8_t ack_delay_exponent() const;

private:
  struct AckFrameInfomation {
    QUICPacketNumber largest_acknowledged = 0;
  };

  virtual void _on_frame_acked(QUICFrameInformation info) override;

  /*
   * Returns QUICAckFrame only if ACK frame is able to be sent.
   * Caller must send the ACK frame to the peer if it was returned.
   */
  std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc> _create_ack_frame(QUICEncryptionLevel level);
  uint64_t _calculate_delay(QUICEncryptionLevel level);
  std::vector<QUICEncryptionLevel>
  _encryption_level_filter() override
  {
    return {
      QUICEncryptionLevel::INITIAL,
      QUICEncryptionLevel::ZERO_RTT,
      QUICEncryptionLevel::HANDSHAKE,
      QUICEncryptionLevel::ONE_RTT,
    };
  }

  bool _available[4]   = {false};
  bool _should_send[4] = {false};

  // Initial, 0/1-RTT, and Handshake
  std::unique_ptr<QUICAckFrameCreator> _ack_creator[3];

  bool _force_to_send         = false;
  uint8_t _ack_delay_exponent = 0;
};
