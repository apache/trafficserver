/** @file

  redis_endpoint.cc - represent Redis endpoints

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

#include "redis_endpoint.h"

redis_endpoint::redis_endpoint(const std::string &endpoint_spec)
{
  std::stringstream ss;
  size_t delim_pos(endpoint_spec.find(':'));
  m_hostname = endpoint_spec.substr(0, delim_pos);

  if (m_hostname.empty()) {
    m_hostname = cDefaultRedisHost;
  }

  if (delim_pos != std::string::npos) {
    ss << endpoint_spec.substr(delim_pos + 1);
    ss >> m_port;
  } else {
    m_port = cDefaultRedisPort;
  }
}

void
addto_endpoint_vector(std::vector<RedisEndpoint> &endpoints, const std::string &endpoint_str)
{
  char delim(',');
  size_t current_start_pos(0);
  size_t current_end_pos(0);
  std::string current_endpoint;

  while ((std::string::npos != current_end_pos) && (current_start_pos < endpoint_str.size())) {
    current_end_pos = endpoint_str.find(delim, current_start_pos);

    if (std::string::npos != current_end_pos) {
      current_endpoint = endpoint_str.substr(current_start_pos, current_end_pos - current_start_pos);
    } else {
      current_endpoint = endpoint_str.substr(current_start_pos);
    }

    endpoints.push_back(RedisEndpoint(current_endpoint));

    current_start_pos = current_end_pos + 1;
  }
}
