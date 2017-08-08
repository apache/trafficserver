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

QUICVersionNegotiator::QUICVersionNegotiator(QUICPacketFactory *packet_factory, QUICPacketTransmitter *tx)
  : _packet_factory(packet_factory), _tx(tx){};

QUICVersionNegotiationStatus
QUICVersionNegotiator::status()
{
  return this->_status;
}

QUICVersionNegotiationStatus
QUICVersionNegotiator::negotiate(const QUICPacket *initial_packet)
{
  if (this->_is_supported(initial_packet->version())) {
    this->_status = QUICVersionNegotiationStatus::NEGOTIATED;
  } else {
    this->_tx->transmit_packet(this->_packet_factory->create_version_negotiation_packet(initial_packet));
  }
  return this->_status;
}

QUICVersionNegotiationStatus
QUICVersionNegotiator::revalidate(QUICVersion version)
{
  // TDOO revalidate the version
  this->_status = QUICVersionNegotiationStatus::FAILED;
  return _status;
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
