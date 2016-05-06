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

#include <string>
#include <set>

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
  uint32_t _method_mask;
  int _src_line;
  typedef std::set<std::string> MethodSet;
  MethodSet _nonstandard_methods;
  bool _deny_nonstandard_methods;
  static const uint32_t ALL_METHOD_MASK = ~0; // Mask for all methods.

  /// Default constructor.
  /// Present only to make Vec<> happy, do not use.
  AclRecord() : _method_mask(0), _src_line(0), _deny_nonstandard_methods(false) {}
  AclRecord(uint32_t method_mask) : _method_mask(method_mask), _src_line(0), _deny_nonstandard_methods(false) {}
  AclRecord(uint32_t method_mask, int ln, const MethodSet &nonstandard_methods, bool deny_nonstandard_methods)
    : _method_mask(method_mask),
      _src_line(ln),
      _nonstandard_methods(nonstandard_methods),
      _deny_nonstandard_methods(deny_nonstandard_methods)
  {
  }

  static uint32_t
  MethodIdxToMask(int wksidx)
  {
    return 1 << (wksidx - HTTP_WKSIDX_CONNECT);
  }

  bool
  isEmpty() const
  {
    return (_method_mask == 0) && _nonstandard_methods.empty();
  }

  bool
  isMethodAllowed(int method_wksidx) const
  {
    return _method_mask & MethodIdxToMask(method_wksidx);
  }

  bool
  isNonstandardMethodAllowed(const std::string &method_str) const
  {
    if (_method_mask == ALL_METHOD_MASK) {
      return true;
    }
    bool method_in_set = _nonstandard_methods.count(method_str);
    return _deny_nonstandard_methods ? !method_in_set : method_in_set;
  }
};

/** Singleton class for access controls.
 */
class IpAllow : public ConfigInfo
{
  friend struct IpAllowUpdate;

public:
  typedef IpAllow self; ///< Self reference type.

  IpAllow(const char *config_var, const char *name, const char *action_val);
  ~IpAllow();
  void Print();
  AclRecord *match(IpEndpoint const *ip) const;
  AclRecord *match(sockaddr const *ip) const;

  static void startup();
  static void reconfigure();
  /// @return The global instance.
  static IpAllow *acquire();
  static void release(IpAllow *params);

  /// @return A mask that permits all methods.
  static const AclRecord *
  AllMethodAcl()
  {
    return &ALL_METHOD_ACL;
  }

  typedef ConfigProcessor::scoped_config<IpAllow, IpAllow> scoped_config;

private:
  static int configid;
  static const AclRecord ALL_METHOD_ACL;

  int BuildTable();

  char config_file_path[PATH_NAME_MAX];
  const char *module_name;
  const char *action;
  IpMap _map;
  Vec<AclRecord> _acls;
};

inline AclRecord *
IpAllow::match(IpEndpoint const *ip) const
{
  return this->match(&ip->sa);
}

inline AclRecord *
IpAllow::match(sockaddr const *ip) const
{
  void *raw;
  if (_map.contains(ip, &raw)) {
    return static_cast<AclRecord *>(raw);
  }
  return NULL;
}

#endif
