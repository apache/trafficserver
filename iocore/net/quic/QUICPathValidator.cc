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

bool
QUICPathValidator::is_validating()
{
  return this->_state == ValidationState::VALIDATING;
}

bool
QUICPathValidator::is_validated()
{
  return this->_state == ValidationState::VALIDATED;
}

void
QUICPathValidator::validate()
{
  if (this->_state == ValidationState::VALIDATING) {
    // Do nothing
  } else {
    this->_state = ValidationState::VALIDATING;
    this->_generate_challenge();
  }
}

void
QUICPathValidator::_generate_challenge()
{
  size_t seed =
    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
  std::minstd_rand random(seed);

  for (auto &i : this->_outgoing_challenge) {
    i = random();
  }
  this->_has_outgoing_challenge = 3;
}

void
QUICPathValidator::_generate_response(const QUICPathChallengeFrame &frame)
{
  memcpy(this->_incoming_challenge, frame.data(), QUICPathChallengeFrame::DATA_LEN);
  this->_has_outgoing_response = true;
}

QUICConnectionErrorUPtr
QUICPathValidator::_validate_response(const QUICPathResponseFrame &frame)
{
  QUICConnectionErrorUPtr error = std::make_unique<QUICConnectionError>(QUICTransErrorCode::PROTOCOL_VIOLATION);

  for (int i = 0; i < 3; ++i) {
    if (memcmp(this->_outgoing_challenge + (QUICPathChallengeFrame::DATA_LEN * i), frame.data(),
               QUICPathChallengeFrame::DATA_LEN) == 0) {
      this->_state                  = ValidationState::VALIDATED;
      this->_has_outgoing_challenge = 0;
      error                         = nullptr;
      break;
    }
  }

  return error;
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
    this->_generate_response(static_cast<const QUICPathChallengeFrame &>(frame));
    break;
  case QUICFrameType::PATH_RESPONSE:
    error = this->_validate_response(static_cast<const QUICPathResponseFrame &>(frame));
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
QUICPathValidator::will_generate_frame(QUICEncryptionLevel level, ink_hrtime timestamp)
{
  if (!this->_is_level_matched(level)) {
    return false;
  }

  if (this->_last_sent_at == timestamp) {
    return false;
  }

  return (this->_has_outgoing_challenge || this->_has_outgoing_response);
}

/**
 * @param connection_credit This is not used. Because PATH_CHALLENGE and PATH_RESPONSE frame are not flow-controlled
 */
QUICFrame *
QUICPathValidator::generate_frame(uint8_t *buf, QUICEncryptionLevel level, uint64_t /* connection_credit */,
                                  uint16_t maximum_frame_size, ink_hrtime timestamp)
{
  QUICFrame *frame = nullptr;

  if (!this->_is_level_matched(level)) {
    return frame;
  }

  if (this->_has_outgoing_response) {
    frame = QUICFrameFactory::create_path_response_frame(buf, this->_incoming_challenge);
    if (frame && frame->size() > maximum_frame_size) {
      // Cancel generating frame
      frame = nullptr;
    } else {
      this->_has_outgoing_response = false;
    }
  } else if (this->_has_outgoing_challenge) {
    frame = QUICFrameFactory::create_path_challenge_frame(
      buf, this->_outgoing_challenge + (QUICPathChallengeFrame::DATA_LEN * (this->_has_outgoing_challenge - 1)));
    if (frame && frame->size() > maximum_frame_size) {
      // Cancel generating frame
      frame = nullptr;
    } else {
      --this->_has_outgoing_challenge;
      ink_assert(this->_has_outgoing_challenge >= 0);
    }
  }

  this->_last_sent_at = timestamp;

  return frame;
}
