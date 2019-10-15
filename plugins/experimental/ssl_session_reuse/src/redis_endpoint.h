/** @file

  redis_endpoint.h

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

#include <string>
#include <vector>

#include "globals.h"

typedef struct redis_endpoint {
  std::string m_hostname;
  int m_port;

  redis_endpoint() : m_hostname(cDefaultRedisHost), m_port(cDefaultRedisPort) {}
  redis_endpoint(const std::string &endpoint_spec);

} RedisEndpoint;

typedef struct redis_endpoint_compare {
  bool
  operator()(const RedisEndpoint &lhs, const RedisEndpoint &rhs) const
  {
    return lhs.m_hostname < rhs.m_hostname || (lhs.m_hostname == rhs.m_hostname && lhs.m_port < rhs.m_port);
  }

} RedisEndpointCompare;

void addto_endpoint_vector(std::vector<RedisEndpoint> &endpoints, const std::string &endpoint_str);
