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

#include "Http3ProtocolEnforcer.h"
#include "Http3DebugNames.h"

std::vector<Http3FrameType>
Http3ProtocolEnforcer::interests()
{
  return {Http3FrameType::DATA,          Http3FrameType::HEADERS,     Http3FrameType::PRIORITY,
          Http3FrameType::CANCEL_PUSH,   Http3FrameType::SETTINGS,    Http3FrameType::PUSH_PROMISE,
          Http3FrameType::X_RESERVED_1,  Http3FrameType::GOAWAY,      Http3FrameType::X_RESERVED_2,
          Http3FrameType::X_RESERVED_3,  Http3FrameType::MAX_PUSH_ID, Http3FrameType::DUPLICATE_PUSH_ID,
          Http3FrameType::X_MAX_DEFINED, Http3FrameType::UNKNOWN};
}

Http3ErrorUPtr
Http3ProtocolEnforcer::handle_frame(std::shared_ptr<const Http3Frame> frame, int32_t frame_seq, Http3StreamType s_type)
{
  Http3ErrorUPtr error  = Http3ErrorUPtr(nullptr);
  Http3FrameType f_type = frame->type();
  if (s_type == Http3StreamType::CONTROL) {
    if (frame_seq == 0 && f_type != Http3FrameType::SETTINGS) {
      error = std::make_unique<Http3Error>(Http3ErrorClass::CONNECTION, Http3ErrorCode::H3_MISSING_SETTINGS,
                                           "first frame of the control stream must be SETTINGS frame");
    } else if (frame_seq != 0 && f_type == Http3FrameType::SETTINGS) {
      error = std::make_unique<Http3Error>(Http3ErrorClass::CONNECTION, Http3ErrorCode::H3_FRAME_UNEXPECTED,
                                           "only one SETTINGS frame is allowed per the control stream");
    } else if (f_type == Http3FrameType::DATA || f_type == Http3FrameType::HEADERS || f_type == Http3FrameType::X_RESERVED_1 ||
               f_type == Http3FrameType::X_RESERVED_2 || f_type == Http3FrameType::X_RESERVED_3) {
      std::string error_msg = Http3DebugNames::frame_type(f_type);
      error_msg.append(" frame is not allowed on control stream");
      error = std::make_unique<Http3Error>(Http3ErrorClass::CONNECTION, Http3ErrorCode::H3_FRAME_UNEXPECTED, error_msg.c_str());
    }
  } else {
    if (f_type == Http3FrameType::X_RESERVED_1 || f_type == Http3FrameType::X_RESERVED_2 ||
        f_type == Http3FrameType::X_RESERVED_3) {
      std::string error_msg = Http3DebugNames::frame_type(f_type);
      error_msg.append(" frame is not allowed on any stream");
      error = std::make_unique<Http3Error>(Http3ErrorClass::CONNECTION, Http3ErrorCode::H3_FRAME_UNEXPECTED, error_msg.c_str());
    }
  }

  return error;
}
