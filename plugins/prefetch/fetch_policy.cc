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
 * @file fetch_policy.cc
 * @brief Fetch policy interface.
 */

#include "fetch_policy.h"

#include <cstring>

#include "common.h"
#include "fetch_policy_lru.h"
#include "fetch_policy_simple.h"

FetchPolicy *
FetchPolicy::getInstance(const char *parameters)
{
  const char *name   = parameters;
  const char *delim  = strchr(parameters, ':');
  size_t len         = (nullptr == delim ? strlen(name) : delim - name);
  const char *params = (nullptr == delim ? nullptr : delim + 1);

  PrefetchDebug("getting '%.*s' policy instance, params: %s", (int)len, name, params);
  FetchPolicy *p = nullptr;
  if (6 == len && 0 == strncmp(name, "simple", 6)) {
    p = new FetchPolicySimple();
  } else if (3 == len && 0 == strncmp(name, "lru", 3)) {
    p = new FetchPolicyLru();
  } else {
    PrefetchError("unrecognized fetch policy type: %.*s", (int)len, name);
    return nullptr;
  }

  if (p->init(params)) {
    return p;
  }
  delete p;

  return nullptr;
}
