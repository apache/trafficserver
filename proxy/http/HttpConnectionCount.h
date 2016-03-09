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

//
#include "ts/ink_platform.h"
#include "ts/ink_inet.h"
#include "ts/ink_mutex.h"
#include "ts/Map.h"

#ifndef _HTTP_CONNECTION_COUNT_H_
#define _HTTP_CONNECTION_COUNT_H_

/**
 * Singleton class to keep track of the number of connections per host
 */
class ConnectionCount
{
public:
  /**
   * Static method to get the instance of the class
   * @return Returns a pointer to the instance of the class
   */
  static ConnectionCount *
  getInstance()
  {
    return &_connectionCount;
  }

  /**
   * Gets the number of connections for the host
   * @param ip IP address of the host
   * @return Number of connections
   */
  int
  getCount(const IpEndpoint &addr)
  {
    ink_mutex_acquire(&_mutex);
    int count = _hostCount.get(ConnAddr(addr));
    ink_mutex_release(&_mutex);
    return count;
  }

  /**
   * Change (increment/decrement) the connection count
   * @param ip IP address of the host
   * @param delta Default is +1, can be set to negative to decrement
   */
  void
  incrementCount(const IpEndpoint &addr, const int delta = 1)
  {
    ConnAddr caddr(addr);
    ink_mutex_acquire(&_mutex);
    int count = _hostCount.get(caddr);
    _hostCount.put(caddr, count + delta);
    ink_mutex_release(&_mutex);
  }

  struct ConnAddr {
    IpEndpoint _addr;

    ConnAddr() { ink_zero(_addr); }
    ConnAddr(int x)
    {
      ink_release_assert(x == 0);
      ink_zero(_addr);
    }
    ConnAddr(const IpEndpoint &addr) : _addr(addr) {}
    operator bool() { return ats_is_ip(&_addr); }
  };

  class ConnAddrHashFns
  {
  public:
    static uintptr_t
    hash(ConnAddr &addr)
    {
      return (uintptr_t)ats_ip_hash(&addr._addr.sa);
    }
    static int
    equal(ConnAddr &a, ConnAddr &b)
    {
      return ats_ip_addr_eq(&a._addr, &b._addr);
    }
  };

private:
  // Hide the constructor and copy constructor
  ConnectionCount() { ink_mutex_init(&_mutex, "ConnectionCountMutex"); }
  ConnectionCount(const ConnectionCount & /* x ATS_UNUSED */) {}

  static ConnectionCount _connectionCount;
  HashMap<ConnAddr, ConnAddrHashFns, int> _hostCount;
  ink_mutex _mutex;
};

#endif
