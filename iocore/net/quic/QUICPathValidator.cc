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

#include "QUICPathValidator.h"

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
  for (int i = countof(this->_outgoing_challenge) - 1; i >= 0; --i) {
    // TODO Randomize challenge data
    this->_outgoing_challenge[i] = i;
  }
  this->_has_outgoing_challenge = true;
}

void
QUICPathValidator::_generate_response(std::shared_ptr<const QUICPathChallengeFrame> frame)
{
  memcpy(this->_incoming_challenge, frame->data(), QUICPathChallengeFrame::DATA_LEN);
  this->_has_outgoing_response = true;
}

QUICErrorUPtr
QUICPathValidator::_validate_response(std::shared_ptr<const QUICPathResponseFrame> frame)
{
  QUICErrorUPtr error = QUICErrorUPtr(new QUICNoError());

  if (memcmp(this->_outgoing_challenge, frame->data(), QUICPathChallengeFrame::DATA_LEN) != 0) {
    error = QUICErrorUPtr(new QUICConnectionError(QUICTransErrorCode::UNSOLICITED_PATH_RESPONSE));
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

QUICErrorUPtr
QUICPathValidator::handle_frame(std::shared_ptr<const QUICFrame> frame)
{
  QUICErrorUPtr error = QUICErrorUPtr(new QUICNoError());

  switch (frame->type()) {
  case QUICFrameType::PATH_CHALLENGE:
    this->_generate_response(std::static_pointer_cast<const QUICPathChallengeFrame>(frame));
    break;
  case QUICFrameType::PATH_RESPONSE:
    error = this->_validate_response(std::static_pointer_cast<const QUICPathResponseFrame>(frame));
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
QUICPathValidator::will_generate_frame()
{
  return (this->_has_outgoing_challenge || this->_has_outgoing_response);
}

QUICFrameUPtr
QUICPathValidator::generate_frame(uint16_t connection_credit, uint16_t maximum_quic_packet_size)
{
  QUICFrameUPtr frame = QUICFrameFactory::create_null_frame();

  if (this->_has_outgoing_response) {
    frame                        = QUICFrameFactory::create_path_response_frame(this->_incoming_challenge);
    this->_has_outgoing_response = false;
  } else if (this->_has_outgoing_challenge) {
    frame                         = QUICFrameFactory::create_path_challenge_frame(this->_outgoing_challenge);
    this->_has_outgoing_challenge = false;
  }
  return frame;
}
