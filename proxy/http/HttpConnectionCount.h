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
#include "ts/Diags.h"
#include "ts/INK_MD5.h"
#include "ts/ink_config.h"
#include "HttpProxyAPIEnums.h"

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
  getCount(const IpEndpoint &addr, const INK_MD5 &hostname_hash, TSServerSessionSharingMatchType match_type)
  {
    if (TS_SERVER_SESSION_SHARING_MATCH_NONE == match_type) {
      return 0; // We can never match a node if match type is NONE
    }

    ink_mutex_acquire(&_mutex);
    int count = _hostCount.get(ConnAddr(addr, hostname_hash, match_type));
    ink_mutex_release(&_mutex);
    return count;
  }

  /**
   * Change (increment/decrement) the connection count
   * @param ip IP address of the host
   * @param delta Default is +1, can be set to negative to decrement
   */
  void
  incrementCount(const IpEndpoint &addr, const INK_MD5 &hostname_hash, TSServerSessionSharingMatchType match_type,
                 const int delta = 1)
  {
    if (TS_SERVER_SESSION_SHARING_MATCH_NONE == match_type) {
      return; // We can never match a node if match type is NONE.
    }

    ConnAddr caddr(addr, hostname_hash, match_type);
    ink_mutex_acquire(&_mutex);
    int count = _hostCount.get(caddr);
    _hostCount.put(caddr, count + delta);
    ink_mutex_release(&_mutex);
  }

  struct ConnAddr {
    IpEndpoint _addr;
    INK_MD5 _hostname_hash;
    TSServerSessionSharingMatchType _match_type;

    ConnAddr() : _match_type(TS_SERVER_SESSION_SHARING_MATCH_NONE)
    {
      ink_zero(_addr);
      ink_zero(_hostname_hash);
    }

    ConnAddr(int x) : _match_type(TS_SERVER_SESSION_SHARING_MATCH_NONE)
    {
      ink_release_assert(x == 0);
      ink_zero(_addr);
      ink_zero(_hostname_hash);
    }

    ConnAddr(const IpEndpoint &addr, const INK_MD5 &hostname_hash, TSServerSessionSharingMatchType match_type)
      : _addr(addr), _hostname_hash(hostname_hash), _match_type(match_type)
    {
    }

    ConnAddr(const IpEndpoint &addr, const char *hostname, TSServerSessionSharingMatchType match_type)
      : _addr(addr), _match_type(match_type)
    {
      MD5Context md5_ctx;
      md5_ctx.hash_immediate(_hostname_hash, static_cast<const void *>(hostname), strlen(hostname));
    }

    operator bool() { return ats_is_ip(&_addr); }
  };

  class ConnAddrHashFns
  {
  public:
    static uintptr_t
    hash(ConnAddr &addr)
    {
      if (addr._match_type == TS_SERVER_SESSION_SHARING_MATCH_IP) {
        return (uintptr_t)ats_ip_port_hash(&addr._addr.sa);
      } else if (addr._match_type == TS_SERVER_SESSION_SHARING_MATCH_HOST) {
        return (uintptr_t)addr._hostname_hash.u64[0];
      } else if (addr._match_type == TS_SERVER_SESSION_SHARING_MATCH_BOTH) {
        return ((uintptr_t)ats_ip_port_hash(&addr._addr.sa) ^ (uintptr_t)addr._hostname_hash.u64[0]);
      } else {
        return 0; // they will never be equal() because of it returns false for NONE matches.
      }
    }

    static int
    equal(ConnAddr &a, ConnAddr &b)
    {
      char addrbuf1[INET6_ADDRSTRLEN];
      char addrbuf2[INET6_ADDRSTRLEN];
      char md5buf1[33];
      char md5buf2[33];
      ink_code_to_hex_str(md5buf1, a._hostname_hash.u8);
      ink_code_to_hex_str(md5buf2, b._hostname_hash.u8);
      Debug("conn_count", "Comparing hostname hash %s dest %s match method %d to hostname hash %s dest %s match method %d", md5buf1,
            ats_ip_nptop(&a._addr.sa, addrbuf1, sizeof(addrbuf1)), a._match_type, md5buf2,
            ats_ip_nptop(&b._addr.sa, addrbuf2, sizeof(addrbuf2)), b._match_type);

      if (a._match_type != b._match_type || a._match_type == TS_SERVER_SESSION_SHARING_MATCH_NONE) {
        Debug("conn_count", "result = 0, a._match_type != b._match_type || a._match_type == TS_SERVER_SESSION_SHARING_MATCH_NONE");
        return 0;
      }

      if (a._match_type == TS_SERVER_SESSION_SHARING_MATCH_IP) {
        if (ats_ip_addr_port_eq(&a._addr.sa, &b._addr.sa)) {
          Debug("conn_count", "result = 1, a._match_type == TS_SERVER_SESSION_SHARING_MATCH_IP");
          return 1;
        } else {
          Debug("conn_count", "result = 0, a._match_type == TS_SERVER_SESSION_SHARING_MATCH_IP");
          return 0;
        }
      }

      if (a._match_type == TS_SERVER_SESSION_SHARING_MATCH_HOST) {
        if ((a._hostname_hash.u64[0] == b._hostname_hash.u64[0] && a._hostname_hash.u64[1] == b._hostname_hash.u64[1])) {
          Debug("conn_count", "result = 1, a._match_type == TS_SERVER_SESSION_SHARING_MATCH_HOST");
          return 1;
        } else {
          Debug("conn_count", "result = 0, a._match_type == TS_SERVER_SESSION_SHARING_MATCH_HOST");
          return 0;
        }
      }

      if (a._match_type == TS_SERVER_SESSION_SHARING_MATCH_BOTH) {
        if ((ats_ip_addr_port_eq(&a._addr.sa, &b._addr.sa)) &&
            (a._hostname_hash.u64[0] == b._hostname_hash.u64[0] && a._hostname_hash.u64[1] == b._hostname_hash.u64[1])) {
          Debug("conn_count", "result = 1, a._match_type == TS_SERVER_SESSION_SHARING_MATCH_BOTH");

          return 1;
        }
      }

      Debug("conn_count", "result = 0, a._match_type == TS_SERVER_SESSION_SHARING_MATCH_BOTH");
      return 0;
    }
  };

protected:
  // Hide the constructor and copy constructor
  ConnectionCount() { ink_mutex_init(&_mutex, "ConnectionCountMutex"); }
  ConnectionCount(const ConnectionCount & /* x ATS_UNUSED */) {}
  static ConnectionCount _connectionCount;
  HashMap<ConnAddr, ConnAddrHashFns, int> _hostCount;
  ink_mutex _mutex;
};

class ConnectionCountQueue : public ConnectionCount
{
public:
  /**
   * Static method to get the instance of the class
   * @return Returns a pointer to the instance of the class
   */
  static ConnectionCountQueue *
  getInstance()
  {
    return &_connectionCount;
  }

private:
  static ConnectionCountQueue _connectionCount;
};

#endif
