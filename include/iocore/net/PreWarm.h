/** @file

  Pre-Warming NetVConnection

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#pragma once

// inknet
#include "iocore/net/SSLTypes.h"

// records
#include "records/RecHttp.h"

#include <netinet/in.h>

#include <memory>
#include <string>
#include <string_view>

namespace PreWarm
{
struct Dst {
  Dst(std::string_view h, in_port_t p, SNIRoutingType t, int a) : host(h), port(p), type(t), alpn_index(a) {}

  std::string    host;
  in_port_t      port       = 0;
  SNIRoutingType type       = SNIRoutingType::NONE;
  int            alpn_index = SessionProtocolNameRegistry::INVALID;
};

using SPtrConstDst = std::shared_ptr<const Dst>;
} // namespace PreWarm
