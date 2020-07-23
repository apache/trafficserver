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

#include "QUICTypes.h"

class QUICRetryIntegrityTag
{
public:
  static constexpr int LEN = 16;
  static bool compute(uint8_t *out, QUICConnectionId odcid, Ptr<IOBufferBlock> header, Ptr<IOBufferBlock> payload);

private:
  static constexpr uint8_t KEY_FOR_RETRY_INTEGRITY_TAG[]   = {0x4d, 0x32, 0xec, 0xdb, 0x2a, 0x21, 0x33, 0xc8,
                                                            0x41, 0xe4, 0x04, 0x3d, 0xf2, 0x7d, 0x44, 0x30};
  static constexpr uint8_t NONCE_FOR_RETRY_INTEGRITY_TAG[] = {0x4d, 0x16, 0x11, 0xd0, 0x55, 0x13,
                                                              0xa5, 0x52, 0xc5, 0x87, 0xd5, 0x75};
};
