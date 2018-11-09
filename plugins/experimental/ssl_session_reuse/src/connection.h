/** @file

  Connection.h - connection class

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
#include <hiredis/hiredis.h>

/**
 * @brief The connection class, represent a connection to a Redis server
 */
class connection
{
public:
  /**
   * @brief Create and open a new connection
   * @param host hostname or ip of redis server, default localhost
   * @param port port of redis server, default: 6379
   * @param timeout time out in milli-seconds, default: 5
   * @return
   */
  inline static connection *
  create(const std::string &host = "localhost", const unsigned int port = 6379, const unsigned int timeout = 5)
  {
    return (new connection(host, port, timeout));
  }

  ~connection();

  bool is_valid() const;

  /**
   * @brief Returns raw ptr to hiredis library connection.
   * Use it with caution and pay attention on memory
   * management.
   * @return
   */
  inline redisContext *const
  c_ptr() const
  {
    return c;
  }

private:
  friend class connection_pool;
  connection(const std::string &host, const unsigned int port, const unsigned int timeout);
  redisContext *c;
};
