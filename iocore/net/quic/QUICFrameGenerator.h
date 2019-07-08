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

#include "QUICFrame.h"
#include "QUICFrameRetransmitter.h"

class QUICFrameGenerator
{
public:
  virtual ~QUICFrameGenerator(){};
  virtual bool will_generate_frame(QUICEncryptionLevel level, size_t current_packet_size, bool ack_eliciting, uint32_t seq_num) = 0;

  /*
   * This function constructs an instance of QUICFrame on buf.
   * It returns a pointer for the frame if it succeeded, and returns nullptr if it failed.
   */
  virtual QUICFrame *generate_frame(uint8_t *buf, QUICEncryptionLevel level, uint64_t connection_credit,
                                    uint16_t maximum_frame_size, size_t current_packet_size, uint32_t seq_num) = 0;

  void on_frame_acked(QUICFrameId id);
  void on_frame_lost(QUICFrameId id);

protected:
  QUICFrameId
  _issue_frame_id()
  {
    return this->_latest_frame_Id++;
  }

  virtual void
  _on_frame_acked(QUICFrameInformationUPtr &info)
  {
  }

  virtual void
  _on_frame_lost(QUICFrameInformationUPtr &info)
  {
  }

  virtual bool _is_level_matched(QUICEncryptionLevel level);
  void _records_frame(QUICFrameId id, QUICFrameInformationUPtr info);

private:
  QUICFrameId _latest_frame_Id                 = 0;
  QUICEncryptionLevel _encryption_level_filter = QUICEncryptionLevel::ONE_RTT;
  std::map<QUICFrameId, QUICFrameInformationUPtr> _info;
};

// only generate one frame per loop
class QUICFrameOnceGenerator : public QUICFrameGenerator
{
public:
  bool
  will_generate_frame(QUICEncryptionLevel level, size_t current_packet_size, bool ack_eliciting, uint32_t seq_num) override
  {
    if (this->_seq_num == seq_num) {
      return false;
    }

    this->_seq_num = seq_num;
    return this->_will_generate_frame(level, current_packet_size, ack_eliciting);
  }

  QUICFrame *
  generate_frame(uint8_t *buf, QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size,
                 size_t current_packet_size, uint32_t seq_num) override
  {
    this->_seq_num = seq_num;
    return this->_generate_frame(buf, level, connection_credit, maximum_frame_size, current_packet_size);
  }

protected:
  virtual bool _will_generate_frame(QUICEncryptionLevel level, size_t current_packet_size, bool ack_eliciting) = 0;
  virtual QUICFrame *_generate_frame(uint8_t *buf, QUICEncryptionLevel level, uint64_t connection_credit,
                                     uint16_t maximum_frame_size, size_t current_packet_size)                  = 0;

private:
  uint32_t _seq_num = UINT32_MAX;
};

enum QUICFrameGeneratorWeight {
  EARLY       = 100,
  BEFORE_DATA = 200,
  AFTER_DATA  = 300,
  LATE        = 400,
};

class QUICFrameGeneratorManager
{
public:
  void add_generator(QUICFrameGenerator &generator, int weight);
  const std::vector<QUICFrameGenerator *> &generators();

private:
  using QUICActiveFrameGenerator = std::pair<int, QUICFrameGenerator *>;

  std::vector<QUICFrameGenerator *> _generators;
  std::vector<QUICActiveFrameGenerator> _inline_vector;
};
