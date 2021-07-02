/** @file
 *
 *  Interface for providing transfer progress
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

#include "I_IOBuffer.h"
#include "QUICStreamAdapter.h"

void
QUICTransferProgressProviderSA::set_stream_adapter(QUICStreamAdapter *adapter)
{
  this->_adapter = adapter;
}

bool
QUICTransferProgressProviderSA::is_transfer_goal_set() const
{
  return this->transfer_goal() != INT64_MAX;
}

uint64_t
QUICTransferProgressProviderSA::transfer_progress() const
{
  return this->_adapter->read_len();
}

uint64_t
QUICTransferProgressProviderSA::transfer_goal() const
{
  return this->_adapter->total_len();
}

bool
QUICTransferProgressProviderSA::is_cancelled() const
{
  return false;
}

//
// QUICTransferProgressProviderVIO::
//

bool
QUICTransferProgressProviderVIO::is_transfer_goal_set() const
{
  return this->_vio.nbytes != INT64_MAX;
}

uint64_t
QUICTransferProgressProviderVIO::transfer_progress() const
{
  return this->_vio.ndone;
}

uint64_t
QUICTransferProgressProviderVIO::transfer_goal() const
{
  return this->_vio.nbytes;
}

bool
QUICTransferProgressProviderVIO::is_cancelled() const
{
  return false;
}
