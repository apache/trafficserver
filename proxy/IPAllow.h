/** @file

  Access control by IP address and HTTP method.

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

/*****************************************************************************
 *
 *  IPAllow.h - Interface to IP Access Control systtem
 *
 *
 ****************************************************************************/

#ifndef _IP_ALLOW_H_
#define _IP_ALLOW_H_

#include "Main.h"
#include "hdrs/HTTP.h"
#include "ts/IpMap.h"
#include "ts/Vec.h"
#include "ProxyConfig.h"

// forward declare in name only so it can be a friend.
struct IpAllowUpdate;

//
// Timeout the IpAllowTable * this amount of time after the
//    a reconfig event happens that the old table gets thrown
//    away
//
static uint64_t const IP_ALLOW_TIMEOUT = HRTIME_HOUR;

/** An access control record.
    It has the methods permitted and the source line.
*/
struct AclRecord {
  int _method_mask;
  int _src_line;

  /// Default constructor.
  /// Present only to make Vec<> happy, do not use.
  AclRecord() : _method_mask(0), _src_line(0) { }

  AclRecord(uint32_t method_mask, int ln) : _method_mask(method_mask), _src_line(ln) { }
};

/** Singleton class for access controls.
 */
class IpAllow : public ConfigInfo
{
  friend int main(int, char**);
  friend struct IpAllowUpdate;

public:
  typedef IpAllow self; ///< Self reference type.

  IpAllow(const char *config_var, const char *name, const char *action_val);
   ~IpAllow();
  void Print();
  uint32_t match(IpEndpoint const* ip) const;
  uint32_t match(sockaddr const* ip) const;

  static void startup();
  static void reconfigure();
  /// @return The global instance.
  static IpAllow * acquire();
  static void release(IpAllow * params);

  static bool CheckMask(uint32_t, int);
  /// @return A mask that permits all methods.
  static uint32_t AllMethodMask() {
    return ALL_METHOD_MASK;
  }

  typedef ConfigProcessor::scoped_config<IpAllow, IpAllow> scoped_config;

private:
  static uint32_t MethodIdxToMask(int);
  static uint32_t ALL_METHOD_MASK;
  static int configid;

  int BuildTable();

  char config_file_path[PATH_NAME_MAX];
  const char *module_name;
  const char *action;
  IpMap _map;
  Vec<AclRecord> _acls;
};

inline uint32_t IpAllow::MethodIdxToMask(int idx) { return 1 << (idx - HTTP_WKSIDX_CONNECT); }

inline uint32_t
IpAllow::match(IpEndpoint const* ip) const {
  return this->match(&ip->sa);
}

inline uint32_t
IpAllow::match(sockaddr const* ip) const {
  uint32_t zret = 0;
  void* raw;
  if (_map.contains(ip, &raw)) {
    AclRecord* acl = static_cast<AclRecord*>(raw);
    if (acl) {
      zret = acl->_method_mask;
    }
  }
  return zret;
}

inline bool
IpAllow::CheckMask(uint32_t mask, int method_idx) {
  return ((mask & MethodIdxToMask(method_idx)) != 0);
}

#endif
