/**
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

/**
 * @file utils.h
 * @brief Contains utility functions available to users of the api.
 *
 */

#pragma once

#include <string>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstdint>

#ifndef ATSCPPAPI_UNUSED
#ifdef __GNUC__
#define ATSCPPAPI_UNUSED __attribute__((unused))
#else
#define ATSCPPAPI_UNUSED
#endif
#endif

namespace atscppapi
{
namespace utils
{
  /**
   * @brief Returns a pretty printed string of a sockaddr *
   *
   * @param sockaddr* A pointer to a sockaddr *
   * @return a string which is the pretty printed address
   */
  std::string getIpString(const sockaddr *);

  /**
   * @brief Returns just the port portion of the sockaddr *
   *
   * @param sockaddr* A pointer to a sockaddr *
   * @return a uint16_t which is the port from the sockaddr *
   */
  uint16_t getPort(const sockaddr *);

  /**
   * @brief Returns a pretty printed string of a sockaddr * including port
   *
   * @param sockaddr* A pointer to a sockaddr *
   * @return a string which is the pretty printed address including port
   */
  std::string getIpPortString(const sockaddr *);
} // namespace utils
} // namespace atscppapi
