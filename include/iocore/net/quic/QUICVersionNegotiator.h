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
#include "QUICPacket.h"
#include "QUICTransportParameters.h"

/**
 * @brief Abstract QUIC Application Class
 * @detail Every quic application must inherits this class
 */
class QUICVersionNegotiator
{
public:
  QUICVersionNegotiationStatus status() const;
  QUICVersionNegotiationStatus negotiate(const QUICPacket &initial_packet);
  QUICVersionNegotiationStatus validate() const;
  QUICVersion negotiated_version() const;

private:
  QUICVersion _negotiated_version      = 0;
  QUICVersionNegotiationStatus _status = QUICVersionNegotiationStatus::NOT_NEGOTIATED;
};
