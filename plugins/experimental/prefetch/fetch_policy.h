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

/**
 * @file fetch_policy.h
 * @brief Fetch policy interface (header file).
 */

#pragma once

#include <list>
#include <openssl/sha.h>
#include <cstring>
#include <string>
#include <unordered_map>

#include "common.h"

class FetchPolicy;
class SimplePolicy;
class Prescaler;

/**
 * @brief Fetch policy interface.
 */
class FetchPolicy
{
public:
  static FetchPolicy *getInstance(const char *name);
  virtual ~FetchPolicy(){};

  virtual bool init(const char *parameters)    = 0;
  virtual bool acquire(const std::string &url) = 0;
  virtual bool release(const std::string &url) = 0;
  virtual const char *name()                   = 0;
  virtual size_t getSize()                     = 0;
  virtual size_t getMaxSize()                  = 0;

private:
  FetchPolicy(const FetchPolicy &);
  FetchPolicy &operator=(const FetchPolicy &);

protected:
  FetchPolicy(){};
  void
  log(const char *msg, const String &url, bool ret)
  {
    PrefetchDebug("%s::%s('%.*s%s'): %s", name(), msg, (int)(url.length() > 100 ? 100 : url.length()), url.c_str(),
                  url.length() > 100 ? "..." : "", ret ? "true" : "false");
  }
};
