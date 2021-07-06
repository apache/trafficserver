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

#include <chrono>
#include "QUICPathValidator.h"
#include "QUICPacket.h"

#define QUICDebug(fmt, ...) Debug("quic_path", "[%s] " fmt, this->_cinfo.cids().data(), ##__VA_ARGS__)

bool
QUICPathValidator::ValidationJob::has_more_challenges() const
{
  return this->_has_outgoing_challenge;
}

const uint8_t *
QUICPathValidator::ValidationJob::get_next_challenge() const
{
  if (this->_has_outgoing_challenge) {
    return this->_outgoing_challenge + ((this->_has_outgoing_challenge - 1) * 8);
  } else {
    return nullptr;
  }
}

void
QUICPathValidator::ValidationJob::consume_challenge()
{
  --(this->_has_outgoing_challenge);
}

bool
QUICPathValidator::is_validating(const QUICPath &path) const
{
  if (auto j = this->_jobs.find(path); j != this->_jobs.end()) {
    return j->second.is_validating();
  } else {
    return false;
  }
}

bool
QUICPathValidator::is_validated(const QUICPath &path) const
{
  if (auto j = this->_jobs.find(path); j != this->_jobs.end()) {
    return j->second.is_validated();
  } else {
    return false;
  }
}

void
QUICPathValidator::validate(const QUICPath &path)
{
  if (this->_jobs.find(path) != this->_jobs.end()) {
    // Do nothing
  } else {
    auto result = this->_jobs.emplace(std::piecewise_construct, std::forward_as_tuple(path), std::forward_as_tuple());
    result.first->second.start();
    // QUICDebug("Validating a path between %s and %s", path.local_ep(), path.remote_ep());
  }
}

bool
QUICPathValidator::ValidationJob::is_validating() const
{
  return this->_state == ValidationState::VALIDATING;
}

bool
QUICPathValidator::ValidationJob::is_validated() const
{
  return this->_state == ValidationState::VALIDATED;
}

void
QUICPathValidator::ValidationJob::start()
{
  this->_state = ValidationState::VALIDATING;
  this->_generate_challenge();
}

void
QUICPathValidator::ValidationJob::_generate_challenge()
{
  size_t seed =
    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
  std::minstd_rand random(seed);

  for (auto &i : this->_outgoing_challenge) {
    i = random();
  }
  this->_has_outgoing_challenge = 3;
}

bool
QUICPathValidator::ValidationJob::validate_response(const uint8_t *data)
{
  for (int i = 0; i < 3; ++i) {
    if (memcmp(this->_outgoing_challenge + (QUICPathChallengeFrame::DATA_LEN * i), data, QUICPathChallengeFrame::DATA_LEN) == 0) {
      this->_state                  = ValidationState::VALIDATED;
      this->_has_outgoing_challenge = 0;
      return true;
    }
  }
  return false;
}

//
// QUICFrameHandler
//
std::vector<QUICFrameType>
QUICPathValidator::interests()
{
  return {QUICFrameType::PATH_CHALLENGE, QUICFrameType::PATH_RESPONSE};
}

QUICConnectionErrorUPtr
QUICPathValidator::handle_frame(QUICEncryptionLevel level, const QUICFrame &frame)
{
  QUICConnectionErrorUPtr error = nullptr;

  switch (frame.type()) {
  case QUICFrameType::PATH_CHALLENGE:
    this->_incoming_challenges.emplace(this->_incoming_challenges.begin(),
                                       static_cast<const QUICPathChallengeFrame &>(frame).data());
    break;
  case QUICFrameType::PATH_RESPONSE:
    if (auto item = this->_jobs.find({frame.packet()->to(), frame.packet()->from()}); item != this->_jobs.end()) {
      if (item->second.validate_response(static_cast<const QUICPathResponseFrame &>(frame).data())) {
        QUICDebug("validation succeeded");
        this->_on_validation_callback(true);
      } else {
        QUICDebug("validation failed");
        this->_on_validation_callback(false);
      }
    } else {
      error = std::make_unique<QUICConnectionError>(QUICTransErrorCode::PROTOCOL_VIOLATION);
    }
    break;
  default:
    ink_assert(!"Can't happen");
  }

  return error;
}

//
// QUICFrameGenerator
//
bool
QUICPathValidator::will_generate_frame(QUICEncryptionLevel level, size_t current_packet_size, bool ack_eliciting, uint32_t seq_num)
{
  if (!this->_is_level_matched(level)) {
    return false;
  }

  if (this->_latest_seq_num == seq_num) {
    return false;
  }

  // Check challenges
  for (auto &&item : this->_jobs) {
    auto &j = item.second;
    if (!j.is_validating() && !j.is_validated()) {
      j.start();
      return true;
    }
    if (j.has_more_challenges()) {
      return true;
    }
  }

  // Check responses
  return !this->_incoming_challenges.empty();
}

/**
 * @param connection_credit This is not used. Because PATH_CHALLENGE and PATH_RESPONSE frame are not flow-controlled
 */
QUICFrame *
QUICPathValidator::generate_frame(uint8_t *buf, QUICEncryptionLevel level, uint64_t /* connection_credit */,
                                  uint16_t maximum_frame_size, size_t current_packet_size, uint32_t seq_num)
{
  QUICFrame *frame = nullptr;

  if (!this->_is_level_matched(level)) {
    return frame;
  }

  if (!this->_incoming_challenges.empty()) {
    frame = QUICFrameFactory::create_path_response_frame(buf, this->_incoming_challenges.back());
    if (frame && frame->size() > maximum_frame_size) {
      // Cancel generating frame
      frame = nullptr;
    } else {
      this->_incoming_challenges.pop_back();
    }
  } else {
    for (auto &&item : this->_jobs) {
      auto &j = item.second;
      if (j.has_more_challenges()) {
        frame = QUICFrameFactory::create_path_challenge_frame(buf, j.get_next_challenge());
        if (frame && frame->size() > maximum_frame_size) {
          // Cancel generating frame
          frame = nullptr;
        } else {
          j.consume_challenge();
        }
        break;
      }
    }
  }

  this->_latest_seq_num = seq_num;

  return frame;
}
