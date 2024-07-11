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

/****************************************************************************

  SocketManager.cc
 ****************************************************************************/

#include "iocore/eventsystem/UnixSocket.h"

#include "tscore/ink_platform.h"
#include "P_EventSystem.h"

#include "tscore/TextBuffer.h"

int
SocketManager::accept4(int s, struct sockaddr *addr, socklen_t *addrlen, int flags)
{
  UnixSocket sock{s};
  return sock.accept4(addr, addrlen, flags);
}

int
SocketManager::ink_bind(int s, struct sockaddr const *name, int namelen, short /* Proto ATS_UNUSED */)
{
  UnixSocket sock{s};
  return sock.bind(name, namelen);
}

int
SocketManager::close(int s)
{
  UnixSocket sock{s};
  return sock.close();
}

bool
SocketManager::fastopen_supported()
{
  return UnixSocket::client_fastopen_supported();
}
