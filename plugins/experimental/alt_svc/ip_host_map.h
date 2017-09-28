/**
  @file
  @brief The IpHostMap takes a client IP address and returns a hostname or host ip they should be routed to.

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

#ifndef __IP_HOST_MAP_H__
#define __IP_HOST_MAP_H__ 1

#include "ts/string_view.h"

#include "prefix_parser.h"
#include "default.h"
#include <string>
#include <set>

// Virtual interfacing class.
class IpHostMap
{
public:
  virtual char *findHostForIP(const sockaddr *ip) const noexcept = 0;
  virtual bool isValid() const noexcept                          = 0;
};

class SingleServiceFileMap : public IpHostMap
{
public:
  char *findHostForIP(const sockaddr *ip) const noexcept override;
  void print_the_map() const noexcept;
  bool isValid() const noexcept override;

  SingleServiceFileMap(ts::string_view filename);
  ~SingleServiceFileMap(){};

  // noncopyable
  SingleServiceFileMap(const SingleServiceFileMap &) = delete;
  SingleServiceFileMap &operator=(const SingleServiceFileMap &) = delete;

private:
  IpMap host_map;
  std::set<std::string> hostnames;
  bool _isValid;
};

#endif // __IP_HOST_MAP_H__
