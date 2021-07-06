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

#include <vector>
#include <unordered_map>
#include "QUICTypes.h"
#include "QUICFrameHandler.h"
#include "QUICFrameGenerator.h"
#include "QUICConnection.h"

class QUICPathValidator : public QUICFrameHandler, public QUICFrameGenerator
{
public:
  QUICPathValidator(const QUICConnectionInfoProvider &info, std::function<void(bool)> callback)
    : _cinfo(info), _on_validation_callback(callback)
  {
  }
  bool is_validating(const QUICPath &path) const;
  bool is_validated(const QUICPath &path) const;
  void validate(const QUICPath &path);

  // QUICFrameHandler
  std::vector<QUICFrameType> interests() override;
  QUICConnectionErrorUPtr handle_frame(QUICEncryptionLevel level, const QUICFrame &frame) override;

  // QUICFrameGenerator
  bool will_generate_frame(QUICEncryptionLevel level, size_t current_packet_size, bool ack_eliciting, uint32_t seq_num) override;
  QUICFrame *generate_frame(uint8_t *buf, QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size,
                            size_t current_packet_size, uint32_t seq_num) override;

private:
  enum class ValidationState : int {
    NOT_VALIDATED,
    VALIDATING,
    VALIDATED,
  };

  class ValidationJob
  {
  public:
    ValidationJob(){};
    ~ValidationJob(){};
    bool is_validating() const;
    bool is_validated() const;
    void start();
    bool validate_response(const uint8_t *data);
    bool has_more_challenges() const;
    const uint8_t *get_next_challenge() const;
    void consume_challenge();

  private:
    ValidationState _state = ValidationState::NOT_VALIDATED;
    uint8_t _outgoing_challenge[QUICPathChallengeFrame::DATA_LEN * 3];
    int _has_outgoing_challenge = 0;

    void _generate_challenge();
  };

  const QUICConnectionInfoProvider &_cinfo;
  std::unordered_map<QUICPath, ValidationJob, QUICPathHasher> _jobs;

  std::function<void(bool)> _on_validation_callback;

  uint32_t _latest_seq_num = 0;
  std::vector<QUICPathValidationData> _incoming_challenges;
};
