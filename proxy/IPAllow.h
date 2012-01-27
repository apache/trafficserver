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

/*****************************************************************************
 *
 *  IPAllow.h - Interface to IP Access Control systtem
 *
 *
 ****************************************************************************/

#ifndef _IP_ALLOW_H_
#define _IP_ALLOW_H_

#include "Main.h"
#include <ts/IpMap.h>
#include <vector>

// forward declare in name only so it can be a friend.
struct IPAllow_UpdateContinuation;

//
// Timeout the IpAllowTable * this amount of time after the
//    a reconfig event happens that the old table gets thrown
//    away
//
static uint64_t const IP_ALLOW_TIMEOUT = HRTIME_HOUR;

enum AclOp {
  ACL_OP_ALLOW, ///< Allow access.
  ACL_OP_DENY, ///< Deny access.
};

struct AclRecord {
  AclOp _op;
  int _src_line;

  AclRecord(AclOp op, int ln) : _op(op), _src_line(ln) { }
};

class IpAllow {
  friend int main(int, char**);
  friend struct IPAllow_UpdateContinuation;
public:
  typedef IpAllow self; ///< Self reference type.

  IpAllow(const char *config_var, const char *name, const char *action_val);
   ~IpAllow();
  int BuildTable();
  void Print();
  bool match(in_addr_t addr) const;
  bool match(ts_ip_endpoint const* ip) const;
  bool match(sockaddr const* ip) const;

  /// @return The global instance.
  static self* instance();
private:

  static void InitInstance();
  static void ReloadInstance();

  const char *config_file_var;
  char config_file_path[PATH_NAME_MAX];
  const char *module_name;
  const char *action;
  bool _allow_all;
  IpMap _map;
  // Can't use Vec<> because it requires a default constructor for AclRecord.
  std::vector<AclRecord> _acls;

  static self* _instance;
};

inline IpAllow* IpAllow::instance() { return _instance; }

inline bool
IpAllow::match(in_addr_t addr) const {
  bool zret = _allow_all;
  if (!zret) {
    void* raw;
    if (_map.contains(addr, &raw)) {
      AclRecord* acl = static_cast<AclRecord*>(raw);
      zret = acl && ACL_OP_ALLOW == acl->_op;
    }
  }
  return zret;
}

inline bool
IpAllow::match(ts_ip_endpoint const* ip) const {
  return this->match(&ip->sa);
}

inline bool
IpAllow::match(sockaddr const* ip) const {
  bool zret = _allow_all;
  if (!zret) {
    void* raw;
    if (_map.contains(ip, &raw)) {
      AclRecord* acl = static_cast<AclRecord*>(raw);
      zret = acl && ACL_OP_ALLOW == acl->_op;
    }
  }
  return zret;
}

#endif
