/** @file
 *
 *  HTTP/3 Config
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

#include "Http3Config.h"

int Http3Config::_config_id = 0;

//
// Http3ConfigParams
//
void
Http3ConfigParams::initialize()
{
  REC_EstablishStaticConfigInt32U(this->_header_table_size, "proxy.config.http3.header_table_size");
  REC_EstablishStaticConfigInt32U(this->_max_header_list_size, "proxy.config.http3.max_header_list_size");
  REC_EstablishStaticConfigInt32U(this->_qpack_blocked_streams, "proxy.config.http3.qpack_blocked_streams");
  REC_EstablishStaticConfigInt32U(this->_num_placeholders, "proxy.config.http3.num_placeholders");
  REC_EstablishStaticConfigInt32U(this->_max_settings, "proxy.config.http3.max_settings");
}

uint32_t
Http3ConfigParams::header_table_size() const
{
  return this->_header_table_size;
}

uint32_t
Http3ConfigParams::max_header_list_size() const
{
  return this->_max_header_list_size;
}

uint32_t
Http3ConfigParams::qpack_blocked_streams() const
{
  return this->_qpack_blocked_streams;
}

uint32_t
Http3ConfigParams::num_placeholders() const
{
  return this->_num_placeholders;
}

uint32_t
Http3ConfigParams::max_settings() const
{
  return this->_max_settings;
}

//
// Http3Config
//
void
Http3Config::startup()
{
  reconfigure();
}

void
Http3Config::reconfigure()
{
  Http3ConfigParams *params;
  params = new Http3ConfigParams;
  // re-read configuration
  params->initialize();
  Http3Config::_config_id = configProcessor.set(Http3Config::_config_id, params);
}

Http3ConfigParams *
Http3Config::acquire()
{
  return static_cast<Http3ConfigParams *>(configProcessor.get(Http3Config::_config_id));
}

void
Http3Config::release(Http3ConfigParams *params)
{
  configProcessor.release(Http3Config::_config_id, params);
}
