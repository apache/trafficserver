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

#include "IpLookup.h"
#include "Main.h"

void initIPAllow();
void reloadIPAllow();

//
// Timeout the IpAllowTable * this amount of time after the
//    a reconfig event happens that the old table gets thrown
//    away
//
#define IP_ALLOW_TIMEOUT            (HRTIME_HOUR*1)

// INKqa05845
#define IP_ALLOW 1
#define IP_DENY -1
class IpAllowRecord
{
public:
  int access;
  int line_num;
};

class IpAllow:public IpLookup
{
public:
  IpAllow(const char *config_var, const char *name, const char *action_val);
   ~IpAllow();
  int BuildTable();
  void Print();
  bool match(ip_addr_t ip);
private:
  const char *config_file_var;
  char config_file_path[PATH_NAME_MAX];
  const char *module_name;
  const char *action;
  bool err_allow_all;
};

extern IpAllow *ip_allow_table;

// INKqa05845
inline bool
IpAllow::match(ip_addr_t ip)
{
  if (err_allow_all == true) {
    return true;
  } else {
    IpAllowRecord *cur = NULL, *result = NULL;
    IpLookupState s;
    bool found;
    found = IpLookup::MatchFirst(ip, &s, (void **) &cur);
    result = cur;
    while (found) {
      if (cur->line_num < result->line_num) {
        result = cur;
      }
      found = IpLookup::MatchNext(&s, (void **) &cur);
    }
    return ((result != NULL) && (result->access == IP_ALLOW));
  }
}

#endif
