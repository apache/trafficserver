/** @file

  simple_pool.h

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

#include "connection.h"
#include <set>
#include <mutex>

/**
 * @brief Manages a pool of connections to a single Redis server
 */
class simple_pool
{
public:
  static inline simple_pool *
  create(const std::string &host = "localhost", unsigned int port = 6379, unsigned int timeout = 5)
  {
    return (new simple_pool(host, port, timeout));
  }

  /**
   * @brief Get a working connection
   * @return
   */
  connection *get();

  /**
   * @brief Put back a connection for reuse
   * @param conn
   */
  void put(connection *conn);

private:
  simple_pool(const std::string &host, unsigned int port, unsigned int timeout);

  std::string _host;
  unsigned int _port;
  unsigned int _timeout;
  std::set<connection *> connections;
  std::mutex access_mutex;
};
