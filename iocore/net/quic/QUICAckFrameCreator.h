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

#include "../../eventsystem/I_EventSystem.h"
#include "../../eventsystem/I_Action.h"
#include "tscore/ink_hrtime.h"
#include "QUICFrameGenerator.h"
#include "QUICTypes.h"
#include "QUICFrame.h"
#include <list>
#include <set>

#define QUIC_ACK_CREATOR_MAX_DELAY 25 * HRTIME_MSECOND

class QUICConnection;

class QUICAckFrameCreator : public QUICFrameGenerator
{
public:
  class QUICAckPacketNumbers : public Continuation
  {
  public:
    struct RecvdPacket {
      bool ack_only                  = false;
      QUICPacketNumber packet_number = 0;
    };
    QUICAckPacketNumbers(QUICConnection *qc, QUICAckFrameCreator *ack_creator);
    ~QUICAckPacketNumbers();

    void set_creator(QUICAckFrameCreator *ack_creator);
    void push_back(QUICEncryptionLevel level, QUICPacketNumber packet_number, size_t size, bool ack_only);
    size_t size();
    void clear();
    void sort();
    void forget(QUICPacketNumber largest_acknowledged);
    bool available() const;
    bool should_send() const;
    std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc> create_ack_frame(QUICEncryptionLevel level, uint16_t maximum_frame_size);

    QUICPacketNumber largest_ack_number();
    ink_hrtime largest_ack_received_time();
    int timer_fired(int event, Event *edata);

  private:
    void _cancel_timer();
    void _start_timer();

    uint64_t _calculate_delay(QUICEncryptionLevel level);
    std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc> _create_ack_frame(QUICEncryptionLevel level);

    std::list<RecvdPacket> _packet_numbers;
    Event *_event                         = nullptr;
    bool _available                       = false;
    bool _should_send                     = false;
    size_t _size_unsend                   = 0;
    QUICPacketNumber _largest_ack_number  = 0;
    QUICPacketNumber _expect_next         = 0;
    ink_hrtime _largest_ack_received_time = 0;

    QUICConnection *_qc               = nullptr;
    QUICAckFrameCreator *_ack_creator = nullptr;
  };

  static constexpr int MAXIMUM_PACKET_COUNT = 256;

  QUICAckFrameCreator(QUICConnection *qc);

  void set_ack_delay_exponent(uint8_t ack_delay_exponent);

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

  QUICFrameId issue_frame_id();
  uint8_t ack_delay_exponent() const;

private:
  struct AckFrameInfomation {
    QUICPacketNumber largest_acknowledged = 0;
  };

  Event *_event = nullptr;

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
  std::unique_ptr<QUICAckPacketNumbers> _packet_numbers[3];

  uint8_t _ack_delay_exponent = 0;

  ink_hrtime _latest_sent_time = 0;
};
