/** @file
 *
 *  SETTINGS Frame Handler for Http3
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

#include "Http3SettingsHandler.h"

//
// SETTINGS frame handler
//
std::vector<Http3FrameType>
Http3SettingsHandler::interests()
{
  return {Http3FrameType::SETTINGS};
}

Http3ErrorUPtr
Http3SettingsHandler::handle_frame(std::shared_ptr<const Http3Frame> frame, int32_t /* frame_seq */, Http3StreamType /* s_type */)
{
  ink_assert(frame->type() == Http3FrameType::SETTINGS);

  const Http3SettingsFrame *settings_frame = dynamic_cast<const Http3SettingsFrame *>(frame.get());

  if (!settings_frame) {
    // make error
    return Http3ErrorUPtr(nullptr);
  }

  if (!settings_frame->is_valid()) {
    return settings_frame->get_error();
  }

  // TODO: Add length check: the maximum number of values are 2^62 - 1, but some fields have shorter maximum than it.
  if (settings_frame->contains(Http3SettingsId::HEADER_TABLE_SIZE)) {
    uint64_t header_table_size = settings_frame->get(Http3SettingsId::HEADER_TABLE_SIZE);
    this->_session->remote_qpack()->update_max_table_size(header_table_size);

    Debug("http3", "SETTINGS_HEADER_TABLE_SIZE: %" PRId64, header_table_size);
  }

  if (settings_frame->contains(Http3SettingsId::MAX_FIELD_SECTION_SIZE)) {
    uint64_t max_field_section_size = settings_frame->get(Http3SettingsId::MAX_FIELD_SECTION_SIZE);
    this->_session->remote_qpack()->update_max_field_section_size(max_field_section_size);

    Debug("http3", "SETTINGS_MAX_FIELD_SECTION_SIZE: %" PRId64, max_field_section_size);
  }

  if (settings_frame->contains(Http3SettingsId::QPACK_BLOCKED_STREAMS)) {
    uint64_t qpack_blocked_streams = settings_frame->get(Http3SettingsId::QPACK_BLOCKED_STREAMS);
    this->_session->remote_qpack()->update_max_blocking_streams(qpack_blocked_streams);

    Debug("http3", "SETTINGS_QPACK_BLOCKED_STREAMS: %" PRId64, qpack_blocked_streams);
  }

  if (settings_frame->contains(Http3SettingsId::NUM_PLACEHOLDERS)) {
    uint64_t num_placeholders = settings_frame->get(Http3SettingsId::NUM_PLACEHOLDERS);
    // TODO: update settings for priority tree

    Debug("http3", "SETTINGS_NUM_PLACEHOLDERS: %" PRId64, num_placeholders);
  }

  return Http3ErrorUPtr(nullptr);
}
