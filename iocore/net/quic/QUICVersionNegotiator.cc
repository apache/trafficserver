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

#include "QUICVersionNegotiator.h"
#include "QUICTransportParameters.h"

QUICVersionNegotiationStatus
QUICVersionNegotiator::status()
{
  return this->_status;
}

QUICVersionNegotiationStatus
QUICVersionNegotiator::negotiate(const QUICPacket *initial_packet)
{
  if (this->_is_supported(initial_packet->version())) {
    this->_status             = QUICVersionNegotiationStatus::NEGOTIATED;
    this->_negotiated_version = initial_packet->version();
  }
  return this->_status;
}

QUICVersionNegotiationStatus
QUICVersionNegotiator::revalidate(const QUICTransportParametersInClientHello *tp)
{
  QUICVersion version = tp->negotiated_version();
  if (this->_negotiated_version == version) {
    if (tp->negotiated_version() != tp->initial_version()) {
      // FIXME Check initial_version
      /* If the initial version is different from the negotiated_version, a
       * stateless server MUST check that it would have sent a version
       * negotiation packet if it had received a packet with the indicated
       * initial_version. (Draft-04 7.3.4. Version Negotiation Validation)
       */
      this->_status             = QUICVersionNegotiationStatus::FAILED;
      this->_negotiated_version = 0;
    }
    this->_status = QUICVersionNegotiationStatus::REVALIDATED;
  } else {
    this->_status             = QUICVersionNegotiationStatus::FAILED;
    this->_negotiated_version = 0;
  }
  return this->_status;
}

QUICVersion
QUICVersionNegotiator::negotiated_version()
{
  return this->_negotiated_version;
}

bool
QUICVersionNegotiator::_is_supported(QUICVersion version)
{
  for (auto v : QUIC_SUPPORTED_VERSIONS) {
    if (v == version) {
      return true;
    }
  }
  return false;
}
