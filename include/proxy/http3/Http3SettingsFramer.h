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

#include "proxy/http3/Http3FrameGenerator.h"
#include "proxy/http3/Http3.h"

class Http3SettingsFramer : public Http3FrameGenerator
{
public:
  Http3SettingsFramer(NetVConnectionContext_t context) : _context(context){};

  // Http3FrameGenerator
  Http3FrameUPtr generate_frame() override;
  bool           is_done() const override;

private:
  NetVConnectionContext_t _context;
  bool                    _is_done = false; ///< Be careful when setting FIN flag on CONTROL stream. Maybe never?
  bool                    _is_sent = false; ///< Send SETTINGS frame only once
};
