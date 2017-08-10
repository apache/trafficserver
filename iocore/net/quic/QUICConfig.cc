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

#include "QUICConfig.h"

#include <records/I_RecHttp.h>

int QUICConfig::_config_id = 0;

//
// QUICConfigParams
//
void
QUICConfigParams::initialize()
{
  REC_EstablishStaticConfigInt32U(this->_no_activity_timeout_in, "proxy.config.quic.no_activity_timeout_in");
}

uint32_t
QUICConfigParams::no_activity_timeout_in() const
{
  return this->_no_activity_timeout_in;
}

//
// QUICConfig
//
void
QUICConfig::startup()
{
  reconfigure();
}

void
QUICConfig::reconfigure()
{
  QUICConfigParams *params;
  params = new QUICConfigParams;
  // re-read configuration
  params->initialize();
  _config_id = configProcessor.set(_config_id, params);
}

QUICConfigParams *
QUICConfig::acquire()
{
  return static_cast<QUICConfigParams *>(configProcessor.get(_config_id));
}

void
QUICConfig::release(QUICConfigParams *params)
{
  configProcessor.release(_config_id, params);
}
