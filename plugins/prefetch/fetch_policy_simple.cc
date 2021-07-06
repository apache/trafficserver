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
 * @file  fetch_policy_simple.cc
 * @brief Simple fetch policy.
 */

#include "fetch_policy_simple.h"

bool
FetchPolicySimple::init(const char *parameters)
{
  PrefetchDebug("initialized %s fetch policy", name());
  return true;
}

bool
FetchPolicySimple::acquire(const std::string &url)
{
  bool ret;
  if (_urls.end() == _urls.find(url)) {
    _urls[url] = true;
    ret        = true;
  } else {
    ret = false;
  }

  log("acquire", url, ret);
  return ret;
}

bool
FetchPolicySimple::release(const std::string &url)
{
  bool ret;
  if (_urls.end() == _urls.find(url)) {
    ret = false;
  } else {
    _urls.erase(url);
    ret = true;
  }

  log("release", url, ret);
  return ret;
}

inline const char *
FetchPolicySimple::name()
{
  return "simple";
}

inline size_t
FetchPolicySimple::getSize()
{
  return _urls.size();
}

inline size_t
FetchPolicySimple::getMaxSize()
{
  /* Unlimited */
  return 0;
}
