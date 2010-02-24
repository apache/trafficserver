/** @file

  A brief file description

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

#include "I_NetVConnection.h"

inline const struct sockaddr_in &
NetVConnection::get_remote_addr()
{
  if (!got_remote_addr) {
    set_remote_addr();
    got_remote_addr = 1;
  }
  return remote_addr;
}

inline unsigned int
NetVConnection::get_remote_ip()
{
  return (unsigned int) get_remote_addr().sin_addr.s_addr;
}


inline int
NetVConnection::get_remote_port()
{
  return ntohs(get_remote_addr().sin_port);
}

inline const struct sockaddr_in &
NetVConnection::get_local_addr()
{
  if (!got_local_addr) {
    set_local_addr();
    if (local_addr.sin_addr.s_addr || local_addr.sin_port) {
      got_local_addr = 1;
    }
  }
  return local_addr;
}

inline unsigned int
NetVConnection::get_local_ip()
{
  return (unsigned int) get_local_addr().sin_addr.s_addr;
}

inline int
NetVConnection::get_local_port()
{
  return ntohs(get_local_addr().sin_port);
}
