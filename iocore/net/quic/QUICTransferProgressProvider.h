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

#include "I_VIO.h"

#pragma once

class QUICTransferProgressProvider
{
public:
  virtual bool is_transfer_goal_set() const  = 0;
  virtual uint64_t transfer_progress() const = 0;
  virtual uint64_t transfer_goal() const     = 0;
  virtual bool is_cancelled() const          = 0;

  virtual bool
  is_transfer_complete() const
  {
    return this->transfer_progress() == this->transfer_goal();
  }
};

class QUICTransferProgressProviderVIO : public QUICTransferProgressProvider
{
public:
  QUICTransferProgressProviderVIO(VIO &vio) : _vio(vio) {}

  bool
  is_transfer_goal_set() const
  {
    return this->_vio.nbytes != INT64_MAX;
  }

  uint64_t
  transfer_progress() const
  {
    return this->_vio.ndone;
  }

  uint64_t
  transfer_goal() const
  {
    return this->_vio.nbytes;
  }

  bool
  is_cancelled() const
  {
    return false;
  }

private:
  VIO &_vio;
};
