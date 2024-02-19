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

#include "proxy/http3/Http3SettingsFramer.h"
#include "proxy/http3/Http3Config.h"

Http3FrameUPtr
Http3SettingsFramer::generate_frame()
{
  if (this->_is_sent) {
    return Http3FrameFactory::create_null_frame();
  }

  this->_is_sent = true;

  ts::Http3Config::scoped_config params;

  Http3SettingsFrame *frame = http3SettingsFrameAllocator.alloc();
  new (frame) Http3SettingsFrame();

  if (params->header_table_size() != HTTP3_DEFAULT_HEADER_TABLE_SIZE) {
    frame->set(Http3SettingsId::HEADER_TABLE_SIZE, params->header_table_size());
  }

  if (params->max_field_section_size() != HTTP3_DEFAULT_MAX_FIELD_SECTION_SIZE) {
    frame->set(Http3SettingsId::MAX_FIELD_SECTION_SIZE, params->max_field_section_size());
  }

  if (params->qpack_blocked_streams() != HTTP3_DEFAULT_QPACK_BLOCKED_STREAMS) {
    frame->set(Http3SettingsId::QPACK_BLOCKED_STREAMS, params->qpack_blocked_streams());
  }

  // Server side only
  if (this->_context == NET_VCONNECTION_IN) {
    if (params->num_placeholders() != HTTP3_DEFAULT_NUM_PLACEHOLDERS) {
      frame->set(Http3SettingsId::NUM_PLACEHOLDERS, params->num_placeholders());
    }
  }

  return Http3SettingsFrameUPtr(frame, &Http3FrameDeleter::delete_settings_frame);
}

bool
Http3SettingsFramer::is_done() const
{
  return this->_is_done;
}
