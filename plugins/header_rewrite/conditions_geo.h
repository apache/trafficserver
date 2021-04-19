/*
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

#include <arpa/inet.h>
#include <string>

#include "conditions.h"

class MMConditionGeo : public ConditionGeo
{
public:
  MMConditionGeo() {}
  virtual ~MMConditionGeo() {}

  static void initLibrary(const std::string &path);

  virtual int64_t get_geo_int(const sockaddr *addr) const override;
  virtual std::string get_geo_string(const sockaddr *addr) const override;
};

class GeoIPConditionGeo : public ConditionGeo
{
public:
  GeoIPConditionGeo() {}
  virtual ~GeoIPConditionGeo() {}

  static void initLibrary(const std::string &path);

  virtual int64_t get_geo_int(const sockaddr *addr) const override;
  virtual std::string get_geo_string(const sockaddr *addr) const override;
};
